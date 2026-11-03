#!/bin/sh
# Build both ROMs with cc65 (set CC65_HOME if cc65 is not installed system-wide):
#   druglord.nes        the normal game
#   druglord-debug.nes  the same plus a debug menu (SELECT+START in the main menu)
set -e
cd "$(dirname "$0")"
python3 -I tools/mkdata.py
cl65 -t nes -C nes.cfg -Os -o druglord.nes druglord.c chr.s
cl65 -t nes -C nes.cfg -Os -DDEBUG -o druglord-debug.nes druglord.c chr.s
ls -l druglord.nes druglord-debug.nes
