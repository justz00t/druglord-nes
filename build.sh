#!/bin/sh
# Build druglord.nes with cc65 (set CC65_HOME if cc65 is not installed system-wide).
set -e
cd "$(dirname "$0")"
python3 -I tools/mkdata.py
cl65 -t nes -C nes.cfg -Os -o druglord.nes druglord.c chr.s
ls -l druglord.nes
