# DRUGLORD for the NES

A separate, playable port of DRUGLORD (Phil Erwin, 1991-93) to the NES, written in C for cc65.
It is **not** part of the byte-matched C source in https://github.com/justz00t/druglord, which is never touched.

![title screen](docs/title.png)

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

On start-up a credits screen (4 seconds) is followed by the title screen: just the DOS title art (converted from the original's `titleArt`, one text cell = 4 x 8 pixels) in white, and PRESS START. A red snake crawls across the logo and back (a bright red head, its tail fading to pink; it moves by rewriting the logo's attribute bytes in vblank), then the whole logo flashes red, white, red, white, and the snake sets off again. The title music is the piano riff of `tools/title.mid` on pulse 1, pulse 2 and triangle with a drum pattern on the noise channel (16 bars, about 41 seconds). After 60 seconds on the title screen the high score table is shown for 2 seconds, then the title again (START on the table starts a game too).

The main screen has its own music, a boom-bap beat at 90 BPM in C minor (`tools/menu_song.py` writes `menu.h`; `menu_frame()` plays it): triangle bass with a pitch-drop thump under each kick, chord stabs as a fast arpeggio on pulse 2, a hook on pulse 1 in bars 5-8, drums on the noise channel. Sound effects take over pulse 2 and the noise while they play; full-screen pages (events, the cops, death, the win) stop it. Quitting shows the NOTICE screen; A returns to the title screen.

| main screen | jet (the map shows where you are) | death |
|---|---|---|
| ![main](docs/main.png) | ![jet](docs/jet.png) | ![death](docs/death.png) |

When you win, the DOS end scene is drawn (the table, the lamp and the coins, from the original's `endScene` data at 3 x 8 pixels per text cell, in its own 4 KB tile set that is loaded into the second pattern table for that screen): a pistol for each gun you own, then the stacks of money pile up with rising beeps, one stack for every $100,000 and a new layer every $2,000,000, and the score list scrolls in a box over the bottom of it (UP/DOWN, A = done), as on the PC.

![win](docs/win.png)

**Sound effects** are played a frame at a time on pulse 2, the noise channel and (outside the title) pulse 1, so they can sweep: the police siren is a slow wail (500 to 1300 Hz and back, eased) followed by four fast yelps, with the second pulse a touch sharp so the two beat; the guns are a crack over a boom (the Auto Mag and AK-47 add a low thump; the Uzi and AK-47 fire bursts); the jet's engines spool up and whine off; deals ring up a cash register, the buzzer refuses, hits thud, the thugs' bat lands a punch, and the cursor ticks. A press of A cuts a long effect short (the debug ROM's sound test plays all of them).

The rules, odds and formulas are the DOS original's (see "GAME RULES AND FORMULAS" in `c/DRUGLORD.C` of https://github.com/justz00t/druglord), including its
random number generator.  The NES has no floating point, so the formulas are redone with integers in a way that gives
the same numbers (price multipliers, air fares, loan terms, interest, the coat prices and the score tables).
Original quirks are kept, e.g. banking does not pass a day and events only happen once per city.

## What is different from the PC version

* The screen is 32 x 30 text tiles, so the layout is rearranged and events and dialogues fill the whole screen.
* Controller input instead of a keyboard.
* No save game (the high-score table is kept in battery RAM instead).
* The cop fight screen is simplified (one action line at a time), but uses the same rules.
* The death screen is the original's text in its frame (no tombstone).
* The jet menu shows a small map of the cities; the main screen colours the labels, drug names and prices.

## Build

`./build.sh` builds both ROMs (`druglord.nes` and `druglord-debug.nes`, the latter with `-DDEBUG`); it needs cc65 on the path; set `CC65_HOME` if it is not installed system-wide).
The source is split by PRG bank: `druglord.c` (fixed bank: engine, rules, main loop), `bank_a.c` (cop fight, events, menus), `bank_b.c` + `hiscore.c` (start/end screens, title, high scores, debug), `crt0.s` (start-up, bank switching), `dl.h` (shared declarations). Calls into the switchable banks go through `_bankcall` (cc65 `wrapped-call`).
`tools/mkdata.py` creates the tiles (`chr.bin`), the title data (`title.h`), the music tables (`music.h`) and the score tables (`tables.h`).
The font is the IBM PC 8x8 font, so tile numbers are DOS characters (tile + 0x80 is the inverted glyph, used as highlight; tiles 8-12 are the block and shade characters).
PRG bank 2 holds three 4 KB tile sets: the game's (pattern table 0), the title's (the font plus the logo tiles built from `tools/title_art.bin`, the DOS `titleArt` data) and the end scene's (the font plus the picture tiles from `tools/end_scene.bin` and `tools/gun_sprite.bin`, the stacks of money and the pistol); the last two are loaded into pattern table 1 as needed (`load_pt1`).
