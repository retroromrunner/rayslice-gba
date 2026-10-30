# Maze Roamer — v3

v3 (md5 2f9912d93abb989489b024d308e9bde5, 32KB ROM ONLY) reworks ONLY
the wraith rendering, using sprite casting. Everything else — maze
layout, brick/slate walls, brown dirt floor, ceiling, gold exit, step/
bump sound, movement, wraith movement rules — is unchanged from v2.
Close-range fix: distance-1 uses a pixel-scaled 64x128 wraith tile
set (128 tiles, 252 tiles total), so its eyes appear as one pair
instead of repeating per tile as they did in the first v3 build.

## v3 wraith rendering (sprite casting)

v2's wraith was a hardware-sprite overlay in fixed discrete sizes
with feet pinned to y=135 for every distance, drawn on top of the
walls with no occlusion — so a far wraith stood on the near floor
and showed through wall corners, which is why it looked wrong.
v3 replaces it with Lode Vandevenne's sprite-casting technique
(Raycasting III: Sprites), as also used per-column by haroldo-ok's
GB raycaster (gameboy/gbray, Apache 2.0; technique reference only,
all code here is original): during the wall raycast a per-column
ZBuffer of wall depths is recorded; the wraith is transformed into
camera space (fwd = perpendicular tiles ahead, lat = lateral tiles;
with cardinal facing and tile-centre positions this is the inverse
camera matrix with the engine's 169/256 plane factor), its screen
x = 80·(1 + lat/fwd), its height uses the SAME perspective as the
walls (144/fwd, capped at 128 like wall line_h), centred on the
horizon (72) so its feet land on the floor line AT THAT DISTANCE,
and it is drawn stripe-by-stripe over the BG tile buffer AFTER the
walls, skipping any screen column where the wraith is not nearer
than the wall in that column (per-column occlusion). The texture is
one 32×64 master (pale hood, glowing eyes, flaring cloak) sampled
per BG tile column/row; all-background tiles are skipped so the
silhouette survives, and the wraith uses its own pale BG palette
(contrast with the grey walls — v2's dark cloak blended into them).
Sizes: 64×128 at 1 tile, 36×72 at 2, 24×48 at 3, 18×36 at 4.

Two real bugs found and fixed while building v3, both worth knowing:
- SDCC long-division narrowing: writing the height as 36864L/(fwd·256)
  made SDCC emit a 16-bit __divsint in which 36864 wraps to a negative
  signed int, so the height came out capped at 128 for EVERY distance
  (a far wraith rendered as tall as a near one). It was caught by a
  temporary debug proof block (fwd=3 but sprite_h=128) and fixed by
  writing the height as 144/fwd exactly (same value, small positive
  ints only). The debug proof was removed from the shipped build.
- Texture minification aliasing: per-tile sampling skips some texture
  rows/columns at far distances, and the eyes were confined to a
  single texture row, so at distance 3 they vanished entirely. The
  eyes are now tall slits spanning two texture rows, so at least one
  eye survives every distance's sampling.

## v3 QA

`qa/playthrough.py` (PyBoy, 24 checks, all passing): title, step,
turn, wraith at distances 1, 2, 3 as full figures (single pale bbox
of the expected sprite size, eyes present, feet/head on the
perspective lines — dist1: 64×128, head 8, feet 135; dist2: 36×72,
head 36, feet 107; dist3: 24×48, head 48, feet 95), a partially
occluded wraith (1 of 3 columns visible, figure does not show
through the wall), dist4 height/feet via that occlusion sliver
(head 54, feet 89), walk-through, gold exit, exit screen, return to
title. Honest limit: a fully-visible distance-4 figure with eyes
never aligned in 400 hunt steps (the wraith must sit exactly 4 tiles
up a straight corridor while the player faces it), so dist4 is proven
by height/feet in the occlusion sliver only — same code path as
1–3, no fwd-specific branch. Screenshots in `qa/shots/`
(wraith-dist1/2/3.png, wraith-occluded.png), all eyeballed: dist1 is
a tall grounded figure with glowing slit eyes, dist2 sits on its own
floor line left of centre, dist3 is a small centred figure on the
distant floor line, and the occluded shot shows only a sliver
peeking from behind a wall corner.

Not verified: real-device audio (step/bump code paths unchanged) and
play on Cori's RetroArch — that remains the real gate.

---

# Maze Roamer — v2 (superseded)

A deliberately small GBC first-person dungeon maze demo. A step back from
the failed Thornkeep build: this is the known-working raycaster-demo
instant tile-step engine, with only a roaming monster and a marked exit
added. Nothing else.

v2 (md5 77f95503023e31510d0336bd261e4dad, 32KB ROM ONLY) fixes Cori's
v1 playtest complaints — gloomy/ugly walls, wraith too small, wraith
sometimes rendering as 4 split copies up close — with no change to the
instant render pipeline, movement, maze layout, gold exit, or step sound.

## What it is

- Instant tile-step first-person dungeon maze (12x12), with brick/slate
  walls in two shades of grey (slate blocks, lighter, finer courses;
  cinder blocks, darker, chunkier, heavier mortar — chosen by wall
  position), a lightened dark-slate ceiling with torch glints, and a
  brown dirt floor. Wall bottom-edge tiles carry brown too, so wall
  bases meet dirt, not grey. The view fills the whole screen on every tap.
- One monster type: the Stone Wraith, a hooded figure with glowing eyes,
  rendered as a hardware-sprite billboard on top of the raycast view.
  Sizes by distance, all from one 32x64 master downsampled in
  gen_tiles.py (nearest neighbour) into unique tiles — no pixel-doubling,
  so it can never render as split copies: 32x64 at 1 tile, 24x40 at
  2 tiles, 16x24 at 3-4 tiles. Every size stands on the same floor line
  (screen y=135), feet on the dirt, never floating mid-screen. It roams:
  on each successful player step it moves one tile to a random adjacent
  open cell, never into walls or the exit cell.
- v1 split-ghost root cause: v1 "pixel-doubled" a 16x16 figure by
  repeating each 8x8 tile 2x2 in a 4x4 sprite grid — that is tiling, not
  scaling, so each original tile appeared 4 times and the close-up
  figure read as four quadrant copies. v2 draws the figure once, at
  real size, from unique tiles.
- The wraith never blocks, never battles, never damages. You can walk
  right through it.
- The exit is marked: the exit wall glows bright gold, unlike grey stone.
  Stepping onto it shows "YOU ESCAPED! / THE DUNGEON LETS GO".

## What it is not

No battles, story, magic, inventory, stats, quests, or NPCs. That is
intentional — this demo exists to prove the base (view + roaming monster
+ marked exit) works on Cori's emulator before anything else is added.

## Controls

- Up / Down: step forward / back one tile
- Left / Right: turn 90 degrees
- Start: start from the title screen; back to title from the exit screen
- A step sound plays on every successful step and turn; a bump sound
  plays on a blocked step. No music.

## Build

```sh
bash build.sh   # GBDK at ~/workspace/gbc/tools/gbdk
```

Produces `releases/maze-roamer.gbc`: plain 32KB ROM ONLY, CGB. build.sh
asserts the header (32KB, cart type 0x00, CGB flag), that the maze
contains no '2' cells, and that the exit (10,10) is reachable from the
start (1,1) by BFS.

## QA

`qa/playthrough.py` (PyBoy, 19 checks, all passing): title, a step, a
turn, the wraith visible at near (1 tile) and far (3+ tiles) distances,
the wraith captured at distances 1, 2, and 3+ with lateral offsets
(dist1 lat 0, dist2 lat -1, dist3 lat 2 — a dist1 lateral capture was
not achieved; do not claim one), each distance shot asserting exactly
2 yellow-eye clusters = ONE figure, a brown-dirt floor check, a
light/dark/mortar wall-shade check, walking through the wraith's tile,
the gold exit wall in view, stepping onto the exit, and returning to
the title. Screenshots in `qa/shots/` (wraith-dist1/2/3.png,
dungeon-view.png, exit-gold.png), all eyeballed: walls read as
brick/slate, floor is brown dirt, the wraith is one large grounded
figure when close and one small grounded figure when far, no split
copies at any distance.

Not verified: real-device audio (step sound code path is present:
`sfx_step()` on every successful step/turn, `sfx_bump()` on blocked
steps), and play on Cori's RetroArch — that is the real gate for this
demo, as with every build.
