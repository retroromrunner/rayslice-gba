# Area Quest — prototype v4 (for judging the experience, not a finished game)

## v4 changes (gate direction fix + Meadow NPC + minimap fog of war)

- **Gate travel now preserves direction — the v3 bounce-back bug
  is fixed (Cori's playtest report).** In v3, return transitions
  always landed the player at the pre-gate cell FACING THE GATE
  (east), reversing travel direction: walking west through a
  return gate left you facing east back toward it, so pressing
  forward walked you straight back through it. v4 rule, general
  for ALL gates (forward and return): stepping onto a gate cell
  travelling in direction D loads the destination area with the
  player on destination_gate + D, FACING D — continuing AWAY
  from the gate, never on a gate tile, never facing back toward
  the gate you just came through. Forward gate -> destination
  is the next area's return gate (1,1) + D; return gate ->
  destination is the previous area's forward gate + D.
  Worked examples, all proven in QA: (a) Meadow entry (2,1)
  facing west, step west onto return gate (1,1) -> Dungeon
  (9,10) facing WEST (gate (10,10) behind, to the east);
  pressing forward (west) from there BUMPS on the Dungeon wall
  at (8,10) — it does NOT go back to the Meadow. (b) Dungeon
  (9,10) facing east, step east onto forward gate (10,10) ->
  Meadow (2,1) facing EAST (gate (1,1) behind, to the west);
  pressing forward (east) moves to (3,1), NOT back.
  (c) Pressing Down (step back) immediately after ANY gate
  transition steps back onto that area's gate and returns to
  the previous area, still direction-preserving — that is the
  intended back route, not a bug (e.g. Down from Meadow entry
  (2,1) east -> Dungeon (9,10) west, and Down from Crypt entry
  (2,1) east -> Meadow (9,9) west; turn-180 + Up returns the
  same way). North/south approaches work the same way, not
  just east/west: Dungeon gate approached from the north
  moving south (10,9) -> Meadow (1,2) south; Meadow gate
  approached from the north moving south (10,8) -> Crypt (1,2)
  south, and approached from the south moving north (10,10)
  -> Crypt (1,0) north; return gates approached from the
  south moving north (1,2) -> Dungeon (10,9) north (Meadow
  return) and Meadow (10,8) north (Crypt return), and from the
  east moving west (2,1) -> Dungeon (9,10) west and Meadow
  (9,9) west. MAP CHANGES for this rule: Crypt (1,2) and
  Crypt (1,0) changed from WALL to open, so every reachable
  gate approach has an open arrival cell (asserted in
  build.sh for every reachable approach of every gate).
  Crypt (1,0) is a border alcove: north of it is out of
  bounds, so forward from there bumps and Down returns —
  that is the intended behaviour there, not a bounce.
  UNREACHABLE approaches (documented, not bugs): Dungeon gate
  east/south approaches (border walls), Meadow gate east
  approach (tree-line), Meadow return gate east/north
  approaches (tree-line), Crypt return gate east/north
  approaches (border walls). Seamless transitions stay
  seamless (no Press Start screens); forward gates still
  require their key (locked iron-red, unlocked gold); return
  gates are always open gold.
- **Meadow NPC (Area 1 only) — talk with A.** One villager
  stands at Meadow tile (2,7) (west meadow, off the key/gate
  route, all four neighbours open, reachable from the Meadow
  entry). Rendered with the SAME sprite-casting BG-billboard
  technique as the keys (camera transform, perspective height,
  per-column wall occlusion, floor-anchored), as a standing
  figure taller than a key — about half wall height at 1 tile
  (64px vs 128px), shrinking with distance — with its own
  small tile set per distance (32x64 at 1 tile, 16x32 at 2,
  16x24 at 3, 8x16 at 4), so no tile ever repeats like the
  old key bug. Total BG tiles are now 243 of 256. The NPC
  never blocks movement: you walk right through its tile,
  and its billboard hides when you are on its tile, exactly
  like the old wraith walk-through rule. The NPC does NOT
  appear in the Dungeon or the Crypt, and does not affect
  keys/gates. TALKING: when you are ADJACENT to the NPC and
  FACING it (NPC tile = your tile + facing direction) and
  press A (edge-triggered), a dialog box opens covering ONLY
  the BOTTOM HALF of the screen (rows 9-17, about 72px),
  leaving the top half raycast view visible and untouched —
  it writes dialog background + text cells directly to the
  bottom rows and never clears the screen, unlike Start/
  Select which take the full screen. Dialog lines (all
  x+length <= 20): "HELLO, TRAVELLER!", "THE SILVER KEY",
  "IS HIDDEN NEARBY.", "PRESS A TO CLOSE". Pressing A again
  (or Start) closes the dialog and restores the full view
  with one frame render. While dialog is open, movement,
  turns, gates, inventory and minimap inputs are ignored
  except the close button. Pressing A when NOT adjacent-and-
  facing the NPC does nothing. Control is on the title screen
  as "A: TALK" (row 15). Dialog uses the key-gold palette
  (light-gold text on dark) and changes no palette, so the
  top half's colours are untouched while dialog is open.
- **Minimap fog of war (Select minimap only; the in-world
  raycast view is NOT fogged).** The minimap now shows ONLY
  tiles the player has actually VISITED so far, per area.
  A tile counts as visited when the player steps onto it —
  including the entry tile on area load and the gate-arrival
  tile on any transition (stepping onto a gate tile itself
  also counts it visited in its own area, e.g. a return gate
  you stepped onto to leave shows gold when you come back).
  Visited state PERSISTS when leaving and returning to an
  area — returning to the Dungeon still shows the tiles
  visited on the first trip. Starting a new game from the
  title screen resets all visited state. Visited tiles draw
  exactly as in v3 (walls/trees/rocks dark, open light,
  player bright yellow, gates, exit); UNVISITED tiles draw as
  blank dark cells with NO wall/open/key/gate/exit
  information at all. Strict step-on visited only: adjacent
  or in-line-of-sight tiles are NOT revealed merely because
  you can see them in the raycast view. Consequence that
  looks odd but is CORRECT fog behaviour: a key tile only
  becomes visited by stepping onto it, which collects it, so
  an uncollected key never appears gold on the minimap
  (a collected key's tile shows as walked-open afterwards,
  not as a key). QA proves fog of war: minimap early in
  Area 1 shows only entry + walked tiles (rest dark), walking
  further reveals newly walked tiles while unwalked tiles
  stay dark, going to Area 2 and back to Area 1 keeps Area 1
  visited tiles, and a new game resets the fog.

## v3 changes (three features, no new areas/items/battles/story)

- **Bidirectional gates, seamless backtracking — CORRECTED
  mirrored-gate design (Cori's correction; the separate (1,4)
  return-gate design built first was DISCARDED and is not in
  this ROM): a gate is a gate. The return gate is the SAME gate
  in the mirrored position. Dungeon forward gate (10,10) ->
  Meadow entry (2,1) facing east, gate directly BEHIND at (1,1)
  in the Meadow; Meadow forward gate (10,9) -> Crypt entry (2,1)
  facing east, gate behind at (1,1) in the Crypt. Cell (1,1) in
  the Meadow and Crypt is the return gate (internal code 'B',
  always open — arrival already unlocked that direction, never
  blocks, never needs a key) and renders EXACTLY like an
  unlocked forward gate: gold door, same tile, same palette —
  NOT a different colour, NOT a different tile, and NOT teal
  anywhere (play or minimap). Stepping onto (1,1) loads the
  previous area IMMEDIATELY, with no intro/Press Start screen,
  at its forward-gate approach cell facing east: Meadow return
  -> Dungeon (9,10) east (gate (10,10) ahead); Crypt return ->
  Meadow (9,9) east (gate (10,9) ahead). Both back routes work
  because they are the same gate cell approached from the entry
  tile: from entry (2,1) facing east, Down (step back west)
  returns, and turning 180 degrees and pressing Up (forward
  west) returns the same way — both proven in QA for both
  gates. Entry (2,1) is never itself a gate, so standing on
  entry never bounces the player back (proven: proof unchanged
  after 60 ticks on entry). Forward gates stay passable after
  unlocking, so going forward again after returning works.
  MAP CHANGES for this design: Meadow (1,1) and Crypt (1,1)
  changed from open entry to return-gate cells; Meadow/Crypt
  entries moved (1,1) -> (2,1) (both (2,1) cells were already
  open and reachable — verified, no other map change needed
  there); Dungeon (9,10) changed from WALL to open (row 10 is
  now 1,0,0,0,0,0,0,1,1,0,G,1) so Cori's specified (9,10)-east
  return destination is a real open cell — in v2 it was a wall.
  Dungeon entry stays (1,1). Each area's key/gate/exit stays
  reachable without passing through its return gate (asserted
  in build.sh, which also asserts each return destination is
  open and faces its forward gate in one step).
- **Inventory HUD on Start:** in play only, pressing Start
  (edge-triggered) opens an inventory screen; pressing Start
  again returns to the exact play view (position, area, keys
  unchanged). It shows "INVENTORY", the current area name
  ("STONE DUNGEON" / "THE MEADOW" / "MOSSY CRYPT"), "BRONZE
  KEY: YES" or "BRONZE KEY: --", "SILVER KEY: YES" or "SILVER
  KEY: --", and "PRESS START" / "TO CONTINUE" (every line
  x+length <= 20). Title Start (start game) and victory Start
  (back to title) are unchanged; Start in play triggers nothing
  else. Inventory state is just the two key flags — no new items
  yet, and no chest/artifact system (bidirectional gates are the
  mechanism that will enable that later).
- **Minimap on Select — SHIPPED (it fit cleanly):** in play only,
  pressing Select (edge-triggered) opens a minimap for the
  CURRENT area only; pressing Select again (or Start) returns to
  the exact play view. It shows the 12x12 area as one BG cell per
  map cell, centred (x 4..15, y 3..14), with the area name at the
  top and "SELECT TO CLOSE" / "OR START" at the bottom. Cells
  use existing BG tiles only (solid T_SKY cells coloured by
  dedicated minimap palettes — no new tile system, tile count
  still 195): walls/trees/rocks dark, open cells light, player
  bright yellow, uncollected keys gold, forward gates red when
  locked / light gold when unlocked, return gates light gold
  (same as unlocked forward gates — a gate is a gate), Area 3
  exit light gold. Returning to play reloads the area palettes
  and renders one fresh frame, so the play view is never left
  corrupted. Select on title/victory is a no-op.
- ROM md5 and QA below are v3. v2 (md5
  `1bbfa08bfb01e18296cb09bed042f766`) and v1 (md5
  `30ab444701032557c210568d193d97cc`) are preserved separately
  and were not overwritten. `~/workspace/maze-roamer/` was not
  modified. Nothing was pushed to GitHub/itch.

## v2 changes (Cori's v1 playtest, two fixes only)

- **Seamless area transitions:** stepping onto an unlocked gate now
  IMMEDIATELY loads the next area (palettes switch, player at
  (1,1) facing east, one frame rendered) and play continues. The
  v1 area-intro screens ("THE MEADOW" + "PRESS START") are GONE —
  they were a prototype choice, not a hardware limit, since all
  area tiles are already in VRAM and only palettes switch. The
  final gold exit still shows the victory screen (an ending, not
  a transition). Title screen unchanged.
- **Keys smaller, overlapping-clip bug fixed:** keys are now
  genuine small ground items, floor-anchored in tile terms
  (bottom tile = floor tile - 1, so feet sit on the floor line
  at that distance, never floating): 32x16 px at 1 tile, 24x8 at
  2, 16x8 at 3, 8x8 at 4. Root cause of the v1 clips (confirmed):
  v1 sampled a fixed tile set per screen tile centre, so at 2
  tiles the key spanned 5 screen rows against a 4-row texture
  and the key shape duplicated. v2 generates one tile set PER
  DISTANCE, downscaled from the master key art to that
  distance's exact tile-aligned size, and places keys at tile
  coordinates with a 1:1 screen-tile -> texture-tile mapping —
  so no screen tile can ever repeat a texture tile, at any
  distance, by construction. Keys still use the sprite-casting
  BG-billboard technique (camera transform, screen x from
  lat/fwd, per-column wall/tree occlusion); only their size,
  anchoring, and tile sets changed.
- ROM md5 and QA below are v2. v1 (md5
  `30ab444701032557c210568d193d97cc`) is preserved separately
  and was not overwritten.

Built from the Maze Roamer v3 base (instant tile-step GBC raycaster,
LCD-off full-frame redraw per tap, step/bump sounds), with the wraith
removed entirely. Instead of a roaming monster, this prototype tests
static items: keys you pick up to unlock gates to the next area,
Sevenfold-style. No battles, story, magic, stats, NPCs, or roaming
monsters.

ROM: `releases/area-quest.gbc` — md5 `409ff04c5e89e9afabb6a88db7b23e84`,
32,768 bytes, ROM ONLY (cart type 0x00), CGB flag. 195 BG tiles
(127 scene + 54 font + key sets 8+3+2+1 = 14), within the
256-tile limit, so no MBC5 fallback was needed. Copy:
`~/workspace/your_files/area-quest-v3.gbc` (same md5). (The
discarded (1,4)-gate build, md5 `4536fb0fef07658310c7342d1e1ac040`,
was overwritten in releases/ and never copied to your_files
under the v3 name — the v3 file now holds only the corrected
mirrored-gate build.)

## The three areas

- **Area 1 — Stone Dungeon** (start): v3 brick/slate walls (lighter
  slate + darker cinder, by wall position), brown dirt floor,
  dark-slate ceiling. Bronze Key at (5,3), in a side branch off the
  gate route, so the gate can be seen locked before the key is taken.
  Gate at (10,10).
- **Area 2 — The Meadow**: wide open, no stone walls. Blue sky,
  green grass floor, a tree-line border (trees, not stone, so you
  cannot walk off the map but the area still feels open), and
  scattered single-cell trees (green foliage) and rocks (grey
  boulders) as blockers. Silver Key at (5,5), gate at (10,9),
  return gate at (1,1) (v3, -> Area 1 (9,10) east), entry (2,1).
- **Area 3 — Mossy Crypt**: dark mossy stone blocks (different
  pattern and green-tinted palettes from Area 1), darker stone
  floor, dim ceiling. No key, no forward gate — it ends with a
  gold exit cell at (10,10) leading to a victory screen, plus a
  return gate at (1,1) (v3, -> Area 2 (9,9) east), entry (2,1).

Each area is 12x12. On area entry the game loads that area's 8 BG
palettes (CGB holds only 8 at once, so palettes switch per area);
wall/floor/ceiling tile patterns also differ per area, not just
palette swaps.

## Keys and gates

- Keys are static items on a tile, rendered with the SAME v3
  sprite-casting BG-billboard technique as the wraith — camera
  transform, screen x from lat/fwd, per-column wall/tree
  occlusion — NOT hardware sprites and NOT a fixed-size overlay.
  A key behind a tree or wall is correctly hidden. v2 keys are
  SMALL and floor-anchored at tile coordinates (see v2 changes
  above): 32x16 at 1 tile, 24x8 at 2, 16x8 at 3, 8x8 at 4, each
  from its own per-distance tile set with a 1:1 tile mapping,
  so no overlapping clips at any distance. Keys are drawn as a
  golden key on a wide gold base, so the billboard tiles are
  mostly solid (a thin key alone would be mostly background
  tiles).
- Walking onto a key's tile picks it up with a distinct pickup
  chime (not the step sound). The gate in that area then turns
  from locked to gold.
- A locked gate is a dark iron-red door, clearly not a normal
  wall; it blocks movement with a bump sound. An unlocked gate
  renders gold, like the v3 exit. Stepping onto an unlocked gate
  loads the next area IMMEDIATELY (v2: no intro screen, no Start
  press — v1's brief area-intro screens are removed), with play
  resuming in the next area at (2,1) facing east in v3 (gate
  behind at (1,1)); Area 1 entry from the title stays (1,1).
- v3 return gates (see v3 changes above): (1,1) in Areas 2 and 3,
  always open, gold in play AND on the minimap, same rendering
  as an unlocked forward gate. Stepping onto one loads the
  previous area immediately at its forward-gate approach cell,
  facing east — no key check, no bump, no intro screen, and no
  loop (entry (2,1) is never a gate).

## Controls

- Up / Down: step forward / back one tile
- Left / Right: turn 90 degrees
- Start: start from the title screen, and from the victory
  screen back to the title (v2: no area-intro screens remain);
  in play, Start opens the inventory screen, and Start again
  closes it (v3)
- Select: in play, opens the minimap screen; Select again (or
  Start) closes it (v3). Select on title/victory does nothing.
- Step sound on every step and turn; bump sound on blocked steps
  (walls, trees, rocks, locked gates); pickup chime on keys;
  victory jingle on the final exit. No music.

## Build

```sh
bash build.sh   # GBDK at ~/workspace/gbc/tools/gbdk
```

build.sh asserts the header (32KB, ROM ONLY, CGB) and, for each
area, that the key is reachable from the entry, the gate is
reachable after the key, and the final exit is reachable (BFS),
plus v3: each return gate is reachable from its area's entry,
key/gate/exit stay reachable WITHOUT passing through the return
gate, return destinations are open cells, and each destination
faces its area's forward gate in one step.

## QA

`qa/playthrough.py` (PyBoy, 83/83 checks passing): title, Area 1
view, locked-gate negative tests in Areas 1 and 2 (bump, no pass,
key flags unchanged), key billboards at distances 1, 2, 3, and 4
plus a distance-2 lateral offset (each measured as ONE small key
of the target size — gold-bbox heights 30/21/12/6 px at 1/2/3/4
tiles, within 8 px of the 32/24/16/8 px tile sizes — with no
repeated key shapes), both pickups, both unlocked/gold gates,
both SEAMLESS gate transitions (proof: the frame after the gate
step is already the next area's view at (2,1) east, state PLAY,
with no intro state and no Start press, and standing on (2,1)
does not bounce back), Area 2 open meadow view (blue sky +
green grass), meadow trees/rocks in view, Area 3 dark view,
inventory after Bronze (Bronze YES, Silver --) and after Silver
(both YES, Start open/close, position/keys unchanged), minimap
in each area (player/key/gate/return/exit cells pixel-checked,
Select or Start close, position/keys unchanged), BOTH back
routes per gate — Down step-back from entry AND turn-180 + Up
forward — each followed by forward again with no loop, the
full back chain (Crypt -> Meadow -> Dungeon), final exit to
victory, and victory back to title.
Screenshots in `qa/shots/`, all eyeballed: the three areas are
distinct at a glance, the meadow is genuinely open with trees
and rocks (not a walled dungeon), keys read as single small gold
keys at every distance, locked gates read as red iron doors,
unlocked/return gates read as gold (same rendering), inventory
is readable in both key states, minimaps are readable and
correct in all three areas (return gate gold at (1,1), player
bright yellow, forward gate/exit gold).

## Honest limits / known compromises

- Not verified: real-device audio (step/bump/pickup code paths are
  present and unchanged in structure from v3) and play on Cori's
  RetroArch — that remains the real gate.
- Meadow palette compromise: trees and rocks share one wall
  palette (tree pattern uses values 1/2 for green, rock pattern
  uses values 1/3 for grey, colour 3 is light grey). As a side
  effect, the bottom row of a tree/rock column shows a light-grey
  strip where the wall meets the grass, instead of grass — it
  reads as a stone base under trees/rocks, not as a bug, but it
  is a palette-sharing artefact, not a separate grass tile.
- QA note: the pickup chime blocks the main loop, so the proof
  block is stale for >30 ticks after a key step; QA waits 60
  ticks after steps for that reason. Also, the Area 1 key was
  moved from (8,9) to (5,3) during QA because (8,9) sat on the
  only route to the gate approach, so a locked gate could never
  actually be seen before the key — (5,3) is a side branch, so
  the locked gate is now genuinely encounterable.
- This is a prototype to judge the experience. It is not a game
  yet: no story, no combat, no save, and only three areas with
  one key each (v3 adds an inventory display and a minimap, but
  still no new items, chests, or artifact system).
- v3 loop/trap note (checked, not just claimed): the only way
  back is stepping onto (1,1); entry (2,1) is open, not a gate,
  and QA proves standing on it does not bounce, forward-after-
  return works for both gates and both back routes, and
  key/gate/exit remain reachable without passing through a
  return gate. A player who walks west from a (2,1) entry on
  purpose will return — that is the doorway working as designed,
  not a trap, since stepping east through the forward gate
  brings them straight back.
