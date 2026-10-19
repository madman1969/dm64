# Dungeon Master 64

A Commodore 64 port of FTL's *Dungeon Master* (Atari ST, 1987), written in C for cc65. It's
built from **your own copy of the original disk**: the build reads `Dungeon Master.stx` and
converts the real dungeon, texts, object names and rules tables. The C64 engine around them is
new code, not a decompile.

None of the game's data is in the source files. `tools/dmconv.py` extracts it into `gen/` and
`bin/` at build time. Don't share those folders or the disk image they produce.

## What you need

- **A C64 with a RAM expansion**: an REU (128K or more) or a SuperCPU. The 14 levels are kept in
  far memory through the [FarMem](../Tedide/samples/FarMem) library. Only the level the party is
  on sits in the C64's own RAM. In VICE, use `-reu -reusize 512`.
- To build: Python 3, cc65 (`C:\cc65`), VICE 3.x (for `c1541`), and Tedide's FarMem sources in
  `..\Tedide\samples\FarMem`.

## Building and running

```powershell
.\build.ps1          # convert, compile, make bin\dm64.d64
.\build.ps1 -Run     # ...and start it in VICE with a 512K REU
```

Or open `DM64.tsln` in Tedide. It builds the FarMemC64 library, runs the converter as a
pre-build step and makes the disk image as a post-build step. Its VICE arguments attach the disk
image and add the REU.

The disk holds three files: `DM64` (the program), `DMHI` and `DMLEV` (data). On a real machine:
`LOAD"DM64",8,1` then `RUN`.

Saved games go on the same disk, as `DMSAVE1` and `DMSAVE2` (133 blocks each; both fit). A
rebuild with `build.ps1` keeps them. Tedide's post-build step doesn't, so copy them off first if
you build there. A save only loads into a build made from the same game data, and
`SAVE_VERSION` in `src/save.c` must go up whenever the saved structures change.

## Playing

You start outside the Hall of Champions. Walk in, look into the mirrors, and recruit up to four
champions. Each comes with their real statistics and possessions from the original.

| Key | Action |
| --- | --- |
| W / cursor up | Forward |
| S / cursor down | Back |
| A, D | Step left, right |
| Q / cursor left, E / cursor right | Turn left, right |
| Space | Press what's ahead: a wall switch, keyhole, door button or mirror; take from or put into an alcove |
| T | Take an object (from the wall ahead, or from the floor) |
| G | Put down what's in your hand (into an alcove ahead, or on the floor) |
| 1-4 | That champion attacks with the weapon in their action hand |
| I | Inventory: A-O swap your hand with a slot, 1-4 pick a champion, E eat/drink/read |
| X | Eat, drink or read what's in your hand |
| C | Cast a spell (see below) |
| H | Help |
| F1, F3 | Save, load (slot 1 or 2). When the party dies, F3 loads a game |

The game runs in real time, as the original does. Creatures move and attack whether you press
keys or not.

### Magic

Press C, choose who casts, then pick runes with 1-6, one row at a time: a power rune (LO, UM, ON,
EE, PAL, MON), then up to three more from the element, form and class rows. Each rune costs
mana as you pick it; DEL takes the last one back. RETURN casts and SPACE gives up. The game
waits while you choose, unlike the original. The spells are DM's own. The dungeon's scrolls and
walls teach some of them: VI into an empty flask heals, FUL IR is a fireball, and ZO opens doors.
A spell needs a skill level of its own level plus the power. A caster short of it may fail,
though less often the wiser they are.

## What's in it

### Taken from the original disk

- All 14 levels, square by square, with every door, pit, staircase, teleporter, fake wall,
  pressure plate, switch, keyhole, alcove, text, creature and object where FTL put them.
- The dungeon's texts (wall writing, messages, scrolls, champion descriptions), decoded from
  DM's 5-bit packed format.
- Object names, and the rules tables from `GRAPHICS.DAT`: weapon damage and weight, armour
  defence, which slots each object fits, and each creature type's speed, attack, defence,
  poison and dexterity, and which creatures are non-material. These are FTL's own LZW-packed
  tables, unpacked by `tools/dmgfx.py`.
- The spell table: all 25 spells' runes, required skill levels, skills, kinds and strengths,
  and the runes' mana costs.

### The engine

- A first-person view in custom characters, with 45-degree perspective so the diagonals fit
  exactly. Creatures ahead are multicolour sprites; ones farther off or to the side are icons.
- Sensors: floor plates (anything, party, object, party carrying an object, facing a
  direction), wall switches, keyholes that keep the key, any-object and specific-object
  sensors, AND/OR gates and countdowns. Effects are set, clear, toggle and hold, with delays,
  once-only and reversed. They open and close doors, pits and teleporters, and show or hide texts.
  The sensor bit layout was worked out from the data (a target's coordinates land on the doors
  it should open, for example). The gates' and countdowns' details are a best guess.
- Doors that slide shut and open over time, door buttons, and doors that won't close on the party.
- Stairs and pits between levels, lined up by each level's place in the dungeon. Teleporters
  move the party, within a level or to another one, with their rotation.
- Recruiting champions from the mirrors, with an inventory of 15 slots each. Objects travel
  with the party from level to level.
- Melee combat using the weapon tables. Creature groups chase and attack at their own speeds,
  and drop what they carry when they die. Weapons pass through non-material creatures (ghosts,
  black flames, materializers, water elementals): only magic, or a Vorpal Blade, hurts them.
- Magic: casting with mana costs and skill checks, and spells as projectiles that fly through
  the view (fireball, lightning, poison bolt and cloud, harm non-material, and ZO, which opens
  doors; fireballs blast open doors that can be burnt), potions cast into empty flasks (all
  nine), party and fire shields, invisibility (creatures stop chasing), and ZOKATHRA.
- Food, health/stamina/mana recovery, potions (VI, MON, EE, ROS, KU, DANE, NETA, water), and
  scrolls.

### Not yet

- Darkness and torches (so the light and darkness spells only say so), magic vision, magic
  footprints, water, experience and levelling up, and poison. Spells can't yet be learnt
  better through practice.
- The dungeon's projectile launchers and creature generators, and creatures casting spells.
- DM's exact formulas for spell damage, shield strength and how long spells last. The ones
  here are simpler stand-ins, scaled by the spell's strength from the table and the power rune.
- Throwing and shooting, bashing doors, creatures using doors and pits, and teleporting objects
  and creatures.
- DM's own graphics (wall and floor decorations, champion portraits, creature pictures) and
  sound.
- The endgame: the Firestaff, Lord Chaos and the fluxcage.

## How it fits in a C64

```text
$0400-$07FF  buffers (the default screen's RAM)           LOWBSS segment, c64-dm64.cfg
$0801-$CBFF  the program, rules tables, the party's level (3K buffer), the party's objects
             (about 300 bytes are left)
$CD00-$CFFF  C stack (768 bytes)
$D000-$DFFF  dungeon texts             } in the RAM under the I/O area and KERNAL,
$E000-$E3FF  screen                    } loaded from DMHI. The video chip uses bank 3
$E400-$E7FF  creature names, help      } and always sees RAM there. The CPU reads the
$E800-$EFFF  character set             } texts and names with the ROMs switched out
$F000-$F7FF  creature sprites          } (hw.c: hi_read, hi_str).
$F800-$FFF9  object names              }
far memory   the 14 level images, about 32K (DMLEV)
```

Each level image (see the top of `tools/dmconv.py`) holds the level's squares, the start of
each square's object list, and its own tables of doors, teleporters, texts, sensors, creatures
and objects. Things are numbered within the level, so the image can be copied in and out of
far memory whole. Objects the party picks up move into a separate pool (`dungeon.c`), which is
how they cross levels.

The C64's own RAM is now nearly full. The next features will need room made first: moving more
of the program's text under the KERNAL, or trimming code. cc65's code is large, about 32K for
this program.

## Source

| File | |
| --- | --- |
| `tools/dmdat.py` | Reads the STX (Pasti) disk image, its FAT12 file system, and `DUNGEON.DAT` |
| `tools/dmgfx.py` | Unpacks `GRAPHICS.DAT`: object names and the rules tables |
| `tools/art.py` | The custom characters and creature sprites (hand-drawn, not DM's) |
| `tools/text.py` | The game's own help text |
| `tools/dmconv.py` | Writes `gen/dmdata.c`, `gen/glyphs.h`, `bin/dmlev.bin`, `bin/dmhi.bin` |
| `src/main.c` | Start-up, main loop, keys |
| `src/dungeon.c` | Levels in far memory, squares, object lists, objects, texts |
| `src/view.c` | The 3D view and sprites |
| `src/ui.c` | Messages, the party panel, the inventory |
| `src/party.c` | Champions, recruiting, combat, eating and drinking |
| `src/sensor.c` | Sensors, doors, pits, stairs, teleporters, moving and clicking |
| `src/creature.c` | Creatures on the party's level |
| `src/magic.c` | Casting spells, projectiles, potions and the other spells |
| `src/save.c` | Saving and loading: the party, then every level image from far memory |
| `src/hw.c` | Video bank, RAM under the ROMs, data files, sound |

## Licence

The source code here is under the MIT licence: see [LICENSE](LICENSE). That covers this code
only. *Dungeon Master* and everything the build extracts from it (the dungeon, texts, names and
rules tables in `gen/` and `bin/`) remain FTL's, and aren't included or licensed here. The
FarMem library is part of Tedide and is used under its own terms.
