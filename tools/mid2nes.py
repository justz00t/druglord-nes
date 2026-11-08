#!/usr/bin/env python3
"""Converts a MIDI file into easter.bin, the song the game plays after the cocaine easter egg.

Input: tools/easter_local.mid if it exists (your own song: it is in .gitignore and never committed),
otherwise tools/easter_default.mid (an original placeholder).

How the MIDI is played on the NES (a generic import, nothing tuned for one song):
  * time is cut into 16th notes; the first tempo in the file is used for the whole song
  * at every 16th, of the notes then sounding (drum channel 10 ignored): the lowest, if below middle G,
    goes to the triangle (bass); of the rest, the highest goes to pulse 1 and the next to pulse 2
  * the game adds a simple drum beat (kick, snare, hats) on the noise channel
  * the song loops; if it does not fit (about 400 changes of notes), it is cut at a bar line

easter.bin: 6-byte header (rate 8.8 = 16ths per frame, event count, 2 spare), 128 pulse periods
(little endian, MIDI note 0-127), then 4 bytes per event: wait in 16ths, pulse 1, pulse 2, triangle
(0 = silent, else the MIDI note; bit 7 on a pulse note = struck again)."""
import os, struct, sys
here = os.path.dirname(os.path.abspath(__file__)); out = os.path.dirname(here)
src = os.path.join(here, 'easter_local.mid')
if not os.path.exists(src): src = os.path.join(here, 'easter_default.mid')
d = open(src, 'rb').read()
ntr, div = struct.unpack('>HH', d[10:14])
def vl(b, i):
    v = 0
    while True:
        c = b[i]; i += 1; v = (v << 7) | (c & 0x7f)
        if not c & 0x80: return v, i
p = 14; tempo = None; notes = []           # (on tick, off tick, note)
for t in range(ntr):
    L = struct.unpack('>I', d[p + 4:p + 8])[0]; b = d[p + 8:p + 8 + L]; p += 8 + L
    i = tm = rs = 0; held = {}
    while i < len(b):
        dt, i = vl(b, i); tm += dt; s = b[i]
        if s == 0xff:
            l, j = vl(b, i + 2)
            if b[i + 1] == 0x51 and tempo is None: tempo = int.from_bytes(b[j:j + 3], 'big')
            i = j + l; continue
        if s in (0xf0, 0xf7): l, j = vl(b, i + 1); i = j + l; continue
        if s & 0x80: rs = s; i += 1
        ty, ch = rs & 0xf0, rs & 15
        if ty in (0xc0, 0xd0): i += 1; continue
        k, v = b[i], b[i + 1]; i += 2
        if ch == 9 or ty not in (0x80, 0x90): continue
        if ty == 0x90 and v: held.setdefault(k, []).append(tm)
        elif held.get(k): notes.append((held[k].pop(0), tm, k))
tempo = tempo or 500000
U = div / 4                                # ticks per 16th
end = max(off for _, off, _ in notes); units = int(-(-end // U)); units = -(-units // 16) * 16
ons = {}; sound = [[] for _ in range(units)]
for on, off, k in notes:
    a = int(round(on / U)); z = max(a + 1, int(round(off / U)))
    ons.setdefault(a, set()).add(k)
    for q in range(a, min(z, units)): sound[q].append(k)
ev = []; last = None
for q in range(units):
    ns = sorted(set(sound[q]), reverse=True)
    tri = ns[-1] if ns and ns[-1] < 55 else 0
    rest = [k for k in ns if k != tri]
    p1 = rest[0] if rest else 0; p2 = rest[1] if len(rest) > 1 else 0
    while p1 and p1 < 45: p1 += 12          # the pulses cannot go that low
    while p2 and p2 < 45: p2 += 12
    hit = ons.get(q, set())
    row = (p1 | (0x80 if p1 and (p1 in hit or p1 - 12 in hit or p1 - 24 in hit) else 0),
           p2 | (0x80 if p2 and (p2 in hit or p2 - 12 in hit or p2 - 24 in hit) else 0), tri)
    if row != last or any(r & 0x80 for r in row[:2]): ev.append([q, row]); last = row
MAXEV = (1900 - 6 - 256) // 4             # bank 2 also holds the tile sets, the menu song, the victory tune and
                                          # the win-scene picture: about 1.9 KB is left for this song
if len(ev) + units // 255 + 2 > MAXEV:
    cut = units
    while len([e for e in ev if e[0] < cut]) + cut // 255 + 2 > MAXEV: cut -= 16
    print('mid2nes: the song is too long for the ROM; it is cut after %d bars' % (cut // 16))
    ev = [e for e in ev if e[0] < cut]; units = cut
if not ev or ev[0][0] != 0: ev.insert(0, [0, (0, 0, 0)])
recs = []
for n, (q, row) in enumerate(ev):
    nq = ev[n + 1][0] if n + 1 < len(ev) else units
    gap = nq - q; first = True
    while True:                            # a wait longer than 255 becomes extra "hold" events
        w = min(gap, 255); gap -= w
        r = row if first else (row[0] & 0x7f, row[1] & 0x7f, row[2])
        recs.append(bytes([w]) + bytes(r)); first = False
        if gap == 0: break
rate = round(256 * (1e6 / tempo) * 4 / 60.0988)          # 16ths per frame, 8.8 fixed point
per = [min(2047, round(1789773 / (16 * 440 * 2 ** ((k - 69) / 12))) - 1) for k in range(128)]
blob = struct.pack('<HHH', rate, len(recs), 0) + b''.join(struct.pack('<H', x) for x in per) + b''.join(recs)
assert len(blob) <= 1900
open(os.path.join(out, 'easter.bin'), 'wb').write(blob)
print('mid2nes: %s -> easter.bin, %d bars, %d events, %d bytes' % (os.path.basename(src), units // 16, len(recs), len(blob)))
