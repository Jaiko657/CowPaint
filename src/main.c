#include <raylib.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>

#define W 800
#define H 600
#define TILE_W 200
#define TILE_H 200
#define MAX_SNAPS 128

#if (W % TILE_W) || (H % TILE_H)
#   error "W and H must be multiples of TILE_W/TILE_H"
#endif

enum { GRID_X = W / TILE_W, GRID_Y = H / TILE_H, NTILES = GRID_X * GRID_Y };
typedef struct
{
    uint8_t *px;  // RGBA8, size TILE_W*TILE_H*4
    int refcnt;
} TileBuf;

typedef struct
{
    TileBuf  *buf; // reference-counted buffer
    Texture2D tex; // GPU copy for rendering
    int       dirty; // 1 if CPU->GPU upload needed
} Tile;

typedef struct
{
    TileBuf *bufs[NTILES];
} Snapshot;

typedef struct
{
    Snapshot snaps[MAX_SNAPS];
    int count;
    int cursor;
} History;

static int brush_radius = 8;
static Vector2 prev_mouse = {0};
static bool    was_down   = false;
static bool stroke_dirty = false; //More global state, who cares

static inline size_t
tile_bytes(void) {
    return (size_t)TILE_W * TILE_H * 4;
}

static inline void
tilebuf_retain(TileBuf *b)
{
    if (b) b->refcnt++;
}

static inline void
tilebuf_release(TileBuf *b)
{
    if (!b) return;
    if (--b->refcnt == 0)
    {
        free(b->px);
        free(b);
    }
}

static void
tile_ensure_unique(Tile *t)
{
    TileBuf *b = t->buf;
    if (b->refcnt > 1)
    {
        TileBuf *nb = malloc(sizeof(TileBuf));
        nb->px = malloc(tile_bytes());
        memcpy(nb->px, b->px, tile_bytes());
        nb->refcnt = 1;

        tilebuf_release(b);
        t->buf = nb;
    }
}

static Snapshot
snapshot_take(Tile tiles[])
{
    Snapshot s;
    for (int i = 0; i < NTILES; ++i)
    {
        s.bufs[i] = tiles[i].buf;
        tilebuf_retain(s.bufs[i]);
    }
    return s;
}
static void
snapshot_restore(const Snapshot *s, Tile tiles[])
{
    for (int i = 0; i < NTILES; ++i)
    {
        tilebuf_release(tiles[i].buf);
        tiles[i].buf = s->bufs[i];
        tilebuf_retain(tiles[i].buf);
        tiles[i].dirty = 1;
    }
}

static void
snapshot_free(Snapshot *s)
{
    for (int i = 0; i < NTILES; ++i)
        tilebuf_release(s->bufs[i]);
}

static void
history_init(History *h)
{
    h->count=0;
    h->cursor=-1;
}

static void
history_free(History *h)
{
    for (int i=0; i<h->count; ++i)
        snapshot_free(&h->snaps[i]);
    h->count=0; h->cursor=-1;
}

static void
history_push(History *h, Tile tiles[])
{
    //Free ahead if needed, and move ahead
    for (int i=h->cursor+1; i<h->count; ++i)
        snapshot_free(&h->snaps[i]);
    h->count = h->cursor + 1;

    if (h->count >= MAX_SNAPS)
    { //Move all back if hit cap
        snapshot_free(&h->snaps[0]);
        memmove(&h->snaps[0], &h->snaps[1], (h->count-1)*sizeof(Snapshot));
        h->count--; h->cursor--;
    }

    h->snaps[h->count] = snapshot_take(tiles);
    h->cursor = h->count;
    h->count++;
}

static bool
history_undo(History *h, Tile tiles[])
{
    if (h->cursor <= 0) return false;
    h->cursor--;
    snapshot_restore(&h->snaps[h->cursor], tiles);
    return true;
}

static bool
history_redo(History *h, Tile tiles[])
{
    if (h->cursor+1 >= h->count) return false;
    h->cursor++;
    snapshot_restore(&h->snaps[h->cursor], tiles);
    return true;
}

static void
tile_init(Tile *t)
{
    TileBuf *buf = malloc(sizeof(TileBuf));
    buf->px = malloc(tile_bytes());
    buf->refcnt = 1;
    memset(buf->px, 0xFF, tile_bytes());
    t->buf = buf;

    Image im = 
      {
        .data = buf->px,
        .width = TILE_W, .height = TILE_H,
        .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
      };
    t->tex = LoadTextureFromImage(im);
    t->dirty = 1;
}

static void
tile_free(Tile *t)
{
    UnloadTexture(t->tex);
    tilebuf_release(t->buf);
}

static inline void
put_px(Tile *t, int lx, int ly, Color c)
{
    tile_ensure_unique(t);
    uint8_t *p = t->buf->px + (ly*TILE_W + lx)*4;
    p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=255;
    t->dirty = 1;
}

static void
paint_circle(Tile *tiles, int mx, int my, int r, Color c)
{
    int x0 = mx - r, x1 = mx + r, y0 = my - r, y1 = my + r, rr = r*r;
    for (int y = y0; y <= y1; ++y)
    {
        if ((unsigned)y >= H) continue;
        int dy = y - my;
        for (int x = x0; x <= x1; ++x)
        {
            if ((unsigned)x >= W) continue;
            int dx = x - mx;
            if (dx*dx + dy*dy > rr) continue;

            int tx = x / TILE_W, ty = y / TILE_H, ti = ty*GRID_X + tx;
            int lx = x - tx*TILE_W, ly = y - ty*TILE_H;
            put_px(&tiles[ti], lx, ly, c);
        }
    }
}

//cant be bothered learning Bezier this will do
static bool
paint_stroke(Tile *tiles, Vector2 a, Vector2 b, int r, Color c)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dist = sqrtf(dx*dx + dy*dy);
    float step = r * 0.5f;
    if (step < 1.0f) step = 1.0f;

    int samples = (int)(dist / step) + 1;
    bool changed = false;

    for (int i = 0; i <= samples; ++i)
    {
        float t = samples ? (float)i / (float)samples : 0.0f;
        int x = (int)(a.x + t * dx);
        int y = (int)(a.y + t * dy);
        paint_circle(tiles, x, y, r, c);
        changed = true;
    }
    return changed;
}

static bool show_stats = true;

static int count_changed_tiles_this_snapshot(const History *h) {
    if (h->cursor <= 0) return 0;
    int changed = 0;
    const Snapshot *cur = &h->snaps[h->cursor];
    const Snapshot *prev = &h->snaps[h->cursor - 1];
    for (int i = 0; i < NTILES; ++i)
        if (cur->bufs[i] != prev->bufs[i]) changed++;
    return changed;
}

static void draw_stats(const History *h) {
    const int changed = count_changed_tiles_this_snapshot(h);
    const int reused  = NTILES - changed;

    char line[256];
    int y = 36;

    snprintf(line, sizeof(line), "Snapshot %d/%d", h->cursor, h->count-1);
    DrawText(line, 10, y, 20, BLACK); y += 22;

    snprintf(line, sizeof(line), "Changed tiles: %d (%.1f%%) | Reused: %d",
             changed, NTILES ? 100.0f*changed/NTILES : 0.f, reused);
    DrawText(line, 10, y, 20, BLACK); y += 22;
}

int main(void)
{
    InitWindow(W, H, "CowPaint");
    SetTargetFPS(240);

  //init
    Tile tiles[NTILES];
    for (int i = 0; i < NTILES; ++i) tile_init(&tiles[i]);

    History hist;
    history_init(&hist);
    history_push(&hist, tiles);

    while (!WindowShouldClose())
    {
        Vector2 m = GetMousePosition();
        bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);

        if (down)
        {
            if (!was_down)
            {
                paint_circle(tiles, (int)m.x, (int)m.y, brush_radius, BLACK);
                stroke_dirty = true;
            }
            else
            {
                stroke_dirty |= paint_stroke(tiles, prev_mouse, m, brush_radius, BLACK);
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            if (stroke_dirty)
            {
                history_push(&hist, tiles);
                stroke_dirty = false;
            }
        }

        was_down = down;
        prev_mouse = m;

        if (IsKeyPressed(KEY_S)) show_stats = !show_stats;
        if (IsKeyPressed(KEY_C))
        {
            for(int i = 0; i < NTILES; ++i)
            {
                tile_ensure_unique(&tiles[i]);
                memset(tiles[i].buf->px, 0xFF, tile_bytes());
                tiles[i].dirty = 1;
            }
            history_push(&hist, tiles);
        }

        if (IsKeyPressed(KEY_EQUAL)) brush_radius = (brush_radius < 128) ? brush_radius + 2 : 128;
        if (IsKeyPressed(KEY_MINUS)) brush_radius = (brush_radius > 2) ? brush_radius - 2 : 2;

        if (IsKeyPressed(KEY_LEFT))  history_undo(&hist, tiles);
        if (IsKeyPressed(KEY_RIGHT)) history_redo(&hist, tiles);

        for (int i = 0; i < NTILES; ++i)
        {
            if (tiles[i].dirty == 0) continue;
            UpdateTexture(tiles[i].tex, tiles[i].buf->px);
            tiles[i].dirty = 0;
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);
            for (int ty = 0; ty < GRID_Y; ++ty)
                for (int tx = 0; tx < GRID_X; ++tx)
                {
                    int i = ty*GRID_X + tx;
                    DrawTexture(tiles[i].tex, tx*TILE_W, ty*TILE_H, WHITE);
                }
            DrawText("LMB paint | C clear | <- / -> History | +/- brush | S stats", 10, 10, 20, BLACK);
            if (show_stats) draw_stats(&hist);
        EndDrawing();
    }

    for (int i = 0; i < NTILES; ++i) tile_free(&tiles[i]);
    history_free(&hist);
    CloseWindow();
    return 0;
}
