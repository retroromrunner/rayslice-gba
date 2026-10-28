/* RAYSLICE GBA - Wizardry-style instant slice renderer.
 *
 * Bare-metal GBA, Mode 0 tiled BG (30x20 tiles of 8px = 240x160).
 * NO smooth animation: a tap steps one tile or turns 90 degrees and the
 * view redraws in exactly ONE map update, then the game idles with NO
 * rendering until the next tap. That is the speed fix for a browser
 * emulator: a bitmap raycaster rewrote 38,400 pixels per frame, while
 * this version writes only 600 map entries per action and lets the
 * tile hardware do the rest.
 *
 * Wall heights still come from a DDA raycaster (30 rays, one per tile
 * column, fixed-point, table trig, zero per-frame divisions via tables),
 * so side openings show as wall-height steps, not as the blank dirt
 * that confused the original GBC slice demo. A close wall is capped
 * at 144px, so sky and floor always stay visible.
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed char s8; typedef short s16; typedef int s32;
#define REG16(a) (*(volatile u16*)(a))
#define REG_DISPCNT REG16(0x04000000)
#define REG_BG0CNT REG16(0x04000008)
#define REG_VCOUNT REG16(0x04000006)
#define REG_KEY REG16(0x04000130)
#define PAL ((volatile u16*)0x05000000)
#define CHARBLOCK ((volatile u32*)0x06000000)
#define MAP ((volatile u16*)0x06008000)
#define SRAM ((volatile u8*)0x0E000000)
#define SND(x) REG16(0x04000060 + (x))
#define REG_SOUNDCNT_L REG16(0x04000080)
#define REG_SOUNDCNT_H REG16(0x04000082)
#define REG_SOUNDCNT_X REG16(0x04000084)
#define KEY_START 0x0008
#define KEY_RIGHT 0x0010
#define KEY_LEFT 0x0020
#define KEY_UP 0x0040
#define KEY_DOWN 0x0080
#define RGB15(r,g,b) ((u16)((r) | ((g)<<5) | ((b)<<10)))

#include "trig.h"
#include "font.h"
#include "tables.h"

#define MW 12
#define MH 12
static const char *MAZE[MH] = {
    "111111111111",
    "100010000121",
    "101110111001",
    "101000100011",
    "100011101011",
    "111010100011",
    "100010111011",
    "101110000011",
    "100000111111",
    "111011000001",
    "100000012131",
    "111111111111",
};
static int cell_at(int x, int y) {
    char c;
    if (x < 0 || y < 0 || x >= MW || y >= MH) return 1;
    c = MAZE[y][x];
    if (c == '0') return 0;
    if (c == '2') return 2;
    if (c == '3') return 3;
    return 1;
}
static int iabs(int v) { return v < 0 ? -v : v; }
static void vsync(void) {
    while (REG_VCOUNT >= 160) { }
    while (REG_VCOUNT < 160) { }
}

/* ---- tiles: 0 sky, 1 floor, 2..69 walls (4 variants x 17), 70.. font ---- */
#define TILE_SKY 0
#define TILE_FLOOR 1
#define WALL_BASE(v) (2 + (v) * 17)
#define WALL_FULL(v) (WALL_BASE(v))
#define WALL_TOP(v,r) (WALL_BASE(v) + 1 + 2 * (r))
#define WALL_BOT(v,r) (WALL_BASE(v) + 2 + 2 * (r))
#define FONT_BASE 70
#define NUM_TILES (FONT_BASE + 54)

static u8 tile_data[NUM_TILES * 32];
static void set_px(int tile, int x, int y, int pix) {
    int off = tile * 32 + y * 4 + (x >> 1);
    if (x & 1) tile_data[off] = (u8)((tile_data[off] & 0x0F) | (pix << 4));
    else tile_data[off] = (u8)((tile_data[off] & 0xF0) | pix);
}
static int wall_tex(int x, int y, int v) {
    int stalk = (x + v * 2) & 7;
    int c;
    if (stalk == 0) c = 0;
    else if (stalk == 1) c = 2;
    else c = 1;
    if (((x * 7 + y * 5 + v * 3) & 7) == 0) c = 2;
    return c + 1; /* palette indices 1..4 in a wall bank */
}
static void build_tiles(void) {
    int i, x, y, v, r;
    for (i = 0; i < NUM_TILES * 32; i++) tile_data[i] = 0;
    /* floor checker: bank0 indices 1/2 */
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) set_px(TILE_FLOOR, x, y, ((x + y) & 1) ? 2 : 1);
    for (v = 0; v < 4; v++) {
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) set_px(WALL_FULL(v), x, y, wall_tex(x, y, v));
        for (r = 0; r < 8; r++) {
            for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
                set_px(WALL_TOP(v, r), x, y, (y < r) ? 0 : wall_tex(x, y, v));
                set_px(WALL_BOT(v, r), x, y, (y > r) ? 5 : wall_tex(x, y, v));
            }
        }
    }
    /* font: bank0 index3 = gold, 0 = sky */
    for (i = 0; i < 54; i++) {
        for (y = 0; y < 8; y++) {
            int bits = FONT_GLYPHS[i * 8 + y];
            for (x = 0; x < 8; x++) set_px(FONT_BASE + i, x, y, (bits & (1 << (7 - x))) ? 3 : 0);
        }
    }
    {
        volatile u32 *dst = CHARBLOCK;
        const u32 *src = (const u32 *)tile_data;
        for (i = 0; i < NUM_TILES * 8; i++) dst[i] = src[i];
    }
}
static void build_palette(void) {
    int s, i;
    static const int base_r[4] = {4, 10, 18, 28};
    static const int base_g[4] = {10, 18, 26, 31};
    static const int base_b[4] = {3, 5, 8, 13};
    static const int fac[6] = {16, 12, 12, 10, 10, 8};
    PAL[0] = RGB15(6, 4, 14);
    PAL[1] = RGB15(8, 5, 2);
    PAL[2] = RGB15(21, 14, 7);
    PAL[3] = RGB15(30, 26, 12);
    for (s = 0; s < 6; s++) {
        int bank = 1 + s;
        PAL[bank * 16 + 0] = RGB15(6, 4, 14);
        for (i = 0; i < 4; i++) PAL[bank * 16 + 1 + i] = RGB15(base_r[i] * fac[s] / 16, base_g[i] * fac[s] / 16, base_b[i] * fac[s] / 16);
        PAL[bank * 16 + 5] = RGB15(8, 5, 2);
    }
    {
        static const int sr[4] = {12, 18, 24, 30};
        static const int sg[4] = {10, 15, 20, 26};
        static const int sb[4] = {4, 6, 9, 12};
        for (s = 0; s < 2; s++) {
            int bank = 7 + s, f = s ? 8 : 16;
            PAL[bank * 16 + 0] = RGB15(6, 4, 14);
            for (i = 0; i < 4; i++) PAL[bank * 16 + 1 + i] = RGB15(sr[i] * f / 16, sg[i] * f / 16, sb[i] * f / 16);
            PAL[bank * 16 + 5] = RGB15(8, 5, 2);
        }
    }
    for (i = 9 * 16; i < 256; i++) PAL[i] = 0;
}

/* ---- map text ---- */
static int glyph_index(char c) {
    int i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (i = 0; FONT_ORDER[i]; i++) if (FONT_ORDER[i] == c) return i;
    return 0;
}
static void clear_map(void) {
    int i;
    for (i = 0; i < 32 * 32; i++) MAP[i] = (u16)TILE_SKY;
}
static void put_text(int tx, int ty, const char *str) {
    int i;
    for (i = 0; str[i]; i++) {
        if (tx + i >= 30) break;
        MAP[ty * 32 + tx + i] = (u16)(FONT_BASE + glyph_index(str[i]));
    }
}
static void fill_row(int ty, int tile) {
    int x;
    for (x = 0; x < 30; x++) MAP[ty * 32 + x] = (u16)tile;
}
static void draw_title(void) {
    clear_map();
    fill_row(17, TILE_FLOOR); fill_row(18, TILE_FLOOR); fill_row(19, TILE_FLOOR);
    put_text(10, 2, "CORN MAZE");
    put_text(10, 4, "SLICE GBA");
    put_text(9, 8, "PRESS START");
    put_text(4, 10, "UP: FORWARD  DOWN: BACK");
    put_text(7, 11, "LEFT/RIGHT: TURN");
    put_text(9, 12, "TAP TO STEP");
}
static void draw_exit(void) {
    clear_map();
    fill_row(17, TILE_FLOOR); fill_row(18, TILE_FLOOR); fill_row(19, TILE_FLOOR);
    put_text(9, 3, "YOU ESCAPED!");
    put_text(6, 5, "THE FIELD LETS GO");
    put_text(9, 8, "PRESS START");
    put_text(6, 10, "TO WALK IT AGAIN");
}

/* ---- sound ---- */
static void audio_init(void) {
    REG_SOUNDCNT_X = 0x0080;
    REG_SOUNDCNT_L = (u16)(0x0077 | (0x77 << 8));
    REG_SOUNDCNT_H = 0x0002;
}
static void sfx_step(void) {
    SND(0x18) = (u16)((12 << 12) | (2 << 8));
    SND(0x1C) = (u16)(0x8000 | 0x60);
}
static void sfx_bump(void) {
    SND(8) = (u16)((2 << 6) | (15 << 12) | (1 << 8));
    SND(12) = (u16)(0x8000 | 0x600);
}
static void sfx_note(int freq) {
    SND(8) = (u16)((2 << 6) | (15 << 12) | (3 << 8));
    SND(12) = (u16)(0x8000 | freq);
}
static void sfx_exit(void) {
    int i, f;
    static const int notes[3] = {0x700, 0x680, 0x600};
    for (f = 0; f < 3; f++) {
        sfx_note(notes[f]);
        for (i = 0; i < 12; i++) vsync();
    }
}

/* ---- slice render: one DDA ray per tile column, one map update ---- */
static int cam_x[30];
static void render_slice(int px, int py, int ang) {
    int dir_x = COS256[ang], dir_y = SIN256[ang];
    int plane_x = -(dir_y * 169) / 256;
    int plane_y = (dir_x * 169) / 256;
    int col, row;
    for (col = 0; col < 30; col++) {
        int ray_x = dir_x + plane_x * cam_x[col] / 256;
        int ray_y = dir_y + plane_y * cam_x[col] / 256;
        int map_x = px >> 8, map_y = py >> 8;
        int ddx, ddy, sdx, sdy, step_x, step_y, side = 0, hit = 0, cell = 1;
        int perp, line_h, top, bottom, top_tile, bot_tile, top_edge, bot_edge, variant, bank;
        int steps = 0, ax = iabs(ray_x), ay = iabs(ray_y);
        ddx = (ax <= 512) ? DDX_TABLE[ax] : 16000;
        ddy = (ay <= 512) ? DDX_TABLE[ay] : 16000;
        if (ray_x < 0) { step_x = -1; sdx = ((px & 255) * ddx) >> 8; }
        else { step_x = 1; sdx = ((256 - (px & 255)) * ddx) >> 8; }
        if (ray_y < 0) { step_y = -1; sdy = ((py & 255) * ddy) >> 8; }
        else { step_y = 1; sdy = ((256 - (py & 255)) * ddy) >> 8; }
        while (!hit && steps < 32) {
            if (sdx < sdy) { sdx += ddx; map_x += step_x; side = 0; }
            else { sdy += ddy; map_y += step_y; side = 1; }
            cell = cell_at(map_x, map_y);
            if (cell != 0) hit = 1;
            steps++;
        }
        perp = (side == 0) ? (sdx - ddx) : (sdy - ddy);
        if (perp < 16) perp = 16;
        line_h = (perp < 4096) ? LINE_H_TABLE[perp] : 144;
        top = (160 - line_h) / 2;
        bottom = top + line_h - 1;
        if (top < 0) top = 0; if (bottom > 159) bottom = 159;
        top_tile = top / 8; bot_tile = bottom / 8; top_edge = top % 8; bot_edge = bottom % 8;
        {
            int wall_x;
            if (side == 0) wall_x = py + (perp * ray_y) / 256;
            else wall_x = px + (perp * ray_x) / 256;
            variant = (wall_x >> 6) & 3;
        }
        if (cell == 2) bank = (perp < 512) ? 7 : 8;
        else if (perp < 512) bank = 1 + (side ? 1 : 0);
        else if (perp < 1024) bank = 3 + (side ? 1 : 0);
        else bank = 5 + (side ? 1 : 0);
        for (row = 0; row < 20; row++) {
            int tile, b = 0;
            if (row < top_tile) { tile = TILE_SKY; b = 0; }
            else if (row == top_tile && row == bot_tile) { tile = WALL_FULL(variant); b = bank; }
            else if (row == top_tile) { tile = WALL_TOP(variant, top_edge); b = bank; }
            else if (row > top_tile && row < bot_tile) { tile = WALL_FULL(variant); b = bank; }
            else if (row == bot_tile) { tile = WALL_BOT(variant, bot_edge); b = bank; }
            else { tile = TILE_FLOOR; b = 0; }
            MAP[row * 32 + col] = (u16)(tile | (b << 12));
        }
    }
    for (row = 0; row < 20; row++) {
        MAP[row * 32 + 30] = (u16)TILE_SKY;
        MAP[row * 32 + 31] = (u16)TILE_SKY;
    }
}

#define ST_TITLE 0
#define ST_PLAY 1
#define ST_EXIT 2

int main(void) {
    int state = ST_TITLE;
    int ptx = 1, pty = 1, facing = 0;
    int prev_keys = 0, frame = 0, bump_timer = 0, idle_frames = 0, c;
    static const int fdx[4] = {1, 0, -1, 0};
    static const int fdy[4] = {0, 1, 0, -1};
    build_palette();
    build_tiles();
    audio_init();
    REG_BG0CNT = (u16)(16 << 8);
    REG_DISPCNT = (u16)0x0100;
    for (c = 0; c < 30; c++) cam_x[c] = ((2 * (c * 8 + 4) - 239) * 256) / 239;
    draw_title();
    while (1) {
        int keys, pressed, held, dir = 0;
        vsync();
        keys = (~REG_KEY) & 0x03FF;
        pressed = keys & ~prev_keys;
        prev_keys = keys;
        held = keys;
        if (state == ST_TITLE) {
            if (pressed & KEY_START) {
                ptx = 1; pty = 1; facing = 0; idle_frames = 0;
                state = ST_PLAY;
                render_slice((ptx * 256) + 128, (pty * 256) + 128, facing * 64);
            }
            frame++;
        } else if (state == ST_EXIT) {
            if (pressed & KEY_START) { state = ST_TITLE; draw_title(); }
            frame++;
        } else {
            if (pressed & KEY_UP) dir = 1;
            else if (pressed & KEY_DOWN) dir = 2;
            else if (pressed & KEY_LEFT) dir = 3;
            else if (pressed & KEY_RIGHT) dir = 4;
            else if (idle_frames >= 20) {
                if (held & KEY_UP) dir = 1;
                else if (held & KEY_DOWN) dir = 2;
                else if (held & KEY_LEFT) dir = 3;
                else if (held & KEY_RIGHT) dir = 4;
            }
            if (dir == 1 || dir == 2) {
                int s = (dir == 1) ? 1 : -1;
                int tx = ptx + fdx[facing] * s, ty = pty + fdy[facing] * s;
                int cell = cell_at(tx, ty);
                if (cell == 0 || cell == 3) {
                    ptx = tx; pty = ty;
                    sfx_step();
                    render_slice((ptx * 256) + 128, (pty * 256) + 128, facing * 64);
                    if (cell == 3) { state = ST_EXIT; draw_exit(); sfx_exit(); }
                } else if (bump_timer == 0) { sfx_bump(); bump_timer = 20; idle_frames = 0; }
            } else if (dir == 3 || dir == 4) {
                facing = (dir == 4) ? ((facing + 1) & 3) : ((facing + 3) & 3);
                sfx_step();
                render_slice((ptx * 256) + 128, (pty * 256) + 128, facing * 64);
            }
            if (bump_timer) bump_timer--;
            if (dir) idle_frames = 0; else idle_frames++;
            frame++;
        }
        SRAM[0] = 'R'; SRAM[1] = 'S';
        SRAM[2] = (u8)state;
        SRAM[3] = (u8)ptx; SRAM[4] = (u8)pty; SRAM[5] = (u8)(facing * 64);
        SRAM[6] = (u8)((frame >> 8) & 255); SRAM[7] = (u8)(frame & 255);
        SRAM[8] = (u8)facing; SRAM[9] = 0;
    }
    return 0;
}
