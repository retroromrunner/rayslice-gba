/* RAYCASTER INSTANT - GBC first-person maze prototype (not a full game).
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

/* maze: 1 = corn wall, 0 = open, 2 = scarecrow (blocks), 3 = exit */
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

static uint8_t cell_at(int8_t x, int8_t y) {
    char c;
    if (x < 0 || y < 0 || x >= MW || y >= MH) return 1;
    c = MAZE[y][x];
    if (c == '0') return 0;
    if (c == '2') return 2;
    if (c == '3') return 3;
    return 1;
}

/* palettes: wall palettes carry [sky, wall-dark, wall-light, floor]
 * so a single edge tile can hold ceiling + wall + floor. */
static const palette_color_t PALS[8][4] = {
    {RGB(6, 4, 14), RGB(10, 6, 20), RGB(18, 10, 26), RGB(26, 16, 30)}, /* 0 sky */
    {RGB(8, 5, 2), RGB(14, 9, 4), RGB(21, 14, 7), RGB(28, 21, 12)},   /* 1 floor */
    {RGB(6, 4, 14), RGB(8, 16, 4), RGB(26, 30, 12), RGB(8, 5, 2)},   /* 2 wall near */
    {RGB(6, 4, 14), RGB(5, 11, 3), RGB(16, 20, 8), RGB(8, 5, 2)},    /* 3 wall near, side */
    {RGB(6, 4, 14), RGB(3, 7, 2), RGB(13, 20, 6), RGB(8, 5, 2)},     /* 4 wall far */
    {RGB(6, 4, 14), RGB(2, 5, 2), RGB(10, 16, 5), RGB(8, 5, 2)},     /* 5 wall far, side */
    {RGB(6, 4, 14), RGB(12, 10, 4), RGB(30, 26, 12), RGB(8, 5, 2)},  /* 6 scarecrow */
    {RGB(6, 4, 14), RGB(12, 10, 4), RGB(22, 18, 8), RGB(30, 26, 12)}, /* 7 text */
};

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
        variant = (uint8_t)((wall_x >> 7) & 1);
        if (cell == 2) pal = 6;
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

static void draw_title(void) {
    DISPLAY_OFF;
    SCX_REG = 0;
    fill_all(T_SKY, 0);
    put_text(5, 4, "CORN MAZE", 7);
    put_text(3, 6, "RAYCASTER DEMO", 7);
    put_text(4, 11, "PRESS START", 7);
    put_text(2, 13, "UP/DOWN: STEP", 7);
    put_text(1, 14, "LEFT/RIGHT: TURN", 7);
    put_text(4, 16, "TAP TO STEP", 7);
    DISPLAY_ON;
}

static void draw_exit(void) {
    DISPLAY_OFF;
    SCX_REG = 0;
    fill_all(T_SKY, 0);
    put_text(4, 6, "YOU ESCAPED!", 7);
    put_text(1, 8, "THE FIELD LETS GO", 7);
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

static void render_one_frame(void) {
    int16_t px = (int16_t)g_ptx * 256 + 128;
    int16_t py = (int16_t)g_pty * 256 + 128;
    DISPLAY_OFF;
    invalidate_prev();
    compose_raycaster(px, py, g_ang);
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
    NR52_REG = 0x80;
    NR51_REG = 0xFF;
    NR50_REG = 0x77;
    SHOW_BKG;
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
                idle_frames = 0;
                state = ST_PLAY;
                render_one_frame();
            }
        } else if (state == ST_EXIT) {
            if (pressed & J_START) {
                state = ST_TITLE;
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
    }
}
