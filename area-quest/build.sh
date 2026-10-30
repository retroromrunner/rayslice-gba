#!/bin/bash
# Build AREA QUEST GBC prototype. Produces releases/area-quest.gbc
# (plain 32KB ROM ONLY, CGB, if the code fits; see the header assert).
set -e
cd "$(dirname "$0")"
python3 gen_tiles.py
GBDK="${GBDK:-$HOME/workspace/gbc/tools/gbdk}"
mkdir -p releases
"$GBDK/bin/lcc" -Wm-yc -Wm-yn"AREAQUEST" -Wl-m -o releases/area-quest.gbc src/main.c
ls -la releases/
python3 - <<'EOF'
d = open("releases/area-quest.gbc", "rb").read()
print("size:", len(d), "bytes; cart type: 0x%02X" % d[0x147], "; CGB flag: 0x%02X" % d[0x143])
assert len(d) == 32768, "expected plain 32KB ROM"
assert d[0x147] == 0x00, "expected ROM ONLY cartridge type"
assert d[0x143] == 0x80, "expected CGB-compatible flag"
print("header OK: ROM ONLY, 32KB, CGB")
import re
from collections import deque
src = open("src/main.c").read()
# Extract the three 12-row maps in order from MAPS (first 36
# 12-char map strings in the file; comments sit between them).
allrows = re.findall(r'"([0-9TGRB]{12})"', src)
assert len(allrows) >= 36, f"expected 36 map rows, found {len(allrows)}"
maps = [allrows[i * 12:(i + 1) * 12] for i in range(3)]

def reachable(rows, start, goal, avoid=()):
    # 'B' counts as walkable only when it IS the goal (a path
    # through a return gate would leave the area, so key/gate/
    # exit paths must avoid it); other walkables are 0, G, 3.
    walk = set("0G3")
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
            nx, ny = x + dx, y + dy
            if (nx, ny) in avoid:
                continue
            if (nx, ny) not in seen and 0 <= nx < 12 and 0 <= ny < 12 and (
                    rows[ny][nx] in walk or (nx, ny) == goal):
                seen.add((nx, ny)); q.append((nx, ny))
    return goal in seen, len(seen)

keys = [(5, 3), (5, 5)]
gates = [(10, 10), (10, 9)]
# v3 mirrored gates: return gate is (1,1) in Areas 1 and 2, the
# gate directly behind the (2,1) entry. Area 0 entry stays (1,1).
ret_gates = [None, (1, 1), (1, 1)]
entries = [(1, 1), (2, 1), (2, 1)]
# v4 direction-preserving gate rule: stepping onto a gate cell
# travelling in direction D lands on destination_gate + D,
# facing D. For every REACHABLE approach to every gate, the
# arrival cell must be open. v4 map changes for this: Crypt
# (1,2) opened (arrival for a southward Meadow-gate crossing
# and the south approach to the Crypt return gate) and Crypt
# (1,0) opened (arrival for a northward Meadow-gate crossing;
# a border alcove, north of it is out of bounds). Unreachable
# approaches (neighbour is a wall/tree/border) are documented
# in the README and are not bugs.
DIRS4 = [(1, 0), (0, 1), (-1, 0), (0, -1)]
for i, rows in enumerate(maps):
    ex, ey = entries[i]
    assert rows[ey][ex] == '0', f"area {i} entry {(ex, ey)} must be open, not a gate"
    ok, n = reachable(rows, (ex, ey), gates[i] if i < 2 else (10, 10), avoid=(ret_gates[i],) if ret_gates[i] else ())
    print(f"area {i}: {n} reachable open cells, gate/exit reachable (avoiding return gate)={ok}")
    assert ok, f"area {i} gate/exit unreachable without return gate"
    if i < 2:
        okk, _ = reachable(rows, (ex, ey), keys[i], avoid=(ret_gates[i],) if ret_gates[i] else ())
        okg, _ = reachable(rows, keys[i], gates[i], avoid=(ret_gates[i],) if ret_gates[i] else ())
        print(f"area {i}: key {keys[i]} reachable={okk}, key->gate reachable={okg}")
        assert okk and okg, f"area {i} key/gate flow broken"
    if ret_gates[i]:
        rg = ret_gates[i]
        assert rows[rg[1]][rg[0]] == 'B', f"area {i} return gate {rg} is not 'B'"
        assert rg != entries[i], f"area {i} return gate must not be the entry tile"
        okr, _ = reachable(rows, (ex, ey), rg)
        print(f"area {i}: return gate {rg} reachable from entry={okr}")
        assert okr, f"area {i} return gate unreachable"
    # Direction-preserving arrivals for every reachable approach.
    gate_list = []
    if i < 2:
        gate_list.append(('G', gates[i], i + 1, (1, 1)))
    if ret_gates[i]:
        gate_list.append(('B', ret_gates[i], i - 1, gates[i - 1]))
    for kind, (gx, gy), da, (dgx, dgy) in gate_list:
        for dx, dy in DIRS4:
            ax, ay = gx - dx, gy - dy
            if not (0 <= ax < 12 and 0 <= ay < 12):
                continue
            if rows[ay][ax] != '0':
                continue  # approach not reachable; documented, not a bug
            arr = (dgx + dx, dgy + dy)
            assert 0 <= arr[0] < 12 and 0 <= arr[1] < 12, \
                f"area {i} {kind} gate {(gx, gy)} approach {(ax, ay)} arrival {arr} out of bounds"
            assert maps[da][arr[1]][arr[0]] == '0', \
                f"area {i} {kind} gate {(gx, gy)} approach {(ax, ay)} arrival {arr} in area {da} is not open"
            assert arr != (dgx, dgy), "arrival must not be the gate tile itself"
            print(f"area {i} {kind} gate {(gx, gy)} approach {(ax, ay)} dir {(dx, dy)} -> area {da} {arr} open")
# v4 Meadow NPC: tile (2,7), open, not key/gate/entry/return,
# reachable from the Meadow entry avoiding the return gate.
npc = (2, 7)
assert maps[1][npc[1]][npc[0]] == '0', "NPC tile must be open"
assert npc not in (keys[1], gates[1], entries[1], ret_gates[1]), "NPC tile must not be key/gate/entry/return"
okn, _ = reachable(maps[1], entries[1], npc, avoid=(ret_gates[1],))
assert okn, "NPC unreachable from Meadow entry"
print(f"Meadow NPC {npc} open, reachable, not key/gate/entry/return")
print("maps OK: all keys, gates, final exit, return gates, direction-preserving arrivals, NPC reachable")
EOF
