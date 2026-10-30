#!/bin/bash
# Build MAZE ROAMER GBC demo. Produces releases/maze-roamer.gbc (plain 32KB ROM, CGB).
set -e
cd "$(dirname "$0")"
python3 gen_tiles.py
GBDK="${GBDK:-$HOME/workspace/gbc/tools/gbdk}"
mkdir -p releases
"$GBDK/bin/lcc" -Wm-yc -Wm-yn"MAZEROAMER" -Wl-m -o releases/maze-roamer.gbc src/main.c
ls -la releases/
python3 - <<'EOF'
import re
d = open("releases/maze-roamer.gbc", "rb").read()
print("size:", len(d), "bytes; cart type: 0x%02X" % d[0x147], "; CGB flag: 0x%02X" % d[0x143])
assert len(d) == 32768, "expected plain 32KB ROM"
assert d[0x147] == 0x00, "expected ROM ONLY cartridge type"
assert d[0x143] == 0x80, "expected CGB-compatible flag"
print("header OK: ROM ONLY, 32KB, CGB")
src = open("src/main.c").read()
rows = re.findall(r'"([0123]{12})"', src)[:12]
assert len(rows) == 12, rows
assert all('2' not in r for r in rows), "maze must contain no '2' cells"
walk = lambda c: c in "03"
from collections import deque
seen = {(1, 1)}
q = deque([(1, 1)])
while q:
    x, y = q.popleft()
    for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
        nx, ny = x + dx, y + dy
        if (nx, ny) not in seen and 0 <= nx < 12 and 0 <= ny < 12 and walk(rows[ny][nx]):
            seen.add((nx, ny))
            q.append((nx, ny))
assert (10, 10) in seen, "exit unreachable!"
print(f"maze OK: {len(seen)} reachable open cells, exit reachable")
EOF
