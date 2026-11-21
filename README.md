# PAWS OFF!

A jewel-defence game for the Game Boy Advance.

You are the Duchess, out for a midnight stroll in the manor garden, dripping
with the family jewels. The local raccoons want them. You can't fight a
raccoon, but no raccoon can resist something shiny: throw coins to keep them
busy, and hold on to your jewels until the sun comes up. Survive five nights.

`paws-off.gba` is the built ROM. It runs in any GBA emulator (mGBA,
NanoBoyAdvance) and on a flash cart; the high score is saved to SRAM.

## How to play

| Button | Action |
| --- | --- |
| D-pad | Walk |
| A | Throw a coin the way you are facing (it homes a little on a nearby raccoon) |
| B | Set down a disco ball: every raccoon nearby drops everything and dances |
| L / R (hold) | Strafe: keep facing the same way while you walk |
| START | Pause |

- A raccoon that touches you snatches a jewel and runs for a gap in the hedge.
  Hit it with a coin (or tempt it with one on the ground) and it drops the
  jewel. Pick it up before another raccoon does. A jewel carried out of the
  garden is gone for good.
- Raccoons crouch and tremble (**!**) before they pounce. Step aside.
- A coin that hits a raccoon keeps it busy for a few seconds. A coin that
  misses lies on the lawn and lures the next raccoon that wanders close. You can
  pick your own missed coins back up.
- Coins and the odd disco ball turn up around the garden. Your pockets are
  only so deep.
- **Kits** (small, brown) are fast, but adore shinies. **Chonks** (big, dark)
  are slow and ignore coins on the ground: only a direct hit or a disco ball
  works on them.
- At dawn the raccoons scatter and anything they had not carried off comes
  back to you. After each night the butler offers a perk.
- The game ends when all five jewels have left the garden.

## Building

Needs devkitARM with libtonc (`DEVKITPRO` / `DEVKITARM` set), same as the
libtonc examples this grew out of.

    make          # build paws-off.gba
    make run      # build and start it in mGBA
    make art      # regenerate graphics, map and music (python3, no extra modules)
    make clean

## Layout

| Path | What |
| --- | --- |
| `source/main.c` | The game: states, raccoon AI, drawing, HUD, save |
| `source/sound.c` | Music + sound effects on the four PSG channels |
| `source/nav.iwram.c` | Path-finding flood fill (ARM code in IWRAM for speed) |
| `source/gfx.c`, `gfx.h` | **Generated** by `tools/mkgfx.py`, do not edit |
| `tools/mkgfx.py` | All the art (ASCII sprites, procedural garden + title screen) and the songs |
| `tools/preview/` | PNG previews written by `mkgfx.py` (sprite sheet, map by night and at dawn, title) |
| `tools/harness.c`, `bot.sh` | Headless mGBA test harness and an auto-player bot |

Things that are easy to change:

- **Sprites**: the ASCII art near the top of `tools/mkgfx.py`. One character is
  one pixel; the legend is the palette list above it.
- **The garden**: the `MAP` grid in `tools/mkgfx.py` (`#` hedge, `=` path,
  `T` tree, `L` lamp, `P` pond, `X` gap the raccoons use).
- **Music**: the `song(...)` calls in `tools/mkgfx.py`, written as
  `note:length` (`a4:2`, `r:4` for a rest).
- **Difficulty**: `night_cfg` and `rtypes` at the top of `source/main.c`.

## Testing without playing

`make harness` builds a small program on top of libmgba (`brew install mgba`)
that runs the ROM with scripted input and saves screenshots:

    tools/harness paws-off.gba 100 +START 3 -START 300 shot:/tmp/night1.png

`make soak` lets a simple bot play a whole game and prints how each night went.
It is how the difficulty was tuned and it is a quick check that a change did not
break anything.

## What it borrows from the libtonc examples

`template` (project and Makefile), `obj_demo` and `obj_aff` (sprites; kits and
chonks are affine-scaled), `sbb_reg` and `bigmap` (a scrolling map bigger than
the screen), `bld_demo` (blended shadows), `snd1_demo` (PSG sound),
`swi_vsync` (the VBlank loop), `bm_modes` (the mode 4 title screen).
