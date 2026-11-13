#!/usr/bin/env python3
"""Converts MIDI files into the data the game's sound player (seq.c) plays.  Every sound effect and every
piece of music written for the NES port is a MIDI file: change one in any MIDI editor, run build.sh, and the
game plays the new version.

  tools/snd/sfx_NAME.mid    a sound effect (plays once)            -> sounds.bin, SFX_NAME in sounds.h
  tools/snd/song_NAME.mid   a piece of music (loops)               -> sounds.bin, SONG_NAME in sounds.h
  tools/snd/once_NAME.mid   a piece of music that plays once       -> sounds.bin, SONG_NAME in sounds.h
  tools/easter_local.mid    the easter-egg song (your own, never committed; else easter_default.mid)
                                                                   -> easter.bin (PRG bank 6)

How a file in tools/snd is played (the NES has four voices):
  MIDI channel 1  -> pulse 1        channel 2 -> pulse 2        channel 3 -> triangle (bass; no volume)
  MIDI channel 10 -> noise (drums and noises, see DRUMS below)   every other channel is ignored
  * each channel plays one note at a time (a new note replaces the old one)
  * velocity sets the volume of a pulse note (127 = loudest) and of a noise hit
  * the instrument (program change) picks the pulse sound, see PROGRAM below
  * pitch bend works on channels 1-3 (the bend range is read from RPN 0; 2 semitones if not set)
  * all tempo changes are followed; time is cut into frames (1/60 second)
  * a marker named "loop" sets where a song restarts (default: the beginning); a song ends at the end of its
    longest track
  A sound effect always gets pulse 2 and the noise channel.  It gets pulse 1 and the triangle only while no
  music is playing, so put the part that matters on channels 2 and 10.

The easter-egg song is imported the old way, so any MIDI file works without editing: time is cut into 16th
notes at the first tempo; at each 16th the lowest note (if below middle G) goes to the triangle, the highest of
the rest to pulse 1, the next to pulse 2; a kick/snare/hat beat is added.  If it is too long it is cut at a bar.

Data format (one "blob" per sound): 6-byte header (flags: bit 0 loop; 3 spare; loop offset), then rows:
  wait      frames until the next row (0 = the end)
  mask      which fields follow, in this order:
    0x40  2 bytes: volumes (pulse 1 high nibble, pulse 2 low), instruments (same)
    0x01  pulse 1 note    (0 = off, else MIDI note, +0x80 = struck: the envelope starts again)
    0x02  pulse 2 note
    0x04  triangle note   (0 = off)
    0x08  2 bytes: noise period (0-15, +0x80 = metallic), volume << 4 | decay (0 = held)
    0x10  2 bytes: pulse 1 period (a pitch bend: replaces the note's own pitch)
    0x20  2 bytes: pulse 2 period
    0x80  2 bytes: triangle period (0 = off)
sounds.bin = a table of 16-bit offsets, one per sound, then the blobs."""
import glob, os, struct
here = os.path.dirname(os.path.abspath(__file__)); out = os.path.dirname(here)
FPS = 60.0988; CPU = 1789773.0

# instruments of the player (seq.c: the i_duty/i_rate/i_drop/i_vib tables must match)
LEAD, HARM, PLUCK, BELL, ORGAN, SOFT, XYLO, REED, LEADV, STAB, BRASS, THIN, BUZZ = range(13)
def PROGRAM(p):                            # General MIDI program (0-127) -> instrument
    if p == 80: return LEAD                # square lead: 50%, settles
    if p == 81: return LEADV               # saw lead: the same with vibrato
    if p == 82: return HARM                # calliope lead: 25%, settles (a second voice)
    if p == 83: return THIN                # chiff lead: thin (12.5%), settles
    if p == 87: return BUZZ                # bass + lead: thin and steady (a buzzer)
    if p in (12, 13): return XYLO          # marimba, xylophone: a very short tap
    if 8 <= p <= 15: return BELL           # bells, glockenspiel: rings and dies away
    if p <= 7: return STAB                 # pianos: a chord stab, dies away
    if 16 <= p <= 23: return ORGAN         # organs: steady, 50%
    if 24 <= p <= 31: return PLUCK         # guitars: a quick pluck
    if 32 <= p <= 39: return ORGAN         # basses
    if 40 <= p <= 55: return SOFT          # strings, voices: steady, 25%
    if 56 <= p <= 63: return BRASS         # brass: bright attack, 50%
    if 64 <= p <= 71: return REED          # reeds: steady, thin (12.5%)
    if 72 <= p <= 79: return SOFT          # flutes
    return HARM
# DRUMS: channel 10 note -> (noise period 0 = hiss .. 15 = rumble, metallic, volume at velocity 127, decay)
#   decay: 0 = held until the note ends; 1-7 = that much volume lost each frame; 8-15 = one step lost every
#   2-9 frames (long tails)
KIT = {35: (12, 0, 10, 4), 36: (12, 0, 10, 4),                       # kick
       37: (4, 0, 8, 4), 38: (6, 0, 12, 2), 40: (6, 0, 12, 2), 39: (5, 0, 11, 3),   # rim, snare, clap
       42: (1, 1, 4, 2), 44: (1, 1, 3, 3), 46: (1, 1, 6, 1),         # hi-hats: closed, pedal, open
       41: (13, 0, 10, 2), 43: (12, 0, 10, 2), 45: (11, 0, 10, 2), 47: (10, 0, 10, 2), 48: (9, 0, 10, 2), 50: (8, 0, 10, 2),   # toms
       49: (3, 0, 12, 8), 57: (3, 0, 12, 8), 51: (2, 1, 5, 1)}       # crash, ride
#   notes 60-75: plain noise, period = note - 60, held          76-91: metallic noise, period = note - 76, held
#   notes 92-107: plain noise, period = note - 92, fading (1 a frame)
#   notes 108-123: plain noise, period = note - 108, a long tail (one step every 4 frames)
def drum(note, vel):
    v = max(1, round(15 * vel / 127))
    if note in KIT: p, m, v, d = KIT[note]; v = max(1, round(v * vel / 127))
    elif 60 <= note <= 75: p, m, d = note - 60, 0, 0
    elif 76 <= note <= 91: p, m, d = note - 76, 1, 0
    elif 92 <= note <= 107: p, m, d = note - 92, 0, 1
    elif 108 <= note <= 123: p, m, d = note - 108, 0, 10
    else: return None
    return (p | (0x80 if m else 0), (v << 4) | d, d == 0)

def pulse_per(note): return max(8, min(2047, round(CPU / (16 * 440 * 2 ** ((note - 69) / 12))) - 1))
def tri_per(note): return max(2, min(2047, round(CPU / (32 * 440 * 2 ** ((note - 69) / 12))) - 1))

def vl(b, i):
    v = 0
    while True:
        c = b[i]; i += 1; v = (v << 7) | (c & 0x7f)
        if not c & 0x80: return v, i
def read_midi(path):
    """-> (division, events sorted by time): (tick, 'tempo', us) (tick, 'on', ch, note, vel) (tick, 'off', ch, note)
    (tick, 'prog', ch, p) (tick, 'bend', ch, -8192..8191) (tick, 'range', ch, semitones) (tick, 'marker', text)
    (tick, 'end')"""
    d = open(path, 'rb').read()
    ntr, div = struct.unpack('>HH', d[10:14])
    p = 14; ev = []; order = 0
    for t in range(ntr):
        L = struct.unpack('>I', d[p + 4:p + 8])[0]; b = d[p + 8:p + 8 + L]; p += 8 + L
        i = tm = rs = 0; rpn = {}
        while i < len(b):
            dt, i = vl(b, i); tm += dt; s = b[i]; order += 1
            if s == 0xff:
                l, j = vl(b, i + 2); typ = b[i + 1]; body = b[j:j + l]
                if typ == 0x51: ev.append((tm, order, 'tempo', int.from_bytes(body, 'big')))
                elif typ == 0x06: ev.append((tm, order, 'marker', body.decode('latin1').strip().lower()))
                elif typ == 0x2f: ev.append((tm, order, 'end'))
                i = j + l; continue
            if s in (0xf0, 0xf7): l, j = vl(b, i + 1); i = j + l; continue
            if s & 0x80: rs = s; i += 1
            ty, ch = rs & 0xf0, rs & 15
            if ty in (0xc0, 0xd0):
                if ty == 0xc0: ev.append((tm, order, 'prog', ch, b[i]))
                i += 1; continue
            a, v = b[i], b[i + 1]; i += 2
            if ty == 0x90 and v: ev.append((tm, order, 'on', ch, a, v))
            elif ty in (0x80, 0x90): ev.append((tm, order, 'off', ch, a))
            elif ty == 0xe0: ev.append((tm, order, 'bend', ch, ((v << 7) | a) - 8192))
            elif ty == 0xb0:
                if a == 101: rpn[ch] = (v, rpn.get(ch, (0, 127))[1])
                elif a == 100: rpn[ch] = (rpn.get(ch, (127, 0))[0], v)
                elif a == 6 and rpn.get(ch) == (0, 0): ev.append((tm, order, 'range', ch, v))
    ev.sort(key=lambda e: (e[0], e[1]))
    return div, [(e[0],) + e[2:] for e in ev]

def frames_of(div, ev):
    """tick -> frame number, following the tempo changes"""
    pts = [(0, 0.0, 500000)]
    for e in ev:
        if e[1] == 'tempo':
            t0, s0, us = pts[-1]
            pts.append((e[0], s0 + (e[0] - t0) * us / div / 1e6, e[2]))
    def f(tick):
        for t0, s0, us in reversed(pts):
            if tick >= t0: return int(round((s0 + (tick - t0) * us / div / 1e6) * FPS))
    return f

class Song:
    """what changes at each frame, then the rows"""
    def __init__(self, nframes):
        self.n = max(1, nframes)
        self.ev = {}                       # frame -> {field: value}
    def set(self, f, key, val):
        if 0 <= f < self.n: self.ev.setdefault(f, {})[key] = val
    def blob(self, loop, loop_frame=0):
        self.ev.setdefault(0, {})
        if loop:                           # the row a loop restarts at states the volumes and instruments again
            self.ev.setdefault(loop_frame, {})
            vi = None
            for f in sorted(self.ev):
                if f <= loop_frame and 'vi' in self.ev[f]: vi = self.ev[f]['vi']
            if vi: self.ev[loop_frame]['vi'] = vi
        fr = sorted(self.ev); rows = b''; loop_off = 0; cur = None
        for n, f in enumerate(fr):
            e = self.ev[f]
            at_loop = loop and f == loop_frame
            if at_loop: loop_off = len(rows)
            mask = 0; body = b''
            if 'vi' in e and (e['vi'] != cur or at_loop): mask |= 0x40; body += bytes(e['vi']); cur = e['vi']
            if 'p1' in e: mask |= 0x01; body += bytes([e['p1']])
            if 'p2' in e: mask |= 0x02; body += bytes([e['p2']])
            if 'tri' in e: mask |= 0x04; body += bytes([e['tri']])
            if 'noi' in e: mask |= 0x08; body += bytes(e['noi'])
            if 'r1' in e: mask |= 0x10; body += struct.pack('<H', e['r1'])
            if 'r2' in e: mask |= 0x20; body += struct.pack('<H', e['r2'])
            if 'rt' in e: mask |= 0x80; body += struct.pack('<H', e['rt'])
            gap = max(1, (fr[n + 1] if n + 1 < len(fr) else self.n) - f); first = True
            while gap:                     # a wait longer than 255 frames becomes extra empty rows
                w = min(gap, 255); gap -= w
                rows += bytes([w, mask if first else 0]) + (body if first else b''); first = False
        rows += b'\0'
        return struct.pack('<BBBBH', 1 if loop else 0, 0, 0, 0, loop_off) + rows

def explicit(path, loop):
    """a file from tools/snd: channels 1, 2, 3 and 10 are the four NES voices"""
    div, ev = read_midi(path)
    fof = frames_of(div, ev)
    n = fof(max((e[0] for e in ev), default=0))
    loop_frame = 0
    for e in ev:
        if e[1] == 'marker' and e[2] == 'loop': loop_frame = fof(e[0])
    notes = {0: [], 1: [], 2: []}; open_ = {}          # per channel: (on frame, off frame, note, velocity, cut)
    prog = {}; bends = {0: [], 1: [], 2: []}; brange = {}; drums = []; dopen = {}
    for e in ev:
        k = e[1]; f = fof(e[0])
        if k == 'prog' and e[2] in (0, 1): prog.setdefault(e[2], []).append((f, PROGRAM(e[3])))
        elif k == 'range': brange[e[2]] = e[3]
        elif k == 'bend' and e[2] in bends: bends[e[2]].append((f, e[3]))
        elif k == 'on':
            ch = e[2]
            if ch in notes:
                if ch in open_: o = open_.pop(ch); notes[ch].append((o[0], max(f, o[0] + 1), o[1], o[2], True))   # cut by the new note
                open_[ch] = (f, e[3], e[4])
            elif ch == 9:
                dr = drum(e[3], e[4])
                if dr: drums.append((f, dr)); dopen[e[3]] = (f, dr)
        elif k == 'off':
            ch = e[2]
            if ch in notes and ch in open_ and open_[ch][1] == e[3]:
                o = open_.pop(ch); notes[ch].append((o[0], max(f, o[0] + 1), o[1], o[2], False))
            elif ch == 9 and e[3] in dopen:
                f0, dr = dopen.pop(e[3])
                if dr[2]: drums.append((max(f, f0 + 1), (dr[0], 0, True, 'off')))
    for ch, o in open_.items(): notes[ch].append((o[0], max(n, o[0] + 1), o[1], o[2], False))
    n = max([n] + [x[1] for c in notes.values() for x in c] + [d[0] + 1 for d in drums])
    S = Song(n)
    def bend_at(ch, f):
        v = 0
        for bf, bv in bends[ch]:
            if bf <= f: v = bv
            else: break
        return v * brange.get(ch, 2) / 8192.0
    def inst_at(ch, f):
        v = LEAD if ch == 0 else HARM
        for pf, pv in prog.get(ch, []):
            if pf <= f: v = pv
        return v
    peak = [12, 9]; inst = [inst_at(0, 0), inst_at(1, 0)]
    S.set(0, 'vi', (peak[0] << 4 | peak[1], inst[0] << 4 | inst[1]))
    for on, ch, (_, off, note, vel, cut) in sorted((x[0], ch, x) for ch in notes for x in notes[ch]):
        if ch < 2:
            peak[ch] = max(1, round(vel * 15 / 127)); inst[ch] = inst_at(ch, on)
            S.set(on, 'vi', (peak[0] << 4 | peak[1], inst[0] << 4 | inst[1]))
            S.set(on, 'p1' if ch == 0 else 'p2', note | 0x80)
            if not cut and off < n: S.ev.setdefault(off, {}).setdefault('p1' if ch == 0 else 'p2', 0)
            if bends[ch]:
                last = pulse_per(note)
                for f in range(on, min(off, n)):
                    p = pulse_per(note + bend_at(ch, f))
                    if p != last: S.set(f, 'r1' if ch == 0 else 'r2', p); last = p
        else:
            S.set(on, 'tri', note)
            if not cut and off < n: S.ev.setdefault(off, {}).setdefault('tri', 0)
            if bends[ch]:
                last = tri_per(note)
                for f in range(on, min(off, n)):
                    p = tri_per(note + bend_at(ch, f))
                    if p != last: S.set(f, 'rt', p); last = p
    for f, dr in sorted(drums, key=lambda d: (d[0], len(d[1]))):
        if len(dr) == 4:                                   # the end of a held noise, unless something new starts there
            if f < n and 'noi' not in S.ev.get(f, {}): S.set(f, 'noi', (dr[0], 0))
        else: S.set(f, 'noi', dr[:2])
    return S.blob(loop, loop_frame), n

def legacy(path, maxbytes):
    """the easter-egg song: any MIDI file, voices picked automatically (see the top of this file)"""
    div, ev = read_midi(path)
    tempo = next((e[2] for e in ev if e[1] == 'tempo'), 500000)
    notes = []; held = {}
    for e in ev:
        if e[1] == 'on' and e[2] != 9: held.setdefault((e[2], e[3]), []).append(e[0])
        elif e[1] == 'off' and e[2] != 9 and held.get((e[2], e[3])): notes.append((held[(e[2], e[3])].pop(0), e[0], e[3]))
    U = div / 4                                            # ticks per 16th
    end = max(off for _, off, _ in notes); units = int(-(-end // U)); units = -(-units // 16) * 16
    fp16 = FPS * tempo / 1e6 / 4                           # frames per 16th
    def make(units):
        ons = {}; sound = [[] for _ in range(units)]
        for on, off, k in notes:
            a = int(round(on / U)); z = max(a + 1, int(round(off / U)))
            ons.setdefault(a, set()).add(k)
            for q in range(a, min(z, units)): sound[q].append(k)
        S = Song(int(round(units * fp16)))
        S.set(0, 'vi', (12 << 4 | 9, LEAD << 4 | HARM))    # the tune 50% duty, the harmony 25% and quieter
        last = (0, 0, 0)
        for q in range(units):
            f = int(round(q * fp16))
            ns = sorted(set(sound[q]), reverse=True)
            tri = ns[-1] if ns and ns[-1] < 55 else 0
            rest = [k for k in ns if k != tri]
            p1 = rest[0] if rest else 0; p2 = rest[1] if len(rest) > 1 else 0
            while p1 and p1 < 45: p1 += 12                 # the pulses cannot go that low
            while p2 and p2 < 45: p2 += 12
            hit = ons.get(q, set())
            s1 = p1 and (p1 in hit or p1 - 12 in hit or p1 - 24 in hit or p1 != last[0])
            s2 = p2 and (p2 in hit or p2 - 12 in hit or p2 - 24 in hit or p2 != last[1])
            if p1 != last[0] or s1: S.set(f, 'p1', p1 | (0x80 if s1 else 0))
            if p2 != last[1] or s2: S.set(f, 'p2', p2 | (0x80 if s2 else 0))
            if tri != last[2]: S.set(f, 'tri', tri)
            last = (p1, p2, tri)
            d = q & 15                                     # the added beat: kick, snare, hats
            if d in (0, 8, 10): S.set(f, 'noi', (12, 10 << 4 | 4))
            elif d in (4, 12): S.set(f, 'noi', (6, 12 << 4 | 2))
            elif not d & 1: S.set(f, 'noi', (0x81, 4 << 4 | 2))
        return S.blob(True)
    blob = make(units); full = units
    while len(blob) > maxbytes and units > 16: units -= 16; blob = make(units)
    if units < full: print('mid2nes: the song is too long for the ROM; it is cut after %d bars' % (units // 16))
    return blob, units // 16

if __name__ == '__main__':
    # ---- the easter-egg song: it has PRG bank 6 to itself
    src = os.path.join(here, 'easter_local.mid')
    if not os.path.exists(src): src = os.path.join(here, 'easter_default.mid')
    blob, bars = legacy(src, 16000)
    open(os.path.join(out, 'easter.bin'), 'wb').write(blob)
    print('mid2nes: %s -> easter.bin, %d bars, %d bytes' % (os.path.basename(src), bars, len(blob)))
    # ---- the sound effects and the music: the MIDI files in tools/snd in name order, the effects first
    g = lambda pat: glob.glob(os.path.join(here, 'snd', pat))
    files = sorted(g('sfx_*.mid')) + sorted(g('song_*.mid') + g('once_*.mid'), key=lambda p: os.path.basename(p).split('_', 1)[1])
    blobs = []; names = []; nsfx = 0
    for p in files:
        kind, name = os.path.basename(p)[:-4].split('_', 1)
        b, n = explicit(p, kind == 'song')
        blobs.append(b); names.append(('SFX_' if kind == 'sfx' else 'SONG_') + name.upper())
        nsfx += kind == 'sfx'
    off = 2 * len(blobs); table = b''
    for b in blobs: table += struct.pack('<H', off); off += len(b)
    data = table + b''.join(blobs)
    open(os.path.join(out, 'sounds.bin'), 'wb').write(data)
    h = ['/* generated by tools/mid2nes.py from the MIDI files in tools/snd - do not edit */']
    for i, nm in enumerate(names): h.append('#define %s %d' % (nm, i))
    h.append('#define SND_SFX %d      /* the sound effects are 0 .. SND_SFX-1; the music follows */' % nsfx)
    h.append('#define SND_ALL %d' % len(names))
    h.append('#ifdef SND_NAMES')
    h.append('static const char* const snd_names[SND_ALL] = {' + ', '.join('"%s"' % nm.split('_', 1)[1].lower() for nm in names) + '};')
    h.append('#endif')
    h.append('#ifdef SND_TABLE   /* the pulse periods of MIDI notes 0-127 */')
    h.append('static const u16 note_per_t[128] = {' + ', '.join(str(min(2047, round(CPU / (16 * 440 * 2 ** ((k - 69) / 12))) - 1)) for k in range(128)) + '};')
    h.append('#endif')
    open(os.path.join(out, 'sounds.h'), 'w').write('\n'.join(h) + '\n')
    print('mid2nes: tools/snd -> sounds.bin, %d effects, %d pieces of music, %d bytes' % (nsfx, len(names) - nsfx, len(data)))
