; IBM CP437 8x8 font, colour 1 on 0 (generated into chr.bin by tools/mkdata.py), kept in PRG bank 2 and copied to CHR-RAM at boot
.export _NESfont
.segment "FONT"
_NESfont:
.incbin "chr.bin"
; the easter-egg song (tools/mid2nes.py), in the 4 KB after the tile sets
.export _EasterSong
_EasterSong:
.incbin "easter.bin"
