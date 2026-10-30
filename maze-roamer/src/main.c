/* MAZE ROAMER - GBC first-person dungeon maze demo (not a full game).
 * Deliberately small: the known-working raycaster-demo instant renderer,
 * plus one roaming walk-through Stone Wraith (sprite-cast BG billboard)
 * and a gold-marked exit. No battles, story, magic, inventory, or NPCs.
 *
 * Doom-style raycaster, adapted to GBC tile hardware: one DDA ray per
 * screen tile column (20 columns), wall height in pixels, with
 * precomputed top/bottom edge tiles so wall tops and bottoms land on
 * pixel rows, not just tile rows. Side openings read as farther walls
 * + floor, because every column's ray travels until it actually hits
 * something - unlike the slice renderer, an open side is never blank.
 *
 * Movement is Wizardry-style INSTANT tile-step (not smooth, not
 * animated): tap Up to step exactly one tile forward, Down to step
 * exactly one tile back, Left/Right to turn exactly 90 degrees.
 * A blocked step plays the bump sound and does not move.
 * After each action the game renders exactly ONE full frame with the
 * LCD off (write_list_all, no vblank chunking) and then goes idle
 * with NO rendering until the next tap. That is the speed fix: the
 * smooth version re-ran the DDA and diff writer every frame, which
 * was unplayably slow on GBC. Here the DDA runs once per tap.
 * Sound is short bursts only: the step rustle uses length-enable +
 * a decaying envelope, so it stops by itself. No constant noise.
 * Known tradeoff: each tap briefly blanks the screen (LCD off/on)
 * while the full frame is written, then the new view appears
 * instantly. That is the intended Wizardry-style redraw, not a bug.
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "tiles.h"
#include "trig.h"

/* maze: 1 = stone wall, 0 = open, 3 = exit (gold in view). No '2' cells. */
#define MW 12
#define MH 12
static const char *MAZE[MH] = {
    "111111111111",
    "100010000111",
    "101110111001",
    "101000100011",
    "100011101011",
    "111010100011",
    "100010111011",
    "101110000011",
    "100000111111",
    "111011000001",
    "100000011131",
    "111111111111",
};

static uint8_t cell_at(int8_t x, int8_t y) {
    char c;
    if (x < 0 || y < 0 || x >= MW || y >= MH) return 1;
    c = MAZE[y][x];
    if (c == '0') return 0;
    if (c == '2') return 2;
    if (c == '3') return 3;
    return 1;
}

/* palettes: wall palettes carry [ceiling, wall-dark, wall-light, floor]
 * so a single edge tile can hold ceiling + wall + floor. v2 theme:
 * lighter dark-slate ceiling (not near-black), grey stone walls in
 * slate/cinder shades, and BROWN dirt floor (palette colour 3 in every
 * wall palette, and colours 1/2/3 in the floor palette, are browns).
 * Palette 7 is the gold exit palette in play. */
static const palette_color_t PALS[8][4] = {
    {RGB(10, 10, 14), RGB(30, 22, 8), RGB(16, 16, 20), RGB(24, 20, 14)},  /* 0 ceiling + torch glints */
    {RGB(10, 10, 14), RGB(12, 7, 3), RGB(20, 12, 5), RGB(28, 18, 9)},     /* 1 dirt floor */
    {RGB(10, 10, 14), RGB(14, 14, 16), RGB(26, 26, 28), RGB(20, 12, 5)},  /* 2 wall near */
    {RGB(10, 10, 14), RGB(10, 10, 12), RGB(18, 18, 20), RGB(20, 12, 5)},  /* 3 wall near, side */
    {RGB(10, 10, 14), RGB(8, 8, 10), RGB(14, 14, 16), RGB(20, 12, 5)},    /* 4 wall far */
    {RGB(10, 10, 14), RGB(6, 6, 8), RGB(11, 11, 13), RGB(20, 12, 5)},     /* 5 wall far, side */
    {RGB(6, 6, 10), RGB(28, 28, 30), RGB(14, 14, 18), RGB(31, 28, 8)},    /* 6 wraith: dark, pale, mid, eyes */
    {RGB(10, 10, 14), RGB(14, 10, 2), RGB(26, 20, 6), RGB(20, 12, 5)},    /* 7 exit gold */
};

/* Wraith BG palette is PALS[6] above: 0 dark, 1 pale, 2 mid, 3 eyes.
 * (Tile value 0 draws as dark in partial tiles; all-background
 * tiles are skipped via wraith_bg_solid, so the silhouette survives.) */

/* wall tile layout from gen_tiles.py: SCENE = SKY, STAR, FLOOR, FLOOR2,
 * then per variant v (0,1): full, then (TOP r, BOT r) for r=0..7. */
#define WALL_BASE(v) (4 + (v) * 17)
#define WALL_FULL(v) (WALL_BASE(v))
#define WALL_TOP(v, r) (WALL_BASE(v) + 1 + (r) * 2)
#define WALL_BOT(v, r) (WALL_BASE(v) + 2 + (r) * 2)

/* view buffers */
static uint8_t cur_tiles[360], cur_attrs[360];
static uint8_t prev_tiles[360], prev_attrs[360];
static uint8_t ch_tile[360], ch_attr[360];
static uint16_t ch_addr[360], ch_idx[360];
static uint16_t ch_count = 0, wr = 0;

static void buf_cell(int8_t x, int8_t y, uint8_t tile, uint8_t pal) {
    uint16_t i;
    if (x < 0 || x >= 20 || y < 0 || y >= 18) return;
    i = (uint16_t)y * 20 + (uint8_t)x;
    cur_tiles[i] = tile;
    cur_attrs[i] = pal;
}

static void scan_diff(void) {
    uint8_t *tp = cur_tiles, *ap = cur_attrs;
    uint8_t *pp = prev_tiles, *qp = prev_attrs;
    uint8_t x, y;
    uint16_t n = 0;
    for (y = 0; y < 18; y++) {
        for (x = 0; x < 20; x++) {
            uint8_t t = *tp, a = *ap;
            if (t != *pp || a != *qp) {
                ch_tile[n] = t;
                ch_attr[n] = a;
                ch_addr[n] = (uint16_t)(0x9800u + ((uint16_t)y << 5) + x);
                ch_idx[n] = (uint16_t)((uint16_t)y * 20 + x);
                n++;
            }
            pp++; qp++; tp++; ap++;
        }
    }
    ch_count = n;
    wr = 0;
}

static void write_list_chunked(void) {
    while (wr < ch_count) {
        uint8_t ly = LY_REG;
        uint16_t a, ci;
        if (ly < 144 || ly > 152) break;
        a = ch_addr[wr];
        ci = ch_idx[wr];
        VBK_REG = 0;
        *(uint8_t *)a = ch_tile[wr];
        VBK_REG = 1;
        *(uint8_t *)a = ch_attr[wr];
        prev_tiles[ci] = ch_tile[wr];
        prev_attrs[ci] = ch_attr[wr];
        wr++;
    }
    VBK_REG = 0;
}

static void write_list_all(void) {
    uint16_t n;
    VBK_REG = 0;
    for (n = 0; n < ch_count; n++) *(uint8_t *)ch_addr[n] = ch_tile[n];
    VBK_REG = 1;
    for (n = 0; n < ch_count; n++) *(uint8_t *)ch_addr[n] = ch_attr[n];
    VBK_REG = 0;
}

static void sync_prev_cur(void) {
    uint16_t i;
    for (i = 0; i < 360; i++) {
        prev_tiles[i] = cur_tiles[i];
        prev_attrs[i] = cur_attrs[i];
    }
}

static void invalidate_prev(void) {
    uint16_t i;
    for (i = 0; i < 360; i++) prev_tiles[i] = prev_attrs[i] = 0xFF;
}

/* ---- raycaster: one DDA ray per tile column ---- */
static int16_t cam_x[20];
/* Per-column wall depth (perp, engine units), filled by
 * compose_raycaster — the ZBuffer for sprite casting: a wraith
 * column is drawn only where the wraith is nearer than the wall. */
static int16_t zbuf[20];

static void compose_raycaster(int16_t px, int16_t py, uint8_t ang) {
    int16_t dir_x = COS256[ang], dir_y = SIN256[ang];
    int16_t plane_x = (int16_t)(-(int32_t)dir_y * 169 / 256);
    int16_t plane_y = (int16_t)((int32_t)dir_x * 169 / 256);
    uint8_t col;
    for (col = 0; col < 20; col++) {
        int16_t ray_x = (int16_t)(dir_x + (int32_t)plane_x * cam_x[col] / 256);
        int16_t ray_y = (int16_t)(dir_y + (int32_t)plane_y * cam_x[col] / 256);
        int8_t map_x = (int8_t)(px >> 8), map_y = (int8_t)(py >> 8);
        int16_t ddx, ddy, sdx, sdy;
        int8_t step_x, step_y, side = 0, hit = 0;
        uint8_t cell = 1;
        int16_t perp, line_h, top, bottom;
        int8_t top_tile, bot_tile, top_edge, bot_edge;
        uint8_t variant, pal;
        int8_t row;
        int32_t wall_x;

        if (ray_x == 0) ddx = 16000; else {
            ddx = (int16_t)(65536L / (ray_x < 0 ? -ray_x : ray_x));
            if (ddx > 16000) ddx = 16000;
        }
        if (ray_y == 0) ddy = 16000; else {
            ddy = (int16_t)(65536L / (ray_y < 0 ? -ray_y : ray_y));
            if (ddy > 16000) ddy = 16000;
        }
        if (ray_x < 0) {
            step_x = -1;
            sdx = (int16_t)(((int32_t)(px & 255) * ddx) >> 8);
        } else {
            step_x = 1;
            sdx = (int16_t)(((int32_t)(256 - (px & 255)) * ddx) >> 8);
        }
        if (ray_y < 0) {
            step_y = -1;
            sdy = (int16_t)(((int32_t)(py & 255) * ddy) >> 8);
        } else {
            step_y = 1;
            sdy = (int16_t)(((int32_t)(256 - (py & 255)) * ddy) >> 8);
        }
        {
            uint8_t steps = 0;
            while (!hit && steps < 32) {
                if (sdx < sdy) {
                    sdx += ddx; map_x += step_x; side = 0;
                } else {
                    sdy += ddy; map_y += step_y; side = 1;
                }
                cell = cell_at(map_x, map_y);
                if (cell != 0) hit = 1;
                steps++;
            }
        }
        if (side == 0) perp = (int16_t)(sdx - ddx); else perp = (int16_t)(sdy - ddy);
        if (perp < 16) perp = 16;
        zbuf[col] = perp;
        line_h = (int16_t)(36864L / perp);
        /* Cap at 128 (not 288/full screen): a wall 0.5 tiles away would
         * otherwise fill all 144 rows. Capped, it leaves 8px of sky
         * above and 8px of floor below, so a close wall never becomes
         * a full-screen wall and the player can still orient. */
        if (line_h > 128) line_h = 128;
        top = (144 - line_h) / 2;
        bottom = top + line_h - 1;
        if (top < 0) top = 0;
        if (bottom > 143) bottom = 143;
        top_tile = (int8_t)(top / 8);
        bot_tile = (int8_t)(bottom / 8);
        top_edge = (int8_t)(top % 8);
        bot_edge = (int8_t)(bottom % 8);
        if (side == 0)
            wall_x = (int32_t)py + ((int32_t)perp * ray_y) / 256;
        else
            wall_x = (int32_t)px + ((int32_t)perp * ray_x) / 256;
        (void)wall_x;
        /* Wall TYPE by hit-cell position (not texture offset):
         * 0 = slate blocks, 1 = cinder blocks. Checkerboarded by
         * cell so neighbouring walls visibly differ in shade. */
        variant = (uint8_t)(((uint8_t)(map_x + map_y)) & 1);
        if (cell == 3) pal = 7; /* exit wall: bright gold, unmistakable */
        else if (perp < 512) pal = (uint8_t)(2 + (side ? 1 : 0));
        else pal = (uint8_t)(4 + (side ? 1 : 0));

        for (row = 0; row < 18; row++) {
            if (row < top_tile) {
                buf_cell((int8_t)col, row,
                         (((col * 3 + row * 5) & 7) == 0) ? T_STAR : T_SKY, 0);
            } else if (row == top_tile && row == bot_tile) {
                buf_cell((int8_t)col, row, (uint8_t)WALL_FULL(variant), pal);
            } else if (row == top_tile) {
                buf_cell((int8_t)col, row, (uint8_t)WALL_TOP(variant, top_edge), pal);
            } else if (row < bot_tile) {
                buf_cell((int8_t)col, row, (uint8_t)WALL_FULL(variant), pal);
            } else if (row == bot_tile) {
                buf_cell((int8_t)col, row, (uint8_t)WALL_BOT(variant, bot_edge), pal);
            } else {
                buf_cell((int8_t)col, row,
                         ((col + row) & 1) ? T_FLOOR2 : T_FLOOR, 1);
            }
        }
    }
}

/* ---- text / screens ---- */
static const char FONT_ORDER[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?'-:;()/&><+=\"";

static void set_cell(int8_t x, int8_t y, uint8_t tile, uint8_t pal) {
    if (x < 0 || x >= 20 || y < 0 || y >= 18) return;
    VBK_REG = 0;
    set_bkg_tile_xy((uint8_t)x, (uint8_t)y, tile);
    VBK_REG = 1;
    set_bkg_tile_xy((uint8_t)x, (uint8_t)y, pal);
    VBK_REG = 0;
}

static void put_text(uint8_t x, uint8_t y, const char *str, uint8_t pal) {
    uint8_t i = 0;
    while (str[i]) {
        const char *p = FONT_ORDER;
        uint8_t idx = 0;
        while (*p && *p != str[i]) { p++; idx++; }
        if (*p) set_cell((int8_t)(x + i), (int8_t)y, (uint8_t)(FONT_BASE + idx), pal);
        i++;
    }
}

static void fill_all(uint8_t tile, uint8_t pal) {
    uint8_t x, y;
    for (y = 0; y < 18; y++)
        for (x = 0; x < 20; x++)
            set_cell((int8_t)x, (int8_t)y, tile, pal);
}

static void hide_all_sprites(void) {
    uint8_t i;
    for (i = 0; i < 40; i++) move_sprite(i, 0, 200);
}

static void draw_title(void) {
    DISPLAY_OFF;
    SCX_REG = 0;
    hide_all_sprites();
    fill_all(T_SKY, 0);
    /* Every line checked: x + length <= 20, >=1 column margin each side
     * where possible, so no letter is cut off at the screen edge. */
    put_text(4, 4, "MAZE ROAMER", 7);   /* 4+11=15 */
    put_text(4, 11, "PRESS START", 7);  /* 4+11=15 */
    put_text(2, 13, "UP/DOWN: STEP", 7); /* 2+14=16 */
    put_text(1, 14, "LEFT/RIGHT: TURN", 7); /* 1+16=17 */
    DISPLAY_ON;
}

static void draw_exit(void) {
    DISPLAY_OFF;
    SCX_REG = 0;
    hide_all_sprites();
    fill_all(T_SKY, 0);
    put_text(4, 6, "YOU ESCAPED!", 7);
    put_text(1, 8, "THE DUNGEON LETS GO", 7); /* 1+19=20, last col, full */
    put_text(4, 11, "PRESS START", 7);
    put_text(1, 13, "TO WALK IT AGAIN", 7);
    DISPLAY_ON;
}

/* ---- sound: short bursts only, length-enable on ---- */
static void sfx_step(void) {
    NR41_REG = 0x30; /* length 16 ticks */
    NR42_REG = 0xC1; /* vol 12, decreasing, period 1 */
    NR43_REG = 0x60;
    NR44_REG = 0xC0; /* trigger + length enable: stops by itself */
}

static void sfx_bump(void) {
    NR11_REG = 0x80;
    NR12_REG = 0xF1;
    NR13_REG = 0x00;
    NR14_REG = 0xC6; /* trigger + length enable */
}

static void sfx_note(uint8_t lo, uint8_t hi) {
    NR11_REG = 0x80;
    NR12_REG = 0xF3;
    NR13_REG = lo;
    NR14_REG = hi;
}

static void sfx_exit(void) {
    uint8_t i;
    sfx_note(0x83, 0xC7);
    for (i = 0; i < 12; i++) wait_vbl_done();
    sfx_note(0x9D, 0xC7);
    for (i = 0; i < 12; i++) wait_vbl_done();
    sfx_note(0xAD, 0xC7);
    for (i = 0; i < 18; i++) wait_vbl_done();
}

#define ST_TITLE 0
#define ST_PLAY 1
#define ST_EXIT 2

/* QA proof block in WRAM, updated every frame: 'R','C',state,ptx,pty,ang,0xA5,checksum.
 * A host QA script can scan WRAM for this pattern instead of guessing
 * a symbol address (SDCC map files do not reliably list locals). */
static uint8_t proof_block[8];

/* Player tile + facing, file-scope so they persist across frames.
 * ang is always one of 0 (east), 64 (south), 128 (west), 192 (north). */
static int8_t g_ptx = 1, g_pty = 1;
static uint8_t g_ang = 0;

/* ---- Stone Wraith: one roaming, walk-through monster ----
 * Moves 1 tile to a random adjacent OPEN cell on each successful player
 * step (never into walls or the exit cell). Never blocks, never battles.
 * v3 rendering — sprite casting (Lode Vandevenne, Raycasting III:
 * Sprites; haroldo-ok gbray does the same per-column idea on GB:
 * objects are drawn per ray column, scaled by distance, only where
 * they are nearer than the wall in that column). Implemented here in
 * original C, adapted to this engine's tile hardware:
 * - The wraith is transformed into camera space from its tile-centre
 *   offset (fwd = perpendicular tiles ahead, lat = lateral tiles).
 *   With cardinal facing and tile-centre positions this IS the
 *   inverse-camera-matrix transform (dir + plane, plane 169/256);
 *   screen x = 80 * (1 + lat/fwd), depth = fwd tiles.
 * - Its height uses the SAME perspective as the walls:
 *   spriteH = 36864 / (fwd*256), capped at 128 like wall line_h,
 *   centred on the horizon (72), so feet = 72 + spriteH/2 — the
 *   floor line at THAT distance, not a fixed screen line, and the
 *   head stays below the ceiling line. Width = spriteH * 0.5
 *   (the 32x64 master texture's aspect).
 * - It is drawn AFTER the walls, stripe by stripe over the BG
 *   tile buffer (the GBC analogue of Lode's per-stripe drawing):
 *   for each screen tile column in its range, the column is
 *   skipped if wraithDepth >= zbuf[col] (a nearer wall occludes
 *   it), so it never shows through wall corners. Visible columns
 *   sample the 32x64 master texture by nearest neighbour into
 *   4x8 BG tiles, palette 6 (pale wraith, contrasting with the
 *   grey walls — v2's dark cloak blended into them).
 * - All divisors here are provably non-zero (fwd >= 1, spriteH/W
 *   >= 16 after clamping), so no SDCC zero-divisor hang.
 * Hidden when behind the player, out of view, or on the player's
 * tile. RNG is a fixed-seed counter so QA runs are reproducible.
 * v2 BUG this replaces: v2 used fixed discrete sprite sizes with
 * feet pinned to y=135 for every distance, drawn on top of the
 * walls with no occlusion — so a far wraith stood on the near
 * floor and showed through corners. v1's bug (pixel-doubled tiles
 * reading as 4 split figures) is also gone by construction: the
 * figure is drawn once from one texture, never tiled. */
static int8_t g_wx = 8, g_wy = 1;
static uint16_t g_rng = 0xACE1;
static uint8_t g_state = 0;
static uint8_t wraith_proof[8];

static void move_wraith(void) {
    static const int8_t NDX[4] = {0, 1, 0, -1};
    static const int8_t NDY[4] = {-1, 0, 1, 0};
    int8_t cx[4], cy[4];
    uint8_t n = 0, i, idx;
    for (i = 0; i < 4; i++) {
        int8_t nx = (int8_t)(g_wx + NDX[i]);
        int8_t ny = (int8_t)(g_wy + NDY[i]);
        if (cell_at(nx, ny) == 0) {
            cx[n] = nx;
            cy[n] = ny;
            n++;
        }
    }
    if (n == 0) return;
    g_rng = (uint16_t)(g_rng * 25173u + 13849u);
    idx = (uint8_t)(g_rng & 3);
    if (idx >= n) idx = (uint8_t)(n - 1);
    g_wx = cx[idx];
    g_wy = cy[idx];
}

/* Draw the wraith billboard into cur_tiles/cur_attrs, AFTER the
 * walls have been composed and BEFORE scan_diff. Must be called
 * from render_one_frame in that order. */
static void draw_wraith_billboard(void) {
    int8_t dx = 0, dy = 0, relx, rely, fwd, lat;
    int16_t depth, sprite_h, sprite_w, centre_x, draw_x0, draw_x1;
    int16_t draw_y0, draw_y1;
    int8_t col0, col1, row0, row1, c, r;
    /* Tile set selection: at fwd==1 the sprite is 64x128, exactly
     * the pixel-magnified 8x16 large set, so screen tiles map 1:1
     * and no tile repeats (the v3 eye-grid flaw was the 4x8 set
     * sampled 2x2 per tile at close range). At fwd>=2 the small
     * set's minification sampling is kept exactly as is. */
    uint8_t bg_cols, bg_rows;
    uint16_t bg_base;
    const unsigned char *bg_solid;
    if (g_state != 1) return;
    if (g_ang == 0) dx = 1;
    else if (g_ang == 64) dy = 1;
    else if (g_ang == 128) dx = -1;
    else dy = -1;
    relx = (int8_t)(g_wx - g_ptx);
    rely = (int8_t)(g_wy - g_pty);
    if (relx == 0 && rely == 0) return;
    fwd = (int8_t)(relx * dx + rely * dy);
    lat = (int8_t)(relx * (int8_t)-dy + rely * dx);
    if (fwd <= 0 || fwd > 4 || lat < -fwd || lat > fwd) return;
    /* Perpendicular depth in engine units, same units as zbuf. */
    depth = (int16_t)fwd * 256;
    /* Height from the SAME perspective as the walls: wall line_h is
     * 36864/perp, and for the wraith perp = fwd*256, so sprite_h =
     * 36864/(fwd*256) = 144/fwd exactly. Written this way on purpose:
     * the earlier 36864L/depth form made SDCC emit a 16-bit
     * __divsint in which 36864 wraps to a negative signed int, and
     * the height came out capped at 128 for every distance (a far
     * wraith rendered as tall as a near one). 144/fwd uses only
     * small positive ints and gives 128(capped)/72/48/36 for
     * fwd 1/2/3/4. */
    sprite_h = (int16_t)(144 / (int16_t)fwd);
    if (sprite_h > 128) sprite_h = 128;
    if (sprite_h < 16) sprite_h = 16;
    if (fwd == 1) {
        bg_cols = WRAITH_BG_LARGE_COLS;
        bg_rows = WRAITH_BG_LARGE_ROWS;
        bg_base = WRAITH_BG_LARGE_BASE;
        bg_solid = wraith_bg_solid_large;
    } else {
        bg_cols = WRAITH_BG_COLS;
        bg_rows = WRAITH_BG_ROWS;
        bg_base = WRAITH_BG_BASE;
        bg_solid = wraith_bg_solid;
    }
    sprite_w = sprite_h / 2;
    if (sprite_w < 8) sprite_w = 8;
    centre_x = (int16_t)(80 + (80 * (int16_t)lat) / (int16_t)fwd);
    draw_x0 = centre_x - sprite_w / 2;
    draw_x1 = centre_x + sprite_w / 2 - 1;
    draw_y0 = (int16_t)(72 - sprite_h / 2);
    draw_y1 = (int16_t)(72 + sprite_h / 2 - 1);
    if (draw_y0 < 0) draw_y0 = 0;
    if (draw_y1 > 143) draw_y1 = 143;
    col0 = (int8_t)(draw_x0 / 8);
    col1 = (int8_t)(draw_x1 / 8);
    if (draw_x0 < 0) col0 = 0;
    if (draw_x1 > 159) col1 = 19;
    row0 = (int8_t)(draw_y0 / 8);
    row1 = (int8_t)(draw_y1 / 8);
    for (c = col0; c <= col1; c++) {
        int16_t sample_x, tex_col;
        /* Per-column occlusion: a nearer wall hides this stripe. */
        if (depth >= zbuf[c]) continue;
        sample_x = (int16_t)(c * 8 + 4 - draw_x0);
        if (sample_x < 0) sample_x = 0;
        if (sample_x >= sprite_w) sample_x = sprite_w - 1;
        tex_col = sample_x * bg_cols / sprite_w;
        if (tex_col < 0) tex_col = 0;
        if (tex_col >= bg_cols) tex_col = bg_cols - 1;
        for (r = row0; r <= row1; r++) {
            int16_t sample_y, tex_row, tile_idx;
            sample_y = (int16_t)(r * 8 + 4 - draw_y0);
            if (sample_y < 0) sample_y = 0;
            if (sample_y >= sprite_h) sample_y = sprite_h - 1;
            tex_row = sample_y * bg_rows / sprite_h;
            if (tex_row < 0) tex_row = 0;
            if (tex_row >= bg_rows) tex_row = bg_rows - 1;
            tile_idx = tex_row * bg_cols + tex_col;
            if (!bg_solid[tile_idx]) continue;
            buf_cell(c, r, (uint8_t)(bg_base + tile_idx), 6);
        }
    }
}

static void render_one_frame(void) {
    int16_t px = (int16_t)g_ptx * 256 + 128;
    int16_t py = (int16_t)g_pty * 256 + 128;
    DISPLAY_OFF;
    invalidate_prev();
    compose_raycaster(px, py, g_ang);
    draw_wraith_billboard();
    scan_diff();
    write_list_all();
    sync_prev_cur();
    ch_count = 0;
    DISPLAY_ON;
}

void main(void) {
    uint8_t state = ST_TITLE;
    uint8_t prev_joy = 0, joy, held;
    uint8_t bump_timer = 0;
    uint8_t idle_frames = 0;
    uint8_t c;

    cpu_fast();
    SCX_REG = 0;
    set_bkg_data(0, TILE_COUNT, tiles);
    set_bkg_palette(0, 8, (palette_color_t *)PALS);
    hide_all_sprites();
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    for (c = 0; c < 20; c++)
        cam_x[c] = (int16_t)(((int16_t)(2 * c - 19) * 256) / 19);

    draw_title();

    while (1) {
        uint8_t pressed, dir = 0;
        wait_vbl_done();
        /* Idle frames render NOTHING: no compose, no diff, no VRAM writes.
         * Only the chunked writer is kept as a safety net, and it is
         * normally idle because render_one_frame() leaves ch_count = 0. */
        if (ch_count) write_list_chunked();
        joy = joypad();
        held = joy;
        pressed = (uint8_t)(joy & (uint8_t)~prev_joy);
        prev_joy = joy;
        if (bump_timer) bump_timer--;

        if (state == ST_TITLE) {
            if (pressed & J_START) {
                g_ptx = 1; g_pty = 1; g_ang = 0;
                g_wx = 8; g_wy = 1; g_rng = 0xACE1;
                idle_frames = 0;
                state = ST_PLAY;
                g_state = 1;
                render_one_frame();
            }
        } else if (state == ST_EXIT) {
            if (pressed & J_START) {
                state = ST_TITLE;
                g_state = 0;
                ch_count = 0;
                draw_title();
            }
        } else {
            /* ST_PLAY, instant tile-step. Tap = pressed edge.
             * Hold-to-repeat: if a direction is still held after
             * 20 idle frames, treat it as one more tap, then reset
             * the counter so repeats come every ~20 frames. */
            if (pressed & J_UP) dir = 1;
            else if (pressed & J_DOWN) dir = 2;
            else if (pressed & J_LEFT) dir = 3;
            else if (pressed & J_RIGHT) dir = 4;
            else if (idle_frames >= 20) {
                if (held & J_UP) dir = 1;
                else if (held & J_DOWN) dir = 2;
                else if (held & J_LEFT) dir = 3;
                else if (held & J_RIGHT) dir = 4;
            }
            if (dir == 1 || dir == 2) {
                int8_t dx = 0, dy = 0, tx, ty;
                uint8_t cell;
                /* facing: 0 east, 64 south, 128 west, 192 north */
                if (g_ang == 0) dx = 1;
                else if (g_ang == 64) dy = 1;
                else if (g_ang == 128) dx = -1;
                else dy = -1;
                if (dir == 2) { dx = (int8_t)-dx; dy = (int8_t)-dy; }
                tx = (int8_t)(g_ptx + dx);
                ty = (int8_t)(g_pty + dy);
                cell = cell_at(tx, ty);
                if (cell == 0 || cell == 3) {
                    g_ptx = tx; g_pty = ty;
                    if (cell == 0) move_wraith();
                    sfx_step();
                    render_one_frame();
                    if (cell == 3) {
                        state = ST_EXIT;
                        ch_count = 0;
                        draw_exit();
                        sfx_exit();
                    }
                } else {
                    if (bump_timer == 0) {
                        sfx_bump();
                        bump_timer = 20;
                    }
                    /* No render: view is unchanged. */
                }
                idle_frames = 0;
            } else if (dir == 3 || dir == 4) {
                if (dir == 4) g_ang = (uint8_t)(g_ang + 64);
                else g_ang = (uint8_t)(g_ang - 64);
                sfx_step();
                render_one_frame();
                idle_frames = 0;
            } else {
                if (held) idle_frames++;
                else idle_frames = 0;
            }
        }
        proof_block[0] = 'R'; proof_block[1] = 'C';
        proof_block[2] = state;
        proof_block[3] = (uint8_t)g_ptx;
        proof_block[4] = (uint8_t)g_pty;
        proof_block[5] = g_ang;
        proof_block[6] = 0xA5;
        proof_block[7] = (uint8_t)((state + (uint8_t)g_ptx + (uint8_t)g_pty + g_ang) & 0xFF);
        /* Wraith QA proof: 'W','R',wx,wy,0,0,0xA5,checksum. */
        wraith_proof[0] = 'W'; wraith_proof[1] = 'R';
        wraith_proof[2] = (uint8_t)g_wx;
        wraith_proof[3] = (uint8_t)g_wy;
        wraith_proof[4] = 0;
        wraith_proof[5] = 0;
        wraith_proof[6] = 0xA5;
        wraith_proof[7] = (uint8_t)(((uint8_t)g_wx + (uint8_t)g_wy) & 0xFF);
    }
}
