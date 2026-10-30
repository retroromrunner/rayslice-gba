#!/usr/bin/env python3
"""Generate src/tiles.h for MAZE ROAMER (v2).

Wall tiles: 2 distinct wall TYPES (not texture offsets), each with
full, top-edge (8) and bottom-edge (8) forms. Edge tiles use a palette
whose 4 colours are [ceiling, wall-dark, wall-light, floor], so one
tile can carry ceiling + wall + floor in a single row.
Top-edge tiles take pattern rows r..7 and bottom-edge tiles take
pattern rows 0..r, so the 8-row brick course cycle runs continuously
down a wall face instead of restarting per tile.
Type 0 = slate blocks (light face, fine courses, offset joints).
Type 1 = cinder blocks (dark face, chunky course, heavy mortar).
Floor tiles are brown dirt speckles (values 1/2/3 = dark/mid/light
brown in the floor palette).
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "src", "tiles.h")

_seven = open(os.path.expanduser("~/workspace/sevenfold/gen_tiles.py")).read()
_ns = {}
exec(_seven[_seven.index("FONT_ORDER"):_seven.index("# ---------------------------------------------------------------- portraits")], _ns)
FONT_ORDER, FONT = _ns["FONT_ORDER"], _ns["FONT"]


def tile_bytes4(rows, name):
    assert len(rows) == 8, f"{name}: need 8 rows"
    out = []
    for r in rows:
        assert len(r) == 8, f"{name}: row {r!r} is not 8 chars"
        lo = hi = 0
        for ch in r:
            v = int(ch)
            lo = (lo << 1) | (v & 1)
            hi = (hi << 1) | ((v >> 1) & 1)
        out.append(lo)
        out.append(hi)
    return out


def tile_bytes_font(rows, name):
    return tile_bytes4(["".join("3" if ch == "#" else "0" for ch in r) for r in rows], name)


# Slate: light face (2), dark mortar (1) at rows 3 and 7, joints
# offset between courses (x=3 upper course, x=6 lower course).
SLATE = [
    "22212222",
    "21212222",
    "22212222",
    "11111111",
    "22222212",
    "21222212",
    "22222212",
    "11111111",
]
# Cinder: dark face (1) with light speckles (2), heavy mortar gap (0)
# at x=4 every course, one full mortar row (0) at row 7.
CINDER = [
    "11110211",
    "12110111",
    "11110121",
    "11110211",
    "12110111",
    "11110121",
    "11110211",
    "00000000",
]
WALL_PATTERNS = [SLATE, CINDER]

SCENE = []
SCENE.append(("T_SKY", ["00000000"] * 8))
SCENE.append(("T_STAR", ["00000000", "00100000", "00000000", "00000010",
                          "00000000", "00000000", "01000000", "00000000"]))
# Brown dirt floors: 1 = dark brown, 2 = mid brown, 3 = light brown clods.
SCENE.append(("T_FLOOR", ["12131211", "11121131", "21131111", "11112111",
                           "13121111", "11131121", "21111131", "11121111"]))
SCENE.append(("T_FLOOR2", ["11131111", "12111121", "11121131", "31111111",
                            "11112111", "21131111", "11111121", "13121111"]))
# wall tiles per type: full, then top/bottom edge tiles sliced from
# the 2D pattern so brick courses run continuously down the wall.
for v in (0, 1):
    pat = WALL_PATTERNS[v]
    SCENE.append((f"T_WALL{v}", pat))
    for r in range(8):
        SCENE.append((f"T_WALL{v}_TOP{r}", ["00000000"] * r + pat[r:]))
        SCENE.append((f"T_WALL{v}_BOT{r}", pat[:r + 1] + ["33333333"] * (7 - r)))


def emit_array(fh, name, data):
    fh.write(f"static const unsigned char {name}[] = {{\n")
    for i in range(0, len(data), 16):
        fh.write("  " + ",".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",\n")
    fh.write("};\n")


# Stone Wraith BG billboard texture, v3 (sprite casting).
# ONE master texture, 32x64, split into 4x8 = 32 BG tiles. The C code
# samples this texture per screen tile column/row at whatever size
# the sprite-casting math gives for the wraith's distance, so there
# are no discrete sprite sizes and no pixel-doubling at all.
# Values: 0=background (drawn as the palette's dark colour in partial
# tiles, skipped entirely for all-background tiles — see solid flags
# below), 1=pale hood/highlights, 2=mid cloak, 3=glowing eyes.
# The silhouette fills its box well: the cloak flares to full width
# at the bottom and the hood is wide at the top, so partial tiles
# read as cloak, not holes.
def wraith_bg_master():
    rows = []
    cx = 16
    for y in range(64):
        if y < 6:
            hw = 7 + y
        elif y < 14:
            hw = 13
        elif y < 22:
            hw = 14
        else:
            hw = 15
        row = ["0"] * 32
        for x in range(cx - hw, cx + hw):
            if not (0 <= x < 32):
                continue
            if y < 22:
                v = "1"  # pale hood
                if 10 <= y <= 18 and abs(x - cx) <= 6:
                    v = "2"  # dark face
                # Glowing eyes span TWO texture rows (12-19) on
                # purpose: at minification the per-tile sampler
                # skips some texture rows (at fwd=3 it skips row 1
                # entirely), so eyes confined to one row vanish at
                # some distances. Tall eye slits survive every
                # distance's sampling; at close range they read as
                # large glowing eyes, which suits a wraith.
                if 12 <= y <= 19 and x in (11, 12, 13, 19, 20, 21):
                    v = "3"  # glowing eyes
                row[x] = v
            else:
                v = "2"  # mid cloak
                if x in (cx - 10, cx - 2, cx + 6) and y % 4 == 0:
                    v = "1"  # pale streaks
                row[x] = v
        if y >= 60:
            # tattered hem: clear some edge pixels to background
            for x in range(32):
                if row[x] != "0" and ((x * 7 + y) % 5) < 1 and (
                        x < cx - hw + 3 or x >= cx + hw - 3):
                    row[x] = "0"
        rows.append("".join(row))
    return rows


def wraith_bg_tiles():
    """Returns (tile_bytes, solid_flags): one flag per 8x8 tile, 1 if
    the tile contains any figure pixel (value != 0), 0 if it is all
    background. The C code skips non-solid tiles so the wraith keeps
    its silhouette instead of becoming a rectangle."""
    art = wraith_bg_master()
    assert len(art) == 64 and all(len(r) == 32 for r in art)
    out = []
    solid = []
    for ty in range(8):
        for tx in range(4):
            sub = [row[tx * 8:(tx + 1) * 8]
                   for row in art[ty * 8:(ty + 1) * 8]]
            solid.append(1 if any(ch != "0" for r in sub for ch in r) else 0)
            out += tile_bytes4(sub, f"wraith bg {tx},{ty}")
    return out, solid


def wraith_bg_large_tiles():
    """Pixel-magnified close-range set: the SAME 32x64 master artwork
    scaled 2x by pixel duplication to 64x128, split into 8x16 = 128
    BG tiles. At fwd=1 the sprite is 64x128 = 8x16 screen tiles, so
    the C sampler maps screen tiles to these tiles 1:1 and NO tile
    repeats — the eye tile appears once, large, instead of as a grid
    of small eyes (the v3 flaw: the 32-tile set was sampled 2x2 per
    tile at close range, and a tile is atomic, so its eye pattern
    repeated). Returns (tile_bytes, solid_flags) like wraith_bg_tiles."""
    art = wraith_bg_master()
    big = []
    for row in art:
        doubled = "".join(ch * 2 for ch in row)  # 64 wide
        big.append(doubled)
        big.append(doubled)  # 128 rows
    assert len(big) == 128 and all(len(r) == 64 for r in big)
    out = []
    solid = []
    for ty in range(16):
        for tx in range(8):
            sub = [row[tx * 8:(tx + 1) * 8]
                   for row in big[ty * 8:(ty + 1) * 8]]
            solid.append(1 if any(ch != "0" for r in sub for ch in r) else 0)
            out += tile_bytes4(sub, f"wraith bg large {tx},{ty}")
    return out, solid


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    data = []
    with open(OUT, "w") as fh:
        fh.write("/* Generated by gen_tiles.py - do not edit by hand. */\n")
        for i, (name, rows) in enumerate(SCENE):
            fh.write(f"#define {name} {i}\n")
            data += tile_bytes4(rows, name)
        fh.write(f"#define SCENE_COUNT {len(SCENE)}\n")
        fh.write(f"#define FONT_BASE SCENE_COUNT\n")
        for ch in FONT_ORDER:
            assert ch in FONT, f"missing glyph {ch!r}"
            data += tile_bytes_font(FONT[ch], f"font {ch!r}")
        fh.write(f"/* FONT_ORDER = {''.join(FONT_ORDER)} */\n")
        bg_bytes, bg_solid = wraith_bg_tiles()
        large_bytes, large_solid = wraith_bg_large_tiles()
        assert len(bg_bytes) == 32 * 16 and len(large_bytes) == 128 * 16
        total = len(SCENE) + len(FONT_ORDER) + 32 + 128
        assert total <= 256, f"tile count {total} exceeds BG tile space"
        fh.write(f"#define WRAITH_BG_BASE (SCENE_COUNT + {len(FONT_ORDER)})\n")
        fh.write("#define WRAITH_BG_COLS 4\n")
        fh.write("#define WRAITH_BG_ROWS 8\n")
        fh.write("#define WRAITH_BG_COUNT 32\n")
        fh.write("#define WRAITH_BG_LARGE_BASE (WRAITH_BG_BASE + 32)\n")
        fh.write("#define WRAITH_BG_LARGE_COLS 8\n")
        fh.write("#define WRAITH_BG_LARGE_ROWS 16\n")
        fh.write("#define WRAITH_BG_LARGE_COUNT 128\n")
        fh.write(f"#define TILE_COUNT (SCENE_COUNT + {len(FONT_ORDER)} + 32 + 128)\n")
        data += bg_bytes
        data += large_bytes
        emit_array(fh, "tiles", data)
        fh.write("static const unsigned char wraith_bg_solid[] = {\n  "
                 + ",".join(str(v) for v in bg_solid) + ",\n};\n")
        fh.write("static const unsigned char wraith_bg_solid_large[] = {\n  "
                 + ",".join(str(v) for v in large_solid) + ",\n};\n")
    print(f"wrote {OUT}: {len(SCENE)} scene tiles + {len(FONT_ORDER)} font glyphs "
          f"+ 32 wraith + 128 wraith-large = {total} tiles")


if __name__ == "__main__":
    main()
