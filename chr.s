; IBM CP437 8x8 font, colour 1 on 0 (generated into chr.bin by tools/mkdata.py), kept in PRG bank 2 and copied to CHR-RAM at boot
.export _NESfont
.segment "FONT"
_NESfont:
.incbin "chr.bin"
; the sound effects and the in-game music (tools/mid2nes.py from the MIDI files in tools/snd), in the bank of their player (seq.c)
.segment "MUSIC"
.export _Sounds
_Sounds:
.incbin "sounds.bin"
; the easter-egg song (tools/mid2nes.py), with PRG bank 6 to itself
.segment "EASTER"
.export _EasterSong
_EasterSong:
.incbin "easter.bin"
