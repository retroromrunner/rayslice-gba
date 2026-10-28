# Slice — GBA and GBC prototypes

First-person maze prototypes in Wizardry style: tile-to-tile movement with instant redraw, no smooth transition. Not full games: a small maze to walk, turn, and escape from.

The GBA version here is the working instant version, verified in RetroArch mGBA. The GBC version is the working instant version, verified in PyBoy and on hardware-style play.

## GBA — instant step

Load `releases/raycaster-gba-instant.gba` in a GBA emulator (verified in RetroArch mGBA). The title screen says INSTANT STEP.

- Up: step forward one tile
- Down: step back one tile
- Left / Right: turn 90 degrees
- Start: start / restart
- Tap to step. Holding a direction repeats after a short pause.

Reach the clearing at the far end of the maze for the exit screen.

## Finding the exit (read this first, it will save your sanity)

The exit is deliberately, unfairly hidden in this prototype, and it is the same maze in the GBA and GBC versions:

- The exit tile is at (10,10), at the far end of the maze, and you can only step onto it from the north, from (10,9) heading south.
- It looks exactly like a normal corn wall. It does not look different, it does not glow, and it is not at the end of a straight corridor you would bump into by accident.
- The two brown walls are scarecrows, at (10,1) and (8,10). They block with a bump sound and are not the exit. If a wall dings, skip it.
- When you step onto the exit tile, you get the YOU ESCAPED! / THE FIELD LETS GO screen, then Press Start to walk it again.

Walkthrough from the start, facing east:

Turn right, step x3, turn left, step x2, turn right, step x2, turn right, step x2, turn left, step x2, turn left, step x2, turn right, step x2, turn left, step x3, turn left, step, turn right, step x4, turn right, step.

That last step is south into (10,10) and triggers the exit screen.


### Screenshots

![Title screen](screenshots/title.png)

![Start view, facing east down the corridor](screenshots/start.png)

![Corridor after stepping south](screenshots/corridor.png)

![Close wall, capped so sky and floor stay visible](screenshots/close-wall.png)

### How it works

- Mode 4 bitmap raycaster, 240 columns, textured corn walls with distance and side shading, capped at 144px so a close wall never fills the whole screen
- Instant movement: animation steps are 1, so a tap renders exactly one final frame, then the game idles with no rendering until the next tap
- Short PSG step / bump / exit sounds only

### Build

Requires `arm-none-eabi-gcc` and Python 3.

```sh
bash build.sh
```

This produces `raycaster.gba` and copies it to `releases/raycaster-gba-instant.gba`. The header is patched by `fix_header.py` (title `RAYCASTER`, game code `RAYC`).

### Files

- `src/main.c` — game, raycaster, sound, title / exit screens
- `src/trig.h` — sine / cosine tables
- `src/font.h` — 8x8 font glyphs
- `src/tables.h` — precomputed DDA / wall-height tables
- `src/crt0.s`, `gba.ld` — GBA startup and linker script
- `qa/harness.c`, `qa/render.py` — host QA harness (runs the game natively, dumps framebuffer / palette) and its framebuffer renderer

### Note on the earlier Mode 0 version

An earlier GBA slice version used Mode 0 tiles (30x20 tiles, 600 map entries per view) instead of the Mode 4 bitmap renderer. That version does not load in RetroArch mGBA, even after copying the proven Mode 0 display setup and reverting the Nintendo logo to zero, and a minimal Mode 0 test also failed there. The cause was never found, so the working Mode 4 instant version above is the one shipped here. The smooth 4-frame Mode 4 version lives separately in the raycaster-gba repo, untouched.

## GBC — instant step

See `gbc/` for the Game Boy Color version. Load `gbc/raycaster-gbc-instant.gbc` in a GBC emulator.

- Same maze, same controls: Up / Down to step, Left / Right to turn 90 degrees, Start to start / restart
- Tap to step, one rendered frame per action, then idle
- Each tap briefly blanks the screen while the full frame is written with the LCD off, then the new view appears instantly. That is the intended Wizardry-style redraw, not a bug.
- Short step / bump sounds only

GBC screenshots and source are in `gbc/screenshots/` and `gbc/src/`. Build with GBDK-2020, see `gbc/build.sh`.
