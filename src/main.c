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
    uint8_t *px;      // RGBA8, size TILE_W*TILE_H*4
    Texture2D tex;    // GPU copy for rendering
    int dirty;        // 1 if CPU->GPU upload needed
} Tile;

static void tile_init(Tile *t) {
    TraceLog(LOG_DEBUG, "Init Tiles");
    const int tile_size = TILE_W*TILE_H*4; //width x height x rgba
    t->px = malloc(tile_size);
    memset(t->px, 0xFF, tile_size); //whIte
    Image im = {
      .data = t->px,
      .width = TILE_W, .height = TILE_H,
      .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    t->tex = LoadTextureFromImage(im);
    t->dirty = 1;
}
static void tile_free(Tile *t) {
    UnloadTexture(t->tex);
    free(t->px);
}
static inline void put_px(uint8_t *p, int w, int x, int y, Color c) {
    uint8_t *t = p + (y*w + x)*4;
    t[0]=c.r; t[1]=c.g; t[2]=c.b; t[3]=255; //paint black
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
            put_px(tiles[ti].px, TILE_W, lx, ly, c);
            tiles[ti].dirty = 1;
        }
    }
}

int main(void) {
    InitWindow(W, H, "CowPaint");
    SetTargetFPS(240);

  //init
    Tile tiles[NTILES];
    for (int i = 0; i < NTILES; ++i) tile_init(&tiles[i]);

    while (!WindowShouldClose()) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 m = GetMousePosition();
            paint_circle(tiles, (int)m.x, (int)m.y, BRUSH, BLACK);
            TraceLog(LOG_DEBUG, "Drawing");
        }

        if (IsKeyPressed(KEY_C)) {
            for(int i = 0; i < NTILES; ++i) {
              memset(tiles[i].px, 0xFF, TILE_H*TILE_W*4);
              tiles[i].dirty = 1;
            }
        }

        if (IsKeyPressed(KEY_EQUAL)) BRUSH = (BRUSH < 128) ? BRUSH + 2 : 128;
        if (IsKeyPressed(KEY_MINUS)) BRUSH = (BRUSH > 2) ? BRUSH - 2 : 2;

        for (int i = 0; i < NTILES; ++i) {
            if (tiles[i].dirty == 0) continue;
            UpdateTexture(tiles[i].tex, tiles[i].px);
            tiles[i].dirty = 0;
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);
            for (int ty = 0; ty < GRID_Y; ++ty)
                for (int tx = 0; tx < GRID_X; ++tx) {
                    int i = ty*GRID_X + tx;
                    DrawTexture(tiles[i].tex, tx*TILE_W, ty*TILE_H, WHITE);
                }
            DrawText("LMB paint | C clear | +/- brush", 10, 10, 20, BLACK);
        EndDrawing();

    }

    for (int i = 0; i < NTILES; ++i) tile_free(&tiles[i]);
    CloseWindow();
    return 0;
}
