#!/usr/bin/env python3
"""Builds chr.bin (font as 2bpp tiles) and music.h (title tune) from the sources in tools/."""
import struct, sys, os
GAME_BARS = 24
here = os.path.dirname(os.path.abspath(__file__)); out = os.path.dirname(here)

# --- CHR: tiles 0x00-0x7F = glyphs (CP437 font; 1-6 are double-line box pieces, 7 is a shade),
#     tiles 0x80-0xFF = the same glyphs inverted (used as the highlight)
f = open(os.path.join(here, 'font8x8.bin'), 'rb').read()
assert len(f) == 2048
remap = {1: 0xC9, 2: 0xCD, 3: 0xBB, 4: 0xBA, 5: 0xC8, 6: 0xBC, 7: 0xB1}
chr_ = bytearray()
for t in range(256):
    src = remap.get(t & 0x7F, t & 0x7F)
    g = bytearray(f[src*8:src*8+8])
    if t >= 128: g = bytearray((~x) & 0xFF for x in g)
    chr_ += g + bytes(8)
chr_ = chr_ + chr_
open(os.path.join(out, 'chr.bin'), 'wb').write(chr_)

# --- MIDI -> event lists.  Each tune is a list of events: wait (time units since the previous
#     event), two nibbles (pulse-1 note, pulse-2 note) and a triangle note.  Notes are indexes
#     into small period tables.
def read_midi(path):
    d = open(path, 'rb').read()
    n, div = struct.unpack('>HH', d[10:14])
    p = 14; tracks = []; tempo = 500000
    def vl(b, i):
        v = 0
        while True:
            c = b[i]; i += 1; v = (v << 7) | (c & 0x7f)
            if not c & 0x80: return v, i
    for t in range(n):
        L = struct.unpack('>I', d[p+4:p+8])[0]; b = d[p+8:p+8+L]; p += 8+L
        i = tm = rs = 0; ons = []
        while i < len(b):
            dt, i = vl(b, i); tm += dt; s_ = b[i]
            if s_ == 0xff:
                l, j = vl(b, i+2)
                if b[i+1] == 0x51: tempo = int.from_bytes(b[j:j+3], 'big')
                i = j+l; continue
            if s_ & 0x80: rs = s_; i += 1
            if rs & 0xf0 in (0xc0, 0xd0): i += 1; continue
            if rs & 0xf0 == 0x90 and b[i+1]: ons.append((tm, b[i]))
            i += 2
        tracks.append(ons)
    return tracks, div, tempo

def clampp(k):
    while k < 33: k += 12
    return k
def per_p(k): return round(1789773 / (16 * 440 * 2 ** ((clampp(k) - 69) / 12))) - 1
def per_t(k): return round(1789773 / (32 * 440 * 2 ** ((k - 69) / 12))) - 1

def emit_tune(prefix, voices, div, tempo, unit_div, bars, o):
    """voices = {1: [(tick,note)..] for pulse 1, 2: ... pulse 2, 3: ... triangle}; a voice entry
    may be a list of alternatives (first one that has a note at that time wins)."""
    UNIT = div // unit_div
    loop_units = bars * 4 * unit_div
    # group by quantised time
    slots = {}
    for v, srcs in voices.items():
        for prio, src in enumerate(srcs):
            for tm, k in src:
                q = round(tm / UNIT)
                if 0 <= q < loop_units:
                    cur = slots.setdefault(q, {}).setdefault(v, {})
                    cur.setdefault(prio, []).append(k)
    used = set(); recs = []
    for q in sorted(slots):
        row = [0, 0, 0]
        for v, byprio in slots[q].items():
            prio = min(byprio)
            ns = byprio[prio]
            row[v-1] = max(ns) if v != 3 else min(ns)
        recs.append((q, row)); used.update(k for k in row if k)
    notes = sorted(used)
    assert len(notes) <= 15, (prefix, len(notes))
    idx = lambda k: notes.index(k) + 1 if k else 0
    ms_per_unit = tempo / 1000.0 * UNIT / div
    rate = round(256 * ms_per_unit ** -1 * (1000.0 / 60.0988) )
    D, AB, C = [], [], []
    last = 0
    def pad(gap):
        while gap > 255:
            D.append(255); AB.append(0); C.append(0); gap -= 255
        return gap
    for q, (a, b, c) in recs:
        gap = pad(q - last)
        D.append(gap); AB.append(idx(a) << 4 | idx(b)); C.append(idx(c)); last = q
    gap = pad(loop_units - last)
    D.append(gap); AB.append(0); C.append(0)
    o.write('#define %s_EVENTS %d\n' % (prefix, len(D)))
    o.write('#define %s_RATE %d   /* time units per frame, 8.8 fixed point */\n' % (prefix, rate))
    o.write('static const unsigned short %s_pp[] = {0, %s};\n' % (prefix.lower(), ', '.join(str(per_p(k)) for k in notes)))
    o.write('static const unsigned short %s_tp[] = {0, %s};\n' % (prefix.lower(), ', '.join(str(per_t(k)) for k in notes)))
    for nm, arr in (('d', D), ('ab', AB), ('c', C)):
        o.write('static const unsigned char %s_%s[%s_EVENTS] = {%s};\n' % (prefix.lower(), nm, prefix, ','.join(map(str, arr))))
    print(prefix, 'events', len(D), 'notes', len(notes), 'rate', rate)

def split_piano(tr, start, split=57):
    """piano riff: highest two notes of the chord -> pulse 1 / pulse 2, a low note -> triangle"""
    groups = {}
    for tm, k in tr: groups.setdefault(tm, []).append(k)
    v1, v2, v3 = [], [], []
    for tm in sorted(groups):
        ns = sorted(set(groups[tm]), reverse=True)
        bass = ns[-1] if ns[-1] < split else 0
        rest = [k for k in ns if k != bass]
        if rest: v1.append((tm - start, rest[0]))
        if len(rest) > 1: v2.append((tm - start, rest[1]))
        if bass: v3.append((tm - start, bass))
    return v1, v2, v3

with open(os.path.join(out, 'music.h'), 'w') as o:
    o.write('/* generated by tools/mkdata.py from tools/title.mid and tools/game.mid - do not edit */\n')
    tr, div, tempo = read_midi(os.path.join(here, 'title.mid'))
    start = min(t for t, _ in tr[0]) // (div // 16) * (div // 16)
    v1, v2, v3 = split_piano(tr[0], start)
    emit_tune('TITLE', {1: [v1], 2: [v2], 3: [v3]}, div, tempo, 16, 16, o)
    # in-game tune: lead square (else the guitar riff) on pulse 1, bass guitar on the triangle
    tr, div, tempo = read_midi(os.path.join(here, 'game.mid'))
    emit_tune('GAME', {1: [tr[1], tr[0]], 3: [tr[3]]}, div, tempo, 8, GAME_BARS, o)

# --- game tables
import math
with open(os.path.join(out, 'tables.h'), 'w') as o:
    o.write('/* generated by tools/mkdata.py - do not edit */\n')
    # trenchcoat: floor(capacity^3.34 * 0.0634) for capacity 10,20..300
    o.write('static const unsigned long coat_base[30] = {%s};\n' % ','.join('%dUL' % int(math.pow(c*10, 3.34)*0.0634) for c in range(1, 31)))
    # score part 2: days^-2.12 * 104e9, capped at 500000 (index = days, 0 unused)
    p2 = [min(500000, int(math.pow(d, -2.12)*104000000000.0)) for d in range(1, 701)]
    first = next(i for i, v in enumerate(p2) if v < 500000)     # days up to 'first' are all capped
    o.write('#define SCORE_DAYS_FIRST %d\n' % (first + 1))
    o.write('static const unsigned long score_days[%d] = {%s};\n' % (700 - first, ','.join('%dUL' % v for v in p2[first:])))
    # score part 3: pct^8.14 * 2.48e-11, capped at 500000 (index = percent of good deals)
    p3 = [min(500000, int(math.pow(d, 8.14)*2.48e-11)) for d in range(0, 101)]
    o.write('static const unsigned long score_deals[101] = {%s};\n' % ','.join('%dUL' % v for v in p3))
    # score part 1: floor(2^(total/10e6)) for total >= 10e6*log2(k): thresholds for k = 2..10
    o.write('static const unsigned long score_wealth[9] = {%s};\n' % ','.join('%dUL' % math.ceil(1e7*math.log2(k)) for k in range(2, 11)))
