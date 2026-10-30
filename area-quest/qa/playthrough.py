"""Area Quest PyBoy QA (v5, facing-preserving gates + NPC + fog).
Tap = button_press + 2 ticks + release + settle (holds, NOT pulses).
Proof: 'A','Q',state,area,ptx,pty,ang,checksum(state+area+ptx+pty+ang+keys)
and 'K','Y',bronze,silver,0,0,0xA5,checksum(bronze+silver) in WRAM.
States: 0 title, 1 play, 2 victory, 3 inventory, 4 minimap,
5 dialog (no intro state in v2/v3/v4).
Maps (12x12): area 0 dungeon (Bronze Key (5,3), gate (10,10)),
area 1 meadow (Silver Key (5,5), gate (10,9), return gate (1,1),
entry (2,1), NPC (2,7)), area 2 crypt (exit (10,10), return
gate (1,1), entry (2,1); v4 opened (1,2) and (1,0) for
direction-preserving gate arrivals).
v5 gate rule (CORRECTS v4): stepping onto a gate travelling in
direction D lands on destination_gate + D, FACING UNCHANGED.
Forward step (facing == travel): gate ends up behind you, so
Down (back) returns through it. Backward step (facing ==
opposite travel): gate ends up ahead of you, so Up (forward)
returns through it. Oscillation back/forward/back/forward with
NO turning is proven for both gates, both facing directions,
plus north/south approaches forward and backward.
Minimap fog of war (v4): only step-on visited tiles show;
unvisited cells are dark, persisting across returns, reset
on a new game. A key tile only becomes visited by stepping
onto it (which collects it), so uncollected keys never show
gold on the minimap -- correct fog behaviour.
"""
import os, sys, math
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
sys.path.insert(0, os.path.expanduser("~/workspace/gbc/tools/venv/lib/python3.12/site-packages"))
from pyboy import PyBoy
from PIL import Image
from collections import deque

ROM = os.path.expanduser("~/workspace/area-quest/releases/area-quest.gbc")
OUT = os.path.expanduser("~/workspace/area-quest/qa/shots")
os.makedirs(OUT, exist_ok=True)

MAPS = [
    ["111111111111", "100010000111", "101110111001", "101000100011",
     "100011101011", "111010100011", "100010111011", "101110000011",
     "100000111111", "111011000001", "1000000110G1", "111111111111"],
    ["TTTTTTTTTTTT", "TB000000000T", "T00T000R000T", "T0000000000T",
     "T0R00000T00T", "T0000000000T", "T000T0000R0T", "T0000000000T",
     "T00R000T000T", "T000000000GT", "T0000000000T", "TTTTTTTTTTTT"],
    ["101111111111", "1B0000000111", "101111010001", "100000010101",
     "101110110101", "100010000101", "111010111101", "100010100001",
     "101110101111", "100000101001", "111011000031", "111111111111"],
]
NPC = (2, 7)  # Meadow only, walk-through, talk with A when adjacent + facing
DIRS = [(1, 0), (0, 1), (-1, 0), (0, -1)]
ANG = {(1, 0): 0, (0, 1): 64, (-1, 0): 128, (0, -1): 192}

checks = []
def check(label, ok, detail=""):
    checks.append(ok)
    print(f"{'PASS' if ok else 'FAIL'} {label} {detail}")

def tick(pb, n):
    for _ in range(n):
        pb.tick()

def tap(pb, btn, settle=30, press=2):
    pb.button_press(btn); tick(pb, press); pb.button_release(btn); tick(pb, settle)

def scan_aq(pb):
    mem = pb.memory
    for a in range(0xC000, 0xE000 - 8):
        if mem[a] == 0x41 and mem[a+1] == 0x51:
            state, area, ptx, pty, ang = mem[a+2], mem[a+3], mem[a+4], mem[a+5], mem[a+6]
            if state <= 5 and area <= 2 and ptx <= 11 and pty <= 11 and ang in (0, 64, 128, 192):
                return (state, area, ptx, pty, ang)
    return None

def scan_keys(pb):
    mem = pb.memory
    for a in range(0xC000, 0xE000 - 8):
        if mem[a] == 0x4B and mem[a+1] == 0x59 and mem[a+6] == 0xA5:
            b, s = mem[a+2], mem[a+3]
            if b <= 1 and s <= 1 and mem[a+7] == ((b + s) & 0xFF):
                return (b, s)
    return (0, 0)

def state_of(pb):
    return scan_aq(pb), scan_keys(pb)

def shot(pb, name):
    path = os.path.join(OUT, name)
    pb.screen.image.save(path)
    return path

def bfs(area, start, goal, allow, avoid=()):
    m = MAPS[area]
    q = deque([start]); prev = {start: None}
    while q:
        cur = q.popleft()
        if cur == goal:
            break
        for d in DIRS:
            nx, ny = cur[0] + d[0], cur[1] + d[1]
            if not (0 <= nx < 12 and 0 <= ny < 12):
                continue
            if (nx, ny) in avoid:
                continue
            if m[ny][nx] not in allow:
                continue
            if (nx, ny) not in prev:
                prev[(nx, ny)] = cur
                q.append((nx, ny))
    if goal not in prev:
        return None
    path, cur = [], goal
    while cur is not None:
        path.append(cur); cur = prev[cur]
    return path[::-1]

def face(pb, want_ang):
    for _ in range(4):
        p, _ = state_of(pb)
        if p and p[4] == want_ang:
            return True
        cur = p[4] if p else 0
        diff = (want_ang - cur) % 256
        tap(pb, "right" if diff in (64, 128) else "left")
    p, _ = state_of(pb)
    return bool(p and p[4] == want_ang)

def step_to(pb, area, path):
    for nxt in path[1:]:
        p, _ = state_of(pb)
        cur = (p[2], p[3])
        d = (nxt[0] - cur[0], nxt[1] - cur[1])
        if not face(pb, ANG[d]):
            return False
        tap(pb, "up")
        tick(pb, 60)
        p, _ = state_of(pb)
        if not p or (p[2], p[3]) != nxt:
            return False
    return True

def go(pb, area, goal, allow="0", avoid=()):
    p, _ = state_of(pb)
    path = bfs(area, (p[2], p[3]), goal, allow, avoid)
    if path is None:
        return False
    return step_to(pb, area, path)

def go_return(pb, area, dest):
    """Step onto the (1,1) return gate in `area`; the final step
    transitions, so step only to the approach cell, then tap up
    and expect `dest` = (state, area, ptx, pty, ang)."""
    p, _ = state_of(pb)
    path = bfs(area, (p[2], p[3]), (1, 1), "0B")
    if path is None or len(path) < 2:
        return False
    if not step_to(pb, area, path[:-1]):
        return False
    approach = path[-2]
    d = (1 - approach[0], 1 - approach[1])
    if not face(pb, ANG[d]):
        return False
    tap(pb, "up")
    tick(pb, 40)
    p, _ = state_of(pb)
    return bool(p == dest)

def count_px(img_path, pred, box=None):
    im = Image.open(img_path).convert("RGB")
    x0, y0, x1, y1 = box or (0, 0, im.width, im.height)
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if pred(im.getpixel((x, y))):
                n += 1
    return n

def is_gold(px):
    r, g, b = px[:3]
    return r > 150 and 80 < g < 220 and b < 110 and r > g

def is_red(px):
    r, g, b = px[:3]
    return r > 120 and g < 110 and b < 110 and r > g + 40

def is_green(px):
    r, g, b = px[:3]
    return g > 60 and g > r and g > b

def is_blue_sky(px):
    r, g, b = px[:3]
    return b > 120 and b > r + 30

def is_white(px):
    r, g, b = px[:3]
    return r > 200 and g > 200 and b > 200

def is_npc_blue(px):
    # NPC tunic: RGB(10,14,26) -> (82,114,214)
    r, g, b = px[:3]
    return b > 150 and b > r + 50 and g > 80

def is_npc_skin(px):
    # NPC skin: RGB(30,22,14) -> (247,181,115)
    r, g, b = px[:3]
    return r > 200 and 150 < g < 210 and 80 < b < 150

def is_dialog_gold(px):
    # Dialog text: palette 6 glyph colour 3 = RGB(31,28,10) -> (255,231,82)
    r, g, b = px[:3]
    return r > 200 and g > 180 and b < 150 and r > g

def gold_bbox(img_path, box=None):
    im = Image.open(img_path).convert("RGB")
    x0, y0, x1, y1 = box or (0, 0, im.width, im.height)
    xs, ys = [], []
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = im.getpixel((x, y))[:3]
            if r >= 200 and 150 <= g <= 235 and 40 <= b <= 90:
                xs.append(x); ys.append(y)
    if not xs:
        return None
    return (min(xs), min(ys), max(xs), max(ys), len(xs))

def check_key(label, img_path, exp_h, exp_w, fwd=1, lat=0):
    wall_h = min(128, 144 // fwd)
    floor_y = 72 + wall_h // 2
    centre_x = 80 + (80 * lat) // fwd
    box = (max(0, centre_x - 24), max(0, floor_y - exp_h - 8),
           min(160, centre_x + 24), floor_y)
    bb = gold_bbox(img_path, box)
    if bb is None:
        check(label, False, "no gold pixels in centre")
        return
    h = bb[3] - bb[1] + 1
    w = bb[2] - bb[0] + 1
    ok = abs(h - exp_h) <= 8 and abs(w - exp_w) <= 8
    check(label, ok, f"gold bbox h={h} (exp {exp_h}) w={w} (exp {exp_w}) px={bb[4]}")

# --- v3 minimap pixel checks ---
# Minimap grid: x 4..15 tiles, y 3..14 tiles, 8 px per cell.
# Palette colour 0 values (5-bit) -> 8-bit via v*255//31.
def c5(v):
    return v * 255 // 31

MINI = {
    "wall": (c5(8), c5(8), c5(12)),
    "open": (c5(24), c5(24), c5(24)),
    "player": (c5(31), c5(28), c5(10)),
    "key": (c5(24), c5(18), c5(4)),
    "locked": (c5(24), c5(6), c5(6)),
    "unlocked": (c5(28), c5(22), c5(8)),
    # No teal: return gates are gold, same as unlocked gates.
}

def mini_px(img_path, mx, my):
    im = Image.open(img_path).convert("RGB")
    return im.getpixel(((4 + mx) * 8 + 4, (3 + my) * 8 + 4))

def mini_close(px, exp):
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(px[:3], exp))) < 45

def check_mini(label, img_path, mx, my, kind):
    px = mini_px(img_path, mx, my)
    check(label, mini_close(px, MINI[kind]), f"cell ({mx},{my}) px={px[:3]} exp {kind}={MINI[kind]}")

def check_inventory(pb, label, shot_name):
    """Open inventory with Start, verify state 3 + unchanged
    position/keys, screenshot, close with Start, verify back."""
    p0, k0 = state_of(pb)
    tap(pb, "start", 40)
    p, k = state_of(pb)
    ok_open = bool(p and p[0] == 3 and (p[1], p[2], p[3], p[4]) == (p0[1], p0[2], p0[3], p0[4]) and k == k0)
    img = shot(pb, shot_name)
    check(f"{label}: inventory opens (state 3, pos/keys unchanged)",
          ok_open, f"before={p0} keys={k0} after={p} keys={k}")
    check(f"{label}: inventory screen has white text",
          count_px(img, is_white) >= 100, f"white_px={count_px(img, is_white)}")
    tap(pb, "start", 40)
    p, k = state_of(pb)
    check(f"{label}: Start closes inventory back to play, unchanged",
          bool(p and p[0] == 1 and (p[1], p[2], p[3], p[4]) == (p0[1], p0[2], p0[3], p0[4]) and k == k0),
          f"proof={p} keys={k}")

def check_minimap(pb, label, shot_name, close_btn, cells):
    """Open minimap with Select, verify state 4 + unchanged
    position/keys, screenshot + cell checks, close, verify back."""
    p0, k0 = state_of(pb)
    tap(pb, "select", 40)
    p, k = state_of(pb)
    ok_open = bool(p and p[0] == 4 and (p[1], p[2], p[3], p[4]) == (p0[1], p0[2], p0[3], p0[4]) and k == k0)
    img = shot(pb, shot_name)
    check(f"{label}: minimap opens (state 4, pos/keys unchanged)",
          ok_open, f"before={p0} keys={k0} after={p} keys={k}")
    for (mx, my, kind, cell_label) in cells:
        check_mini(f"{label}: {cell_label}", img, mx, my, kind)
    tap(pb, close_btn, 40)
    p, k = state_of(pb)
    check(f"{label}: {close_btn} closes minimap back to play, unchanged",
          bool(p and p[0] == 1 and (p[1], p[2], p[3], p[4]) == (p0[1], p0[2], p0[3], p0[4]) and k == k0),
          f"proof={p} keys={k}")

pb = PyBoy(ROM, window="null")
pb.set_emulation_speed(0)
tick(pb, 90)
shot(pb, "title.png")
p, k = state_of(pb)
check("title state", p is not None and p[0] == 0, f"proof={p} keys={k}")

tap(pb, "start", 40)
shot(pb, "area1-view.png")
p, k = state_of(pb)
check("start -> area 0 (1,1) east", p == (1, 0, 1, 1, 0), f"proof={p}")

# v4 FOG OF WAR, early: at start only the entry tile is visited.
# Unvisited open/key/gate cells all draw dark (no information).
check_minimap(pb, "minimap fog early area 0", "minimap-fog-early.png", "select", [
    (1, 1, "player", "player/entry cell (1,1) bright"),
    (2, 1, "wall", "unvisited open cell (2,1) dark"),
    (5, 3, "wall", "unvisited key cell (5,3) dark (fog, not gold)"),
    (10, 10, "wall", "unvisited gate cell (10,10) dark (fog, not gold)"),
])

ok = go(pb, 0, (10, 9), "0", avoid={(5, 3)}) and face(pb, 64)
p, k = state_of(pb)
check("reach gate approach (10,9) facing south", ok and p and (p[2], p[3]) == (10, 9) and p[4] == 64, f"proof={p}")
tap(pb, "up")
p, k = state_of(pb)
check("locked gate blocks (no pass, still PLAY)", p and (p[2], p[3]) == (10, 9) and p[0] == 1 and k == (0, 0), f"proof={p} keys={k}")
locked0 = shot(pb, "gate-locked-0.png")
check("locked gate shows red in centre view", count_px(locked0, is_red, (50, 20, 110, 124)) >= 20,
      f"red_px={count_px(locked0, is_red, (50, 20, 110, 124))}")

ok = go(pb, 0, (4, 3), "0", avoid={(5, 3)}) and face(pb, 0)
p, k = state_of(pb)
check("stand west of Bronze Key facing east", ok and p and (p[2], p[3]) == (4, 3) and p[4] == 0, f"proof={p}")
key0 = shot(pb, "key-0.png")
check("Bronze Key billboard visible (gold centre)", count_px(key0, is_gold, (40, 20, 120, 124)) >= 20,
      f"gold_px={count_px(key0, is_gold, (40, 20, 120, 124))}")
check_key("Bronze Key dist1: ONE small key 32x16, no repeats", key0, 32, 16, fwd=1, lat=0)
tap(pb, "down")
p, k = state_of(pb)
check("step back to (3,3) facing east", p and (p[2], p[3]) == (3, 3) and p[4] == 0, f"proof={p}")
keyd2 = shot(pb, "key-dist2.png")
check_key("Bronze Key dist2: ONE small key 24x8, no repeats", keyd2, 24, 8, fwd=2, lat=0)
tap(pb, "up")
p, k = state_of(pb)
check("step forward to (4,3) facing east", p and (p[2], p[3]) == (4, 3) and p[4] == 0, f"proof={p}")
tap(pb, "up")
tick(pb, 60)
p, k = state_of(pb)
check("pick up Bronze Key", p and (p[2], p[3]) == (5, 3) and k == (1, 0), f"proof={p} keys={k}")

# v4: inventory after Bronze (Bronze YES, Silver --), minimap area 0.
# Fog: the forward gate (10,10) is still unvisited (only approached,
# never stepped onto), so it draws dark -- NOT gold. Walked tiles
# (entry, key route) are light; unwalked open cells stay dark.
check_inventory(pb, "inventory after Bronze", "inventory-bronze.png")
check_minimap(pb, "minimap area 0", "minimap-area1.png", "select", [
    (5, 3, "player", "player cell (5,3) bright"),
    (1, 1, "open", "walked open cell (1,1) light"),
    (0, 0, "wall", "wall cell (0,0) dark"),
    (10, 10, "wall", "unvisited gate (10,10) dark (fog, not gold)"),
    (4, 3, "open", "walked open cell (4,3) light"),
    (5, 1, "wall", "unwalked open cell (5,1) dark (spur only reachable via the key tile, never walked)"),
])

# v5 NORTH approach to the Dungeon gate: from (10,9) facing south
# (D=south, forward step) the gate lands on Meadow (1,2) facing
# SOUTH (gate (1,1) behind, to the north). Forward south must
# move AWAY (to (1,3)). Down (back north) returns to (10,9)
# facing SOUTH -- facing preserved, NOT flipped to north (v4
# flipped it). Then Up/Down oscillates with NO turning.
ok = go(pb, 0, (10, 9), "0", avoid={(5, 3)}) and face(pb, 64)
p, k = state_of(pb)
check("gate north approach (10,9) facing south, key held", ok and p and (p[2], p[3]) == (10, 9) and p[4] == 64, f"proof={p}")
gold0 = shot(pb, "gate-gold-0.png")
check("unlocked gate shows gold in centre view", count_px(gold0, is_gold, (50, 20, 110, 124)) >= 20,
      f"gold_px={count_px(gold0, is_gold, (50, 20, 110, 124))}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("SEAMLESS gate north approach -> Meadow (1,2) SOUTH, gate behind (1,1), NO bounce",
      p == (1, 1, 1, 2, 64), f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("forward after north-approach transition moves AWAY (south to (1,3)), NOT back",
      p == (1, 1, 1, 3, 64), f"proof={p}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("step back north to arrival (1,2) south, still Meadow",
      p == (1, 1, 1, 2, 64), f"proof={p}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("Down after north-approach transition returns to Dungeon (10,9) SOUTH, facing preserved (v5, NOT north)",
      p == (1, 0, 10, 9, 64), f"proof={p}")
# v5 north-approach FORWARD oscillation, no turning: Up/Down/Up.
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc north forward: Up from (10,9) south -> Meadow (1,2) SOUTH",
      p == (1, 1, 1, 2, 64), f"proof={p}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc north forward: Down from (1,2) south -> Dungeon (10,9) SOUTH",
      p == (1, 0, 10, 9, 64), f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc north forward: Up again -> Meadow (1,2) SOUTH, no turn",
      p == (1, 1, 1, 2, 64), f"proof={p}")
# v5 north-approach BACKWARD oscillation: back to Dungeon, face
# NORTH, then Down (back SOUTH) through the gate must land in
# Meadow facing NORTH (gate ahead) -- NOT flip to south (v4
# bug pattern: backward step spun the player 180 degrees).
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("back to Dungeon for backward test -> (10,9) SOUTH",
      p == (1, 0, 10, 9, 64), f"proof={p}")
ok = face(pb, 192)
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc north BACKWARD: Down (back south) from (10,9) north -> Meadow (1,2) NORTH, gate ahead, NO spin",
      ok and p == (1, 1, 1, 2, 192), f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc north backward: Up (forward north) -> Dungeon (10,9) NORTH",
      p == (1, 0, 10, 9, 192), f"proof={p}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc north backward: Down again -> Meadow (1,2) NORTH, no turn",
      p == (1, 1, 1, 2, 192), f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc north backward: Up again -> Dungeon (10,9) NORTH, no turn",
      p == (1, 0, 10, 9, 192), f"proof={p}")
ok = go(pb, 0, (9, 10), "0") and face(pb, 0)
p, k = state_of(pb)
check("gate west approach (9,10) facing east, key held", ok and p and (p[2], p[3]) == (9, 10) and p[4] == 0, f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("SEAMLESS gate west approach -> Meadow (2,1) EAST, gate behind (1,1), NO intro, NO Start press",
      p == (1, 1, 2, 1, 0), f"proof={p}")
tick(pb, 60)
p, k = state_of(pb)
check("no bounce: standing on entry (2,1) stays in Area 1",
      p == (1, 1, 2, 1, 0), f"proof={p}")
trans = shot(pb, "transition-meadow.png")
check("transition frame is the meadow (blue sky on top)",
      count_px(trans, is_blue_sky, (0, 0, 160, 40)) >= 500,
      f"sky_px={count_px(trans, is_blue_sky, (0, 0, 160, 40))}")

# v5 gate-1 oscillations (the reported bug, fixed): BACKWARD
# pattern first. Turn 180 at the entry, Up (forward west) onto
# the return gate -> Dungeon (9,10) WEST. Then Down (back east)
# onto the Dungeon gate -> Meadow (2,1) WEST: facing preserved,
# gate (1,1) ahead, so Up goes straight back. No turning at
# any point of the oscillation.
ok = face(pb, 128)
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("BACK route turn 180 + Up from entry -> Dungeon (9,10) WEST, seamless, keys kept",
      ok and p == (1, 0, 9, 10, 128) and k == (1, 0), f"proof={p} keys={k}")
shot(pb, "return-turn-2.png")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 BACKWARD: Down (back east) from (9,10) west -> Meadow (2,1) WEST, gate ahead, NO spin",
      p == (1, 1, 2, 1, 128) and k == (1, 0), f"proof={p} keys={k}")
shot(pb, "osc-back-1.png")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 backward: Up (forward west) -> Dungeon (9,10) WEST, no turn",
      p == (1, 0, 9, 10, 128) and k == (1, 0), f"proof={p} keys={k}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 backward: Down again -> Meadow (2,1) WEST, no turn",
      p == (1, 1, 2, 1, 128) and k == (1, 0), f"proof={p} keys={k}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 backward: Up again -> Dungeon (9,10) WEST, no turn",
      p == (1, 0, 9, 10, 128) and k == (1, 0), f"proof={p} keys={k}")
# v5 gate-1 FORWARD oscillation: face east, Up/Down/Up/Down/Up.
ok = face(pb, 0)
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 FORWARD: Up (forward east) from (9,10) east -> Meadow (2,1) EAST, gate behind",
      ok and p == (1, 1, 2, 1, 0) and k == (1, 0), f"proof={p} keys={k}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 forward: Down (back west) -> Dungeon (9,10) EAST, gate ahead",
      p == (1, 0, 9, 10, 0) and k == (1, 0), f"proof={p} keys={k}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 forward: Up again -> Meadow (2,1) EAST, no turn",
      p == (1, 1, 2, 1, 0) and k == (1, 0), f"proof={p} keys={k}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 forward: Down again -> Dungeon (9,10) EAST, no turn",
      p == (1, 0, 9, 10, 0) and k == (1, 0), f"proof={p} keys={k}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate1 forward: Up again -> Meadow (2,1) EAST, no turn",
      p == (1, 1, 2, 1, 0) and k == (1, 0), f"proof={p} keys={k}")
ok = face(pb, 128)
p, k = state_of(pb)
check("face west from entry (return gate ahead)", ok and p and p[4] == 128, f"proof={p}")
ret2 = shot(pb, "return-gate-2.png")
check("return gate shows gold in centre view (Area 1, same as unlocked gate)",
      count_px(ret2, is_gold, (50, 20, 110, 124)) >= 20,
      f"gold_px={count_px(ret2, is_gold, (50, 20, 110, 124))}")
face(pb, 0)

meadow = shot(pb, "area2-view.png")
check("meadow view has blue sky on top", count_px(meadow, is_blue_sky, (0, 0, 160, 40)) >= 500,
      f"sky_px={count_px(meadow, is_blue_sky, (0, 0, 160, 40))}")
check("meadow view has green grass at bottom", count_px(meadow, is_green, (0, 110, 160, 144)) >= 500,
      f"grass_px={count_px(meadow, is_green, (0, 110, 160, 144))}")
ok = go(pb, 1, (1, 2), "0") and face(pb, 0)
p, k = state_of(pb)
check("stand (1,2) facing east (tree 2 ahead, rock beyond)", ok and p and (p[2], p[3]) == (1, 2) and p[4] == 0, f"proof={p}")
trees = shot(pb, "meadow-trees.png")
check("meadow trees/rocks visible in view (green foliage centre)",
      count_px(trees, is_green, (40, 20, 120, 124)) >= 100,
      f"green_px={count_px(trees, is_green, (40, 20, 120, 124))}")

# v4 MEADOW NPC at (2,7): billboard at distances 3, 2, 1,
# walk-through, talk with A (bottom-half dialog only), close,
# A-elsewhere does nothing. NPC is in Area 1 only.
ok = go(pb, 1, (6, 7), "0", avoid={(5, 5)}) and face(pb, 128)
p, k = state_of(pb)
check("stand (6,7) facing west (NPC 4 ahead)", ok and p and (p[2], p[3]) == (6, 7) and p[4] == 128, f"proof={p}")
npc4 = shot(pb, "npc-dist4.png")
check("NPC billboard at dist4 (blue tunic centre, small)",
      count_px(npc4, is_npc_blue, (40, 50, 120, 124)) >= 3,
      f"blue_px={count_px(npc4, is_npc_blue, (40, 50, 120, 124))}")
ok = go(pb, 1, (5, 7), "0", avoid={(5, 5)}) and face(pb, 128)
p, k = state_of(pb)
check("stand (5,7) facing west (NPC 3 ahead)", ok and p and (p[2], p[3]) == (5, 7) and p[4] == 128, f"proof={p}")
npc3 = shot(pb, "npc-dist3.png")
check("NPC billboard at dist3 (blue tunic centre)", count_px(npc3, is_npc_blue, (40, 40, 120, 124)) >= 5,
      f"blue_px={count_px(npc3, is_npc_blue, (40, 40, 120, 124))}")
ok = go(pb, 1, (2, 5), "0", avoid={(5, 5)}) and face(pb, 64)
p, k = state_of(pb)
check("stand (2,5) facing south (NPC 2 ahead)", ok and p and (p[2], p[3]) == (2, 5) and p[4] == 64, f"proof={p}")
npc2 = shot(pb, "npc-dist2.png")
check("NPC billboard at dist2 (blue tunic centre)", count_px(npc2, is_npc_blue, (40, 30, 120, 124)) >= 10,
      f"blue_px={count_px(npc2, is_npc_blue, (40, 30, 120, 124))}")
ok = go(pb, 1, (2, 6), "0", avoid={(5, 5)}) and face(pb, 64)
p, k = state_of(pb)
check("stand (2,6) facing south (NPC 1 ahead, adjacent)", ok and p and (p[2], p[3]) == (2, 6) and p[4] == 64, f"proof={p}")
npc1 = shot(pb, "npc-dist1.png")
check("NPC billboard at dist1 (blue tunic + skin, tall figure)",
      count_px(npc1, is_npc_blue, (40, 20, 120, 124)) >= 20
      and count_px(npc1, is_npc_skin, (40, 20, 120, 124)) >= 5,
      f"blue_px={count_px(npc1, is_npc_blue, (40, 20, 120, 124))} skin_px={count_px(npc1, is_npc_skin, (40, 20, 120, 124))}")
tap(pb, "a")
tick(pb, 40)
p, k = state_of(pb)
check("A adjacent+facing NPC opens dialog (state 5), position unchanged",
      p == (5, 1, 2, 6, 64), f"proof={p}")
dlg = shot(pb, "npc-dialog.png")
check("dialog is BOTTOM HALF only: gold dialog text in bottom half, none in top half",
      count_px(dlg, is_dialog_gold, (0, 72, 160, 144)) >= 100
      and count_px(dlg, is_dialog_gold, (0, 0, 160, 72)) == 0,
      f"bottom_gold={count_px(dlg, is_dialog_gold, (0, 72, 160, 144))} top_gold={count_px(dlg, is_dialog_gold, (0, 0, 160, 72))}")
check("dialog leaves top half raycast view visible (blue sky on top)",
      count_px(dlg, is_blue_sky, (0, 0, 160, 40)) >= 100,
      f"sky_px={count_px(dlg, is_blue_sky, (0, 0, 160, 40))}")
tap(pb, "a")
tick(pb, 40)
p, k = state_of(pb)
check("A again closes dialog back to play, position unchanged",
      p == (1, 1, 2, 6, 64), f"proof={p}")
closed = shot(pb, "npc-dialog-close.png")
check("dialog close restores full view (NPC visible again, grass at bottom)",
      count_px(closed, is_npc_blue, (40, 20, 120, 124)) >= 20
      and count_px(closed, is_green, (0, 110, 160, 144)) >= 500,
      f"blue_px={count_px(closed, is_npc_blue, (40, 20, 120, 124))} grass_px={count_px(closed, is_green, (0, 110, 160, 144))}")
# A-elsewhere: from (2,5) facing east (NPC is south, not faced) A does nothing.
ok = go(pb, 1, (2, 5), "0", avoid={(5, 5)}) and face(pb, 0)
tap(pb, "a")
tick(pb, 40)
p, k = state_of(pb)
check("A NOT adjacent-and-facing NPC does nothing (still play, same tile)",
      ok and p == (1, 1, 2, 5, 0), f"proof={p}")
# Walk-through: step onto the NPC tile itself, billboard hides, no block.
ok = go(pb, 1, (2, 7), "0", avoid={(5, 5)})
p, k = state_of(pb)
check("NPC never blocks movement: walk onto NPC tile (2,7)",
      ok and p and (p[2], p[3]) == (2, 7), f"proof={p}")

ok = go(pb, 1, (5, 1), "0", avoid={(5, 5)}) and face(pb, 64)
p, k = state_of(pb)
check("stand (5,1) facing south (Silver Key 4 ahead)", ok and p and (p[2], p[3]) == (5, 1) and p[4] == 64, f"proof={p}")
kd4 = shot(pb, "key-dist4.png")
check_key("Silver Key dist4: ONE small key 8x8, no repeats", kd4, 8, 8, fwd=4, lat=0)
ok = go(pb, 1, (5, 2), "0", avoid={(5, 5)}) and face(pb, 64)
p, k = state_of(pb)
check("stand (5,2) facing south (Silver Key 3 ahead)", ok and p and (p[2], p[3]) == (5, 2) and p[4] == 64, f"proof={p}")
kd3 = shot(pb, "key-dist3.png")
check_key("Silver Key dist3: ONE small key 16x8, no repeats", kd3, 16, 8, fwd=3, lat=0)
ok = go(pb, 1, (4, 3), "0", avoid={(5, 5)}) and face(pb, 64)
p, k = state_of(pb)
check("stand (4,3) facing south (Silver Key 2 ahead, lateral -1)", ok and p and (p[2], p[3]) == (4, 3) and p[4] == 64, f"proof={p}")
kd2l = shot(pb, "key-dist2-lat.png")
check_key("Silver Key dist2 lateral: ONE small key 24x8, no repeats", kd2l, 24, 8, fwd=2, lat=-1)

ok = go(pb, 1, (10, 8), "0", avoid={(5, 5)}) and face(pb, 64)
p, k = state_of(pb)
check("reach area 1 gate approach (10,8) facing south", ok and p and (p[2], p[3]) == (10, 8) and p[4] == 64, f"proof={p}")
tap(pb, "up")
p, k = state_of(pb)
check("locked gate blocks in area 1", p and (p[2], p[3]) == (10, 8) and p[0] == 1 and k == (1, 0), f"proof={p} keys={k}")
locked1 = shot(pb, "gate-locked-1.png")
check("area 1 locked gate shows red", count_px(locked1, is_red, (50, 20, 110, 124)) >= 20,
      f"red_px={count_px(locked1, is_red, (50, 20, 110, 124))}")

ok = go(pb, 1, (4, 5), "0", avoid={(5, 5)}) and face(pb, 0)
p, k = state_of(pb)
check("stand west of Silver Key facing east", ok and p and (p[2], p[3]) == (4, 5) and p[4] == 0, f"proof={p}")
key1 = shot(pb, "key-1.png")
check("Silver Key billboard visible (gold centre)", count_px(key1, is_gold, (40, 20, 120, 124)) >= 20,
      f"gold_px={count_px(key1, is_gold, (40, 20, 120, 124))}")
tap(pb, "up")
tick(pb, 60)
p, k = state_of(pb)
check("pick up Silver Key", p and (p[2], p[3]) == (5, 5) and k == (1, 1), f"proof={p} keys={k}")

# v4: inventory after Silver (both YES), minimap area 1 (Start closes it).
# Fog: the forward gate has not been stepped onto yet
# (only approached to (10,8)), so it draws dark -- NOT gold.
# The return gate WAS stepped onto during the back-route
# tests, so it shows gold (visited, unlocked). Entry, NPC
# walk-through cell and key route are light; unwalked cells
# stay dark.
check_inventory(pb, "inventory after Silver", "inventory-silver.png")
check_minimap(pb, "minimap area 1", "minimap-area2.png", "start", [
    (5, 5, "player", "player cell (5,5) bright"),
    (2, 1, "open", "entry cell (2,1) light"),
    (0, 0, "wall", "tree-line cell (0,0) dark"),
    (10, 9, "wall", "unvisited forward gate (10,9) dark (fog, not gold)"),
    (1, 1, "unlocked", "visited return gate (1,1) gold (stepped onto it in back-route tests)"),
    (2, 7, "open", "walked NPC cell (2,7) light (walk-through visited it)"),
])

ok = go(pb, 1, (10, 8), "0") and face(pb, 64)
p, k = state_of(pb)
check("return to area 1 gate with key", ok and p and (p[2], p[3]) == (10, 8) and p[4] == 64, f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("SEAMLESS gate north approach -> Crypt (1,2) SOUTH, gate behind (1,1), NO intro, NO Start press",
      p == (1, 2, 1, 2, 64), f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("forward after Meadow-gate north approach moves AWAY south to (1,3), NOT back",
      p == (1, 2, 1, 3, 64), f"proof={p}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("step back north to arrival (1,2) south, still Crypt",
      p == (1, 2, 1, 2, 64), f"proof={p}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("Down after Meadow-gate north approach returns to Meadow (10,8) SOUTH, facing preserved (v5, NOT north)",
      p == (1, 1, 10, 8, 64), f"proof={p}")
# v5 north-approach oscillation, no turning: Up/Down/Up.
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 north: Up from (10,8) south -> Crypt (1,2) SOUTH",
      p == (1, 2, 1, 2, 64), f"proof={p}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 north: Down -> Meadow (10,8) SOUTH",
      p == (1, 1, 10, 8, 64), f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 north: Up again -> Crypt (1,2) SOUTH, no turn",
      p == (1, 2, 1, 2, 64), f"proof={p}")
# Back to Meadow for the main west approach.
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("back to Meadow for west approach -> (10,8) SOUTH",
      p == (1, 1, 10, 8, 64), f"proof={p}")
# Main west approach to the Crypt.
ok = go(pb, 1, (9, 9), "0") and face(pb, 0)
p, k = state_of(pb)
check("gate west approach (9,9) facing east, key held", ok and p and (p[2], p[3]) == (9, 9) and p[4] == 0, f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("SEAMLESS gate west approach -> Crypt (2,1) EAST, gate behind (1,1), NO intro, NO Start press",
      p == (1, 2, 2, 1, 0), f"proof={p}")
tick(pb, 60)
p, k = state_of(pb)
check("no bounce: standing on entry (2,1) stays in Area 2",
      p == (1, 2, 2, 1, 0), f"proof={p}")
trans2 = shot(pb, "transition-crypt.png")
check("transition frame is the crypt (dark top, not blue sky)",
      count_px(trans2, is_blue_sky, (0, 0, 160, 40)) < 100,
      f"sky_px={count_px(trans2, is_blue_sky, (0, 0, 160, 40))}")
crypt = shot(pb, "area3-view.png")
check("crypt view is dark on top (dim ceiling, not blue sky)",
      count_px(crypt, is_blue_sky, (0, 0, 160, 40)) < 100,
      f"sky_px={count_px(crypt, is_blue_sky, (0, 0, 160, 40))}")

# v4: minimap area 2, then both back routes for the Crypt
# gate, then BACK to Area 0, then full flow to victory.
# Fog: the Crypt entry (2,1) is visited, (3,1) was walked
# during the north-approach forward step? No -- the
# north-approach landing was (1,2), forward to (1,3), so
# (3,1) is NOT visited yet at this minimap check (player
# arrived at (2,1) via the west approach, then stood still).
# (3,1) is open but unwalked, so it draws dark. The exit
# (10,10) is unvisited, dark -- NOT gold. The return gate
# (1,1) WAS visited: stepping onto it to return to Meadow
# during the north-approach Down test marks it visited in
# its own area, so it shows gold (visited, unlocked).
check_minimap(pb, "minimap area 2", "minimap-area3.png", "select", [
    (2, 1, "player", "player cell (2,1) bright"),
    (3, 1, "wall", "unwalked open cell (3,1) dark (fog)"),
    (0, 0, "wall", "wall cell (0,0) dark"),
    (10, 10, "wall", "unvisited exit (10,10) dark (fog, not gold)"),
    (1, 1, "unlocked", "visited return gate (1,1) gold (stepped onto it in north-approach Down test)"),
    (1, 2, "open", "visited arrival cell (1,2) light (north-approach landing)"),
    (1, 3, "open", "visited cell (1,3) light (forward after north approach)"),
])
# v5 gate-2 FORWARD oscillation: Down (back west) from Crypt
# entry (2,1) EAST -> Meadow (9,9) EAST (gate ahead), then
# Up/Down/Up with NO turning.
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 FORWARD: Down (back west) from entry east -> Meadow (9,9) EAST, gate ahead, NO spin",
      p == (1, 1, 9, 9, 0) and k == (1, 1), f"proof={p} keys={k}")
shot(pb, "return-down-3.png")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 forward: Up (forward east) -> Crypt (2,1) EAST, no turn",
      p == (1, 2, 2, 1, 0) and k == (1, 1), f"proof={p} keys={k}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 forward: Down again -> Meadow (9,9) EAST, no turn",
      p == (1, 1, 9, 9, 0) and k == (1, 1), f"proof={p} keys={k}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 forward: Up again -> Crypt (2,1) EAST, no turn",
      p == (1, 2, 2, 1, 0) and k == (1, 1), f"proof={p} keys={k}")
# v5 gate-2 BACKWARD oscillation: turn 180 at the entry, Up
# (forward west) onto the return gate -> Meadow (9,9) WEST,
# then Down (back east) -> Crypt (2,1) WEST, gate ahead, and
# Up/Down with NO turning.
ok = face(pb, 128)
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("BACK route turn 180 + Up from entry -> Meadow (9,9) WEST, seamless, keys kept",
      ok and p == (1, 1, 9, 9, 128) and k == (1, 1), f"proof={p} keys={k}")
shot(pb, "return-turn-3.png")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 BACKWARD: Down (back east) from (9,9) west -> Crypt (2,1) WEST, gate ahead, NO spin",
      p == (1, 2, 2, 1, 128) and k == (1, 1), f"proof={p} keys={k}")
shot(pb, "osc-back-3.png")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 backward: Up (forward west) -> Meadow (9,9) WEST, no turn",
      p == (1, 1, 9, 9, 128) and k == (1, 1), f"proof={p} keys={k}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 backward: Down again -> Crypt (2,1) WEST, no turn",
      p == (1, 2, 2, 1, 128) and k == (1, 1), f"proof={p} keys={k}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 backward: Up again -> Meadow (9,9) WEST, no turn",
      p == (1, 1, 9, 9, 128) and k == (1, 1), f"proof={p} keys={k}")
# Re-enter Crypt via the west approach for the south-approach tests.
ok = face(pb, 0)
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("re-enter Crypt via west approach -> (2,1) east",
      ok and p == (1, 2, 2, 1, 0) and k == (1, 1), f"proof={p} keys={k}")
# v4 Crypt return SOUTH approach: from (1,2) facing north, step
# onto (1,1) -> Meadow (10,8) NORTH (gate (10,9) behind, south).
ok = go(pb, 2, (1, 2), "0") and face(pb, 192)
p, k = state_of(pb)
check("Crypt return south approach (1,2) facing north", ok and p and (p[2], p[3]) == (1, 2) and p[4] == 192, f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("Crypt return south approach -> Meadow (10,8) NORTH, gate behind (10,9)",
      p == (1, 1, 10, 8, 192), f"proof={p} keys={k}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("forward after Crypt return south approach moves AWAY north to (10,7), NOT back",
      p == (1, 1, 10, 7, 192), f"proof={p} keys={k}")
# Back to Crypt via the Meadow gate south approach: (10,10)
# facing north, step onto (10,9) -> Crypt (1,0) NORTH.
ok = go(pb, 1, (10, 10), "0") and face(pb, 192)
p, k = state_of(pb)
check("Meadow gate south approach (10,10) facing north", ok and p and (p[2], p[3]) == (10, 10) and p[4] == 192, f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("Meadow gate south approach -> Crypt (1,0) NORTH (border alcove), gate behind (1,1)",
      p == (1, 2, 1, 0, 192), f"proof={p} keys={k}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("forward after south-approach transition bumps out of bounds, NOT back through gate",
      p == (1, 2, 1, 0, 192), f"proof={p} keys={k}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("Down after south-approach transition returns to Meadow (10,10) NORTH, facing preserved (v5, NOT south)",
      p == (1, 1, 10, 10, 192), f"proof={p} keys={k}")
# v5 south-approach oscillation, no turning: Up (forward north)
# through the gate, Down (back south) through it again.
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 south: Up from (10,10) north -> Crypt (1,0) NORTH",
      p == (1, 2, 1, 0, 192), f"proof={p} keys={k}")
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("osc gate2 south: Down again -> Meadow (10,10) NORTH, no turn",
      p == (1, 1, 10, 10, 192), f"proof={p} keys={k}")
# Re-enter Crypt via the west approach for the back chain.
ok = go(pb, 1, (9, 9), "0") and face(pb, 0)
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("re-enter Crypt via west approach -> (2,1) east",
      ok and p == (1, 2, 2, 1, 0), f"proof={p} keys={k}")
ok = face(pb, 128)
p, k = state_of(pb)
check("face west from entry in crypt (return gate ahead)", ok and p and p[4] == 128, f"proof={p}")
ret3 = shot(pb, "return-gate-3.png")
check("return gate shows gold in centre view (Area 2, same as unlocked gate)",
      count_px(ret3, is_gold, (50, 20, 110, 124)) >= 20,
      f"gold_px={count_px(ret3, is_gold, (50, 20, 110, 124))}")
face(pb, 0)
tap(pb, "down")
tick(pb, 40)
p, k = state_of(pb)
check("BACK: Area 2 return (Down) -> Meadow (9,9) EAST, for the Area 0 leg",
      p == (1, 1, 9, 9, 0) and k == (1, 1), f"proof={p} keys={k}")
shot(pb, "return-area2.png")
# Explicit east approach (not go_return: BFS to (1,1) may pick
# the (1,2) south approach instead, landing at (10,9) north).
ok = go(pb, 1, (2, 1), "0") and face(pb, 128)
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("BACK: Area 1 return gate east approach -> Dungeon (9,10) WEST, seamless, keys kept",
      ok and p == (1, 0, 9, 10, 128) and k == (1, 1), f"proof={p} keys={k}")
shot(pb, "return-area1b.png")
# v4 FOG PERSISTENCE: back in Dungeon, tiles visited on the
# first trip are still light (entry, key, gate, return arrival).
check_minimap(pb, "minimap fog persists area 0", "minimap-fog-persist.png", "select", [
    (9, 10, "player", "player cell (9,10) bright"),
    (1, 1, "open", "first-trip entry cell (1,1) still light"),
    (5, 3, "open", "first-trip key cell (5,3) still light"),
    (10, 10, "unlocked", "visited forward gate (10,10) gold (stepped onto it, key held)"),
    (5, 1, "wall", "never-walked cell (5,1) still dark"),
])
# v4 Meadow return SOUTH approach: from (1,2) facing north,
# step onto (1,1) -> Dungeon (10,9) NORTH. First return to
# Meadow via the Dungeon west approach.
ok = go(pb, 0, (9, 10), "0") and face(pb, 0)
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("return to Meadow via west approach -> (2,1) east",
      ok and p == (1, 1, 2, 1, 0), f"proof={p} keys={k}")
ok = go(pb, 1, (1, 2), "0") and face(pb, 192)
p, k = state_of(pb)
check("Meadow return south approach (1,2) facing north", ok and p and (p[2], p[3]) == (1, 2) and p[4] == 192, f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("Meadow return south approach -> Dungeon (10,9) NORTH, gate behind (10,10)",
      p == (1, 0, 10, 9, 192), f"proof={p} keys={k}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("forward after Meadow return south approach bumps on wall (10,8), NOT back",
      p == (1, 0, 10, 9, 192), f"proof={p} keys={k}")

# Full flow to victory: Area 0 -> Area 1 -> Area 2 -> exit.
# Player is at Dungeon (10,9) north after the south-approach
# test, so the victory flow takes the north approaches:
# south onto the Dungeon gate -> Meadow (1,2) south, then
# south onto the Meadow gate -> Crypt (1,2) south, then exit.
ok = face(pb, 64)
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("victory flow: Dungeon gate north approach -> Meadow (1,2) south",
      ok and p == (1, 1, 1, 2, 64), f"proof={p}")
ok = go(pb, 1, (10, 8), "0") and face(pb, 64)
p, k = state_of(pb)
check("victory flow: reach area 1 gate approach", ok and p and (p[2], p[3]) == (10, 8), f"proof={p}")
tap(pb, "up")
tick(pb, 40)
p, k = state_of(pb)
check("victory flow: Meadow gate north approach -> Crypt (1,2) south",
      p == (1, 2, 1, 2, 64), f"proof={p}")
ok = go(pb, 2, (10, 10), "03")
p, k = state_of(pb)
check("reach final exit (10,10) -> victory screen", ok and p and p[0] == 2, f"proof={p}")
shot(pb, "victory.png")
tap(pb, "start", 40, press=10)
tick(pb, 60)
p, k = state_of(pb)
check("victory Start -> title", p and p[0] == 0, f"proof={p}")

# v4 FOG RESET: start a new game from the title screen --
# visited state clears, keys reset, minimap shows only entry.
tap(pb, "start", 40)
p, k = state_of(pb)
check("new game after victory -> area 0 (1,1) east, keys reset",
      p == (1, 0, 1, 1, 0) and k == (0, 0), f"proof={p} keys={k}")
check_minimap(pb, "minimap fog reset area 0", "minimap-fog-reset.png", "select", [
    (1, 1, "player", "player/entry cell (1,1) bright after reset"),
    (9, 10, "wall", "previously visited return cell (9,10) dark again after reset"),
    (5, 3, "wall", "previously walked key cell (5,3) dark again after reset"),
    (10, 10, "wall", "previously visited gate (10,10) dark again after reset"),
])

pb.stop()
print(f"QA done: {sum(checks)}/{len(checks)} checks passed")
sys.exit(0 if all(checks) else 1)
