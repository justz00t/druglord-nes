#!/usr/bin/env python3
"""Writes the MIDI files in tools/snd: every sound effect and the in-game music of the NES port, all original
(the death march is Chopin's, 1839, public domain).  build.sh does NOT run this: the .mid files are the
source, so you can edit or replace any of them in a MIDI editor and just rebuild.  Run this script only to get
the originals back (it overwrites the files it knows).

Sound effects are written with one beat = 60 frames (tempo 60.1 BPM), so times below are in frames (1/60 s).
See tools/mid2nes.py for how the MIDI channels, instruments and drum notes map to the NES voices."""
import math, os, struct
here = os.path.dirname(os.path.abspath(__file__)); snd = os.path.join(here, 'snd')
os.makedirs(snd, exist_ok=True)
CPU = 1789773.0
NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
def N(s):                                   # 'C#4' -> MIDI note (C4 = 60)
    k = NAMES[s[0]]; i = 1
    while s[i] in '#b': k += 1 if s[i] == '#' else -1; i += 1
    return k + 12 * (int(s[i:]) + 1)
def V(vol): return max(1, min(127, int(round(vol * 127 / 15.0))))   # NES volume 1-15 -> velocity
def hz2n(f): return 69 + 12 * math.log2(f / 440.0)

class Mid:
    def __init__(self, us=998356, ppq=480):
        self.us = us; self.ppq = ppq; self.tr = {}; self.meta = [(0, b'\xff\x51\x03' + us.to_bytes(3, 'big'))]
        self.end = 0
    def T(self, beats): return int(round(beats * self.ppq))
    def ev(self, ch, t, data, order=1):
        self.tr.setdefault(ch, []).append((self.T(t), order, data)); self.end = max(self.end, self.T(t))
    def prog(self, ch, p, t=0): self.ev(ch, t, bytes([0xC0 | (ch - 1), p]), 0)
    def note(self, ch, t, d, n, v=100):
        self.ev(ch, t, bytes([0x90 | (ch - 1), n, v]), 2); self.ev(ch, t + d, bytes([0x80 | (ch - 1), n, 0]), 0)
    def brange(self, ch, semis):
        c = 0xB0 | (ch - 1)
        self.ev(ch, 0, bytes([c, 101, 0, c, 100, 0, c, 6, semis]), 0)
    def bend(self, ch, t, semis, rng):
        v = max(-8192, min(8191, int(round(semis / rng * 8192)))) + 8192
        self.ev(ch, t, bytes([0xE0 | (ch - 1), v & 127, v >> 7]), 1)
    def marker(self, t, text): self.meta.append((self.T(t), b'\xff\x06' + bytes([len(text)]) + text.encode()))
    def save(self, name, length=None):
        end = self.T(length) if length is not None else self.end
        def track(evs):
            out = b''; last = 0
            for t, _, d in sorted(evs, key=lambda e: (e[0], e[1])):
                # one event may hold several messages of the same status byte group (the RPN set-up): split them
                msgs = [d] if d[0] != 0xB0 | (d[0] & 15) or len(d) == 3 else [d[i:i + 3] for i in range(0, len(d), 3)]
                for m in msgs: out += vlq(t - last) + m; last = t
            out += vlq(max(0, end - last)) + b'\xff\x2f\x00'
            return b'MTrk' + struct.pack('>I', len(out)) + out
        def vlq(n):
            b = [n & 127]; n >>= 7
            while n: b.append(0x80 | (n & 127)); n >>= 7
            return bytes(reversed(b))
        trs = [track([(t, 0, d) for t, d in self.meta])] + [track(self.tr[c]) for c in sorted(self.tr)]
        open(os.path.join(snd, name + '.mid'), 'wb').write(b'MThd' + struct.pack('>IHHH', 6, 1, len(trs), self.ppq) + b''.join(trs))

# ---- helpers for sound effects (times in frames) ----
F = 1 / 60.0
class Sfx(Mid):
    def n(self, ch, f, d, note, vol=12): self.note(ch, f * F, d * F, N(note) if isinstance(note, str) else note, V(vol))
    def nz(self, f, per, vol, d=1, metal=False): self.note(10, f * F, d * F, (76 if metal else 60) + per, V(vol))   # held noise
    def nzfade(self, f, per, vol): self.note(10, f * F, vol * F, 92 + per, V(vol))          # loses 1 a frame
    def nztail(self, f, per, vol): self.note(10, f * F, vol * 4 * F, 108 + per, V(vol))     # loses 1 every 4 frames
    def sweep(self, ch, f, freqs, vol=12):
        """one note whose pitch follows freqs (Hz, one value a frame), by pitch bend"""
        ns = [hz2n(x) for x in freqs]; base = int(round((max(ns) + min(ns)) / 2)); rng = 12
        while max(abs(x - base) for x in ns) > rng - 0.01: rng += 12
        if not getattr(self, '_r', {}).get(ch) == rng:
            self._r = getattr(self, '_r', {}); assert ch not in self._r, 'one bend range per channel'
            self._r[ch] = rng; self.brange(ch, rng)
        for i, x in enumerate(ns): self.bend(ch, (f + i) * F, x - base, rng)
        self.note(ch, f * F, len(ns) * F, base, V(vol))
        self.bend(ch, (f + len(ns)) * F, 0, rng)
def geo(a, b, n): return [a * (b / a) ** (i / max(1, n - 1)) for i in range(n)]
def lin(a, b, n): return [a + (b - a) * i / max(1, n - 1) for i in range(n)]

# General MIDI programs used (see PROGRAM in mid2nes.py)
PIANO, XYLO, BELL, ORGAN, GUITAR, STRINGS, BRASS, REED, SQUARE, SAW, CALLIOPE, CHIFF = 0, 13, 9, 16, 24, 48, 56, 64, 80, 81, 82, 83

# ======================================================================== sound effects
s = Sfx(); s.prog(2, GUITAR)                               # the menu cursor moves: a soft tick
s.n(2, 0, 2, 'C7', 5); s.nz(0, 1, 3, 1, True)
s.save('sfx_tick')

s = Sfx(); s.prog(2, GUITAR)                               # a menu choice: "bi-ding"
s.n(2, 0, 3, 'G6', 11); s.n(2, 3, 5, 'C7', 11)
s.save('sfx_pick')

s = Sfx(); s.prog(2, REED)                                 # no can do: a double buzzer, "bzz-bzzzt"
for a, b, v in ((0, 7, 12), (10, 23, 12)):
    for f in range(a, b): s.n(2, f, 1, 'A2' if f & 1 else 'G#2', v if f < 17 else v - (f - 16))
    s.nz(a, 13, 3, b - a)
s.save('sfx_denied')

s = Sfx(); s.prog(2, BELL)                                 # a deal: the cash register, "ka-ching"
s.nz(0, 3, 10); s.nz(1, 5, 7); s.nz(2, 3, 4)               # the drawer
s.nz(4, 2, 6, 1, True)
for i in range(12): s.n(2, 4 + i * 2, 2, 'E7' if i & 1 else 'B6', 13 - i)   # the bell, shimmering away
s.save('sfx_deal')

# the police siren: one slow wail (500 -> 1300 Hz and back) then four fast yelps, on both pulses, pulse 1 a
# touch flat so the two beat against each other (the sweep the game has always had, now as pitch bend)
siren_t = [224,223,223,222,220,218,215,213,209,206,202,198,193,189,184,179,175,170,165,160,155,150,146,141,137,132,128,124,120,117,113,110,107,105,102,100,97,95,94,92,91,89,88,88,87,86,86,86]
idx = list(range(48)) + list(range(47, -1, -1))
for c in range(4): idx += list(range(0, 48, 4)) + list(range(44, -1, -4))
s = Sfx(); s.prog(2, ORGAN); s.prog(1, ORGAN)
s.sweep(2, 0, [CPU / (16 * (siren_t[i] + 1)) for i in idx], 11)
s.sweep(1, 0, [CPU / (16 * (siren_t[i] + 4)) for i in idx], 7)
s.save('sfx_siren')

s = Sfx(); s.prog(2, GUITAR)                               # a tip from the street: "psst!" and a sneaky little run
s.nzfade(0, 0, 5); s.nzfade(7, 0, 7)
for i, k in enumerate(('E5', 'G5', 'A5', 'B5')): s.n(2, 16 + i * 4, 4, k, 10)
s.n(2, 32, 12, 'D6', 11)
s.save('sfx_tip')

# EXTRA! EXTRA!: the news-wire xylophone - the same tap-tap-taaap figure on one note, all the way through
s = Sfx(); s.prog(2, XYLO); f = 0
for i in range(9):
    for gap in (5, 5, 10): s.n(2, f, 4, 'E6', 13); s.nz(f, 2, 4, 1, True); f += gap
s.save('sfx_extra')

s = Sfx(); s.prog(2, ORGAN)                                # a bullet lands: a thud
for i, v in enumerate((12, 10, 8, 6, 4, 2)): s.nz(i, 6, v)
s.sweep(2, 0, geo(300, 60, 8), 14)
s.nzfade(8, 12, 5)
s.save('sfx_hit')

s = Sfx(); s.prog(2, CHIFF)                                # the jet: engines spool up, the whine climbs, and it is gone
for i in range(15): s.nz(i * 4, 15 - (i * 11) // 14, 3 + i // 2, 4)                # rumble rising to a roar
s.sweep(2, 16, geo(180, 1000, 44) + geo(1000, 1500, 12) + geo(1500, 650, 30), 6)   # the whine, then the fly-past
for i in range(10): s.nz(60 + i * 4, 4 + i, 10 - i, 4)                             # the roar fades into the distance
s.save('sfx_jet')

def bang(s, f, crack, boom, thump, tail=0):
    """one gunshot: a crack (short bright noise) over a boom (noise rumble); thump adds a low pulse thud"""
    for i in range(boom): s.nz(f + i, 4 if i < crack else 10, max(1, 15 - 15 * i // boom))
    if thump: s.sweep(2, f, [CPU / (16 * (800 + i * 20)) for i in range(10)], 15)
    if tail: s.nztail(f + boom, 13, tail)
    return f + boom
s = Sfx(); bang(s, 0, 2, 5, 0); s.save('sfx_gun1')                         # Saturday Night Special: a pop
s = Sfx(); bang(s, 0, 3, 10, 0, 2); s.save('sfx_gun2')                     # Ruger: a crack
s = Sfx(); s.prog(2, ORGAN); bang(s, 0, 3, 18, 1, 3); s.save('sfx_gun3')   # Auto Mag: a boom that rolls away
s = Sfx()                                                                  # Uzi: a burst
for i in range(5): bang(s, i * 4, 1, 3, 0)
s.save('sfx_gun4')
s = Sfx(); s.prog(2, ORGAN)                                                # AK-47: three heavy rounds
for i in range(3):
    for j in range(6): s.nz(i * 8 + j, 4 if j < 2 else 10, 15 - 15 * j // 6)
s.sweep(2, 0, [CPU / (16 * (800 + (i % 8) * 28)) for i in range(22)], 15)
s.nztail(24, 13, 3)
s.save('sfx_gun5')

s = Sfx(); s.prog(2, ORGAN)                                # the baseball bat: crack, then the thud
s.nz(0, 2, 15); s.nz(1, 3, 12)
for i, v in enumerate((12, 10, 8, 6, 5, 4, 3, 2)): s.nz(2 + i, 9, v)
s.sweep(2, 0, geo(220, 70, 8), 13)
s.save('sfx_bat')

s = Sfx(); s.prog(2, ORGAN)                                # a fist (no gun in hand)
for i, v in enumerate((14, 11, 8, 5, 2)): s.nz(i, 8, v)
s.sweep(2, 0, geo(200, 80, 6), 12)
s.save('sfx_punch')

for L in range(4):                                         # the win scene: a coin for each stack of money, higher with each layer
    s = Sfx(); s.prog(2, GUITAR)
    s.n(2, 0, 1, ('B5', 'F#6', 'B6', 'D#7')[L], 11); s.n(2, 1, 2, ('F#7', 'A7', 'B7', 'C#8')[L], 11)
    s.save('sfx_stack%d' % L)

s = Sfx(); s.prog(2, BELL)                                 # the bank: the bell on the counter
s.nz(0, 1, 5, 1, True); s.n(2, 0, 18, 'E7', 13)
s.save('sfx_bank')
for name, ks in (('sfx_coins_in', ('B6', 'G6', 'E6')), ('sfx_coins_out', ('E6', 'G6', 'B6'))):   # money in / money out
    s = Sfx(); s.prog(2, GUITAR)
    for i, k in enumerate(ks): s.n(2, i * 5, 5 if i < 2 else 9, k, 12); s.nz(i * 5, 2, 4, 1, True)
    s.save(name)

s = Sfx(); s.prog(2, ORGAN)                                # the loan shark's office: Guido's three notes
s.n(2, 0, 6, 'D3', 11); s.n(2, 9, 6, 'D3', 11); s.n(2, 18, 20, 'G#3', 12)
s.nz(18, 14, 3, 20)
s.save('sfx_shark')

s = Sfx(); s.prog(2, STRINGS)                              # the hospital: a heart monitor
s.n(2, 0, 5, 'A5', 9); s.n(2, 24, 5, 'A5', 9)
s.save('sfx_hospital')

s = Sfx(); s.prog(2, GUITAR)                               # patched up: a rising sparkle
for i, k in enumerate(('C6', 'E6', 'G6', 'C7', 'E7', 'G7')): s.n(2, i * 3, 3, k, 11)
s.prog(2, BELL, 18 * F); s.n(2, 18, 14, 'C7', 12)
s.save('sfx_heal')

s = Sfx(); s.prog(2, ORGAN)                                # the factory blows up
s.nz(0, 3, 15); s.nz(1, 6, 15); s.nz(2, 10, 15)
s.nztail(3, 14, 15)
s.sweep(2, 0, geo(110, 38, 24), 13)
s.save('sfx_boom')

s = Sfx(); s.prog(2, GUITAR)                               # cheap stuff on the street: a happy little run
for i, k in enumerate(('G5', 'B5', 'D6', 'G6', 'D6')): s.n(2, i * 3, 3, k, 11)
s.n(2, 15, 10, 'G6', 12)
s.save('sfx_cheap')

s = Sfx(); s.prog(2, STRINGS)                              # the brownies: a mellow, woozy "wah wah wah waaah"
for i, k in enumerate(('C5', 'Bb4', 'A4')): s.n(2, i * 10, 9, k, 10)
f0 = 440 * 2 ** ((N('G4') - 69) / 12)                      # the last note wavers, head-swimmy
s.sweep(2, 30, [f0 * 2 ** (0.3 * math.sin(i * 0.4) / 12) for i in range(32)], 10)
s.save('sfx_brownies')

s = Sfx(); s.prog(2, GUITAR)                               # mugged: your money runs off down the street
for i, k in enumerate(('E6', 'C6', 'A5', 'F5', 'D5', 'B4')): s.n(2, i * 3, 3, k, 11)
for i, v in enumerate((9, 7, 5, 3)): s.nz(18 + i, 11, v)
s.save('sfx_mugged')

s = Sfx()                                                  # running feet (the mugger bolts; you run from the cops)
for i, f in enumerate((0, 6, 11, 15, 18, 21, 23, 25)): s.nz(f, 8 if i & 1 else 10, 9 - i // 2); s.nz(f + 1, 9, 4)
for i in range(12): s.nz(27 + i, max(2, 10 - i), max(1, 6 - i // 2))
s.save('sfx_run')

s = Sfx(); s.prog(2, BELL)                                 # the new trenchcoat: zip it up
for i in range(14): s.nz(i, 12 - (i * 10) // 13, 6, 1, True)
s.n(2, 15, 12, 'G6', 12)
s.save('sfx_zip')

s = Sfx(); s.prog(2, STRINGS)                              # no deal: a shrug
s.n(2, 0, 6, 'E5', 8); s.n(2, 6, 12, 'C5', 8)
s.save('sfx_nodeal')

s = Sfx(); s.prog(2, GUITAR)                               # guns on order: "click-clack"
s.nz(0, 1, 12, 1, True); s.nz(1, 5, 8)
s.nz(8, 3, 14, 1, True); s.nz(9, 8, 10); s.nz(10, 8, 5)
s.n(2, 8, 8, 'A3', 11)
s.save('sfx_cock')

s = Sfx(); s.prog(2, ORGAN)                                # blackmail: a low tritone, shaking
s.n(2, 0, 12, 'D3', 11)
fr = [440 * 2 ** ((N('G#3') - 69 + 0.4 * math.sin(i * 1.3)) / 12) for i in range(30)]
s.sweep(2, 12, fr, 12)
s.nz(12, 15, 3, 30)
s.save('sfx_blackmail')

s = Sfx(); s.prog(2, GUITAR)                               # money gone
for i, k in enumerate(('A5', 'F5', 'D5', 'A4')): s.n(2, i * 4, 4, k, 11)
s.n(2, 16, 14, 'D4', 12)
s.save('sfx_loss')

s = Sfx(); s.prog(2, BRASS)                                # nobody pushes you around
s.n(2, 0, 4, 'E4', 12); s.n(2, 5, 4, 'E4', 12); s.n(2, 10, 4, 'B4', 12); s.n(2, 15, 18, 'E5', 13)
s.save('sfx_tough')

s = Sfx(); s.prog(2, BRASS); s.prog(1, BRASS)              # the cops are down: a short fanfare
for i, (k, d) in enumerate((('C5', 4), ('E5', 4), ('G5', 4), ('C6', 10), ('G5', 5), ('C6', 22))):
    f = (0, 4, 8, 12, 22, 27)[i]
    s.n(2, f, d, k, 13); s.n(1, f, d, N(k) - 12, 9)
s.nz(12, 3, 6, 2); s.nz(27, 3, 8, 2)
s.save('sfx_win')

# the credits (bank_b.c): the Erwin block drops in with a cartoon fall, each justz00t letter is a rain drop
s = Sfx(); s.prog(2, ORGAN)                                # the block falls: a cartoon slide whistle down
s.sweep(2, 0, geo(1800, 150, 60), 10)
s.save('sfx_credfall')

s = Sfx(); s.prog(2, SQUARE)                               # ...and lands: a boing and a thud
s.sweep(2, 0, [300 * 2 ** (7 * math.sin(i * 0.9) * math.exp(-i * 0.14) / 12) for i in range(18)], 11)
for i, v in enumerate((13, 10, 7, 4)): s.nz(i, 12, v)
s.save('sfx_credland')

s = Sfx(); s.prog(2, ORGAN)                                # a rain drop: a little high plink falling
s.sweep(2, 0, [2000, 1500, 1050, 700, 460], 8)
s.save('sfx_drop')

# ======================================================================== music
class Tune(Mid):
    def __init__(self, bpm): Mid.__init__(self, int(round(60e6 / bpm)))
    def line(self, ch, bar, toks, vel, unit=0.5, gate=0.9, octave=0):
        """one bar of notes: toks = names on a grid of `unit` beats, '-' holds the note before, '.' is a rest"""
        t = bar * 4.0; i = 0; toks = toks.split()
        while i < len(toks):
            k = toks[i]; n = 1
            while i + n < len(toks) and toks[i + n] == '-': n += 1
            if k != '.': self.note(ch, t + i * unit, n * unit * gate, N(k) + octave, vel)
            i += n
    def drums(self, bar, pat, vel=100):
        """16 characters, one a 16th: k kick, s snare, h closed hat, o open hat, r rim, t tom, c crash"""
        m = {'k': 36, 's': 38, 'h': 42, 'o': 46, 'r': 37, 't': 45, 'c': 49}
        for i, c in enumerate(pat.replace(' ', '')):
            if c in m: self.note(10, bar * 4 + i * 0.25, 0.1, m[c], vel if c != 'h' else vel * 3 // 4)

# ---- the cop fight: six tracks to pick from (metal / gangsta rap), all fast.  The lead (pulse 1) and the bass
#      (triangle) carry each one, because the gunshots take pulse 2 and the noise.  Change the one the game uses
#      in fight.c (SONG_FIGHT1..6).
def bars(*rows):                              # validate: every bar is 16 sixteenth-note slots
    for r in rows:
        assert len(r.split()) == 16, ('bad bar (%d tokens): %s' % (len(r.split()), r))
    return rows

# --- 1: thrash metal, 160 BPM, E minor - tremolo picking and a galloping bass ---
t = Tune(160); t.prog(1, SAW); t.prog(2, SQUARE)
L1 = bars('E5 E5 E5 E5 G5 G5 E5 E5 D5 D5 E5 E5 B4 B4 D5 D5',
          'E5 E5 E5 E5 G5 G5 A5 A5 B5 B5 A5 A5 G5 G5 E5 E5',
          'E5 E5 E5 E5 G5 G5 E5 E5 C5 C5 B4 B4 C5 C5 D5 D5',
          'B5 B5 A5 A5 G5 G5 E5 E5 F#5 F#5 G5 G5 B5 B5 B5 B5')
B1 = bars('E2 . E2 E2 . E2 E2 . E2 E2 . E2 D2 . C2 .',
          'E2 . E2 E2 . E2 E2 . E2 C2 . C2 D2 . D2 .')
d1 = 'k.ksk.ksk.ksk.ks'; d1f = 'ksksksksksksksss'
for b in range(8):
    t.line(1, b, L1[b % 4], V(13), unit=0.25, gate=0.6)
    t.line(3, b, B1[b % 2], 110, unit=0.25, gate=0.7)
    t.note(2, b * 4, 0.45, N('E3'), V(9)); t.note(2, b * 4 + 2, 0.45, N('G3'), V(9))
    t.drums(b, d1f if b % 4 == 3 else d1)
t.save('song_fight1', 32)

# --- 2: trap / drill gangsta, 150 BPM, A minor - sparse menacing lead, hi-hat rolls, booming bass ---
t = Tune(150); t.prog(1, SAW); t.prog(2, SQUARE)
L2 = bars('A4 - - - - - C5 - B4 - - - A4 - - -',
          '. . E5 - - - D5 - C5 - B4 - A4 - - -',
          'A4 - - - C5 - - - E5 - - - D5 - C5 -',
          'E5 - F5 - E5 - C5 - B4 - - - . . . .')
B2 = bars('A1 - - - A1 - A2 - E2 - - - A1 - - -',
          'A1 - - - A1 - A2 - F2 - - - G2 - - -')
d2 = 'k.hhs.h.k.hhs.hh'; d2f = 'k.hhs.h.k.hssssss'[:16]
for b in range(8):
    t.line(1, b, L2[b % 4], V(12), unit=0.25, gate=0.9)
    t.line(3, b, B2[b % 2], 110, unit=0.25, gate=0.85)
    t.note(2, b * 4 + 2, 0.3, N('C4'), V(7))
    t.drums(b, d2f if b % 4 == 3 else d2, 104)
t.save('song_fight2', 32)

# --- 3: boom-bap gangsta rap, 140 BPM, C minor - a soul-sample riff and a walking bass ---
t = Tune(140); t.prog(1, PIANO); t.prog(2, STRINGS)
L3 = bars('G4 - Bb4 - C5 - Eb5 - D5 - C5 - Bb4 - G4 -',
          'Ab4 - G4 - F4 - G4 - Ab4 - Bb4 - C5 - - -',
          'G4 - Bb4 - C5 - Eb5 - F5 - Eb5 - D5 - C5 -',
          'Bb4 - C5 - D5 - Eb5 - D5 - C5 - Bb4 - G4 -')
B3 = bars('C2 - - G2 C3 - - G2 Ab2 - - Eb2 F2 - G2 -',
          'C2 - - G2 C3 - - Bb2 Ab2 - F2 - G2 - - -')
d3 = 'k...s..kk...s...'; d3b = 'k..ks..k.k..s..o'
for b in range(8):
    t.line(1, b, L3[b % 4], V(11), unit=0.25, gate=0.85)
    t.line(3, b, B3[b % 2], 110, unit=0.25, gate=0.8)
    t.drums(b, d3b if b % 2 else d3, 112)
t.save('song_fight3', 32)

# --- 4: heavy-metal gallop, 150 BPM, D minor - NWOBHM gallop, power-chord stabs, a soaring lead ---
t = Tune(150); t.prog(1, SAW); t.prog(2, SQUARE)
L4 = bars('D5 - - - A5 - - - F5 - - - A5 - - -',
          'D5 - E5 - F5 - E5 - D5 - C5 - A4 - - -',
          'F5 - - - E5 - - - D5 - - - C5 - A4 -',
          'A5 - - - G5 - F5 - E5 - D5 - E5 - F5 -')
B4 = bars('D2 . D2 D2 . D2 D2 . D2 D2 . D2 C2 . A1 .',
          'D2 . D2 D2 . D2 Bb1 . Bb1 Bb1 . Bb1 C2 . C2 .')
for b in range(8):
    t.line(1, b, L4[b % 4], V(13), unit=0.25, gate=0.8)
    t.line(3, b, B4[b % 2], 110, unit=0.25, gate=0.7)
    t.note(2, b * 4, 0.4, N('D3'), V(9)); t.note(2, b * 4 + 2.5, 0.3, N('F3'), V(8))
    t.drums(b, 'k.ksk.k.k.ksk.ks', 108)
t.save('song_fight4', 32)

# --- 5: G-funk gangsta, 145 BPM, G minor - a whiny portamento lead over a funky syncopated bass ---
t = Tune(145); t.prog(1, SAW); t.prog(2, SQUARE)
L5 = bars('D5 - - - F5 - D5 - C5 - D5 - Bb4 - - -',
          'D5 - - - G5 - F5 - D5 - C5 - A4 - - -',
          'Bb4 - - - D5 - C5 - Bb4 - A4 - G4 - - -',
          'G5 - F5 - D5 - C5 - D5 - - - . . . .')
B5 = bars('G1 - - G2 - G1 D2 - G1 - - G2 - F2 - -',
          'G1 - - G2 - G1 Eb2 - F2 - - D2 - D2 - -')
for b in range(8):
    t.line(1, b, L5[b % 4], V(12), unit=0.25, gate=0.9)
    t.line(3, b, B5[b % 2], 110, unit=0.25, gate=0.75)
    t.note(2, b * 4 + 1, 0.3, N('Bb3'), V(7)); t.note(2, b * 4 + 3, 0.3, N('D4'), V(7))
    t.drums(b, 'k.h.s.h.kkh.s.h.', 104)
t.save('song_fight5', 32)

# --- 6: death / industrial metal, 170 BPM, E minor - a brutal chromatic chug and blasting drums ---
t = Tune(170); t.prog(1, SAW); t.prog(2, SQUARE)
L6 = bars('E5 E5 F5 E5 E5 F5 E5 E5 G5 F5 E5 E5 D#5 E5 E5 E5',
          'E5 E5 F5 E5 E5 G5 E5 E5 A5 G5 F5 E5 D5 C5 B4 B4',
          'E5 E5 F5 E5 E5 F5 E5 E5 B5 A5 G5 F5 E5 D5 C5 B4',
          'C5 C5 C5 B4 B4 B4 A4 A4 G4 G4 A4 B4 C5 D5 E5 E5')
B6 = bars('E1 E1 E1 F1 E1 E1 E1 F1 E1 E1 E1 G1 E1 E1 D1 D1',
          'E1 E1 E1 F1 E1 E1 D1 D1 C1 C1 C1 D1 E1 E1 E1 E1')
for b in range(8):
    t.line(1, b, L6[b % 4], V(13), unit=0.25, gate=0.55)
    t.line(3, b, B6[b % 2], 110, unit=0.25, gate=0.6)
    t.drums(b, 'ksksksksksksksks' if b % 2 else 'ksksksksksksssss', 110)
t.save('song_fight6', 32)

# --- 7: hardcore punk, 180 BPM, E - fast downstroke power chords, a d-beat, a shouty lead ---
t = Tune(180); t.prog(1, SQUARE); t.prog(2, SQUARE)
L7 = bars('E5 E5 E5 E5 E5 E5 E5 E5 G5 G5 G5 G5 G5 G5 G5 G5',
          'A5 A5 A5 A5 A5 A5 A5 A5 G5 G5 G5 G5 E5 E5 E5 E5',
          'E5 - E5 - G5 - A5 - B5 - A5 - G5 - E5 -',
          'D5 D5 E5 E5 G5 G5 A5 A5 B5 B5 B5 B5 B5 B5 B5 B5')
B7 = bars('E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2 E2',
          'A2 A2 A2 A2 A2 A2 A2 A2 G2 G2 G2 G2 E2 E2 E2 E2')
for b in range(8):
    t.line(1, b, L7[b % 4], V(13), unit=0.25, gate=0.7)
    t.line(3, b, B7[0] if b % 4 != 1 else B7[1], 110, unit=0.25, gate=0.85)
    for q in range(4): t.note(2, b * 4 + q, 0.22, N('B3'), V(9))   # the fifth, hammered
    t.drums(b, 'ksksksksksksksss' if b % 4 == 3 else 'ksksksksksksksks', 112)
t.save('song_fight7', 32)

# --- 8: skate / pop-punk, 175 BPM, A - a catchy palm-muted lead over a driving root bass ---
t = Tune(175); t.prog(1, SQUARE); t.prog(2, SAW)
L8 = bars('A5 - - - C#6 - A5 - E5 - - - A5 - - -',
          'B5 - - - A5 - F#5 - E5 - D5 - C#5 - - -',
          'D5 - E5 - F#5 - A5 - B5 - A5 - F#5 - E5 -',
          'E5 - F#5 - E5 - C#5 - D5 - E5 - A5 - - -')
B8 = bars('A1 A1 A1 A1 A1 A1 A1 A1 D2 D2 D2 D2 D2 D2 D2 D2',
          'E2 E2 E2 E2 E2 E2 E2 E2 A1 A1 A1 A1 A1 A1 A1 A1')
for b in range(8):
    t.line(1, b, L8[b % 4], V(13), unit=0.25, gate=0.8)
    t.line(3, b, B8[b % 2], 110, unit=0.25, gate=0.8)
    t.note(2, b * 4 + 2, 0.3, N('E4'), V(8))
    t.drums(b, 'k.h.s.h.k.h.s.hh', 108)
t.save('song_fight8', 32)

# --- 9-12: riffs off track 2 (trap / drill, A minor, 150 BPM) with the lead dropped low - fewer high notes ---
def trap_track(name, lead, inst=SAW, drums=('k.hhs.h.k.hhs.hh', 'k.hhs.h.k.hssssss'[:16])):
    t = Tune(150); t.prog(1, inst); t.prog(2, SQUARE)
    B = bars('A1 - - - A1 - A2 - E2 - - - A1 - - -',
             'A1 - - - A1 - A2 - F2 - - - G2 - - -')
    for b in range(8):
        t.line(1, b, lead[b % 4], V(12), unit=0.25, gate=0.9)
        t.line(3, b, B[b % 2], 110, unit=0.25, gate=0.85)
        t.note(2, b * 4 + 2, 0.3, N('A3'), V(6))          # a low stab
        t.drums(b, drums[1] if b % 4 == 3 else drums[0], 104)
    t.save(name, 32)

trap_track('song_fight9', bars(                           # low and menacing
    'A3 - - - - - C4 - A3 - - - E3 - - -',
    '. . A3 - C4 - D4 - C4 - A3 - E3 - - -',
    'A3 - - - C4 - - - E4 - - - D4 - C4 -',
    'E4 - D4 - C4 - A3 - E3 - - - . . . .'))
trap_track('song_fight10', bars(                          # darker drill, a blue note
    'A3 - - - A3 - C4 - E4 - D4 - C4 - - -',
    'A3 - - - A3 - C4 - D4 - - - E3 - - -',
    'C4 - - - D4 - C4 - A3 - - - G3 - - -',
    'A3 - - - - - - - E3 - G3 - A3 - - -'), inst=SQUARE)
trap_track('song_fight11', bars(                          # minimal and hypnotic, lots of space
    'A3 - - - - - - - C4 - - - - - - -',
    'A3 - - - - - - - E3 - - - D4 - - -',
    'A3 - - - - - C4 - D4 - - - - - - -',
    'C4 - - - A3 - - - E3 - - - - - - -'))
trap_track('song_fight12', bars(                          # harder, faster hats
    'A3 A3 - C4 - A3 - E3 A3 A3 - C4 - D4 - -',
    'A3 A3 - C4 - D4 - E4 D4 - C4 - A3 - - -',
    'A3 A3 - E4 - D4 - C4 A3 - - - G3 - - -',
    'E4 - D4 - C4 - A3 - E3 - G3 - A3 - - -'), inst=SQUARE,
    drums=('k.hhs.hhk.hhs.hh', 'k.hssssss.hsssss'))

# ---- Guido's boys: slow and heavy, D minor
t = Tune(72); t.prog(1, REED); t.prog(2, STRINGS)
bass = ['D2 - . D2 . . A1 -', 'D2 - . D2 . . C2 -', 'Bb1 - . Bb1 . . A1 -', 'A1 - . A1 A1 . C#2 -']
lead = ['D4 - - - F4 - E4 -', 'D4 - - - - - . .', 'D4 - - - F4 - G4 -', 'A4 - - - - G4 E4 C#4']
drone = ['A3', 'A3', 'F3', 'E3']
for b in range(4):
    t.line(3, b, bass[b], 100); t.line(1, b, lead[b], V(9), gate=0.95)
    t.note(2, b * 4, 3.9, N(drone[b]), V(4))
    t.drums(b, 'k.....k.r...h.h.', 90)
t.save('song_guido', 16)

# ---- something is up on the street: a sneaky walk, E minor with a blue note; pulse 2 echoes the tune
t = Tune(100); t.prog(1, PIANO); t.prog(2, PIANO)
bass = ['E2 . G2 . A2 . Bb2 A2', 'G2 . E2 . D2 . E2 .', 'E2 . G2 . A2 . Bb2 A2', 'G2 . B2 . E2 . . .']
lead = ['. . B4 . D5 . E5 .', '. G5 E5 D5 B4 . . .', '. . B4 . D5 . E5 G5', 'Bb5 A5 G5 E5 - . . .',
        '. . E5 . G5 . A5 .', '. B5 A5 G5 E5 . D5 .', '. . E5 . D5 . B4 .', 'A4 B4 D5 E5 - . . .']
for b in range(8):
    t.line(3, b, bass[b & 3], 100, gate=0.8); t.line(1, b, lead[b], V(11))
    e = Tune(100); e.tr = t.tr; e.line(2, b + 0.125, lead[b], V(4))     # the echo, an 8th later and quieter
    t.drums(b, 'k.h.r.h.h.k.r.h.', 80)
t.save('song_shady', 32)

# ---- the end: the funeral march (Chopin, sonata no. 2, 1839), B flat minor
t = Tune(60); t.prog(1, SQUARE); t.prog(2, CALLIOPE)
tune = [(0, 2, 'Bb3'), (2, 1.5, 'Bb3'), (3.5, .5, 'Bb3'), (4, 2, 'Bb3'), (6, 1.5, 'Db4'), (7.5, .5, 'C4'),
        (8, 1.5, 'C4'), (9.5, .5, 'Bb3'), (10, 1.5, 'Bb3'), (11.5, .5, 'A3'), (12, 3, 'Bb3')]
for a, d, k in tune: t.note(1, a, d * 0.88, N(k), V(12))
for q in range(16):
    t.note(3, q, 0.8, N('Bb2'), 100)
    t.note(2, q, 0.9, N('F3') if not q & 1 else N('Gb3'), V(6))
    if not q & 1: t.note(10, q, 0.1, 41, 70)                 # a muffled drum
t.save('song_funeral', 16)

# ---- the notice on the way out: quiet and a little sad, A minor
t = Tune(72); t.prog(1, STRINGS); t.prog(2, GUITAR)
ch = ['Am', 'F', 'C', 'G'] * 2
arp = {'Am': ('A3', 'C4', 'E4'), 'F': ('F3', 'A3', 'C4'), 'C': ('C4', 'E4', 'G4'), 'G': ('G3', 'B3', 'D4')}
rt = {'Am': 'A2', 'F': 'F2', 'C': 'C3', 'G': 'G2'}
tune = ['E5 - D5 C5', 'A4 - - C5', 'E5 - G5 -', 'D5 - - -', 'E5 - D5 C5', 'F5 - - E5', 'D5 - C5 B4', 'C5 - B4 -']
for b in range(8):
    a = arp[ch[b]]
    t.line(1, b, tune[b], V(8), unit=1.0, gate=0.97)
    t.note(3, b * 4, 1.9, N(rt[ch[b]]), 100); t.note(3, b * 4 + 2, 1.9, N(rt[ch[b]]), 100)
    for i in range(8): t.note(2, b * 4 + i * 0.5, 0.45, N(a[(0, 1, 2, 1)[i & 3]]), V(6))
t.save('song_notice', 32)
print('make_snd: wrote', len([f for f in os.listdir(snd) if f.endswith('.mid')]), 'files in tools/snd')
import shutil
# The six cop-fight candidates are kept in tools/fight_tracks (not compiled); only the chosen one is copied to
# tools/snd/song_fight.mid and built into the ROM.  To switch tracks, change DEFAULT_FIGHT below and rerun.
DEFAULT_FIGHT = 9
ftdir = os.path.join(here, 'fight_tracks'); os.makedirs(ftdir, exist_ok=True)
for i in range(1, 13):
    shutil.move(os.path.join(snd, 'song_fight%d.mid' % i), os.path.join(ftdir, 'fight%d.mid' % i))
shutil.copy(os.path.join(ftdir, 'fight%d.mid' % DEFAULT_FIGHT), os.path.join(snd, 'song_fight.mid'))
print('make_snd: fight candidates in tools/fight_tracks; song_fight.mid = fight%d' % DEFAULT_FIGHT)

