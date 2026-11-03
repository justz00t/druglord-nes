# DRUGLORD for the NES

A separate, playable port of DRUGLORD (Phil Erwin, 1991-93) to the NES, written in C for cc65.
It is **not** part of the byte-matched `c/` folder, which is never touched.

`druglord.nes` is the ready-to-run ROM (mapper 2 / UxROM, 64 KB PRG, CHR-RAM, 8 KB battery-backed WRAM). Any NES emulator will run it.

`druglord-debug.nes` is the same game plus a **debug menu**: press SELECT and START together on the main menu to pick any event or screen (cop fight, every random event, mugger variants, thugs, death, win, replay prompt, NOTICE, credits, title) or open the **sound test**, which plays every sound effect and the title music. The regular ROM has no debug menu.

## Controls

| Button | Does |
|---|---|
| D-pad up/down | move the cursor in a menu |
| D-pad left/right | number entry: move between digits |
| D-pad up/down (number entry) | change the digit |
| SELECT (number entry) | fill in the largest sensible amount (most you can buy, everything you own, everything you owe) |
| A | choose / confirm / continue |
| B | cancel / go back |
| START | start from the title screen |

## What is in it

The whole game: 8 cities and 8 drugs, buying and selling, the air fares, the bank, the loan shark and his thugs,
the hospital, the ten random events (cops fight with friends or a posse and guns, mugger, trenchcoat, gun smuggler, blackmail,
price events, brownies), the status/rank system, the final score and the NOTICE screen when you quit.
**High scores:** the top 10 (3-letter initials, score and rank) are kept in the cartridge's battery RAM, so an emulator keeps them in its save file. A fresh cartridge starts with ten made-up players. When you win and your score makes the table you enter your initials (UP/DOWN letter, LEFT/RIGHT or A/B move, START = done). The table also flashes for a second before the title screen whenever you leave a game (not between the credits and the title).

On start-up a credits screen (4 seconds) is followed by the title screen. The title music is the piano riff of `tools/title.mid` (pulse 1, pulse 2 and triangle, 16 bars, looped). There is no music during the game, only sound effects. Quitting shows the NOTICE screen; A returns to the title screen.

The rules, odds and formulas are the DOS original's (see "GAME RULES AND FORMULAS" in `c/DRUGLORD.C`), including its
random number generator.  The NES has no floating point, so the formulas are redone with integers in a way that gives
the same numbers (price multipliers, air fares, loan terms, interest, the coat prices and the score tables).
Original quirks are kept, e.g. banking does not pass a day and events only happen once per city.

## What is different from the PC version

* The screen is 32 x 30 text tiles, so the layout is rearranged and events and dialogues fill the whole screen.
* Controller input instead of a keyboard.
* No save game and no high-score list.
* The cop fight screen is simplified (one action line at a time), but uses the same rules.
* Pictures (the title, tombstone, final scene) are drawn with text characters.

## Build

`./build.sh` builds both ROMs (`druglord.nes` and `druglord-debug.nes`, the latter with `-DDEBUG`); it needs cc65 on the path; set `CC65_HOME` if it is not installed system-wide).
The source is split by PRG bank: `druglord.c` (fixed bank: engine, rules, main loop), `bank_a.c` (cop fight, events, menus), `bank_b.c` + `hiscore.c` (start/end screens, title, high scores, debug), `crt0.s` (start-up, bank switching), `dl.h` (shared declarations). Calls into the switchable banks go through `_bankcall` (cc65 `wrapped-call`).
`tools/mkdata.py` creates the font tiles (`chr.bin`), the music tables (`music.h`) and the score tables (`tables.h`).
The font is the IBM PC 8x8 font, so tile numbers are DOS characters (tile + 0x80 is the inverted glyph, used as highlight).
