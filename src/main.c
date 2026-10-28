/* RAYCASTER GBA - Doom-style first-person corn maze prototype.
 *
 * Bare-metal GBA, Mode 4 bitmap (240x160, 8-bit palette, page flip).
 * One DDA ray per screen column (240 columns), textured corn walls,
 * distance + side shading, smooth continuous movement and turning,
 * walking bob, and short PSG bursts only (no constant noise).
 *
 * Why GBA after the GBC prototype: the GBC version ran at ~20 tile
 * columns and was unplayably slow. The ARM7 at 16.7 MHz renders 240
 * columns with a full framebuffer fill + wall strips in a few ms,
 * so this version runs at full frame rate with real animation.
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed char s8; typedef short s16; typedef int s32;
#define REG16(a) (*(volatile u16*)(a))
#define REG_DISPCNT REG16(0x04000000)
#define REG_VCOUNT REG16(0x04000006)
#define REG_KEY REG16(0x04000130)
#define PAL ((volatile u16*)0x05000000)
#define FB0 ((volatile u8*)0x06000000)
#define FB1 ((volatile u8*)0x0600A000)
#define SRAM ((volatile u8*)0x0E000000)
#define SND(x) REG16(0x04000060 + (x))
#define REG_SOUNDCNT_L REG16(0x04000080)
#define REG_SOUNDCNT_H REG16(0x04000082)
#define REG_SOUNDCNT_X REG16(0x04000084)
#define KEY_A 0x0001
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

/* ---- palette + texture ---- */
static u8 corn_tex[64 * 64];
static void build_palette(void) {
    int i, s;
    static const int base_r[4] = {4, 10, 18, 28};
    static const int base_g[4] = {10, 18, 26, 31};
    static const int base_b[4] = {3, 5, 8, 13};
    static const int fac[6] = {16, 12, 12, 10, 10, 8};
    PAL[0] = RGB15(6, 4, 14);
    PAL[1] = RGB15(8, 5, 2);
    PAL[2] = RGB15(21, 14, 7);
    PAL[3] = RGB15(30, 26, 12);
    for (s = 0; s < 6; s++) {
        for (i = 0; i < 4; i++) {
            PAL[16 + s * 4 + i] = RGB15(base_r[i] * fac[s] / 16, base_g[i] * fac[s] / 16, base_b[i] * fac[s] / 16);
        }
    }
    /* scarecrow straw: bases 40..47 (near/far, 4 shades each) */
    {
        static const int sr[4] = {12, 18, 24, 30};
        static const int sg[4] = {10, 15, 20, 26};
        static const int sb[4] = {4, 6, 9, 12};
        for (s = 0; s < 2; s++) {
            int f = s ? 8 : 16;
            for (i = 0; i < 4; i++) PAL[40 + s * 4 + i] = RGB15(sr[i] * f / 16, sg[i] * f / 16, sb[i] * f / 16);
        }
    }
    for (i = 48; i < 256; i++) PAL[i] = 0;
}
static void build_texture(void) {
    int tx, ty;
    for (ty = 0; ty < 64; ty++) {
        for (tx = 0; tx < 64; tx++) {
            int c;
            int stalk = tx & 7;
            if (stalk == 0) c = 0;
            else if (stalk == 1) c = 2;
            else c = 1;
            if (((tx * 7 + ty * 5) & 7) == 0) c = 2;
            if ((ty & 31) == 0 && (tx & 3) != 0) c = 3;
            corn_tex[ty * 64 + tx] = (u8)c;
        }
    }
}

/* ---- framebuffer text ---- */
static int glyph_index(char c) {
    int i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (i = 0; FONT_ORDER[i]; i++) if (FONT_ORDER[i] == c) return i;
    return 0;
}
static void draw_text(volatile u8 *fb, int x, int y, const char *str, int scale, int color) {
    int ci, gx, gy, sx, sy;
    for (ci = 0; str[ci]; ci++) {
        int g = glyph_index(str[ci]);
        for (gy = 0; gy < 8; gy++) {
            int bits = FONT_GLYPHS[g * 8 + gy];
            for (gx = 0; gx < 8; gx++) {
                if (bits & (1 << (7 - gx))) {
                    for (sy = 0; sy < scale; sy++) for (sx = 0; sx < scale; sx++) {
                        int px = x + ci * 8 * scale + gx * scale + sx;
                        int py = y + gy * scale + sy;
                        if (px >= 0 && px < 240 && py >= 0 && py < 160) fb[py * 240 + px] = (u8)color;
                    }
                }
            }
        }
    }
}
static void fill_rect(volatile u8 *fb, int x0, int y0, int x1, int y1, int color) {
    int x, y;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) {
        if (x >= 0 && x < 240 && y >= 0 && y < 160) fb[y * 240 + x] = (u8)color;
    }
}
static void draw_title(void) {
    volatile u8 *fb = FB0;
    int i;
    for (i = 0; i < 240 * 160; i++) fb[i] = 0;
    fill_rect(fb, 0, 132, 239, 159, 1);
    draw_text(fb, 48, 20, "CORN MAZE", 2, 3);
    draw_text(fb, 24, 44, "INSTANT STEP", 2, 3);
    draw_text(fb, 52, 84, "PRESS START", 1, 3);
    draw_text(fb, 40, 100, "UP: FORWARD  DOWN: BACK", 1, 3);
    draw_text(fb, 36, 110, "LEFT/RIGHT: TURN", 1, 3);
    draw_text(fb, 76, 120, "TAP TO STEP", 1, 3);
    REG_DISPCNT = (u16)(4 | 0x0400);
}
static void draw_exit(void) {
    volatile u8 *fb = FB0;
    int i;
    for (i = 0; i < 240 * 160; i++) fb[i] = 0;
    fill_rect(fb, 0, 120, 239, 159, 1);
    draw_text(fb, 24, 30, "YOU ESCAPED!", 2, 3);
    draw_text(fb, 16, 54, "THE FIELD LETS GO", 1, 3);
    draw_text(fb, 52, 84, "PRESS START", 1, 3);
    draw_text(fb, 16, 100, "TO WALK IT AGAIN", 1, 3);
    REG_DISPCNT = (u16)(4 | 0x0400);
}

/* ---- sound: short PSG bursts ---- */
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

/* ---- raycaster ---- */
static int cam_x[240];
static int bob_phase = 0;

static void render_frame(volatile u8 *fb, int px, int py, int ang) {
    int dir_x = COS256[ang], dir_y = SIN256[ang];
    int plane_x = -(dir_y * 169) / 256;
    int plane_y = (dir_x * 169) / 256;
    int bob = (SIN256[bob_phase & 255] * 3) / 256;
    int x, y;
    /* ceiling (top half) + floor (bottom half), 32-bit fills */
    {
        volatile u32 *fb32 = (volatile u32*)fb;
        for (y = 0; y < 80; y++) {
            u32 w = 0x00000000u;
            for (x = 0; x < 60; x++) fb32[y * 60 + x] = w;
        }
        for (y = 80; y < 160; y++) {
            u32 c = ((y >> 2) & 1) ? 2u : 1u;
            u32 w = c | (c << 8) | (c << 16) | (c << 24);
            for (x = 0; x < 60; x++) fb32[y * 60 + x] = w;
        }
    }
    for (x = 0; x < 240; x++) {
        int ray_x = dir_x + plane_x * cam_x[x] / 256;
        int ray_y = dir_y + plane_y * cam_x[x] / 256;
        int map_x = px >> 8, map_y = py >> 8;
        int ddx, ddy, sdx, sdy, step_x, step_y, side = 0, hit = 0, cell = 1;
        int perp, line_h, orig_top, top, bottom, tex_step, tex_pos, tex_x, shade;
        int steps = 0;
        int ax = iabs(ray_x), ay = iabs(ray_y);
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
        /* Wall height: 184/perp is the correct projection for a 240-wide
           screen at 66 deg FOV (far walls look closer, corridors shorter),
           capped at 144 so a close wall never fills the whole screen:
           at least 8px of sky and 8px of floor always stay visible. */
        line_h = (perp < 4096) ? LINE_H_TABLE[perp] : 144;
        orig_top = (160 - line_h) / 2 + bob;
        top = orig_top < 0 ? 0 : orig_top;
        bottom = orig_top + line_h - 1;
        if (bottom > 159) bottom = 159;
        if (top > 159 || bottom < 0) continue;
        tex_step = (line_h > 0 && line_h <= 144) ? TEX_STEP_TABLE[line_h] : 256;
        tex_pos = (top - orig_top) * tex_step;
        {
            int wall_x;
            if (side == 0) wall_x = py + (perp * ray_y) / 256;
            else wall_x = px + (perp * ray_x) / 256;
            tex_x = (wall_x & 255) >> 2;
        }
        if (cell == 2) shade = (perp < 512) ? 40 : 44;
        else if (perp < 512) shade = 16 + (side ? 4 : 0);
        else if (perp < 1024) shade = 24 + (side ? 4 : 0);
        else shade = 32 + (side ? 4 : 0);
        {
            volatile u8 *p = fb + top * 240 + x;
            for (y = top; y <= bottom; y++) {
                int tex_y = tex_pos >> 8;
                if (tex_y < 0) tex_y = 0; if (tex_y > 63) tex_y = 63;
                *p = (u8)(shade + corn_tex[tex_y * 64 + tex_x]);
                p += 240;
                tex_pos += tex_step;
            }
        }
    }
}

#define ST_TITLE 0
#define ST_PLAY 1
#define ST_EXIT 2

/* Tile-step play: Wizardry-style. Tap Up/Down to step one tile forward/back,
 * tap Left/Right to turn 90 degrees. Each action animates over 4 frames
 * (gliding position, or rotating angle, with a walking bob on steps),
 * then the game goes idle and renders NOTHING until the next tap.
 * That is the speed fix for a browser emulator: free movement re-rendered
 * a full 240x160 bitmap every frame, so any emulator slowdown dragged
 * walking to a crawl. Tile-step renders only 4 frames per action, and
 * per-frame divisions are replaced by precomputed tables (ARM has no
 * divide instruction, so a software division per column was the drag). */
#define MOVE_STEPS 1
#define TURN_STEPS 1
int main(void) {
    int state = ST_TITLE;
    int ptx = 1, pty = 1, facing = 0; /* 0=east 1=south 2=west 3=north */
    int px = (1 * 256) + 128, py = (1 * 256) + 128, ang = 0;
    int prev_keys = 0, frame = 0, bump_timer = 0, page = 0, c;
    int anim = 0; /* 0 idle, 1 move, 2 turn */
    int anim_step = 0, anim_from_px = 0, anim_from_py = 0, anim_to_px = 0, anim_to_py = 0;
    int anim_from_ang = 0, anim_to_ang = 0, anim_to_tx = 0, anim_to_ty = 0, anim_to_facing = 0;
    int idle_frames = 0;
    static const int fdx[4] = {1, 0, -1, 0};
    static const int fdy[4] = {0, 1, 0, -1};
    build_palette();
    build_texture();
    audio_init();
    for (c = 0; c < 240; c++) cam_x[c] = ((2 * c - 239) * 256) / 239;
    draw_title();
    while (1) {
        int keys, pressed, held;
        vsync();
        keys = (~REG_KEY) & 0x03FF;
        pressed = keys & ~prev_keys;
        prev_keys = keys;
        held = keys;
        if (state == ST_TITLE) {
            if (pressed & KEY_START) {
                ptx = 1; pty = 1; facing = 0;
                px = (1 * 256) + 128; py = (1 * 256) + 128; ang = 0;
                anim = 0; idle_frames = 0;
                state = ST_PLAY;
                page = page ? 0 : 1;
                render_frame(page ? FB1 : FB0, px, py, ang);
                REG_DISPCNT = (u16)(4 | 0x0400 | (page ? 0x0010 : 0));
            }
            frame++;
        } else if (state == ST_EXIT) {
            if (pressed & KEY_START) {
                state = ST_TITLE;
                draw_title();
            }
            frame++;
        } else if (anim == 0) {
            /* idle: no rendering at all until an action starts */
            int dir = 0; /* 1 fwd, 2 back, 3 left, 4 right */
            if (pressed & KEY_UP) dir = 1;
            else if (pressed & KEY_DOWN) dir = 2;
            else if (pressed & KEY_LEFT) dir = 3;
            else if (pressed & KEY_RIGHT) dir = 4;
            else if (idle_frames >= 18) {
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
                    anim = 1; anim_step = 0;
                    anim_from_px = px; anim_from_py = py;
                    anim_to_px = tx * 256 + 128; anim_to_py = ty * 256 + 128;
                    anim_to_tx = tx; anim_to_ty = ty;
                    sfx_step();
                } else if (bump_timer == 0) {
                    sfx_bump(); bump_timer = 20; idle_frames = 0;
                }
            } else if (dir == 3 || dir == 4) {
                anim = 2; anim_step = 0;
                anim_from_ang = facing * 64;
                if (dir == 4) { anim_to_ang = anim_from_ang + 64; anim_to_facing = (facing + 1) & 3; }
                else { anim_to_ang = anim_from_ang - 64; anim_to_facing = (facing + 3) & 3; }
                sfx_step();
            }
            if (bump_timer) bump_timer--;
            if (dir) idle_frames = 0; else idle_frames++;
            frame++;
        } else {
            /* animating: one interpolated frame per game frame */
            anim_step++;
            if (anim == 1) {
                px = anim_from_px + (anim_to_px - anim_from_px) * anim_step / MOVE_STEPS;
                py = anim_from_py + (anim_to_py - anim_from_py) * anim_step / MOVE_STEPS;
                bob_phase = (bob_phase + 64) & 255;
            } else {
                ang = anim_from_ang + (anim_to_ang - anim_from_ang) * anim_step / TURN_STEPS;
            }
            page = page ? 0 : 1;
            render_frame(page ? FB1 : FB0, px, py, ang & 255);
            REG_DISPCNT = (u16)(4 | 0x0400 | (page ? 0x0010 : 0));
            if (anim == 1 && anim_step >= MOVE_STEPS) {
                ptx = anim_to_tx; pty = anim_to_ty;
                px = anim_to_px; py = anim_to_py;
                bob_phase = 0;
                anim = 0; idle_frames = 0;
                if (cell_at(ptx, pty) == 3) {
                    state = ST_EXIT;
                    draw_exit();
                    sfx_exit();
                }
            } else if (anim == 2 && anim_step >= TURN_STEPS) {
                facing = anim_to_facing;
                ang = facing * 64;
                anim = 0; idle_frames = 0;
            }
            if (bump_timer) bump_timer--;
            frame++;
        }
        SRAM[0] = 'R'; SRAM[1] = 'C';
        SRAM[2] = (u8)state;
        SRAM[3] = (u8)ptx; SRAM[4] = (u8)pty; SRAM[5] = (u8)(ang & 255);
        SRAM[6] = (u8)((frame >> 8) & 255); SRAM[7] = (u8)(frame & 255);
        SRAM[8] = (u8)anim; SRAM[9] = (u8)facing;
    }
    return 0;
}
