#!/bin/sh
# Build both ROMs with cc65 (set CC65_HOME if cc65 is not installed system-wide):
#   druglord.nes        the normal game
#   druglord-debug.nes  the same plus a debug menu (SELECT+START in the main menu)
set -e
cd "$(dirname "$0")"
python3 -I tools/mkdata.py
build() {   # $1 = output, $2.. = extra defines
    out=$1; shift
    ca65 -t nes -o $B/crt0.o crt0.s
    ca65 -t nes -o $B/chr.o chr.s
    cc65 -t nes -Os "$@" -o $B/druglord.s druglord.c && ca65 -t nes -o $B/druglord.o $B/druglord.s
    cc65 -t nes -Os --code-name CODE_A --rodata-name RODATA_A "$@" -o $B/bank_a.s bank_a.c && ca65 -t nes -o $B/bank_a.o $B/bank_a.s
    cc65 -t nes -Os --code-name CODE_B --rodata-name RODATA_B "$@" -o $B/bank_b.s bank_b.c && ca65 -t nes -o $B/bank_b.o $B/bank_b.s
    cc65 -t nes -Os --code-name CODE_B --rodata-name RODATA_B "$@" -o $B/hiscore.s hiscore.c && ca65 -t nes -o $B/hiscore.o $B/hiscore.s
    ld65 -C nes.cfg -o $out $B/crt0.o $B/druglord.o $B/bank_a.o $B/bank_b.o $B/hiscore.o $B/chr.o nes.lib
}
B=$(mktemp -d)
build druglord.nes
build druglord-debug.nes -DDEBUG
rm -rf $B
ls -l druglord.nes druglord-debug.nes
