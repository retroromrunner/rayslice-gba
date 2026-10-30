/* AREA QUEST v4 - GBC first-person prototype (not a finished game).
 * v4 gate rule (direction-preserving, general): stepping onto a
 * gate cell travelling in direction D loads the destination area
 * with the player on destination_gate + D, FACING D -- continuing
 * away from the gate, never on a gate tile, never facing back
 * toward it. Forward gate -> destination return gate (1,1) + D;
 * return gate -> destination forward gate + D. Worked examples:
 * (2,1) west onto Meadow return (1,1) -> Dungeon (9,10) WEST
 * (gate (10,10) behind); (9,10) east onto Dungeon gate (10,10)
 * -> Meadow (2,1) EAST (gate (1,1) behind). Pressing Down
 * immediately after any transition steps back onto that area's
 * gate and returns, still direction-preserving -- that is the
 * intended back route, not a bounce. Reachable approaches and
 * their open arrivals (verified, see build.sh): Dungeon gate
 * west->(2,1)E, north->(1,2)S in Meadow; Meadow gate west->
 * (2,1)E, north->(1,2)S, south->(1,0)N in Crypt; Meadow return
 * east->(9,10)W, south->(10,9)N in Dungeon; Crypt return east->
 * (9,9)W, south->(10,8)N in Meadow. Unreachable (border) and
 * documented, not bugs: Dungeon gate east/south, Meadow gate
 * east, Meadow return east/north, Crypt return east/north.
 * v4 also adds: one Meadow NPC at (2,7) (walk-through billboard,
 * talk with A when adjacent and facing it, dialog covers only
 * the bottom half of the screen), and minimap fog of war
 * (Select minimap shows only step-on visited tiles per area,
 * persisting across returns, reset on a new game).
 * AREA QUEST - GBC first-person prototype (not a finished game).
 * Built on the Maze Roamer v3 instant raycaster: one DDA ray per
 * screen tile column, wall height in pixels with precomputed
 * top/bottom edge tiles, instant tile-step movement (tap = one
 * tile, turn = 90 degrees), one full LCD-off frame redraw per tap,
 * step/bump sounds as short bursts with length-enable on.
 * No battles, story, magic, stats, NPCs, or roaming monsters.
 * Instead: 3 areas with distinct tilesets, static keys rendered
 * with the v3 sprite-casting BG-billboard technique (camera
 * transform, horizon-centred perspective height, per-column
 * wall occlusion), and gates that unlock with keys.
 * Area 0 Stone Dungeon (brick/slate, dirt floor), Area 1 The
 * Meadow (open grass, tree line border, scattered trees/rocks),
 * Area 2 Mossy Crypt (mossy stone, dark floor). Area 0 holds the
 * Bronze Key at (5,3) in a side branch off the gate route, so
 * the gate can be seen locked before the key is taken; Area 1
 * holds the Silver Key for its gate,
 * Area 2 ends with a gold exit to a victory screen.
 * v3 (CORRECTED, mirrored-gate design -- the earlier separate
 * (1,4) return-gate design was discarded): a gate is a gate.
 * The return gate is the SAME gate in the mirrored position:
 * Area 1's forward gate (10,10) leads to Area 2 at (2,1) facing
 * east, with the gate directly behind at (1,1) in Area 2;
 * Area 2's forward gate (10,9) leads to Area 3 at (2,1) facing
 * east, with the gate behind at (1,1) in Area 3. Cell (1,1) in
 * Areas 1 and 2 is code 'B' (return gate, always open) but
 * renders EXACTLY like an unlocked forward gate: gold door,
 * same tile, same palette -- no separate colour. Stepping onto
 * (1,1) returns to the previous area at its forward-gate
 * approach cell facing east: Area 2 return -> Area 1 (9,10)
 * east (gate (10,10) ahead); Area 3 return -> Area 2 (9,9)
 * east (gate (10,9) ahead). Entry (2,1) is never a gate, so
 * standing on entry never bounces the player back. Map change
 * required for this: Dungeon (9,10) was a wall and is now open
 * (Dungeon row 10, col 9 opened), so (9,10) east is a real approach;
 * Meadow/Crypt (2,1) were already open. Inventory screen on
 * Start in play, minimap screen on Select in play (existing BG
 * tiles only: solid T_SKY cells coloured by dedicated minimap
 * palettes; return gates on the minimap are gold, like unlocked
 * forward gates, not teal).
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "tiles.h"
#include "trig.h"

#define MW 12
#define MH 12
/* Cell codes: '0' open, '1' stone/mossy wall, 'T' tree, 'R' rock,
 * 'G' gate to next area, 'B' return gate to previous area
 * (always open, v3), '3' final exit. */
static const char *MAPS[3][MH] = {
    {   /* Area 0: Stone Dungeon, Bronze Key at (8,9), gate (10,10) */
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
        "1000000110G1",
        "111111111111",
    },
    {   /* Area 1: The Meadow, Silver Key at (5,5), gate (10,9).
         * Border is a tree line, not stone; interior is wide open
         * with scattered single-cell trees and rocks. v3: (1,1)
         * is the return gate 'B' (gold, always open); entry is
         * (2,1) facing east after the Dungeon gate. */
        "TTTTTTTTTTTT",
        "TB000000000T",
        "T00T000R000T",
        "T0000000000T",
        "T0R00000T00T",
        "T0000000000T",
        "T000T0000R0T",
        "T0000000000T",
        "T00R000T000T",
        "T000000000GT",
        "T0000000000T",
        "TTTTTTTTTTTT",
    },
    {   /* Area 2: Mossy Crypt, final exit (10,10), no key.
         * v3: (1,1) is the return gate 'B' (gold, always open);
         * entry is (2,1) facing east after the Meadow gate.
         * v4: (1,0) and (1,2) opened so direction-preserving
         * gate travel has an open arrival cell for every
         * reachable approach: (1,2) is the arrival for a
         * southward Meadow-gate crossing and the south
         * approach to the Crypt return gate; (1,0) is the
         * arrival for a northward Meadow-gate crossing
         * (a border alcove: north of it is out of bounds,
         * so forward from there bumps and Down returns). */
        "101111111111",
        "1B0000000111",
        "101111010001",
        "100000010101",
        "101110110101",
        "100010000101",
        "111010111101",
        "100010100001",
        "101110101111",
        "100000101001",
        "111011000031",
        "111111111111",
    },
};
static const int8_t KEY_POS[2][2] = {{5, 3}, {5, 5}};
static const int8_t GATE_POS[2][2] = {{10, 10}, {10, 9}};
/* v3 mirrored return gates: (1,1) in Areas 1 and 2, the gate
 * directly behind the east entry (2,1). v4: there is no fixed
 * return destination any more -- gate travel is direction-
 * preserving (destination gate + step direction, facing that
 * direction), so RET_POS is only used as the forward-gate
 * destination gate, and return destinations are computed
 * from GATE_POS + D in the step handler. Entry (2,1) is open,
 * never a gate, so arrival never bounces back. */
static const int8_t RET_POS[2][2] = {{1, 1}, {1, 1}};
static const char *AREA_NAMES[3] = {"STONE DUNGEON", "THE MEADOW", "MOSSY CRYPT"};
static const uint8_t AREA_NAME_X[3] = {3, 5, 4};

static uint8_t cell_at(uint8_t area, int8_t x, int8_t y) {
    char c;
    if (area > 2 || x < 0 || y < 0 || x >= MW || y >= MH) return 1;
    c = MAPS[area][y][x];
    if (c == '0') return 0;
    if (c == 'T') return 'T';
    if (c == 'R') return 'R';
    if (c == 'G') return 'G';
    if (c == 'B') return 'B';
    if (c == '3') return 3;
    return 1;
}

/* Per-area BG palettes (8 each, loaded on area entry; CGB holds
 * only 8 at once, so they are switched, never held together).
 * Layout in every area: 0 ceiling/sky, 1 floor, 2 wall near,
 * 3 wall near side, 4 wall far, 5 locked gate, 6 key, 7 gold.
 * Meadow wall palette doubles for trees and rocks: tree pattern
 * uses values 1/2 (dark/light green), rock pattern uses values
 * 1/3 (dark/light grey), and colour 3 is light grey in the meadow
 * wall palettes, so both read correctly under one palette. */
static const palette_color_t AREA_PALS[3][8][4] = {
    {   /* Area 0: Stone Dungeon */
        {RGB(10, 10, 14), RGB(30, 22, 8), RGB(16, 16, 20), RGB(24, 20, 14)},
        {RGB(10, 10, 14), RGB(12, 7, 3), RGB(20, 12, 5), RGB(28, 18, 9)},
        {RGB(10, 10, 14), RGB(14, 14, 16), RGB(26, 26, 28), RGB(20, 12, 5)},
        {RGB(10, 10, 14), RGB(10, 10, 12), RGB(18, 18, 20), RGB(20, 12, 5)},
        {RGB(10, 10, 14), RGB(8, 8, 10), RGB(14, 14, 16), RGB(20, 12, 5)},
        {RGB(10, 10, 14), RGB(8, 6, 6), RGB(24, 8, 6), RGB(20, 12, 5)},
        {RGB(8, 8, 10), RGB(26, 20, 6), RGB(16, 12, 2), RGB(31, 28, 10)},
        {RGB(10, 10, 14), RGB(14, 10, 2), RGB(26, 20, 6), RGB(20, 12, 5)},
    },
    {   /* Area 1: The Meadow. Palette 3 is the v4 NPC palette:
         * compose_raycaster never uses palette 3 for Meadow
         * walls (Meadow walls use 2 near / 4 far regardless of
         * side, and palette 3 here was identical to palette 2),
         * so repurposing it costs nothing. Values: 0 dark
         * outline, 1 blue tunic, 2 skin, 3 brown hair/trousers. */
        {RGB(10, 18, 30), RGB(31, 31, 31), RGB(16, 22, 30), RGB(24, 28, 30)},
        {RGB(10, 18, 30), RGB(6, 14, 4), RGB(12, 22, 6), RGB(20, 28, 10)},
        {RGB(10, 18, 30), RGB(8, 14, 6), RGB(16, 26, 10), RGB(22, 22, 24)},
        {RGB(8, 8, 10), RGB(10, 14, 26), RGB(30, 22, 14), RGB(20, 12, 6)},
        {RGB(10, 18, 30), RGB(5, 10, 4), RGB(11, 18, 7), RGB(15, 15, 17)},
        {RGB(10, 18, 30), RGB(8, 6, 6), RGB(24, 8, 6), RGB(12, 22, 6)},
        {RGB(8, 8, 10), RGB(26, 20, 6), RGB(16, 12, 2), RGB(31, 28, 10)},
        {RGB(10, 18, 30), RGB(14, 10, 2), RGB(26, 20, 6), RGB(12, 22, 6)},
    },
    {   /* Area 2: Mossy Crypt */
        {RGB(4, 6, 5), RGB(20, 18, 8), RGB(8, 10, 8), RGB(14, 14, 12)},
        {RGB(4, 6, 5), RGB(6, 6, 7), RGB(12, 12, 13), RGB(18, 18, 19)},
        {RGB(4, 6, 5), RGB(8, 12, 7), RGB(16, 20, 12), RGB(12, 12, 13)},
        {RGB(4, 6, 5), RGB(6, 9, 5), RGB(12, 15, 9), RGB(12, 12, 13)},
        {RGB(4, 6, 5), RGB(4, 7, 4), RGB(9, 12, 7), RGB(12, 12, 13)},
        {RGB(4, 6, 5), RGB(8, 6, 6), RGB(24, 8, 6), RGB(12, 12, 13)},
        {RGB(8, 8, 10), RGB(26, 20, 6), RGB(16, 12, 2), RGB(31, 28, 10)},
        {RGB(4, 6, 5), RGB(14, 10, 2), RGB(26, 20, 6), RGB(12, 12, 13)},
    },
};

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

static int16_t cam_x[20];
static int16_t zbuf[20];
static uint8_t g_area = 0;
static uint8_t g_keys = 0; /* bit0 Bronze (area 0), bit1 Silver (area 1) */
static uint8_t g_state = 0;

/* v4 Meadow NPC: Area 1 only, tile (2,7), west meadow, off the
 * key/gate route, all four neighbours open, walk-through. */
static const int8_t NPC_POS[2] = {2, 7};

/* v4 minimap fog of war: step-on visited only, per area.
 * visited[area][y*12+x] = 1 once the player has stepped onto
 * that tile (entry tile on area load and gate-arrival tile on
 * any transition count). Persists across returns; cleared only
 * on a new game from the title screen. */
static uint8_t visited[3][144];

static void mark_visited(uint8_t area, int8_t x, int8_t y) {
    if (area > 2 || x < 0 || y < 0 || x >= MW || y >= MH) return;
    visited[area][(uint8_t)y * 12 + (uint8_t)x] = 1;
}

static void clear_visited(void) {
    uint8_t i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 144; j++) visited[i][j] = 0;
}

/* Angle for a travel step (dx,dy): east 0, south 64, west 128,
 * north 192 -- the same encoding g_ang already uses. */
static uint8_t angle_from_step(int8_t dx, int8_t dy) {
    if (dx == 1) return 0;
    if (dy == 1) return 64;
    if (dx == -1) return 128;
    return 192;
}

static uint8_t key_held(uint8_t area) {
    if (area == 0) return (uint8_t)(g_keys & 1);
    if (area == 1) return (uint8_t)(g_keys & 2);
    return 1;
}

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
                cell = cell_at(g_area, map_x, map_y);
                if (cell != 0) hit = 1;
                steps++;
            }
        }
        if (side == 0) perp = (int16_t)(sdx - ddx); else perp = (int16_t)(sdy - ddy);
        if (perp < 16) perp = 16;
        zbuf[col] = perp;
        line_h = (int16_t)(36864L / perp);
        if (line_h > 128) line_h = 128;
        top = (144 - line_h) / 2;
        bottom = top + line_h - 1;
        if (top < 0) top = 0;
        if (bottom > 143) bottom = 143;
        top_tile = (int8_t)(top / 8);
        bot_tile = (int8_t)(bottom / 8);
        top_edge = (int8_t)(top % 8);
        bot_edge = (int8_t)(bottom % 8);
        if (cell == 'G') variant = 6;
        else if (cell == 'B') variant = 6;
        else if (cell == 'T') variant = 2;
        else if (cell == 'R') variant = 3;
        else if (g_area == 2) variant = (uint8_t)(4 + (((uint8_t)(map_x + map_y)) & 1));
        else variant = (uint8_t)(((uint8_t)(map_x + map_y)) & 1);
        if (cell == 3) pal = 7;
        else if (cell == 'B') pal = 7;
        else if (cell == 'G') pal = key_held(g_area) ? 7 : 5;
        else if (g_area == 1) pal = (perp < 512) ? 2 : 4;
        else if (perp < 512) pal = (uint8_t)(2 + (side ? 1 : 0));
        else pal = 4;

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
                         (uint8_t)(FLOOR_BASE(g_area) + (((col + row) & 1) ? 1 : 0)), 1);
            }
        }
    }
}

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

static void load_area_palettes(uint8_t area) {
    set_bkg_palette(0, 8, (palette_color_t *)AREA_PALS[area]);
}

/* Text screens: background = the area's ceiling/sky, glyphs white,
 * glyph background = same ceiling colour so text has no dark box. */
static void text_palette_for(uint8_t area) {
    palette_color_t tp[4];
    tp[0] = AREA_PALS[area][0][0];
    tp[1] = RGB(14, 10, 2);
    tp[2] = RGB(26, 20, 6);
    tp[3] = RGB(31, 31, 31);
    set_bkg_palette(7, 1, tp);
}

static void draw_title(void) {
    DISPLAY_OFF;
    SCX_REG = 0;
    hide_all_sprites();
    load_area_palettes(0);
    text_palette_for(0);
    fill_all(T_SKY, 0);
    put_text(5, 4, "AREA QUEST", 7);
    put_text(4, 11, "PRESS START", 7);
    put_text(3, 13, "UP/DOWN: STEP", 7);
    put_text(1, 14, "LEFT/RIGHT: TURN", 7);
    put_text(6, 15, "A: TALK", 7);
    put_text(2, 16, "KEYS OPEN GATES", 7);
    DISPLAY_ON;
}

static void draw_victory(void) {
    DISPLAY_OFF;
    SCX_REG = 0;
    hide_all_sprites();
    load_area_palettes(2);
    text_palette_for(2);
    fill_all(T_SKY, 0);
    put_text(4, 6, "YOU ESCAPED!", 7);
    put_text(1, 8, "ALL AREAS CLEARED", 7);
    put_text(4, 11, "PRESS START", 7);
    put_text(1, 13, "TO WALK IT AGAIN", 7);
    DISPLAY_ON;
}

/* Forward declarations: g_keys/g_ptx/g_pty are defined below with
 * the other proof globals; the v3 overlay screens need them. */
static uint8_t g_keys;
static int8_t g_ptx;
static int8_t g_pty;

/* v3 inventory screen (Start in play; Start again to return).
 * Every line satisfies x + length <= 20. State is just the two
 * key flags; no new items yet. */
static void draw_inventory(void) {
    DISPLAY_OFF;
    SCX_REG = 0;
    hide_all_sprites();
    load_area_palettes(g_area);
    text_palette_for(g_area);
    fill_all(T_SKY, 0);
    put_text(5, 2, "INVENTORY", 7);
    put_text(AREA_NAME_X[g_area], 4, AREA_NAMES[g_area], 7);
    if (g_keys & 1) put_text(2, 6, "BRONZE KEY: YES", 7);
    else put_text(2, 6, "BRONZE KEY: --", 7);
    if (g_keys & 2) put_text(2, 7, "SILVER KEY: YES", 7);
    else put_text(2, 7, "SILVER KEY: --", 7);
    put_text(4, 10, "PRESS START", 7);
    put_text(4, 11, "TO CONTINUE", 7);
    DISPLAY_ON;
}

/* v3 minimap palettes (dedicated, restored on return to play by
 * load_area_palettes + render_one_frame). Cells are solid T_SKY
 * tiles, so only colour 0 of each palette shows:
 *  0 wall/tree/rock + background (dark), 1 open (light),
 *  2 player (bright yellow), 3 uncollected key (gold),
 *  4 forward gate locked (red), 5 forward gate unlocked /
 *  Area 3 exit / return gate (light gold -- a gate is a gate,
 *  return gates are NOT a different colour, corrected v3),
 *  6 spare (same light gold, unused),
 *  7 text (dark bg, white glyph value 3). */
static const palette_color_t MINIMAP_PALS[8][4] = {
    {RGB(8, 8, 12), RGB(8, 8, 12), RGB(8, 8, 12), RGB(8, 8, 12)},
    {RGB(24, 24, 24), RGB(24, 24, 24), RGB(24, 24, 24), RGB(24, 24, 24)},
    {RGB(31, 28, 10), RGB(31, 28, 10), RGB(31, 28, 10), RGB(31, 28, 10)},
    {RGB(24, 18, 4), RGB(24, 18, 4), RGB(24, 18, 4), RGB(24, 18, 4)},
    {RGB(24, 6, 6), RGB(24, 6, 6), RGB(24, 6, 6), RGB(24, 6, 6)},
    {RGB(28, 22, 8), RGB(28, 22, 8), RGB(28, 22, 8), RGB(28, 22, 8)},
    {RGB(28, 22, 8), RGB(28, 22, 8), RGB(28, 22, 8), RGB(28, 22, 8)},
    {RGB(4, 4, 6), RGB(4, 4, 6), RGB(4, 4, 6), RGB(31, 31, 31)},
};

/* v3 minimap screen (Select in play; Select or Start to return).
 * Current area only, 12x12 grid of one BG cell per map cell,
 * centred: x 4..15, y 3..14, area name at the top. Uses only
 * existing tiles (T_SKY + font); no new tile system.
 * v4 FOG OF WAR: only step-on visited tiles are shown, per
 * area, persisting across returns. Unvisited tiles draw as
 * blank dark cells (palette 0, same as a wall) with NO wall/
 * open/key/gate/exit information -- strict step-on only, the
 * raycast view itself is NOT fogged. The player's tile is
 * always visited, so it always shows. Consequence, correct
 * fog behaviour: a key tile only becomes visited by stepping
 * onto it, which collects it, so an uncollected key never
 * appears gold on the minimap. */
static void draw_minimap(void) {
    uint8_t mx, my;
    DISPLAY_OFF;
    SCX_REG = 0;
    hide_all_sprites();
    set_bkg_palette(0, 8, (palette_color_t *)MINIMAP_PALS);
    fill_all(T_SKY, 0);
    put_text(AREA_NAME_X[g_area], 1, AREA_NAMES[g_area], 7);
    for (my = 0; my < 12; my++) {
        for (mx = 0; mx < 12; mx++) {
            char c = MAPS[g_area][my][mx];
            uint8_t pal;
            if (mx == (uint8_t)g_ptx && my == (uint8_t)g_pty) pal = 2;
            else if (!visited[g_area][(uint8_t)my * 12 + mx]) pal = 0;
            else if (c == '1' || c == 'T' || c == 'R') pal = 0;
            else if (c == 'G') pal = key_held(g_area) ? 5 : 4;
            else if (c == 'B') pal = 5;
            else if (c == '3') pal = 5;
            else if (g_area <= 1 && !key_held(g_area)
                     && mx == (uint8_t)KEY_POS[g_area][0]
                     && my == (uint8_t)KEY_POS[g_area][1]) pal = 3;
            else pal = 1;
            set_cell((int8_t)(4 + mx), (int8_t)(3 + my), T_SKY, pal);
        }
    }
    put_text(2, 16, "SELECT TO CLOSE", 7);
    put_text(6, 17, "OR START", 7);
    DISPLAY_ON;
}

/* v4 dialog box (A when adjacent to and facing the Meadow NPC).
 * Covers ONLY the bottom half of the screen (rows 9-17, 72px),
 * leaving the top half raycast view visible and untouched:
 * it writes dialog background + text cells directly to the
 * bottom rows in VRAM (DISPLAY_OFF, write, DISPLAY_ON) and
 * never clears the screen, unlike Start/Select which take the
 * full screen. Text uses palette 6 (key gold: glyph colour 3
 * is light gold on dark colour 0) and NO palette is changed,
 * so the top half's palettes are untouched too. Every line
 * satisfies x + length <= 20. Closing restores the full view
 * with one render_one_frame() call. */
static void draw_dialog(void) {
    uint8_t x, y;
    DISPLAY_OFF;
    SCX_REG = 0;
    hide_all_sprites();
    for (y = 9; y < 18; y++)
        for (x = 0; x < 20; x++) set_cell((int8_t)x, (int8_t)y, T_SKY, 6);
    put_text(1, 10, "HELLO, TRAVELLER!", 6);
    put_text(3, 12, "THE SILVER KEY", 6);
    put_text(1, 13, "IS HIDDEN NEARBY.", 6);
    put_text(2, 16, "PRESS A TO CLOSE", 6);
    DISPLAY_ON;
}

static void sfx_step(void) {
    NR41_REG = 0x30;
    NR42_REG = 0xC1;
    NR43_REG = 0x60;
    NR44_REG = 0xC0;
}

static void sfx_bump(void) {
    NR11_REG = 0x80;
    NR12_REG = 0xF1;
    NR13_REG = 0x00;
    NR14_REG = 0xC6;
}

static void sfx_note(uint8_t lo, uint8_t hi) {
    NR11_REG = 0x80;
    NR12_REG = 0xF3;
    NR13_REG = lo;
    NR14_REG = hi;
}

static void sfx_pickup(void) {
    uint8_t i;
    sfx_note(0xAD, 0xC7);
    for (i = 0; i < 10; i++) wait_vbl_done();
    sfx_note(0xC0, 0xC7);
    for (i = 0; i < 14; i++) wait_vbl_done();
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
#define ST_INV 3
#define ST_MAP 4
#define ST_TALK 5
/* v2: no ST_INTRO — gates load the next area immediately.
 * v3: ST_INV (inventory, Start to return) and ST_MAP (minimap,
 * Select or Start to return) are overlay states on top of play;
 * g_state stays 1 through them, and returning to play reloads
 * the area palettes and renders one fresh frame. */

static uint8_t proof_block[8];
static uint8_t key_proof[8];
static int8_t g_ptx = 1, g_pty = 1;
static uint8_t g_ang = 0;

/* ---- Static key billboard: v3 sprite casting, for items ----
 * Same technique as the v3 wraith (Lode Raycasting III, per-column
 * ZBuffer occlusion), but for a SMALL floor-standing item, drawn
 * at TILE coordinates, not pixel coordinates:
 * - transform the key's tile-centre offset into camera space
 *   (fwd = tiles ahead, lat = lateral tiles), screen centre tile
 *   from lat/fwd;
 * - floor-anchored in tile terms: floor_tile = (72 + wall_h/2)/8,
 *   the key's bottom tile is floor_tile-1, so its feet sit on the
 *   floor line at its distance, never floating mid-air and never
 *   sunk into the floor tile;
 * - one tile set PER DISTANCE (gen_tiles.py KEY_SIZES), each
 *   downscaled from the master key art to that distance's exact
 *   tile-aligned size: fwd 1: 16x32 (2x4 tiles), fwd 2: 8x24
 *   (1x3), fwd 3: 8x16 (1x2), fwd 4: 8x8 (1x1). Screen tiles map
 *   1:1 to texture tiles (tex = screen - origin), so NO screen
 *   tile ever repeats a texture tile at any distance. This is the
 *   fix for the v1 overlapping clips: v1 sampled a fixed tile set
 *   per screen tile centre, so at 2 tiles the key spanned 5
 *   screen rows against a 4-row texture and the key shape
 *   duplicated; v2 cannot do that, by construction.
 * Per-column occlusion is unchanged: a column is skipped when
 * key depth >= zbuf[col], so a key behind a tree/wall is hidden.
 * NOT hardware sprites, NOT a fixed-size overlay. */
static void draw_key_billboard(void) {
    int8_t dx = 0, dy = 0, relx, rely, fwd, lat;
    int16_t depth, wall_h, floor_tile, centre_tile;
    int8_t col0, col1, row0, row1, c, r;
    uint8_t bg_cols, bg_rows;
    uint16_t bg_base;
    const unsigned char *bg_solid;
    int8_t kx, ky;
    if (g_state != 1 || g_area > 1) return;
    if (key_held(g_area)) return;
    kx = KEY_POS[g_area][0];
    ky = KEY_POS[g_area][1];
    if (g_ang == 0) dx = 1;
    else if (g_ang == 64) dy = 1;
    else if (g_ang == 128) dx = -1;
    else dy = -1;
    relx = (int8_t)(kx - g_ptx);
    rely = (int8_t)(ky - g_pty);
    if (relx == 0 && rely == 0) return;
    fwd = (int8_t)(relx * dx + rely * dy);
    lat = (int8_t)(relx * (int8_t)-dy + rely * dx);
    if (fwd <= 0 || fwd > 4 || lat < -fwd || lat > fwd) return;
    depth = (int16_t)fwd * 256;
    wall_h = (int16_t)(144 / (int16_t)fwd);
    if (wall_h > 128) wall_h = 128;
    if (fwd == 1) {
        bg_cols = KEY_D1_COLS; bg_rows = KEY_D1_ROWS;
        bg_base = KEY_D1_BASE; bg_solid = key_solid_d1;
    } else if (fwd == 2) {
        bg_cols = KEY_D2_COLS; bg_rows = KEY_D2_ROWS;
        bg_base = KEY_D2_BASE; bg_solid = key_solid_d2;
    } else if (fwd == 3) {
        bg_cols = KEY_D3_COLS; bg_rows = KEY_D3_ROWS;
        bg_base = KEY_D3_BASE; bg_solid = key_solid_d3;
    } else {
        bg_cols = KEY_D4_COLS; bg_rows = KEY_D4_ROWS;
        bg_base = KEY_D4_BASE; bg_solid = key_solid_d4;
    }
    floor_tile = (int16_t)((72 + wall_h / 2) / 8);
    row1 = (int8_t)(floor_tile - 1);
    row0 = (int8_t)(row1 - bg_rows + 1);
    centre_tile = (int16_t)((80 + (80 * (int16_t)lat) / (int16_t)fwd) / 8);
    col0 = (int8_t)(centre_tile - bg_cols / 2);
    col1 = (int8_t)(col0 + bg_cols - 1);
    for (c = col0; c <= col1; c++) {
        int8_t tex_col;
        if (c < 0 || c > 19) continue;
        if (depth >= zbuf[c]) continue;
        tex_col = (int8_t)(c - col0);
        if (tex_col < 0 || tex_col >= bg_cols) continue;
        for (r = row0; r <= row1; r++) {
            int8_t tex_row;
            uint8_t tile_idx;
            if (r < 0 || r > 17) continue;
            tex_row = (int8_t)(r - row0);
            if (tex_row < 0 || tex_row >= bg_rows) continue;
            tile_idx = (uint8_t)(tex_row * bg_cols + tex_col);
            if (!bg_solid[tile_idx]) continue;
            buf_cell(c, r, (uint8_t)(bg_base + tile_idx), 6);
        }
    }
}

/* Forward distance (tiles ahead, 1..4) of a billboard target in
 * the player's view cone, or 0 if it is not visible. Used only
 * to order billboard drawing: the farther billboard is drawn
 * first so the nearer one correctly overwrites it where their
 * columns overlap (keys and the NPC are both in the Meadow). */
static int8_t billboard_fwd(int8_t tx, int8_t ty) {
    int8_t dx = 0, dy = 0, relx, rely, fwd, lat;
    if (g_ang == 0) dx = 1;
    else if (g_ang == 64) dy = 1;
    else if (g_ang == 128) dx = -1;
    else dy = -1;
    relx = (int8_t)(tx - g_ptx);
    rely = (int8_t)(ty - g_pty);
    if (relx == 0 && rely == 0) return 0;
    fwd = (int8_t)(relx * dx + rely * dy);
    lat = (int8_t)(relx * (int8_t)-dy + rely * dx);
    if (fwd <= 0 || fwd > 4 || lat < -fwd || lat > fwd) return 0;
    return fwd;
}

/* ---- v4 Meadow NPC billboard: same sprite-casting technique
 * as the keys (camera transform, perspective height, per-column
 * ZBuffer occlusion, floor-anchored, tile-coordinate placement
 * with one tile set per distance so no tile ever repeats), but
 * for a TALLER standing figure: about half wall height at
 * 1 tile (64px vs 128), shrinking with distance. Tile sets
 * (gen_tiles.py NPC_SIZES): fwd 1: 32x64 (4x8 tiles), fwd 2:
 * 16x32 (2x4), fwd 3: 16x24 (2x3), fwd 4: 8x16 (1x2).
 * Palette 3 in the Meadow is the NPC palette (see AREA_PALS:
 * Meadow walls never use palette 3, it was identical to
 * palette 2). The NPC never blocks movement -- its tile is an
 * open map cell, the player walks through it, and the billboard
 * hides when on the player's tile, exactly like the old
 * wraith walk-through rule. Meadow only. */
static void draw_npc_billboard(void) {
    int8_t dx = 0, dy = 0, relx, rely, fwd, lat;
    int16_t depth, wall_h, floor_tile, centre_tile;
    int8_t col0, col1, row0, row1, c, r;
    uint8_t bg_cols, bg_rows;
    uint16_t bg_base;
    const unsigned char *bg_solid;
    if (g_state != 1 || g_area != 1) return;
    if (g_ang == 0) dx = 1;
    else if (g_ang == 64) dy = 1;
    else if (g_ang == 128) dx = -1;
    else dy = -1;
    relx = (int8_t)(NPC_POS[0] - g_ptx);
    rely = (int8_t)(NPC_POS[1] - g_pty);
    if (relx == 0 && rely == 0) return;
    fwd = (int8_t)(relx * dx + rely * dy);
    lat = (int8_t)(relx * (int8_t)-dy + rely * dx);
    if (fwd <= 0 || fwd > 4 || lat < -fwd || lat > fwd) return;
    depth = (int16_t)fwd * 256;
    wall_h = (int16_t)(144 / (int16_t)fwd);
    if (wall_h > 128) wall_h = 128;
    if (fwd == 1) {
        bg_cols = NPC_D1_COLS; bg_rows = NPC_D1_ROWS;
        bg_base = NPC_D1_BASE; bg_solid = npc_solid_d1;
    } else if (fwd == 2) {
        bg_cols = NPC_D2_COLS; bg_rows = NPC_D2_ROWS;
        bg_base = NPC_D2_BASE; bg_solid = npc_solid_d2;
    } else if (fwd == 3) {
        bg_cols = NPC_D3_COLS; bg_rows = NPC_D3_ROWS;
        bg_base = NPC_D3_BASE; bg_solid = npc_solid_d3;
    } else {
        bg_cols = NPC_D4_COLS; bg_rows = NPC_D4_ROWS;
        bg_base = NPC_D4_BASE; bg_solid = npc_solid_d4;
    }
    floor_tile = (int16_t)((72 + wall_h / 2) / 8);
    row1 = (int8_t)(floor_tile - 1);
    row0 = (int8_t)(row1 - bg_rows + 1);
    centre_tile = (int16_t)((80 + (80 * (int16_t)lat) / (int16_t)fwd) / 8);
    col0 = (int8_t)(centre_tile - bg_cols / 2);
    col1 = (int8_t)(col0 + bg_cols - 1);
    for (c = col0; c <= col1; c++) {
        int8_t tex_col;
        if (c < 0 || c > 19) continue;
        if (depth >= zbuf[c]) continue;
        tex_col = (int8_t)(c - col0);
        if (tex_col < 0 || tex_col >= bg_cols) continue;
        for (r = row0; r <= row1; r++) {
            int8_t tex_row;
            uint8_t tile_idx;
            if (r < 0 || r > 17) continue;
            tex_row = (int8_t)(r - row0);
            if (tex_row < 0 || tex_row >= bg_rows) continue;
            tile_idx = (uint8_t)(tex_row * bg_cols + tex_col);
            if (!bg_solid[tile_idx]) continue;
            buf_cell(c, r, (uint8_t)(bg_base + tile_idx), 3);
        }
    }
}

static void render_one_frame(void) {
    int16_t px = (int16_t)g_ptx * 256 + 128;
    int16_t py = (int16_t)g_pty * 256 + 128;
    DISPLAY_OFF;
    invalidate_prev();
    compose_raycaster(px, py, g_ang);
    /* Farther billboard first, so the nearer one wins where
     * their columns overlap (Meadow key + NPC). */
    if (g_area <= 1 && !key_held(g_area)
        && billboard_fwd(NPC_POS[0], NPC_POS[1])
        > billboard_fwd(KEY_POS[g_area][0], KEY_POS[g_area][1])) {
        draw_npc_billboard();
        draw_key_billboard();
    } else {
        draw_key_billboard();
        draw_npc_billboard();
    }
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
    load_area_palettes(0);
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
        if (ch_count) write_list_chunked();
        joy = joypad();
        held = joy;
        pressed = (uint8_t)(joy & (uint8_t)~prev_joy);
        prev_joy = joy;
        if (bump_timer) bump_timer--;

        if (state == ST_TITLE) {
            if (pressed & J_START) {
                g_ptx = 1; g_pty = 1; g_ang = 0;
                g_area = 0; g_keys = 0;
                clear_visited();
                mark_visited(0, 1, 1);
                idle_frames = 0;
                state = ST_PLAY;
                g_state = 1;
                load_area_palettes(0);
                render_one_frame();
            }
        } else if (state == ST_EXIT) {
            if (pressed & J_START) {
                state = ST_TITLE;
                g_state = 0;
                ch_count = 0;
                draw_title();
            }
            /* Select on title/victory is a no-op (no branch here
             * or in ST_TITLE reads it). */
        } else if (state == ST_INV) {
            if (pressed & J_START) {
                state = ST_PLAY;
                g_state = 1;
                idle_frames = 0;
                load_area_palettes(g_area);
                render_one_frame();
            }
        } else if (state == ST_MAP) {
            if ((pressed & J_SELECT) || (pressed & J_START)) {
                state = ST_PLAY;
                g_state = 1;
                idle_frames = 0;
                load_area_palettes(g_area);
                render_one_frame();
            }
        } else if (state == ST_TALK) {
            /* v4 dialog: ONLY A (again) or Start closes it.
             * Movement, turns, gates, inventory and minimap
             * are all ignored while dialog is open. Closing
             * restores the full view with one frame render;
             * no palette was changed by the dialog, so no
             * palette reload is needed. */
            if ((pressed & J_A) || (pressed & J_START)) {
                state = ST_PLAY;
                g_state = 1;
                idle_frames = 0;
                render_one_frame();
            }
        } else if (pressed & J_START) {
            /* v3 inventory: edge-triggered Start in play only;
             * it moves nothing and starts nothing else. */
            state = ST_INV;
            ch_count = 0;
            draw_inventory();
        } else if (pressed & J_SELECT) {
            /* v3 minimap: edge-triggered Select in play only. */
            state = ST_MAP;
            ch_count = 0;
            draw_minimap();
        } else if (pressed & J_A) {
            /* v4 talk: edge-triggered A in play only, and ONLY
             * when adjacent to and facing the Meadow NPC
             * (NPC tile == player tile + facing direction).
             * A anywhere else does nothing at all. */
            int8_t fdx = 0, fdy = 0;
            if (g_ang == 0) fdx = 1;
            else if (g_ang == 64) fdy = 1;
            else if (g_ang == 128) fdx = -1;
            else fdy = -1;
            if (g_area == 1
                && (int8_t)(g_ptx + fdx) == NPC_POS[0]
                && (int8_t)(g_pty + fdy) == NPC_POS[1]) {
                state = ST_TALK;
                ch_count = 0;
                draw_dialog();
            }
        } else {
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
                if (g_ang == 0) dx = 1;
                else if (g_ang == 64) dy = 1;
                else if (g_ang == 128) dx = -1;
                else dy = -1;
                if (dir == 2) { dx = (int8_t)-dx; dy = (int8_t)-dy; }
                tx = (int8_t)(g_ptx + dx);
                ty = (int8_t)(g_pty + dy);
                cell = cell_at(g_area, tx, ty);
                if (cell == 0) {
                    g_ptx = tx; g_pty = ty;
                    mark_visited(g_area, tx, ty);
                    if (g_area <= 1 && !key_held(g_area)
                        && tx == KEY_POS[g_area][0] && ty == KEY_POS[g_area][1]) {
                        if (g_area == 0) g_keys = (uint8_t)(g_keys | 1);
                        else g_keys = (uint8_t)(g_keys | 2);
                        render_one_frame();
                        sfx_pickup();
                    } else {
                        sfx_step();
                        render_one_frame();
                    }
                } else if (cell == 'G') {
                    if (key_held(g_area)) {
                        /* v4 DIRECTION-PRESERVING seamless
                         * transition (general rule, all gates):
                         * destination = destination gate + the
                         * step direction D, facing D -- the
                         * player continues AWAY from the gate,
                         * never lands on a gate tile, never
                         * faces back toward it. Forward gate:
                         * destination gate is the next area's
                         * return gate (1,1). E.g. east onto
                         * (10,10) -> (2,1) east; south onto
                         * (10,10) -> (1,2) south; north onto
                         * (10,9) -> (1,0) north (Crypt border
                         * alcove, opened in v4). The source
                         * gate tile itself counts as visited
                         * (stepped onto it) for fog of war. */
                        int8_t dgx = RET_POS[g_area][0];
                        int8_t dgy = RET_POS[g_area][1];
                        mark_visited(g_area, tx, ty);
                        g_area++;
                        g_ptx = (int8_t)(dgx + dx);
                        g_pty = (int8_t)(dgy + dy);
                        g_ang = angle_from_step(dx, dy);
                        mark_visited(g_area, g_ptx, g_pty);
                        idle_frames = 0;
                        state = ST_PLAY;
                        g_state = 1;
                        load_area_palettes(g_area);
                        sfx_step();
                        render_one_frame();
                    } else {
                        if (bump_timer == 0) {
                            sfx_bump();
                            bump_timer = 20;
                        }
                    }
                } else if (cell == 'B') {
                    /* v4 DIRECTION-PRESERVING seamless backtrack
                     * (same general rule as forward gates):
                     * destination = previous area's forward
                     * gate + the step direction D, facing D.
                     * Return gates are always open (arrival
                     * already unlocked that direction), never
                     * block, never need a key, and render gold
                     * like any unlocked gate. E.g. west onto
                     * (1,1) -> (9,10) WEST in the Dungeon
                     * (gate (10,10) behind, to the east), so
                     * forward from there moves to (8,10),
                     * NOT back through the gate -- the v3 bug
                     * was landing at (9,10) facing EAST, back
                     * toward the gate, so forward walked
                     * straight back through it. The source
                     * return-gate tile counts as visited. */
                    uint8_t da = (uint8_t)(g_area - 1);
                    int8_t dgx = GATE_POS[da][0];
                    int8_t dgy = GATE_POS[da][1];
                    mark_visited(g_area, tx, ty);
                    g_area = da;
                    g_ptx = (int8_t)(dgx + dx);
                    g_pty = (int8_t)(dgy + dy);
                    g_ang = angle_from_step(dx, dy);
                    mark_visited(g_area, g_ptx, g_pty);
                    idle_frames = 0;
                    state = ST_PLAY;
                    g_state = 1;
                    load_area_palettes(g_area);
                    sfx_step();
                    render_one_frame();
                } else if (cell == 3) {
                    g_ptx = tx; g_pty = ty;
                    mark_visited(g_area, tx, ty);
                    sfx_step();
                    render_one_frame();
                    state = ST_EXIT;
                    g_state = 0;
                    ch_count = 0;
                    draw_victory();
                    sfx_exit();
                } else {
                    if (bump_timer == 0) {
                        sfx_bump();
                        bump_timer = 20;
                    }
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
        proof_block[0] = 'A'; proof_block[1] = 'Q';
        proof_block[2] = state;
        proof_block[3] = g_area;
        proof_block[4] = (uint8_t)g_ptx;
        proof_block[5] = (uint8_t)g_pty;
        proof_block[6] = g_ang;
        proof_block[7] = (uint8_t)((state + g_area + (uint8_t)g_ptx + (uint8_t)g_pty + g_ang + g_keys) & 0xFF);
        key_proof[0] = 'K'; key_proof[1] = 'Y';
        key_proof[2] = (uint8_t)(g_keys & 1);
        key_proof[3] = (uint8_t)((g_keys >> 1) & 1);
        key_proof[4] = 0;
        key_proof[5] = 0;
        key_proof[6] = 0xA5;
        key_proof[7] = (uint8_t)(((g_keys & 1) + ((g_keys >> 1) & 1)) & 0xFF);
    }
}
