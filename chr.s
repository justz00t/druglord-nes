; 8 KB CHR-ROM: IBM CP437 8x8 font, colour 1 on 0 (generated into chr.bin by tools/mkdata.py)
.export NESfont
.segment "CHARS"
NESfont:
.incbin "chr.bin"
