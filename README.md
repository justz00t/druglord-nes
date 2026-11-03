# DRUGLORD for the NES (work in progress)

A separate port of DRUGLORD (Phil Erwin, 1991-93) to the NES, written in C for cc65.
It is **not** part of the byte-matched `c/` folder, which is never touched.

Status: title screen and title music. The game itself is still to be written.

* Mapper 0 (NROM), 32 KB PRG, 8 KB CHR. The PC's CP437 8x8 font is the tile set, so tile number = DOS character code.
* Title music: `tools/title.mid` (90 bpm, one melody voice plus hat/clap/snare), converted by
  `tools/mkdata.py` into step tables (`music.h`). Melody plays on pulse 1, drums on the noise channel.

Build: `./build.sh` (needs cc65 on the path; `CC65_HOME` if it is not installed system-wide).
