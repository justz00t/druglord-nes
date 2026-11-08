/* fixed PRG bank: the runtime, the text/sound/music engine, the rules, the main loop */
#include "dl.h"
#include "music.h"

/* ======================================================================== */
/*  PPU / text                                                              */
/* ======================================================================== */
volatile u8 latch;       /* a real read of $2002 resets the PPUADDR write latch */
u8 ppu_on;               /* rendering is switched off lazily while we draw */
u8 cx, cy;               /* text cursor */
u8 wx0 = 1, wx1 = 30;    /* word-wrap window (columns) */
u8 wy0 = 1, wy1 = 27;    /* rows used by wrapped text */

u8 bgctl;                /* PPUCTRL value: bit 4 picks pattern table 1 (the title screen tiles) */
u8 ink, attr_dirty;      /* current text colour (palette 0..3); pc() colours its 2x2-tile attribute block with it */
u8 attr[64];
u8 pend;
extern volatile u8 nmi_count;   /* bumped by the NMI at the start of every vblank (crt0.s) */
void wait_vblank(void) { u8 c = nmi_count; while (nmi_count == c) {} if (pend < 6) pend++; }
void music_update(void);
void ppu_off(void) { if (ppu_on) { wait_vblank(); PPUMASK = 0; ppu_on = 0; } }
void ppu_show(void) {
    if (!ppu_on) {
        if (attr_dirty) { u8 i; attr_dirty = 0; latch = PPUSTATUS; PPUADDR = 0x23; PPUADDR = 0xC0; for (i = 0; i < 64; i++) PPUDATA = attr[i]; }
        wait_vblank();
        PPUCTRL = bgctl | 0x80; latch = PPUSTATUS; PPUSCROLL = 0; PPUSCROLL = 0;
        PPUMASK = 0x0A; ppu_on = 1;
    }
}
void ppu_at(u8 x, u8 y) {
    u16 a = 0x2000 + (u16)y * 32 + x;
    latch = PPUSTATUS; PPUADDR = a >> 8; PPUADDR = a & 255;
}
void put(u8 x, u8 y, u8 ch) { ppu_off(); ppu_at(x, y); PPUDATA = ch; }
/* write two tiles during vblank without blanking the screen (used for cursors) */
void put2(u8 x1, u8 y1, u8 c1, u8 x2, u8 y2, u8 c2) {
    if (ppu_on) wait_vblank();
    ppu_at(x1, y1); PPUDATA = c1;
    ppu_at(x2, y2); PPUDATA = c2;
    if (ppu_on) { PPUCTRL = bgctl | 0x80; PPUSCROLL = 0; PPUSCROLL = 0; }
}
void fill(u8 x, u8 y, u8 ch, u8 n) {
    ppu_off(); ppu_at(x, y);
    while (n--) PPUDATA = ch;
}
void clear_rows(u8 y0, u8 y1) {      /* inclusive */
    u8 y;
    for (y = y0; y <= y1; y++) fill(0, y, ' ', 32);
}
void set_palette(void) {
    /* 0: white text, colours 2-3 the health colours pink and light red; 1: yellow, colour 3 white (the line);
     * 2: red, colour 2 dark red (health); 3: cyan */
    static const u8 pal[16] = {0x0F, 0x30, 0x36, 0x26,  0x0F, 0x28, 0x18, 0x30,
                               0x0F, 0x16, 0x06, 0x00,  0x0F, 0x2C, 0x1C, 0x00};
    u8 i;
    ppu_off();
    latch = PPUSTATUS; PPUADDR = 0x3F; PPUADDR = 0x00;
    for (i = 0; i < 32; i++) PPUDATA = pal[i & 15];
}
void clear_screen(void) {
    u16 i;
    ppu_off(); ppu_at(0, 0);
    for (i = 0; i < 1024; i++) PPUDATA = 0;
    for (i = 0; i < 64; i++) attr[i] = 0;
    ink = 0; attr_dirty = 0; bgctl = 0;
    pal3(0x2C, 0x1C);                                    /* cyan, unless a screen asks for green */
    cx = 1; cy = 1;
}
/* colour a band of rows: pal 0 white, 1 yellow, 2 red, 3 green.  An attribute byte covers
 * 4x4 tiles, two bits per 2x2 block, so colours change every two rows (y0 even, y1 odd). */
void colour_cols(u8 y0, u8 y1, u8 bx0, u8 bx1, u8 pal) {    /* bx = 16-pixel column blocks 0..15 */
    u8 r, x, i;
    for (r = y0 >> 1; r <= (y1 >> 1); r++) {
        u8 mask = (r & 1) ? 0xF0 : 0x0F;
        u8 v = (pal | (pal << 2) | (pal << 4) | (pal << 6)) & mask;
        for (x = bx0 >> 1; x <= (bx1 >> 1); x++) { i = (r >> 1) * 8 + x; attr[i] = (attr[i] & ~mask) | v; }
    }
    ppu_off();
    latch = PPUSTATUS; PPUADDR = 0x23; PPUADDR = 0xC0;
    for (i = 0; i < 64; i++) PPUDATA = attr[i];
    attr_dirty = 0;
}
void pal3(u8 a, u8 b) {                      /* palette 3 = cyan by default; green for a few screens */
    ppu_off();
    latch = PPUSTATUS; PPUADDR = 0x3F; PPUADDR = 0x0D; PPUDATA = a; PPUDATA = b;
}
void colour_rows(u8 y0, u8 y1, u8 pal) { colour_cols(y0, y1, 0, 15, pal); }

/* ---- printing ---- */
void pc(u8 ch) {
    put(cx, cy, ch);
    if (ch != ' ') {
        u8 i = (cy >> 2) * 8 + (cx >> 2), sh = ((cy & 2) ? 4 : 0) + ((cx & 2) ? 2 : 0), o = attr[i];
        if (((o >> sh) & 3) != ink) { attr[i] = (o & ~(3 << sh)) | (ink << sh); attr_dirty = 1; }
    }
    cx++;
}
void at(u8 x, u8 y) { cx = x; cy = y; }
void ps(const char* s) { while (*s) pc((u8)*s++); }
void ps_at(u8 x, u8 y, const char* s) { at(x, y); ps(s); }
char nb[12];
void ltoa_s(i32 v) {         /* decimal into nb */
    char t[12]; u8 n = 0, i = 0;
    u32 u;
    if (v < 0) { nb[i++] = '-'; u = (u32)(-v); } else u = (u32)v;
    if (u == 0) t[n++] = '0';
    while (u >= 65536UL) { t[n++] = '0' + (u8)(u % 10); u /= 10; }        /* 32-bit division is slow on the 6502: */
    { u16 w = (u16)u; while (w) { t[n++] = '0' + (u8)(w % 10); w /= 10; } }  /* finish with 16-bit arithmetic */
    while (n) nb[i++] = t[--n];
    nb[i] = 0;
}
void pn(i32 v) { ltoa_s(v); ps(nb); }
void pn_r(i32 v, u8 w) {     /* right-aligned in w columns */
    u8 l;
    ltoa_s(v);
    for (l = 0; nb[l]; l++) {}
    while (l++ < w) pc(' ');
    ps(nb);
}
void pn_l(i32 v, u8 w) {     /* left-aligned, padded with blanks to w columns */
    u8 l;
    ltoa_s(v);
    for (l = 0; nb[l]; l++) {}
    ps(nb);
    while (l++ < w) pc(' ');
}
void ps_pad(const char* s, u8 w) {
    u8 l = 0;
    while (*s) { pc((u8)*s++); l++; }
    while (l++ < w) pc(' ');
}

/* ======================================================================== */
/*  Controller, frames, sound                                               */
/* ======================================================================== */
u8 pad_old;
u16 frames;
extern u8 music_on;
u8 read_pad(void) {
    u8 i, v = 0;
    JOY1 = 1; JOY1 = 0;
    for (i = 0; i < 8; i++) v = (v << 1) | (JOY1 & 1);
    return v;
}
/* one frame; returns the buttons that went down this frame */
u8 vsync_pad(void) {
    u8 p;
    wait_vblank(); frames++;
    music_update();
    p = read_pad();
    { u8 e = p & ~pad_old; pad_old = p; return e; }
}
void delay(u8 n) {           /* n frames; holding A skips the wait */
    ppu_show();
    while (n--) { vsync_pad(); if (pad_old & BTN_A) break; }
}
u8 pend_btn;                 /* a button pressed while a short sound effect played: handed to the next wait_btn() */
u8 wait_btn(void) {          /* wait for any new button, return it */
    u8 e;
    ppu_show();
    if (pend_btn) { e = pend_btn; pend_btn = 0; return e; }
    for (;;) { e = vsync_pad(); if (e) return e; }
}

/* ---- sound effects: pulse 2, noise and (when no music plays) pulse 1, stepped once a frame while
 *      the game waits.  A press of A cuts a long effect short. ---- */
static u8 p1hi, p2hi;                /* last high period byte written: a $4003/$4007 write restarts the wave (a click), so it is only written when it changes */
void p2(u16 per) { APU(0x06) = per & 255; if ((per >> 8) != p2hi) { p2hi = per >> 8; APU(0x07) = 0x08 | p2hi; } }
void p1(u16 per) { APU(0x02) = per & 255; if ((per >> 8) != p1hi) { p1hi = per >> 8; APU(0x03) = 0x08 | p1hi; } }
static u8 skip, sfx_t;
u8 sfx_busy;                 /* a sound effect is playing: the menu music leaves pulse 2 and the noise alone */
/* one frame of an effect.  A pressed after the first 8 frames cuts the effect short (and is used up by that);
 * a button pressed earlier is kept for the next wait_btn(), so a quick press is never lost to a click. */
static u8 tick(void) {
    u8 e = vsync_pad();
    sfx_t++;
    if (e) { if ((e & BTN_A) && sfx_t > 8) { skip = 1; pend_btn = 0; } else pend_btn |= e; }
    return skip;
}
static void sfx_off(void) { APU(0x04) = 0x30; APU(0x0C) = 0x30; if (!music_on) APU(0x00) = 0x30; p1hi = p2hi = 0xFF; sfx_busy = 0; }
/* a steady tone on pulse 2: dv = duty | 0x30 | volume, n frames */
void tone(u16 per, u8 dv, u8 n) { APU(0x04) = dv; p2(per); while (n--) if (tick()) break; }
void snd(u16 per, u8 n) { ppu_show(); skip = 0; sfx_t = 0; p2hi = 0xFF; sfx_busy = 1; tone(per, 0xBC, n); APU(0x04) = 0x30; sfx_busy = 0; }
/* a noise burst that fades: idx = period index 0 (hiss) .. 15 (rumble), from volume vol over n frames */
void noi(u8 idx, u8 vol, u8 n) {
    u8 i;
    APU(0x0E) = idx; APU(0x0F) = 0x08;
    for (i = 0; i < n; i++) { APU(0x0C) = 0x30 | (u8)((u16)vol * (n - i) / n); if (tick()) break; }
    APU(0x0C) = 0x30;
}
/* a pulse-2 note that decays (a pluck), 12.5% duty */
static void pluck(u16 per, u8 n) {
    u8 v = 15;
    p2(per);
    while (n--) { APU(0x04) = 0x30 | v; if (v > 1) v--; if (tick()) break; }
    APU(0x04) = 0x30;
}
/* a pulse-2 slide from one period to another over n frames (the period changes linearly) */
static void slide(u16 from, u16 to, u8 dv, u8 n) {
    u8 i;
    APU(0x04) = dv;
    for (i = 0; i < n; i++) { p2(from + (i32)((i32)to - from) * i / n); if (tick()) break; }
}
/* one gunshot: a crack (short bright noise) over a boom (noise rumble plus a low pulse thump) */
static void bang(u8 crack, u8 boom, u8 thump) {
    u8 i, v;
    APU(0x0F) = 0x08;
    if (thump) { APU(0x04) = 0xBF; p2(HZ(140)); }
    for (i = 0; i < boom; i++) {
        v = (u8)(15 - (u16)15 * i / boom);
        APU(0x0E) = i < crack ? 4 : 10;
        APU(0x0C) = 0x30 | v;
        if (thump) { if (i < 10) p2(HZ(140) + i * 20); else APU(0x04) = 0x30; }
        if (tick()) break;
    }
    APU(0x0C) = 0x30; APU(0x04) = 0x30;
}
/* a police siren: one slow wail (500 -> 1300 Hz and back, eased) then four fast yelps, on both pulses
 * (pulse 1 a touch sharp so the two beat against each other) */
static const u8 siren_t[48] = {224,223,223,222,220,218,215,213,209,206,202,198,193,189,184,179,175,170,165,160,155,150,146,141,137,132,128,124,120,117,113,110,107,105,102,100,97,95,94,92,91,89,88,88,87,86,86,86};
static u8 siren_step(u8 i) { p2(siren_t[i]); if (!music_on) p1(siren_t[i] + 3); return tick(); }
static void siren(void) {
    u8 i, c;
    APU(0x04) = 0xBB; if (!music_on) APU(0x00) = 0xB7;
    for (i = 0; i < 48; i++) if (siren_step(i)) goto out;
    for (i = 48; i--; ) if (siren_step(i)) goto out;
    for (c = 0; c < 4; c++) {
        for (i = 0; i < 48; i += 4) if (siren_step(i)) goto out;
        for (i = 48; i >= 4; i -= 4) if (siren_step(i - 4)) goto out;
    }
out:;
}
/* the EXTRA! sting: a news-wire xylophone.  One short figure on one note, over and over (tap-tap-taaap, a teletype
 * feel) on pulse 2, with a little noise tick for each mallet; the last figure ends on a ringing two-note hit.
 * xs_gap = frames to the next tap; xs_acc = 0 a tap, 3 the end. */
static const u8 xs_gap[] = {5,5,10, 5,5,10, 5,5,10, 5,5,10, 5,5,10, 5,5,10, 5,5,10, 5,5,10, 0};
static const u8 xs_acc[] = {0,0,0,  0,0,0,  0,0,0,  0,0,0,  0,0,0,  0,0,0,  0,0,0,  0,0,0,  3};
static void news_sting(void) {
    u8 i = 0, w = 0, v1 = 0, v2 = 0, f = 0;
    APU(0x04) = 0x70; APU(0x00) = 0x70;
    for (;;) {
        if (w == 0) {
            u8 a = xs_acc[i];
            if (a == 3) {                                   /* the end: E6 and the octave, ringing */
                p2hi = p1hi = 0xFF; p2(HZ(1319)); p1(HZ(2637)); v1 = v2 = 15;
                for (f = 0; f < 50; f++) {
                    APU(0x04) = 0x70 | v2; APU(0x00) = 0x70 | v1;
                    if ((f & 3) == 3) { if (v1) v1--; if (v2) v2--; }
                    if (tick()) break;
                }
                break;
            }
            p2hi = 0xFF; p2(HZ(1319)); v2 = 13;              /* the tap: E6 */
            if (a) { p1hi = 0xFF; p1(a == 1 ? HZ(1976) : HZ(2637)); v1 = 14; }   /* B6 or E7 */
            APU(0x0E) = 0x82; APU(0x0F) = 0x08; APU(0x0C) = 0x34;
            w = xs_gap[i++];
        }
        APU(0x04) = 0x70 | v2; APU(0x00) = 0x70 | v1;
        if (v2) v2 = v2 > 2 ? v2 - 2 : 0;
        if (v1) v1 = v1 > 1 ? v1 - 1 : 0;
        APU(0x0C) = 0x30;
        w--;
        if (tick()) break;
    }
    APU(0x00) = 0x30;
}
void sfx(u8 n) {
    u8 i;
    ppu_show();
    skip = 0; sfx_t = 0; p1hi = p2hi = 0xFF; sfx_busy = 1;
    switch (n) {
    case 2: tone(HZ(1760), 0x7A, 2); tone(HZ(2349), 0x78, 3); break;                 /* beep (menu pick) */
    case 3: for (i = 0; i < 18; i++) { tone(i & 1 ? HZ(110) : HZ(104), 0xBD - (i >> 2), 1); } break;   /* denied: a buzzer */
    case 4: noi(0, 8, 2); for (i = 0; i < 2; i++) { pluck(HZ(2093), 4); pluck(HZ(2637), 4); } pluck(HZ(3136), 14); break;   /* deal: "cha-ching" */
    case 5: siren(); break;                                                              /* police siren */
    case 6: pluck(HZ(784), 5); pluck(HZ(988), 5); pluck(HZ(1175), 5); pluck(HZ(1568), 16); break;        /* news: a short fanfare */
    case 7: news_sting(); break;                                                         /* EXTRA!: the news-wire xylophone */
    case 8: noi(6, 12, 6); slide(HZ(300), HZ(60), 0xBE, 8); break;                           /* hit: a thud */
    case 9: for (i = 0; i < 40; i++) { APU(0x0E) = 15 - (i >> 2); APU(0x0F) = 0x08; APU(0x0C) = 0x30 | (i < 32 ? 4 + (i >> 3) : 8 - (i & 7)); if (tick()) break; }   /* jet: engines spool up */
            APU(0x0C) = 0x30; break;
    case 10: slide(HZ(100), HZ(220), 0xBE, 10); slide(HZ(220), HZ(600), 0xBE, 25); break;      /* take-off: a rising whine */
    case 11: bang(2, 5, 0); break;                                                       /* Saturday Night Special: a pop */
    case 12: bang(3, 10, 0); break;                                                      /* Ruger: a crack */
    case 13: bang(3, 18, 1); break;                                                      /* Auto Mag: a boom */
    case 14: for (i = 0; i < 4; i++) { bang(1, 3, 0); APU(0x0C) = 0x30; tick(); } break;    /* Uzi: a burst */
    case 15: for (i = 0; i < 3; i++) { bang(2, 6, 1); tick(); tick(); } break;              /* AK-47: three heavy rounds */
    case 16: tone(HZ(330), 0xBC, 12); tone(HZ(311), 0xBC, 12); tone(HZ(294), 0xBC, 12); tone(HZ(220), 0xBC, 30); break;   /* death: four falling notes */
    case 17: noi(8, 14, 5); slide(HZ(200), HZ(80), 0xBE, 6); break;                         /* punch */
    case 20: tone(HZ(2093), 0x74, 1); break;                                             /* cursor tick */
    }
    sfx_off();
}

/* ---- music: the title tune from tools/title.mid, played on pulse 1 / pulse 2 / triangle.  Each tune is a list of
 *      events (wait, pulse-1 and pulse-2 note nibbles, triangle note). ---- */
const u8* m_d; static const u8* m_ab; static const u8* m_c;
const u16* m_pp; static const u16* m_tp;
u16 m_n, m_rate, m_ev, m_acc;
u8 m_wait, m_v1, m_v2, m_tri, m_tage, m_cnt, m_use2, music_on;
u8 m_unit, m_nv;                /* 64th-note count in the bar (the drums), noise volume */
u8 m_loops;                     /* times the tune has played through since music_start() */
extern u8 curbank;              /* the PRG bank mapped at $8000 (crt0.s) */
u8 m_song;                      /* 0 = the title tune, 1 = the main-menu song, 2 = the easter-egg song, 3 = the victory tune
                                 * (1-3 are played by music.c / victory.c in PRG bank 3) */
u8 easter_on;                   /* the easter egg has been found this game (reset by init_game) */
void music_stop(void) {
    music_on = 0;
    APU(0x00) = 0x30; APU(0x04) = 0x30; APU(0x08) = 0x80; APU(0x0C) = 0x30;
}
void music_start(void) {
    APU(0x15) = 0x0F;               /* pulse 1, pulse 2, triangle, noise */
    APU(0x01) = 0x08; APU(0x05) = 0x08;
    m_song = 0;
    m_d = title_d; m_ab = title_ab; m_c = title_c; m_pp = title_pp; m_tp = title_tp; m_n = TITLE_EVENTS; m_rate = TITLE_RATE; m_use2 = 1;
    m_ev = 0; m_acc = 0; m_wait = m_d[0]; m_v1 = m_v2 = 0; m_tri = 0; m_tage = 0; m_cnt = 0; m_unit = 0; m_nv = 0; m_loops = 0;
    pend = 0; music_on = 1;
}
void music_note(u8 ab, u8 c) {
    u8 a = ab >> 4, b = ab & 15;
    if (a) { u16 p = m_pp[a]; APU(0x02) = p & 255; APU(0x03) = 0x08 | (p >> 8); m_v1 = 12; }
    if (b && m_use2) { u16 p = m_pp[b]; APU(0x06) = p & 255; APU(0x07) = 0x08 | (p >> 8); m_v2 = 9; }
    if (c) { u16 p = m_tp[c]; APU(0x0A) = p & 255; APU(0x0B) = 0x08 | (p >> 8); APU(0x08) = 0xFF; m_tage = 0; m_tri = 1; }
}
void music_frame(void) {
    if (m_song == 1) { menu_frame(); return; }
    if (m_song == 2) { easter_frame(); return; }
    if (m_song == 3) { vic_frame(); return; }
    m_acc += m_rate;
    while (m_acc >= 256) {
        m_acc -= 256;
        if (m_wait) m_wait--;
        while (m_wait == 0) {
            music_note(m_ab[m_ev], m_c[m_ev]);
            if (++m_ev >= m_n) { m_ev = 0; m_unit = 0; m_loops++; }
            m_wait = m_d[m_ev];
        }
        /* drums on the noise channel: kick on the beat, snare on 2 and 4, a closed hat on the off-beats */
        switch (m_unit & 63) {
        case 0: case 32: APU(0x0E) = 0x0C; APU(0x0F) = 0x08; m_nv = 10; break;
        case 16: case 48: APU(0x0E) = 0x06; APU(0x0F) = 0x08; m_nv = 8; break;
        case 8: case 24: case 40: case 56: APU(0x0E) = 0x00; APU(0x0F) = 0x08; m_nv = 3; break;
        }
        m_unit++;
    }
    if (m_nv) m_nv--;
    APU(0x0C) = 0x30 | m_nv;
    m_cnt++;
    if (!(m_cnt & 3)) { if (m_v1 > 3) m_v1--; if (m_v2 > 2) m_v2--; }
    APU(0x00) = 0x70 | m_v1;        /* 25% duty, constant volume */
    if (m_use2) APU(0x04) = 0x70 | m_v2;
    if (m_tri && ++m_tage > 40) { APU(0x08) = 0x80; m_tri = 0; }
}
/* called from the frame waits: plays the frames that have passed (vblanks are counted by wait_vblank) */
void music_update(void) {
    while (pend) { pend--; if (music_on) music_frame(); }
}

/* ======================================================================== */
/*  Random numbers: the Turbo C library generator, as in the original       */
/* ======================================================================== */
u32 seed;
u16 rnd(void) {
    seed = seed * 0x015A4E35UL + 1;
    return (u16)((seed >> 16) & 0x7FFF);
}
i16 dice(i16 sides, i16 count) {
    i16 s = 0;
    while (count-- > 0) s += rnd() % sides + 1;
    return s;
}

/* ======================================================================== */
/*  Game data                                                               */
/* ======================================================================== */
const char* const drug_name[NUM_DRUGS] = {"Cocaine", "Crack", "Heroin", "Acid", "Crystal", "Grass", "Speed", "Ludes"};
const char* const gun_name[6] = {"None", "Sat. Nt. Spcl", "Ruger", "Auto Mag", "Uzi", "AK-47"};
const char* const gun_long[5] = {"Saturday Night Special", "Ruger Revolver", "Auto Mag", "Uzi", "AK-47 Assault Rifle"};
const char* const city_name[NUM_CITIES] = {"Chicago", "Detroit", "Las Vegas", "Los Angeles", "Miami", "New York", "San Diego", "Washington D.C."};
const char* const rank_name[11] = {"Loser!", "Dopehead", "Peddler", "Pusher", "Gangster", "Dealer", "Trafficker", "Racketeer", "Marketeer", "Entrepreneur", "DRUGLORD"};
const u16 price_lo[NUM_DRUGS] = {40000u, 25000u, 10000u, 2000, 600, 200, 70, 10};
const u16 price_hi[NUM_DRUGS] = {50000u, 30000u, 15000u, 3000, 1000, 400, 100, 20};
/* city price multipliers in percent (rows = city, columns = drug) */
const u8 mult_pct[NUM_CITIES][NUM_DRUGS] = {
    {95, 90, 90, 100, 105, 95, 110, 105},
    {110, 90, 95, 95, 110, 110, 100, 90},
    {100, 110, 90, 105, 105, 90, 90, 110},
    {110, 105, 95, 90, 105, 105, 90, 100},
    {90, 95, 110, 105, 90, 110, 105, 95},
    {90, 95, 110, 110, 95, 100, 100, 100},
    {105, 95, 100, 105, 90, 100, 95, 110},
    {95, 110, 105, 105, 95, 90, 110, 90}};
const u8 city_x[NUM_CITIES] = {3, 3, 1, 0, 5, 5, 0, 5};
const u8 city_y[NUM_CITIES] = {2, 1, 2, 3, 4, 2, 3, 2};
const u8 gun_dice[6] = {0, 1, 2, 4, 2, 4};
const u8 days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
const u32 rank_thr[10] = {250000UL, 500000UL, 600000UL, 700000UL, 800000UL, 850000UL, 900000UL, 950000UL, 1000000UL, 1100000UL};

/* ---- game state ---- */
i32 cash, bank, debt;
u8 city, month, day, year;
i16 hold, space;
i16 inv[NUM_DRUGS];
i32 basis[NUM_DRUGS], price[NUM_DRUGS];
i16 num_guns, gun_type, status, health;
u8 dead, loan_flag, angry, thugs, cop_flag, playing, mkt_today;
i16 loan_days;
u8 done[10];
i16 total_days, good_deals, total_deals;

/* ======================================================================== */
/*  Rules and formulas (integer versions)                                   */
/* ======================================================================== */

/* today's price of one drug: (random(lo..hi-1) + 1) * city multiplier.  The multiplier is
 * a float in the original, and 0.9, 0.95 and 1.05 are slightly low in float form, so a
 * product that comes out exactly whole loses one dollar; this reproduces that. */
i32 roll_price(u8 d) {
    i32 span = (i32)price_hi[d] - price_lo[d];
    i32 base = (i32)(rnd() % span) + price_lo[d] + 1;
    u8 pct = mult_pct[city][d];
    i32 p = base * pct;
    i32 r = p / 100;
    if (pct < 110 && pct != 100 && p % 100 == 0) r--;
    return r;
}
void gen_prices(void) {
    u8 d;
    for (d = 0; d < NUM_DRUGS; d++) price[d] = roll_price(d);
}
/* days the loan shark gives you:  ln(status*10000 / debt) / ln(1.2) + 4 */
i16 loan_term(i16 stat, i32 total) {
    i32 limit = (i32)stat * 10000L, t = total;
    i16 k = 0;
    for (;;) {
        t = (t * 6 + 4) / 5;
        if (t > limit) break;
        k++;
    }
    return k + 4;
}
void interest_one_day(void) {
    debt = debt + debt / 20;                /* x 1.05 */
    bank = bank + bank / 5208;              /* x 1.000192 */
}
void next_day(void) {
    loan_days--;
    day++; total_days++;
    if (days_in_month[month - 1] < day) {
        day = 1; month++;
        if (month > 12) { month = 1; year++; if (year == 100) year = 0; }
    }
    interest_one_day();
}
u8 sell_weight(u8 d) { return d < 3 ? (3 - d) * 3 : 1; }   /* cocaine 9, crack 6, heroin 3, rest 1 */
i16 status_from_sale(u8 d, i32 sell, i32 paid) {
    /* (sell / paid - 1) * 3 * weight, truncated toward zero */
    i32 w, num;
    if (paid <= 0) paid = sell;
    w = 3L * sell_weight(d);
    num = (sell - paid) * w;                /* sell, paid <= ~70000, w <= 27: fits */
    return (i16)(num / paid);
}
i32 new_basis(i32 old, i32 p) {
    if (old == 0) return p & 0xFFFF;
    return ((p + old) / 2) & 0xFFFF;
}

/* ======================================================================== */
/*  UI pieces                                                               */
/* ======================================================================== */
void say(const char* s);

void nl(void) {
    cx = wx0; cy++;
    if (cy > wy1) {
        ps_at(wx0, wy1, " Press A ");
        while (!(wait_btn() & BTN_A)) {}
        clear_rows(wy0, wy1);
        cy = wy0;
    }
}
/* word-wrapped text in the window; '\n' = new line */
void say(const char* s) {
    while (*s) {
        u8 len = 0;
        if (*s == '\n') { nl(); s++; continue; }
        if (*s == ' ') { if (cx > wx0) pc(' '); s++; continue; }
        while (s[len] && s[len] != ' ' && s[len] != '\n') len++;
        if (cx + len > wx1 + 1 && cx > wx0) nl();
        while (len--) pc((u8)*s++);
    }
}
void sayc(u8 col, const char* s) { ink = col; say(s); }
void say_num(i32 v) {
    u8 l;
    ltoa_s(v);
    for (l = 0; nb[l]; l++) {}
    if (cx + l > wx1 + 1) nl();
    ps(nb);
}
void say_money(i32 v) {
    u8 l;
    ltoa_s(v);
    for (l = 0; nb[l]; l++) {}
    if (cx + l + 1 > wx1 + 1) nl();
    pc('$'); ps(nb);
}
void press_a(void) {
    u8 e;
    ink = 3; ps_at(1, 27, "Press A to continue");
    do { e = wait_btn(); } while (!(e & BTN_A));
    fill(1, 27, ' ', 31);
}
/* full-screen message page: clears the screen, returns with the cursor at the top */
void page(void) {
    if (m_song == 1 || m_song == 2) music_stop();        /* the menu songs are for the main screen only (the victory tune goes on) */
    clear_screen();
    wx0 = 1; wx1 = 30; wy0 = 1; wy1 = 26;
    cx = 1; cy = 1;
}
u8 yn(void) {                /* A = yes, B = no */
    u8 e;
    ink = 1; ps_at(1, 26, "A = Yes     B = No");
    for (;;) {
        e = wait_btn();
        if (e & BTN_A) return 1;
        if (e & BTN_B) return 0;
    }
}

/* a health value in its colour: white at full health, then pink, light red, red and dark red in steps of 25%
 * (palette 0 colours 1-3, palette 2 colours 1-2; the digits in colours 2 and 3 are tiles 0x80-0x93).  w = the
 * width to pad to; dw = show 0 as "dead" */
void hp_put(i16 hp, i16 max, u8 w, u8 dw) {
    u8 lv, base, l;
    if (hp < 0) hp = 0;
    lv = hp >= max ? 4 : (u8)((hp * 4 + max / 2) / max);
    ink = lv >= 2 ? 0 : 2;
    base = (lv == 3 || lv == 0) ? 0x80 - '0' : lv == 2 ? 0x8A - '0' : 0;
    if (hp == 0 && dw) { pc(0x94); pc(0x95); pc(0x96); pc(0x94); l = 4; }
    else { ltoa_s(hp); for (l = 0; nb[l]; l++) pc(nb[l] + base); }
    ink = 0;
    while (l++ < w) pc(' ');
}

/* ---- main screen ---- */
/* the status block: labels in yellow (columns 1-7), values in white from column 8 (health in its colour); drug names
 * cyan, prices white */
static void label(u8 y, const char* s) { ink = 1; ps_at(1, y, s); ink = 0; at(8, y); }
void draw_status(void) {
    u8 i;
    space = hold;
    for (i = 0; i < NUM_DRUGS; i++) space -= inv[i];
    label(2, "Status:"); pn_l(status, 7); ink = 1; ps_at(16, 2, "Health:"); ink = 0; at(24, 2); hp_put(health, 100, 4, 0);
    label(3, "Date:");
    { u8 y = year; pc('0' + month / 10); pc('0' + month % 10); pc('/');
      pc('0' + day / 10); pc('0' + day % 10); pc('/'); pc('0' + y / 10); pc('0' + y % 10); }
    label(4, "City:"); ps_pad(city_name[city], 16);
    label(5, "Cash:"); pc('$'); pn_l(cash, 11);
    label(6, "Bank:"); pc('$'); pn_l(bank, 11);
    label(7, "Debt:"); pc('$'); pn_l(debt, 10);
    if (loan_flag == 1) { pc('('); pn(loan_days); pc(')'); ps("   "); }
    else ps("     ");
    label(8, "Hold:"); pn_l(space, 4); ps("("); pn(hold); ps(" max) ");
    label(9, "Guns:"); pn_l(num_guns, 3); ps_pad(gun_name[gun_type], 14);
    for (i = 0; i < NUM_DRUGS; i++) {
        ink = 3; at(2, ROW_DRUGS + i); ps_pad(drug_name[i], 8);
        ink = 0; at(10, ROW_DRUGS + i); pc('$'); pn_r(price[i], 6);
        ink = 0; at(17, ROW_DRUGS + i); pn_r(inv[i], 8);
    }
}
void draw_main(void) {
    u8 x;
    clear_screen();
    set_palette();
    ps_at(2, 11, "Drug       Price    Have");
    for (x = 0; x < 32; x++) put(x, 10, 0x97);          /* the double line, in white (palette 1 colour 3) */
    colour_rows(10, 11, 1);
    draw_status();
}

/* choose one entry of a list (rows y..y+n-1, cursor in column x); B = cancel (-1) */
i16 choose(u8 x, u8 y, u8 n, i16 start) {
    i16 s = start, o = start;
    u8 e, i;
    for (i = 0; i < n; i++) put(x, y + i, i == s ? 0x10 : ' ');
    for (;;) {
        e = wait_btn();
#ifdef DEBUG
        if ((pad_old & (BTN_SELECT | BTN_START)) == (BTN_SELECT | BTN_START)) {      /* debug build: SELECT+START */
            for (i = 0; i < n; i++) put(x, y + i, ' ');
            return -2;
        }
#endif
        o = s;
        if (e & BTN_UP) s = s ? s - 1 : n - 1;
        else if (e & BTN_DOWN) s = s + 1 < n ? s + 1 : 0;
        else if (e & BTN_A) { put(x, y + s, ' '); sfx(2); return s; }
        else if (e & BTN_B) { for (i = 0; i < n; i++) put(x, y + i, ' '); return -1; }
        if (o != s) { put2(x, y + o, ' ', x, y + s, 0x10); sfx(20); }
    }
}

/* number entry: UP/DOWN change the digit, LEFT/RIGHT move, SELECT = maximum,
 * A = done, B = cancel (-1).  Shows 'width' digits at (x,y). */
i32 pick_number(u8 x, u8 y, u8 width, i32 maxv) {
    u8 dig[10], pos = width - 1, e, i, op;
    i32 v;
    for (i = 0; i < width; i++) dig[i] = 0;
    for (i = 0; i < width; i++) put(x + i, y, (u8)('0' + dig[i]) | (i == pos ? 0x80 : 0));
    for (;;) {
        e = wait_btn();
        op = pos;
        if (e & BTN_UP) dig[pos] = dig[pos] == 9 ? 0 : dig[pos] + 1;
        else if (e & BTN_DOWN) dig[pos] = dig[pos] == 0 ? 9 : dig[pos] - 1;
        else if (e & BTN_LEFT) { if (pos) pos--; }
        else if (e & BTN_RIGHT) { if (pos < width - 1) pos++; }
        else if (e & BTN_SELECT) {
            if (maxv >= 0) {
                v = maxv;
                for (i = width; i--; ) { dig[i] = (u8)(v % 10); v /= 10; }
                for (i = 0; i < width; i++) put(x + i, y, (u8)('0' + dig[i]) | (i == pos ? 0x80 : 0));
            }
            continue;
        } else if (e & BTN_A) {
            v = 0;
            for (i = 0; i < width; i++) v = v * 10 + dig[i];
            return v;
        } else if (e & BTN_B) return -1;
        put2(x + op, y, '0' + dig[op], x + pos, y, (u8)('0' + dig[pos]) | 0x80);
        sfx(20);
    }
}
void msg_area(void) { clear_rows(20, 27); wx0 = 1; wx1 = 30; wy0 = 21; wy1 = 27; cx = 1; cy = 21; }
void msg_wait(void) { u8 e; ppu_show(); do { e = wait_btn(); } while (!(e & (BTN_A | BTN_B))); }


/* one session: play games until the player does not want another */
void game(void) {
    for (;;) {                                           /* one pass = one game */
        playing = 1;
        init_game();
        draw_main();
        msg_area();
        for (;;) {
            i16 sel;
            u8 adv = 0;
            draw_status();
            if (status >= WIN_STATUS) break;
            if (!music_on) menu_music_start();
            msg_area();
            ink = 3; ps_at(2, 21, "Buy drugs");
            ps_at(2, 22, "Sell drugs");
            ps_at(2, 23, "Jet to another city");
            ps_at(2, 24, "Visit de Bank");
            ps_at(2, 25, "See de Loan Shark");
            ps_at(2, 26, "Go to de Hospital");
            ps_at(2, 27, "Quit de game");
            ink = 0;
            sel = choose(0, 21, 7, 0);
#ifdef DEBUG
            if (sel == -2) { debug_menu(); draw_main(); continue; }
#endif
            if (sel < 0) continue;
            switch (sel) {
            case 0: adv = menu_buy(); break;
            case 1: adv = menu_sell(); break;
            case 2: adv = menu_jet(); break;
            case 3: adv = menu_bank(); break;
            case 4: adv = menu_loan(); break;
            case 5: adv = menu_hospital(); break;
            case 6: adv = menu_quit(); break;
            }
            if (!adv) continue;
            /* ---- a day passes ---- */
            next_day();
            draw_status();
#ifdef TEST_EVENT
            done[TEST_EVENT] = 0; do_event(TEST_EVENT);
#endif
            if (dead || !playing) break;
            if (rnd() % 10 == 0 && rnd() % (status / 20 + 1) > 4) {
                if (rnd() % 2 == 0) do_event(7); else do_event(8);
            }
            if (cop_flag == 1 || (status >= 100 && rnd() % 400 < status && rnd() % 8 == 0)) do_event(0);
            if (dead || !playing) break;
            if (rnd() % 5 == 0) do_event((u8)(rnd() % 10));
            if (dead || !playing) break;
            if (loan_days < 0 && loan_flag == 1) thugs_scene();
            if (dead || !playing) break;
        }
        music_stop();
        if (playing) {
            if (dead) show_death(); else show_win();
        }
        if (!ask_play_again()) break;
    }
}


extern const u8 NESfont[];                              /* the font lives in PRG bank 2 and is copied to CHR-RAM */
void __fastcall__ bank_set(u8 b);
u8 pt1_set;                  /* which 4 KB set is in pattern table 1: 1 = the title, 2 = the end scene */
void load_font(void) {
    const u8* p = NESfont;
    u16 n;
    bank_set(2);
    latch = PPUSTATUS; PPUADDR = 0; PPUADDR = 0;
    for (n = 0; n < 8192; n++) PPUDATA = *p++;
    bank_set(0);
    pt1_set = 1;
}
/* copy n bytes of data kept in PRG bank 2 to the PPU (at the address already set; rendering off) */
void ppu_write2(const u8* src, u8 n) {
    u8 b = curbank;
    bank_set(2);
    while (n--) PPUDATA = *src++;
    bank_set(b);
}
/* copy one of the 4 KB tile sets of PRG bank 2 into pattern table 1 (rendering must be off) */
void load_pt1(u8 set) {
    const u8* p = NESfont + (u16)set * 4096;
    u16 n;
    u8 b = curbank;                                  /* the caller may live in a switchable bank */
    if (pt1_set == set) return;
    ppu_off();
    bank_set(2);
    latch = PPUSTATUS; PPUADDR = 0x10; PPUADDR = 0;
    for (n = 0; n < 4096; n++) PPUDATA = *p++;
    bank_set(b);
    pt1_set = set;
}

void main(void) {
    PPUCTRL = 0x80; PPUMASK = 0;           /* NMI on from the start (wait_vblank counts on it) */
    load_font();
    wait_vblank(); wait_vblank();
    hs_init();
    credits_screen();
    for (;;) {                                           /* credits once, then title -> game -> notice -> title ... */
        title_wait();
        seed = ((u32)frames << 8) ^ 0x2A5Bu ^ (u32)frames * 977UL;
        game();
        exit_screen();
        hs_flash();
    }
}
