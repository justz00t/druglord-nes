#!/usr/bin/env python3
"""Builds chr.bin (font as 2bpp tiles) and music.h (title tune) from the sources in tools/."""
import struct, sys, os
here = os.path.dirname(os.path.abspath(__file__)); out = os.path.dirname(here)

# --- CHR (8 KB CHR-RAM, loaded from PRG bank 2 at boot):
#     pattern table 0 (the game):  0x00-0x7F glyphs in colour 1 (CP437 font; 1-6 double-line box pieces,
#                                  7 shade, 8 full block, 9 lower half, 10 upper half, 11 dark shade, 12 light shade),
#                                  0x80-0xFF the same glyphs inverted (the highlight)
#     pattern table 1 (the title): 0x00-0x7F the same glyphs, then the logo tiles (from the DOS title art,
#                                  one 8x16 text cell = 4x8 pixels)
f = open(os.path.join(here, 'font8x8.bin'), 'rb').read()
assert len(f) == 2048
remap = {1: 0xC9, 2: 0xCD, 3: 0xBB, 4: 0xBA, 5: 0xC8, 6: 0xBC, 7: 0xB1, 8: 0xDB, 9: 0xDC, 10: 0xDF, 11: 0xB2, 12: 0xB0}
def glyph(t, col=1, inv=False):
    g = bytearray(f[remap.get(t, t)*8:remap.get(t, t)*8+8])
    if inv: g = bytearray((~x) & 0xFF for x in g)
    return bytes(g if col & 1 else bytes(8)) + bytes(g if col & 2 else bytes(8))
t0 = bytearray(b''.join(glyph(t & 0x7F, 1, t >= 128) for t in range(256)))
# 0x80-0x9F would be the inverted control characters, which are never shown: they hold the health colours instead
#   0x80-0x89 the digits in colour 2, 0x8A-0x93 the digits in colour 3, 0x94-0x96 "d", "e", "a" in colour 2 ("dead"),
#   0x97 the double line in colour 3 (the white line under the status block)
extra = [glyph(ord(c), 2) for c in '0123456789'] + [glyph(ord(c), 3) for c in '0123456789'] + \
        [glyph(ord(c), 2) for c in 'dea'] + [glyph(2, 3)]
#   0x98-0x99 a star split over two tiles (centred between them): the hit flash in a cop fight
star = glyph(ord('*'), 1)
extra += [bytes(b >> 4 for b in star), bytes((b << 4) & 0xFF for b in star)]
#   0x9A the menu arrow in colour 3 (blue, palette 2) for the main menu
extra += [glyph(0x10, 3)]
#   0x9B-0x9C a skull in colour 3 (grey, palette 2) split over two tiles like the star: a fighter at 0 health
SKULL = ['.XXXXX..', 'XXXXXXX.', 'X..X..X.', 'X..X..X.', 'XXXXXXX.', '.XX.XX..', '.XXXXX..', '.X.X.X..']
sk = [sum(0x80 >> i for i, c in enumerate(r) if c == 'X') << 3 for r in SKULL]    # 7 wide: columns 5-11 of the pair
L = [b >> 8 for b in sk]; R = [b & 0xFF for b in sk]
extra += [bytes(L + L), bytes(R + R)]
for i, g in enumerate(extra): t0[(0x80 + i) * 16:(0x81 + i) * 16] = g
t0 = bytes(t0)

# logo: 6 rows x 62 cells of (char, attr); shade = the letter face (colour 3),
# full/half blocks = the shadow (colour 2).  One blank cell in front centres it (63 cells -> 32 tiles).
art = open(os.path.join(here, 'title_art.bin'), 'rb').read()
cells = [[0x20] + [art[(r*62 + c)*2] for c in range(62)] + [0x20] for r in range(6)]
def cell_px(ch, x, y):          # colour of pixel (x 0..3, y 0..7) of one cell
    if ch == 0xB1: return 3                # the face: solid colour 3 (the text uses colour 1, so the two can differ)
    if ch == 0xDB: return 2
    if ch == 0xDC: return 2 if y >= 4 else 0
    if ch == 0xDF: return 2 if y < 4 else 0
    return 0
def tile_bytes(px):             # px[y][x] -> 16 bytes
    lo = bytes(sum(((px[y][x] & 1) << (7 - x)) for x in range(8)) for y in range(8))
    hi = bytes(sum(((px[y][x] >> 1) << (7 - x)) for x in range(8)) for y in range(8))
    return lo + hi
LOGO_W, LOGO_H = 32, 6
t1 = bytearray(b''.join(glyph(t) for t in range(128)))
logo_tiles, logo_map = {}, []
for ty in range(LOGO_H):
    for tx in range(LOGO_W):
        px = [[cell_px(cells[ty][tx*2 + (x >> 2)], x & 3, y) for x in range(8)] for y in range(8)]
        b = tile_bytes(px)
        if b == bytes(16): logo_map.append(0); continue
        if b not in logo_tiles:
            logo_tiles[b] = 0x80 + len(logo_tiles); t1 += b
        logo_map.append(logo_tiles[b])
# big letters for the "DRUGLORD!!!" flashes before the win scene: each glyph of BIG at double size (2x2 tiles),
# and the 16 "quarter block" tiles (a 4x4-pixel block in each corner that is set) for 4x size
BIG = 'DRUGLO!'
big2 = []
for ch in BIG:
    g = f[ord(ch)*8:ord(ch)*8+8]
    px = [[1 if g[y >> 1] & (0x80 >> (x >> 1)) else 0 for x in range(16)] for y in range(16)]
    base = len(t1) // 16
    for ty in range(2):
        for tx in range(2):
            t1 += tile_bytes([[px[ty*8 + y][tx*8 + x] for x in range(8)] for y in range(8)])
    big2.append(base)
# glyphs in colour 2 for the initials screen's "You made place N of 10" line (yellow on palette 0 there)
RANKS = ['Loser!', 'Dopehead', 'Peddler', 'Pusher', 'Gangster', 'Dealer', 'Trafficker', 'Racketeer', 'Marketeer',
         'Entrepreneur', 'DRUGLORD']            # the rank names (druglord.c rank_name), shown in yellow
Y2 = ''.join(sorted(set('SCORE RANK -0123456789' + ''.join(RANKS)) - {' '}))
assert len(Y2) <= 256 - len(t1) // 16, 'the yellow glyphs do not fit the title tile set'
y2_base = len(t1) // 16
for ch in Y2: t1 += glyph(ord(ch), 2)
quad_base = len(t1) // 16
for q in range(16):            # bit 0 top left, 1 top right, 2 bottom left, 3 bottom right
    t1 += tile_bytes([[1 if q & (1 << ((y >> 2) * 2 + (x >> 2))) else 0 for x in range(8)] for y in range(8)])
assert len(t1) <= 4096, len(t1)
t1 += bytes(4096 - len(t1))

# --- the end scene (the win): the DOS picture (80 x 25 text cells) as 30 x 25 tiles, one cell = 3 x 8
#     pixels.  Palette 0: dark brown / yellow / brown (the table and the coins), 1: dark grey / grey / brown
#     (the lamp and the guns), 2: green / yellow / brown (the stacks of money).  Brown and yellow keep
#     their index in every palette, so a palette change under a stack or a gun leaves the table alone.
scene = open(os.path.join(here, 'end_scene.bin'), 'rb').read()
gun = open(os.path.join(here, 'gun_sprite.bin'), 'rb').read()
BROWN, DBROWN, YELLOW = 3, 1, 2          # palette 0 (and 2: 1 is green there)
GREY, DGREY = 2, 1                        # palette 1
def scene_px(ch, at, x, y):              # colour index of pixel (x 0..2, y 0..7) of one cell
    fg, bg = at & 15, (at >> 4) & 7
    base = BROWN if bg == 6 else 0
    if ch == 0x20: return base
    if ch == 0xDB: return BROWN if fg == 6 else (GREY if fg == 7 else base)
    if ch == 0xB1: return DBROWN                          # dark shade on black/brown: the table's edges and the crate
    if ch == 0xB0: return DBROWN if (x + y) & 1 else base # light shade: the legs' edges, the crate's slats
    if ch == 0xB2: return GREY                            # the lamp shade
    if ch == 0xDC: return GREY if y >= 4 else base
    if ch == 0xDF: return GREY if y < 4 else base
    if ch == 0xEC: return YELLOW if 2 <= y <= 5 else base # a coin
    if ch == 0xC4: return BROWN if y == 4 else base       # lamp frame, horizontal
    if ch == 0xB3: return BROWN if x == 1 else base       # lamp frame, vertical
    if ch in (0xDA, 0xBF, 0xC0, 0xD9, 0xC2, 0xC3, 0xB4):  # lamp frame corners and tees
        h = (y == 4) and (ch in (0xC2,) or (x >= 1 and ch in (0xDA, 0xC0, 0xC3)) or (x <= 1 and ch in (0xBF, 0xD9, 0xB4)))
        v = (x == 1) and (ch in (0xC3, 0xB4) or (y >= 4 and ch in (0xDA, 0xBF, 0xC2)) or (y <= 4 and ch in (0xC0, 0xD9)))
        return BROWN if (h or v) else base
    if ch == 0xBA: return GREY if x == 1 else base        # the cord
    if ch == 0x4F: return GREY if (y in (1, 6) and x == 1) or (2 <= y <= 5 and x != 1) else base   # the cord's end
    return base
W_, H_ = 30, 25
pic = [[0] * 240 for _ in range(200)]
for r in range(25):
    for c in range(80):
        ch, at = scene[(r*80 + c)*2], scene[(r*80 + c)*2 + 1]
        for y in range(8):
            for x in range(3): pic[r*8 + y][c*3 + x] = scene_px(ch, at, x, y)
t2 = bytearray(b''.join(glyph(t) for t in range(128)))
scene_tiles, scene_map = {}, []
def add_tile(px):
    global t2
    b = tile_bytes(px)
    if b == bytes(16): return 0
    if b not in scene_tiles:
        assert len(t2) < 4096, 'scene: too many tiles'
        scene_tiles[b] = len(t2) // 16; t2 += b
    return scene_tiles[b]
for ty in range(H_):
    for tx in range(W_):
        scene_map.append(add_tile([[pic[ty*8 + y][tx*8 + x] for x in range(8)] for y in range(8)]))
# a stack of money: 16 x 8 pixels, "$" in yellow on a green block (palette 2)
dollar = f[0x24*8:0x24*8+8]
bar = [[(2 if (5 <= x <= 10 and dollar[y] & (0x80 >> (x - 4))) else (1 if ((x + y) & 1) or (0 < y < 7) else 0)) for x in range(16)] for y in range(8)]
bar_tiles = [add_tile([[bar[y][x + 8*i] for x in range(8)] for y in range(8)]) for i in range(2)]
# a gun: 8 x 3 cells = 24 x 24 pixels = 3 x 3 tiles (palette 1)
def gun_px(ch, x, y):
    if ch == 0xDE: return GREY if x >= 1 else 0
    if ch == 0xB0: return GREY if (x + y) & 1 else DGREY
    if ch == 0xDF: return GREY if y < 4 else 0
    if ch == 0xE9: return GREY if (y in (1, 6) and x == 1) or (2 <= y <= 5 and x != 1) or y in (3, 4) else 0
    return 0
gpic = [[gun_px(gun[(r*8 + c)*2], x, y) for c in range(8) for x in range(3)] for r in range(3) for y in range(8)]
gun_tiles = [add_tile([[gpic[ty*8 + y][tx*8 + x] for x in range(8)] for y in range(8)]) for ty in range(3) for tx in range(3)]
t2 += bytes(4096 - len(t2))
open(os.path.join(out, 'chr.bin'), 'wb').write(t0 + t1 + t2)
with open(os.path.join(out, 'scene.h'), 'w') as o:
    o.write('/* generated by tools/mkdata.py from tools/end_scene.bin and tools/gun_sprite.bin - do not edit */\n')
    o.write('#define SCENE_W %d\n#define SCENE_H %d\n' % (W_, H_))
    o.write('/* the picture lives in PRG bank 2 (copied to the screen with ppu_write2) */\n#pragma rodata-name (push, "FONT")\n')
    o.write('static const unsigned char scene_map[SCENE_H * SCENE_W] = {%s};\n' % ','.join(map(str, scene_map)))
    o.write('#pragma rodata-name (pop)\n')
    o.write('static const unsigned char bar_tiles[2] = {%s};\n' % ','.join(map(str, bar_tiles)))
    o.write('static const unsigned char gun_tiles[9] = {%s};\n' % ','.join(map(str, gun_tiles)))
print('scene tiles', len(scene_tiles))
with open(os.path.join(out, 'title.h'), 'w') as o:
    o.write('/* generated by tools/mkdata.py from tools/title_art.bin - do not edit */\n')
    o.write('#define LOGO_W %d\n#define LOGO_H %d\n' % (LOGO_W, LOGO_H))
    o.write('static const unsigned char logo_map[LOGO_H * LOGO_W] = {%s};\n' % ','.join(map(str, logo_map)))
    o.write('#define BIG_CHARS "%s"\n#define BIG_QUAD %d\n' % (BIG, quad_base))
    o.write('static const unsigned char big2[%d] = {%s};          /* first of the 2x2 tiles of each */\n' % (len(BIG), ','.join(map(str, big2))))
    o.write('static const unsigned char big_glyph[%d] = {%s};     /* the 8x8 glyphs, a byte a row */\n' % (len(BIG) * 8, ','.join(str(b) for ch in BIG for b in f[ord(ch)*8:ord(ch)*8+8])))
print('logo tiles', len(logo_tiles), 'title table', len(t1) // 16, 'tiles')
with open(os.path.join(out, 'y2.h'), 'w') as o:
    o.write('/* generated by tools/mkdata.py - do not edit: the yellow glyphs of the title tile set (initials screen) */\n')
    o.write('#define Y2_CHARS "%s"\n#define Y2_BASE %d\n' % (Y2, y2_base))

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
    o.write('static const u16 %s_pp[] = {0, %s};\n' % (prefix.lower(), ', '.join(str(per_p(k)) for k in notes)))
    o.write('static const u16 %s_tp[] = {0, %s};\n' % (prefix.lower(), ', '.join(str(per_t(k)) for k in notes)))
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
    o.write('/* generated by tools/mkdata.py from tools/title.mid - do not edit */\n')
    tr, div, tempo = read_midi(os.path.join(here, 'title.mid'))
    start = min(t for t, _ in tr[0]) // (div // 16) * (div // 16)
    v1, v2, v3 = split_piano(tr[0], start)
    emit_tune('TITLE', {1: [v1], 2: [v2], 3: [v3]}, div, tempo, 16, 16, o)

# --- game tables
import math
with open(os.path.join(out, 'tables.h'), 'w') as o:
    o.write('/* generated by tools/mkdata.py - do not edit */\n')
    # trenchcoat: floor(capacity^3.34 * 0.0634) for capacity 10,20..300
    o.write('#ifdef TBL_COAT\n')
    o.write('static const unsigned long coat_base[30] = {%s};\n' % ','.join('%dUL' % int(math.pow(c*10, 3.34)*0.0634) for c in range(1, 31)))
    o.write('#endif\n')
    # score part 2: days^-2.12 * 104e9, capped at 500000 (index = days, 0 unused)
    p2 = [min(500000, int(math.pow(d, -2.12)*104000000000.0)) for d in range(1, 701)]
    first = next(i for i, v in enumerate(p2) if v < 500000)     # days up to 'first' are all capped
    o.write('#ifdef TBL_SCORE\n')
    o.write('#define SCORE_DAYS_FIRST %d\n' % (first + 1))
    sd = p2[first:700]
    dl = [sd[i-1]-sd[i] for i in range(1, len(sd))]
    assert all(0 <= d < 65536 for d in dl), 'delta range'
    o.write('#define SCORE_DAYS_START %dUL\n' % sd[0])
    s2 = [dl[i+1]-dl[i] for i in range(len(dl)-1)] + [0]
    assert all(-128 <= x < 128 for x in s2), 'second diff range'
    o.write('#define SCORE_DAYS_D0 %d\n' % dl[0])
    o.write('static const signed char score_days_s[%d] = {%s};\n' % (len(s2), ','.join(str(d) for d in s2)))
    # score part 3: pct^8.14 * 2.48e-11, capped at 500000 (index = percent of good deals)
    p3 = [min(500000, int(math.pow(d, 8.14)*2.48e-11)) for d in range(0, 101)]
    dd = [p3[i]-p3[i-1] for i in range(1, 101)]
    assert all(0 <= d < 65536 for d in dd), 'deal delta range'
    z = 0
    while dd[z] == 0: z += 1
    o.write('#define SCORE_DEALS_Z %d\n' % z)
    o.write('static const u16 score_deals_d[%d] = {%s};\n' % (100 - z, ','.join(str(d) for d in dd[z:])))
    # score part 1: floor(2^(total/10e6)) for total >= 10e6*log2(k): thresholds for k = 2..10
    o.write('static const unsigned long score_wealth[9] = {%s};\n' % ','.join('%dUL' % math.ceil(1e7*math.log2(k)) for k in range(2, 11)))
    o.write('#endif\n')
