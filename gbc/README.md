# Slice — GBC instant prototype

First-person maze prototype for the Game Boy Color, in Wizardry style: tile-to-tile movement with instant redraw, no smooth transition. Not a full game: a small maze to walk, turn, and escape from.

Load `raycaster-gbc-instant.gbc` in a GBC emulator.

## Finding the exit (read this first, it will save your sanity)

The exit is deliberately, unfairly hidden in this prototype, and it is the same maze in the GBA and GBC versions:

- The exit tile is at (10,10), at the far end of the maze, and you can only step onto it from the north, from (10,9) heading south.
- It looks exactly like a normal corn wall. It does not look different, it does not glow, and it is not at the end of a straight corridor you would bump into by accident.
- The two brown walls are scarecrows, at (10,1) and (8,10). They block with a bump sound and are not the exit. If a wall dings, skip it.
- When you step onto the exit tile, you get the YOU ESCAPED! / THE FIELD LETS GO screen, then Press Start to walk it again.

Walkthrough from the start, facing east:

Turn right, step x3, turn left, step x2, turn right, step x2, turn right, step x2, turn left, step x2, turn left, step x2, turn right, step x2, turn left, step x3, turn left, step, turn right, step x4, turn right, step.

That last step is south into (10,10) and triggers the exit screen.


## Controls

- Up: step forward one tile
- Down: step back one tile
- Left / Right: turn 90 degrees
- Start: start / restart
- Tap to step. Holding a direction repeats after a short pause.

## Screenshots

![Title screen](screenshots/title.png)

![Start view](screenshots/start.png)

![Turn](screenshots/turn.png)

![Step south](screenshots/step.png)

![Blocked step, bump, no move](screenshots/bump.png)

![Exit](screenshots/exit.png)

## How it works

- GBC tile hardware, one DDA ray per screen tile column (20 columns), wall height in pixels with precomputed top / bottom edge tiles
- Instant movement: the DDA runs once per tap, then the game renders exactly one full frame with the LCD off and goes idle with no rendering until the next tap
- Each tap briefly blanks the screen while the frame is written, then the new view appears instantly. That is the intended redraw, not a bug.
- Short step / bump sounds only

## Build

Requires GBDK-2020. See `build.sh` (adjust the GBDK path for your setup). This produces a plain 32KB ROM ONLY, CGB-compatible ROM.
