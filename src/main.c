#include <raylib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define W 800
#define H 600
#define TILE_W 200
#define TILE_H 200

#if (W % TILE_W) || (H % TILE_H)
  #error "W and H must be multiples of TILE_W/TILE_H"
#endif

enum { GRID_X = W / TILE_W, GRID_Y = H / TILE_H, NTILES = GRID_X * GRID_Y };

static int BRUSH = 8; //fuck it global state

typedef struct {
    uint8_t *px; // RGBA8, size TILE_W*TILE_H*4
    int refcnt;
} TileBuf;

typedef struct {
    TileBuf *buf; //Reference counted buffer
    Texture2D tex;    // GPU copy for rendering
    int dirty;        // 1 if CPU->GPU upload needed
} Tile;

typedef struct {
    TileBuf *bufs[NTILES];
} Snapshot;

static inline size_t tile_bytes(void) { return (size_t)TILE_W * TILE_H * 4; }
static inline void tilebuf_retain(TileBuf *b) {
    if (b) b->refcnt++;
}
static inline void tilebuf_release(TileBuf *b) {
    if (!b) return;
    if (--b->refcnt == 0) {
        free(b->px);
        free(b);
    }
}
static void tile_ensure_unique(Tile *t) {
    TileBuf *b = t->buf;
    if (b->refcnt > 1) {
        TileBuf *nb = malloc(sizeof(TileBuf));
        nb->px = malloc(tile_bytes());
        memcpy(nb->px, b->px, tile_bytes());
        nb->refcnt = 1;

        tilebuf_release(b);
        t->buf = nb;
    }
}

static Snapshot snapshot_take(Tile tiles[]) {
    Snapshot s;
    for (int i = 0; i < NTILES; ++i) {
        s.bufs[i] = tiles[i].buf;
        tilebuf_retain(s.bufs[i]);
    }
    return s;
}
static void snapshot_restore(const Snapshot *s, Tile tiles[]) {
    for (int i = 0; i < NTILES; ++i) {
        tilebuf_release(tiles[i].buf);
        tiles[i].buf = s->bufs[i];
        tilebuf_retain(tiles[i].buf);
        tiles[i].dirty = 1;
    }
}
static void snapshot_free(Snapshot *s) {
    for (int i = 0; i < NTILES; ++i)
        tilebuf_release(s->bufs[i]);
}

static void tile_init(Tile *t) {
    TileBuf *buf = malloc(sizeof(TileBuf));
    buf->px = malloc(tile_bytes());
    buf->refcnt = 1;
    memset(buf->px, 0xFF, tile_bytes());
    t->buf = buf;

    Image im = {
      .data = buf->px,
      .width = TILE_W, .height = TILE_H,
      .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    t->tex = LoadTextureFromImage(im);
    t->dirty = 1;
}

static void tile_free(Tile *t) {
    UnloadTexture(t->tex);
    tilebuf_release(t->buf);
}
static inline void put_px(Tile *t, int lx, int ly, Color c) {
    tile_ensure_unique(t);
    uint8_t *p = t->buf->px + (ly*TILE_W + lx)*4;
    p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=255;
    t->dirty = 1;
}
static void paint_circle(Tile *tiles, int mx, int my, int r, Color c) {
    int x0 = mx - r, x1 = mx + r, y0 = my - r, y1 = my + r, rr = r*r;
    for (int y = y0; y <= y1; ++y) {
        if ((unsigned)y >= H) continue;
        int dy = y - my;
        for (int x = x0; x <= x1; ++x) {
            if ((unsigned)x >= W) continue;
            int dx = x - mx;
            if (dx*dx + dy*dy > rr) continue;

            int tx = x / TILE_W, ty = y / TILE_H, ti = ty*GRID_X + tx;
            int lx = x - tx*TILE_W, ly = y - ty*TILE_H;
            put_px(&tiles[ti], lx, ly, c);
        }
    }
}

int main(void) {
    InitWindow(W, H, "CowPaint");
    SetTargetFPS(240);

  //init
    Tile tiles[NTILES];
    for (int i = 0; i < NTILES; ++i) tile_init(&tiles[i]);

    Snapshot snap;
    int has_snap = 0;

    while (!WindowShouldClose()) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 m = GetMousePosition();
            paint_circle(tiles, (int)m.x, (int)m.y, BRUSH, BLACK);
        }

        if (IsKeyPressed(KEY_C)) {
            for(int i = 0; i < NTILES; ++i) {
              tile_ensure_unique(&tiles[i]);
              memset(tiles[i].buf->px, 0xFF, tile_bytes());
              tiles[i].dirty = 1;
            }
        }
        if (IsKeyPressed(KEY_S)) {
            if (has_snap) snapshot_free(&snap);
            snap = snapshot_take(tiles);
            has_snap = 1;
        }
        if (has_snap && IsKeyPressed(KEY_R)) {
            snapshot_restore(&snap, tiles);
        }

        if (IsKeyPressed(KEY_EQUAL)) BRUSH = (BRUSH < 128) ? BRUSH + 2 : 128;
        if (IsKeyPressed(KEY_MINUS)) BRUSH = (BRUSH > 2) ? BRUSH - 2 : 2;

        for (int i = 0; i < NTILES; ++i) {
            if (tiles[i].dirty == 0) continue;
            UpdateTexture(tiles[i].tex, tiles[i].buf->px);
            tiles[i].dirty = 0;
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);
            for (int ty = 0; ty < GRID_Y; ++ty)
                for (int tx = 0; tx < GRID_X; ++tx) {
                    int i = ty*GRID_X + tx;
                    DrawTexture(tiles[i].tex, tx*TILE_W, ty*TILE_H, WHITE);
                }
            DrawText("LMB paint | C clear | S save | R undo | +/- brush", 10, 10, 20, BLACK);
        EndDrawing();
    }

    for (int i = 0; i < NTILES; ++i) tile_free(&tiles[i]);
    if (has_snap) snapshot_free(&snap);
    CloseWindow();
    return 0;
}
