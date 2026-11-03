/* DRUGLORD for the NES - a separate port, NOT part of the byte-matched c/ folder.
 *
 * Written for cc65 (mapper 0 / NROM, 32 KB PRG, 8 KB CHR).  The rules, odds and
 * formulas are those of the DOS original (see "GAME RULES AND FORMULAS" in c/DRUGLORD.C);
 * the NES has no floating point, so every formula is redone with integers, in a way that
 * gives the same numbers.  The screen is 32 x 30 tiles of text, so the layout and the
 * input (a controller instead of a keyboard) are different from the PC version.
 *
 * Tiles: 0x20-0x7E are ASCII, 1-6 box pieces, 7 a shade; tile + 0x80 is the same glyph
 * inverted (the highlight).  See tools/mkdata.py.
 */
#include <stdint.h>
#include "music.h"
#include "tables.h"

typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;

#define PPUCTRL   (*(volatile u8*)0x2000)
#define PPUMASK   (*(volatile u8*)0x2001)
#define PPUSTATUS (*(volatile u8*)0x2002)
#define PPUSCROLL (*(volatile u8*)0x2005)
#define PPUADDR   (*(volatile u8*)0x2006)
#define PPUDATA   (*(volatile u8*)0x2007)
#define APU(a)    (*(volatile u8*)(0x4000+(a)))
#define JOY1      (*(volatile u8*)0x4016)

#define BTN_A 0x80
#define BTN_B 0x40
#define BTN_SELECT 0x20
#define BTN_START 0x10
#define BTN_UP 0x08
#define BTN_DOWN 0x04
#define BTN_LEFT 0x02
#define BTN_RIGHT 0x01

#define NUM_DRUGS 8
#define NUM_CITIES 8
#define WIN_STATUS 300

/* ======================================================================== */
/*  PPU / text                                                              */
/* ======================================================================== */
static volatile u8 latch;       /* a real read of $2002 resets the PPUADDR write latch */
static u8 ppu_on;               /* rendering is switched off lazily while we draw */
static u8 cx, cy;               /* text cursor */
static u8 wx0 = 1, wx1 = 30;    /* word-wrap window (columns) */
static u8 wy0 = 1, wy1 = 27;    /* rows used by wrapped text */

static u8 pend;
static void wait_vblank(void) { while (!(PPUSTATUS & 0x80)) {} if (pend < 6) pend++; }
static void music_update(void);
static void ppu_off(void) { if (ppu_on) { wait_vblank(); PPUMASK = 0; ppu_on = 0; } }
static void ppu_show(void) {
    if (!ppu_on) {
        wait_vblank();
        PPUCTRL = 0; latch = PPUSTATUS; PPUSCROLL = 0; PPUSCROLL = 0;
        PPUMASK = 0x0A; ppu_on = 1;
    }
}
static void ppu_at(u8 x, u8 y) {
    u16 a = 0x2000 + (u16)y * 32 + x;
    latch = PPUSTATUS; PPUADDR = a >> 8; PPUADDR = a & 255;
}
static void put(u8 x, u8 y, u8 ch) { ppu_off(); ppu_at(x, y); PPUDATA = ch; }
/* write two tiles during vblank without blanking the screen (used for cursors) */
static void put2(u8 x1, u8 y1, u8 c1, u8 x2, u8 y2, u8 c2) {
    if (ppu_on) wait_vblank();
    ppu_at(x1, y1); PPUDATA = c1;
    ppu_at(x2, y2); PPUDATA = c2;
    if (ppu_on) { PPUCTRL = 0; PPUSCROLL = 0; PPUSCROLL = 0; }
}
static void fill(u8 x, u8 y, u8 ch, u8 n) {
    ppu_off(); ppu_at(x, y);
    while (n--) PPUDATA = ch;
}
static void clear_rows(u8 y0, u8 y1) {      /* inclusive */
    u8 y;
    for (y = y0; y <= y1; y++) fill(0, y, ' ', 32);
}
static void set_palette(void) {
    static const u8 pal[16] = {0x0F, 0x30, 0x10, 0x00,  0x0F, 0x28, 0x18, 0x00,
                               0x0F, 0x16, 0x06, 0x00,  0x0F, 0x2A, 0x1A, 0x00};
    u8 i;
    ppu_off();
    latch = PPUSTATUS; PPUADDR = 0x3F; PPUADDR = 0x00;
    for (i = 0; i < 32; i++) PPUDATA = pal[i & 15];
}
static u8 attr[64];
static void clear_screen(void) {
    u16 i;
    ppu_off(); ppu_at(0, 0);
    for (i = 0; i < 1024; i++) PPUDATA = 0;
    for (i = 0; i < 64; i++) attr[i] = 0;
    cx = 1; cy = 1;
}
/* colour a band of rows: pal 0 white, 1 yellow, 2 red, 3 green.  An attribute byte covers
 * 4x4 tiles, two bits per 2x2 block, so colours change every two rows (y0 even, y1 odd). */
static void colour_cols(u8 y0, u8 y1, u8 bx0, u8 bx1, u8 pal) {    /* bx = 16-pixel column blocks 0..15 */
    u8 r, x, i;
    for (r = y0 >> 1; r <= (y1 >> 1); r++) {
        u8 mask = (r & 1) ? 0xF0 : 0x0F;
        u8 v = (pal | (pal << 2) | (pal << 4) | (pal << 6)) & mask;
        for (x = bx0 >> 1; x <= (bx1 >> 1); x++) { i = (r >> 1) * 8 + x; attr[i] = (attr[i] & ~mask) | v; }
    }
    ppu_off();
    latch = PPUSTATUS; PPUADDR = 0x23; PPUADDR = 0xC0;
    for (i = 0; i < 64; i++) PPUDATA = attr[i];
}
static void colour_rows(u8 y0, u8 y1, u8 pal) { colour_cols(y0, y1, 0, 15, pal); }

/* ---- printing ---- */
static void pc(u8 ch) { put(cx, cy, ch); cx++; }
static void at(u8 x, u8 y) { cx = x; cy = y; }
static void ps(const char* s) { while (*s) pc((u8)*s++); }
static void ps_at(u8 x, u8 y, const char* s) { at(x, y); ps(s); }
static char nb[12];
static void ltoa_s(i32 v) {         /* decimal into nb */
    char t[12]; u8 n = 0, i = 0;
    u32 u;
    if (v < 0) { nb[i++] = '-'; u = (u32)(-v); } else u = (u32)v;
    if (u == 0) t[n++] = '0';
    while (u) { t[n++] = '0' + (u % 10); u /= 10; }
    while (n) nb[i++] = t[--n];
    nb[i] = 0;
}
static void pn(i32 v) { ltoa_s(v); ps(nb); }
static void pn_r(i32 v, u8 w) {     /* right-aligned in w columns */
    u8 l;
    ltoa_s(v);
    for (l = 0; nb[l]; l++) {}
    while (l++ < w) pc(' ');
    ps(nb);
}
static void pn_l(i32 v, u8 w) {     /* left-aligned, padded with blanks to w columns */
    u8 l;
    ltoa_s(v);
    for (l = 0; nb[l]; l++) {}
    ps(nb);
    while (l++ < w) pc(' ');
}
static void ps_pad(const char* s, u8 w) {
    u8 l = 0;
    while (*s) { pc((u8)*s++); l++; }
    while (l++ < w) pc(' ');
}

/* ======================================================================== */
/*  Controller, frames, sound                                               */
/* ======================================================================== */
static u8 pad_old;
static u16 frames;
static u8 read_pad(void) {
    u8 i, v = 0;
    JOY1 = 1; JOY1 = 0;
    for (i = 0; i < 8; i++) v = (v << 1) | (JOY1 & 1);
    return v;
}
/* one frame; returns the buttons that went down this frame */
static u8 vsync_pad(void) {
    u8 p;
    wait_vblank(); frames++;
    music_update();
    p = read_pad();
    { u8 e = p & ~pad_old; pad_old = p; return e; }
}
static void delay(u8 n) {           /* n frames; holding A skips the wait */
    ppu_show();
    while (n--) { vsync_pad(); if (pad_old & BTN_A) break; }
}
static u8 wait_btn(void) {          /* wait for any new button, return it */
    u8 e;
    ppu_show();
    for (;;) { e = vsync_pad(); if (e) return e; }
}

/* ---- sound effects (pulse 2 + noise, played while waiting) ---- */
static void snd(u16 per, u8 n) {
    APU(0x04) = 0xBC; APU(0x06) = per & 255; APU(0x07) = 0x08 | (per >> 8);
    ppu_show();
    while (n--) { wait_vblank(); music_update(); }
    APU(0x04) = 0xB0;
}
static void noi(u8 idx, u8 vol, u8 n) {
    APU(0x0C) = 0x30 | vol; APU(0x0E) = idx; APU(0x0F) = 0x08;
    ppu_show();
    while (n--) { wait_vblank(); music_update(); if (vol) APU(0x0C) = 0x30 | --vol; }
    APU(0x0C) = 0x30;
}
#define HZ(f) ((u16)(111860UL / (f)))
static void sfx(u8 n) {
    u8 i;
    switch (n) {
    case 2: snd(HZ(1000), 4); break;                                    /* beep */
    case 3: snd(HZ(400), 6); snd(HZ(200), 10); break;                   /* denied */
    case 4: snd(HZ(500), 3); snd(HZ(750), 3); snd(HZ(1000), 4); break;  /* deal */
    case 5: for (i = 0; i < 4; i++) { snd(HZ(900), 8); snd(HZ(600), 8); } break;   /* siren */
    case 6: for (i = 0; i < 10; i++) snd(HZ(400 + i * 120), 2); break;  /* news */
    case 7: for (i = 0; i < 8; i++) snd(HZ(i & 1 ? 1200 : 800), 3); break;         /* extra */
    case 8: noi(4, 12, 6); break;                                       /* hit */
    case 11: case 12: case 13: case 14: case 15:
            noi(15 - (n - 10) * 2, 15, 8 + (n - 10) * 2); break;        /* gun bang */
    case 20: snd(HZ(2000), 1); break;                                   /* click */
    }
}

/* ---- music: the title tune from tools/title.mid, played on pulse 1 / pulse 2 / triangle.  Each tune is a list of
 *      events (wait, pulse-1 and pulse-2 note nibbles, triangle note). ---- */
static const u8* m_d; static const u8* m_ab; static const u8* m_c;
static const u16* m_pp; static const u16* m_tp;
static u16 m_n, m_rate, m_ev, m_acc;
static u8 m_wait, m_v1, m_v2, m_tri, m_tage, m_cnt, m_use2, music_on;
static void music_stop(void) {
    music_on = 0;
    APU(0x00) = 0x30; APU(0x04) = 0x30; APU(0x08) = 0x80; APU(0x0C) = 0x30;
}
static void music_start(void) {
    APU(0x15) = 0x0F;               /* pulse 1, pulse 2, triangle, noise */
    APU(0x01) = 0x08; APU(0x05) = 0x08;
    m_d = title_d; m_ab = title_ab; m_c = title_c; m_pp = title_pp; m_tp = title_tp; m_n = TITLE_EVENTS; m_rate = TITLE_RATE; m_use2 = 1;
    m_ev = 0; m_acc = 0; m_wait = m_d[0]; m_v1 = m_v2 = 0; m_tri = 0; m_tage = 0; m_cnt = 0;
    pend = 0; music_on = 1;
}
static void music_note(u8 ab, u8 c) {
    u8 a = ab >> 4, b = ab & 15;
    if (a) { u16 p = m_pp[a]; APU(0x02) = p & 255; APU(0x03) = 0x08 | (p >> 8); m_v1 = 12; }
    if (b && m_use2) { u16 p = m_pp[b]; APU(0x06) = p & 255; APU(0x07) = 0x08 | (p >> 8); m_v2 = 9; }
    if (c) { u16 p = m_tp[c]; APU(0x0A) = p & 255; APU(0x0B) = 0x08 | (p >> 8); APU(0x08) = 0xFF; m_tage = 0; m_tri = 1; }
}
static void music_frame(void) {
    m_acc += m_rate;
    while (m_acc >= 256) {
        m_acc -= 256;
        if (m_wait) m_wait--;
        while (m_wait == 0) {
            music_note(m_ab[m_ev], m_c[m_ev]);
            if (++m_ev >= m_n) m_ev = 0;
            m_wait = m_d[m_ev];
        }
    }
    m_cnt++;
    if (!(m_cnt & 3)) { if (m_v1 > 3) m_v1--; if (m_v2 > 2) m_v2--; }
    APU(0x00) = 0x70 | m_v1;        /* 25% duty, constant volume */
    if (m_use2) APU(0x04) = 0x70 | m_v2;
    if (m_tri && ++m_tage > 40) { APU(0x08) = 0x80; m_tri = 0; }
}
/* called from the frame waits: plays the frames that have passed (vblanks are counted by wait_vblank) */
static void music_update(void) {
    while (pend) { pend--; if (music_on) music_frame(); }
}

/* ======================================================================== */
/*  Random numbers: the Turbo C library generator, as in the original       */
/* ======================================================================== */
static u32 seed;
static u16 rnd(void) {
    seed = seed * 0x015A4E35UL + 1;
    return (u16)((seed >> 16) & 0x7FFF);
}
static i16 dice(i16 sides, i16 count) {
    i16 s = 0;
    while (count-- > 0) s += rnd() % sides + 1;
    return s;
}

/* ======================================================================== */
/*  Game data                                                               */
/* ======================================================================== */
static const char* const drug_name[NUM_DRUGS] = {"Cocaine", "Crack", "Heroin", "Acid", "Crystal", "Grass", "Speed", "Ludes"};
static const char* const gun_name[6] = {"None", "Sat. Nt. Spcl", "Ruger", "Auto Mag", "Uzi", "AK-47"};
static const char* const gun_long[5] = {"Saturday Night Special", "Ruger Revolver", "Auto Mag", "Uzi", "AK-47 Assault Rifle"};
static const char* const city_name[NUM_CITIES] = {"Chicago", "Detroit", "Las Vegas", "Los Angeles", "Miami", "New York", "San Diego", "Washington D.C."};
static const char* const rank_name[11] = {"Loser!", "Dopehead", "Peddler", "Pusher", "Gangster", "Dealer", "Trafficker", "Racketeer", "Marketeer", "Entrepreneur", "DRUGLORD"};
static const u16 price_lo[NUM_DRUGS] = {40000u, 25000u, 10000u, 2000, 600, 200, 70, 10};
static const u16 price_hi[NUM_DRUGS] = {50000u, 30000u, 15000u, 3000, 1000, 400, 100, 20};
/* city price multipliers in percent (rows = city, columns = drug) */
static const u8 mult_pct[NUM_CITIES][NUM_DRUGS] = {
    {95, 90, 90, 100, 105, 95, 110, 105},
    {110, 90, 95, 95, 110, 110, 100, 90},
    {100, 110, 90, 105, 105, 90, 90, 110},
    {110, 105, 95, 90, 105, 105, 90, 100},
    {90, 95, 110, 105, 90, 110, 105, 95},
    {90, 95, 110, 110, 95, 100, 100, 100},
    {105, 95, 100, 105, 90, 100, 95, 110},
    {95, 110, 105, 105, 95, 90, 110, 90}};
static const u8 city_x[NUM_CITIES] = {3, 3, 1, 0, 5, 5, 0, 5};
static const u8 city_y[NUM_CITIES] = {2, 1, 2, 3, 4, 2, 3, 2};
static const u8 gun_dice[6] = {0, 1, 2, 4, 2, 4};
static const u8 days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const u32 rank_thr[10] = {250000UL, 500000UL, 600000UL, 700000UL, 800000UL, 850000UL, 900000UL, 950000UL, 1000000UL, 1100000UL};

/* ---- game state ---- */
static i32 cash, bank, debt;
static u8 city, month, day, year;
static i16 hold, space;
static i16 inv[NUM_DRUGS];
static i32 basis[NUM_DRUGS], price[NUM_DRUGS];
static i16 num_guns, gun_type, status, health;
static u8 dead, loan_flag, angry, thugs, cop_flag, playing, mkt_today;
static i16 loan_days;
static u8 done[10];
static i16 total_days, good_deals, total_deals;

/* ======================================================================== */
/*  Rules and formulas (integer versions)                                   */
/* ======================================================================== */

/* today's price of one drug: (random(lo..hi-1) + 1) * city multiplier.  The multiplier is
 * a float in the original, and 0.9, 0.95 and 1.05 are slightly low in float form, so a
 * product that comes out exactly whole loses one dollar; this reproduces that. */
static i32 roll_price(u8 d) {
    i32 span = (i32)price_hi[d] - price_lo[d];
    i32 base = (i32)(rnd() % span) + price_lo[d] + 1;
    u8 pct = mult_pct[city][d];
    i32 p = base * pct;
    i32 r = p / 100;
    if (pct < 110 && pct != 100 && p % 100 == 0) r--;
    return r;
}
static void gen_prices(void) {
    u8 d;
    for (d = 0; d < NUM_DRUGS; d++) price[d] = roll_price(d);
}
static u16 isqrt32(u32 n) {
    u16 r = 0;
    while ((u32)(r + 1) * (r + 1) <= n) r++;
    return r;
}
/* air fare = distance on the map * 60 + 25 */
static i32 travel_cost(u8 a, u8 b) {
    i16 dx = (i16)city_x[a] - city_x[b], dy = (i16)city_y[a] - city_y[b];
    return (i32)isqrt32(3600UL * (dx * dx + dy * dy)) + 25;
}
/* days the loan shark gives you:  ln(status*10000 / debt) / ln(1.2) + 4 */
static i16 loan_term(i16 stat, i32 total) {
    i32 limit = (i32)stat * 10000L, t = total;
    i16 k = 0;
    for (;;) {
        t = (t * 6 + 4) / 5;
        if (t > limit) break;
        k++;
    }
    return k + 4;
}
static void interest_one_day(void) {
    debt = debt + debt / 20;                /* x 1.05 */
    bank = bank + bank / 5208;              /* x 1.000192 */
}
static void next_day(void) {
    loan_days--;
    day++; total_days++;
    if (days_in_month[month - 1] < day) {
        day = 1; month++;
        if (month > 12) { month = 1; year++; if (year == 100) year = 0; }
    }
    interest_one_day();
}
static u8 sell_weight(u8 d) { return d < 3 ? (3 - d) * 3 : 1; }   /* cocaine 9, crack 6, heroin 3, rest 1 */
static i16 status_from_sale(u8 d, i32 sell, i32 paid) {
    /* (sell / paid - 1) * 3 * weight, truncated toward zero */
    i32 w, num;
    if (paid <= 0) paid = sell;
    w = 3L * sell_weight(d);
    num = (sell - paid) * w;                /* sell, paid <= ~70000, w <= 27: fits */
    return (i16)(num / paid);
}
static i32 new_basis(i32 old, i32 p) {
    if (old == 0) return p & 0xFFFF;
    return ((p + old) / 2) & 0xFFFF;
}

/* ======================================================================== */
/*  UI pieces                                                               */
/* ======================================================================== */
static void say(const char* s);

static void nl(void) {
    cx = wx0; cy++;
    if (cy > wy1) {
        ps_at(wx0, wy1, " Press A ");
        while (!(wait_btn() & BTN_A)) {}
        clear_rows(wy0, wy1);
        cy = wy0;
    }
}
/* word-wrapped text in the window; '\n' = new line */
static void say(const char* s) {
    while (*s) {
        u8 len = 0;
        if (*s == '\n') { nl(); s++; continue; }
        if (*s == ' ') { if (cx > wx0) pc(' '); s++; continue; }
        while (s[len] && s[len] != ' ' && s[len] != '\n') len++;
        if (cx + len > wx1 + 1 && cx > wx0) nl();
        while (len--) pc((u8)*s++);
    }
}
static void say_num(i32 v) {
    u8 l;
    ltoa_s(v);
    for (l = 0; nb[l]; l++) {}
    if (cx + l > wx1 + 1) nl();
    ps(nb);
}
static void say_money(i32 v) {
    u8 l;
    ltoa_s(v);
    for (l = 0; nb[l]; l++) {}
    if (cx + l + 1 > wx1 + 1) nl();
    pc('$'); ps(nb);
}
static void press_a(void) {
    u8 e;
    ps_at(1, 27, "Press A to continue");
    do { e = wait_btn(); } while (!(e & BTN_A));
    fill(1, 27, ' ', 31);
}
/* full-screen message page: clears the screen, returns with the cursor at the top */
static void page(void) {
    clear_screen();
    wx0 = 1; wx1 = 30; wy0 = 1; wy1 = 26;
    cx = 1; cy = 1;
}
static u8 yn(void) {                /* A = yes, B = no */
    u8 e;
    ps_at(1, 26, "A = Yes     B = No");
    for (;;) {
        e = wait_btn();
        if (e & BTN_A) return 1;
        if (e & BTN_B) return 0;
    }
}

/* ---- main screen ---- */
#define ROW_DRUGS 12
static void draw_status(void) {
    u8 i;
    space = hold;
    for (i = 0; i < NUM_DRUGS; i++) space -= inv[i];
    ps_at(1, 3, "Date: ");
    { u8 y = year; pc('0' + month / 10); pc('0' + month % 10); pc('/');
      pc('0' + day / 10); pc('0' + day % 10); pc('/'); pc('0' + y / 10); pc('0' + y % 10); }
    at(16, 3); ps_pad(city_name[city], 15);
    ps_at(1, 4, "Cash:  $"); pn_l(cash, 11);
    ps_at(1, 5, "Bank:  $"); pn_l(bank, 11);
    ps_at(1, 6, "Debt:  $"); pn_l(debt, 10);
    if (loan_flag == 1) { pc('('); pn(loan_days); pc(')'); ps("   "); }
    else ps("     ");
    ps_at(1, 7, "Hold: "); pn_l(space, 4); ps("("); pn(hold); ps(" max) ");
    ps_at(1, 8, "Guns: "); pn_l(num_guns, 4); ps_pad(gun_name[gun_type], 14);
    ps_at(1, 9, "Status: "); pn_l(status, 6); ps("Health: "); pn_l(health, 4);
    for (i = 0; i < NUM_DRUGS; i++) {
        at(2, ROW_DRUGS + i); ps_pad(drug_name[i], 8);
        at(10, ROW_DRUGS + i); pc('$'); pn_r(price[i], 6);
        ps("   ");
        at(20, ROW_DRUGS + i); pn_r(inv[i], 5);
    }
}
static void draw_main(void) {
    u8 x;
    clear_screen();
    set_palette();
    ps_at(2, 1, "* * *  D R U G L O R D  * * *");
    ps_at(2, 11, "Drug       Price    Have");
    for (x = 0; x < 28; x++) put(2 + x, 10, 2);
    colour_rows(0, 1, 2);
    colour_rows(10, 11, 1);
    draw_status();
}

/* choose one entry of a list (rows y..y+n-1, cursor in column x); B = cancel (-1) */
static i16 choose(u8 x, u8 y, u8 n, i16 start) {
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
        else if (e & BTN_A) { put(x, y + s, ' '); return s; }
        else if (e & BTN_B) { for (i = 0; i < n; i++) put(x, y + i, ' '); return -1; }
        if (o != s) put2(x, y + o, ' ', x, y + s, 0x10);
    }
}

/* number entry: UP/DOWN change the digit, LEFT/RIGHT move, SELECT = maximum,
 * A = done, B = cancel (-1).  Shows 'width' digits at (x,y). */
static i32 pick_number(u8 x, u8 y, u8 width, i32 maxv) {
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
    }
}
static void msg_area(void) { clear_rows(20, 27); wx0 = 1; wx1 = 30; wy0 = 21; wy1 = 27; cx = 1; cy = 21; }
static void msg_wait(void) { u8 e; ppu_show(); do { e = wait_btn(); } while (!(e & (BTN_A | BTN_B))); }

/* ======================================================================== */
/*  Cop fight                                                               */
/* ======================================================================== */
static i16 php[10], chp[12];
static void fight_row(u8 side, u8 i, i16 hp) {
    u8 x = side ? 17 : 1, y = 4 + i;
    at(x, y);
    if (!side) { if (i == 0) ps_pad("You", 8); else { ps("Mate "); pn_l(i, 3); } }
    else { ps("Cop "); pn_l(i + 1, 4); }
    at(x + 9, y);
    if (hp > 0) pn_l(hp, 4); else ps("dead");
}
static void fight_msg(const char* s) { fill(1, 22, ' ', 30); fill(1, 23, ' ', 30); at(1, 22); ps(s); }

static void cop_fight(void) {
    i16 choice, nf = 0, nc, ct, shots = 1, i, t, copsAlive, partyAlive, result = 0, att, si;
    u8 k;
    cop_flag = 0;
    if (gun_type >= 4) shots = 3;
    page();
    ps_at(1, 1, "The cops are on your tail!!!");
    sfx(5);
    ps_at(1, 3, "Do you wanna");
    ps_at(3, 4, "Go at it alone");
    ps_at(3, 5, "Call a friend (1 status pt.)");
    ps_at(3, 6, "Call a posse (5 status pts.)");
    for (;;) {
        choice = choose(1, 4, 3, 0);
        if (choice < 0) continue;
        choice++;
        if ((choice == 2 && status == 0) || (choice == 3 && status < 5)) {
            ps_at(1, 8, "You don't have enough points!");
            sfx(3); delay(90);
            fill(1, 8, ' ', 30);
            continue;
        }
        break;
    }
    if (choice == 2) { nf = 1; status--; }
    else if (choice == 3) {
        nf = rnd() % 4 + status / 100 + 4;
        if (nf > 8) nf = 8;
        status -= 5;
    }
    clear_screen();
    ps_at(2, 1, "* * *  D R U G L O R D  * * *");
    ps_at(1, 3, "YOU"); ps_at(17, 3, "COPS");
    php[0] = health;
    for (i = 1; i <= nf; i++) php[i] = rnd() % 10 + dice(10, status / 50) + 25;
    /* how many cops and what gun they carry (re-rolled until the gun type is 2 or 3) */
    for (;;) {
        nc = rnd() % 8 + 3;
        ct = status / (nc * 2);
        if (rnd() % 20 == 0) {
            if (ct < 2) {
                ct = status / (rnd() % 4 + 1);
                if (ct > 3) ct = 3;
                if (ct < 2) ct = 2;
            } else {
                nc = rnd() % 3 + 8;
                ct = 3;
            }
            break;
        }
        if (ct >= 2 && ct <= 3) break;
    }
    for (i = 0; i < nc; i++) chp[i] = dice(15, 2) + 15;
    copsAlive = nc; partyAlive = nf + 1;
    for (i = 0; i <= nf; i++) fight_row(0, i, php[i]);
    for (i = 0; i < nc; i++) fight_row(1, i, chp[i]);
    at(1, 16); ps("Guns: "); pn(num_guns); ps(" "); ps(gun_name[gun_type]);
    at(1, 17); ps("Cops: "); ps(gun_name[ct]);
    for (;;) {
        fill(1, 20, ' ', 30);
        ps_at(1, 20, "(F)ight or (R)un?  A=F B=R");
        for (;;) { k = wait_btn(); if (k & (BTN_A | BTN_B)) break; }
        if (k & BTN_B) {                                 /* run */
            fight_msg("You take off running...");
            delay(60);
            if (rnd() % 10 + nc / 3 < rnd() % 10 + nf / 3) {
                fight_msg("and ya got away!!!"); health = php[0]; result = 1;
            } else { fight_msg("and ya didn't get away!!!"); result = 2; k = BTN_A; }
            delay(60);
        }
        if (k & BTN_A) {                                 /* fight */
            if (result != 2) {
                for (si = 0; si <= nf; si++) {
                    if (php[si] == 0) continue;
                    att = 0;
                    for (;;) {
                        do { t = rnd() % nc; } while (chp[t] == 0);
                        sfx(gun_type + 10);
                        fill(1, 22, ' ', 30); at(1, 22);
                        ps(si < num_guns ? "Firing at cop " : "Punching cop "); pn(t + 1);
                        if (rnd() % 2 == 0) {
                            ps(" *Hit!*"); sfx(8);
                            chp[t] -= (si < num_guns) ? dice(6, gun_dice[gun_type]) : dice(4, 1);
                            if (chp[t] <= 0) { chp[t] = 0; copsAlive--; }
                        } else ps(" -Missed-");
                        fight_row(1, t, chp[t]);
                        delay(14);
                        att++;
                        if (php[0] != 0 && copsAlive != 0 && gun_type > 3 && att < shots && si < num_guns) continue;
                        break;
                    }
                    if (copsAlive == 0) break;
                }
            }
            if (copsAlive != 0) {
                result = 0;
                for (si = 0; si < nc; si++) {
                    if (chp[si] == 0) continue;
                    do { t = rnd() % (nf + 1); } while (php[t] == 0);
                    sfx(ct + 10);
                    fill(1, 22, ' ', 30); at(1, 22);
                    ps("Cop "); pn(si + 1); ps(" shoots at "); if (t == 0) ps("you"); else { ps("mate "); pn(t); }
                    if (rnd() % 2 == 0) {
                        ps(" *Hit!*"); sfx(8);
                        php[t] -= dice(6, gun_dice[ct]);
                        if (php[t] <= 0) { status--; php[t] = 0; partyAlive--; }
                    } else ps(" -Missed-");
                    fight_row(0, t, php[t]);
                    delay(14);
                    if (php[0] == 0 || partyAlive == 0) break;
                }
            }
        }
        if (!(copsAlive > 0 && partyAlive > 0 && php[0] > 0 && result != 1)) break;
    }
    if (result == 1) { delay(60); return; }
    health = php[0];
    if (health == 0) { dead = 1; return; }
    fight_msg("Yous wasted 'em!!! Way ta go, man!!!");
    delay(60);
    {
        u8 sell = 1;
        if (!(ct < gun_type && nc <= num_guns)) {
            fill(1, 24, ' ', 30);
            ps_at(1, 24, nc == 1 ? "Take his gun, man? (Y/N)" : "Take their guns, man? (Y/N)");
            ps_at(1, 26, "A = Yes     B = No");
            for (;;) { k = wait_btn(); if (k & (BTN_A | BTN_B)) break; }
            if (k & BTN_A) {
                fight_msg("You want 'em, you got 'em! What a bunch'a chumps!");
                sell = 0;
                if (gun_type == ct) num_guns += nc; else { gun_type = ct; num_guns = nc; }
            } else { fight_msg("OK, man, it's yer call."); delay(90); }
        }
        if (sell) {
            i32 gold = (i32)nc * ct * 200;
            fill(1, 22, ' ', 30); fill(1, 23, ' ', 30);
            at(1, 22); wx0 = 1; wx1 = 30; wy0 = 22; wy1 = 26;
            say("You sell the "); say_num(nc); say(" guns that were on the pigs you wasted for ");
            say_num(gold); say(" bucks! This'll teach 'em not to mess with ya, huh?");
            cash += gold; status += nc;
            delay(200);
        }
    }
    press_a();
}

/* ======================================================================== */
/*  Other random events                                                     */
/* ======================================================================== */
static u8 market_event(u8 count, i16 mul_pct) {
    u8 n = rnd() % count + 1, used[8], k, d, i;
    for (i = 0; i < 8; i++) used[i] = 0;
    say(" ");
    for (k = 0; k < n; k++) {
        do { d = rnd() % 8; } while (used[d]);
        if (k) say(n == 2 ? " and " : (k + 2 == n ? ", and " : ", "));
        say(drug_name[d]);
        used[d] = 1;
        rnd();                                           /* the original rolls and then ignores this */
        price[d] = (price[d] * mul_pct) / 100;
    }
    mkt_today = 1;
    return n;
}
static void ev_cops_sell(void) {
    if (mkt_today == 1) return;
    page();
    say("\"Hey man, check dis out!  Word has it on de street dat deres des cops, see? They musta decided that workin' for 25 G's a year ain't enough, so de've decided ta sell some of de take drugs confiscated on de side! De market is flooded with");
    market_event(3, 70);
    say("!\"");
    sfx(6); press_a();
}
static void ev_cheap(void) {
    page();
    say("\"Hey, I heard it through de grapevine dat deres some cheap homemade stuff available from some of the homeboys downtown.  Real primo stuff, dey say! All I hear on de streets is 'Cheap");
    market_event(3, 60);
    say("!'\"");
    press_a();
}
static void ev_newspaper(void) {
    u8 d;
    page();
    say("EXTRA! EXTRA! Dope prices soar after a big drug bust!!!");
    for (d = 0; d < NUM_DRUGS; d++) price[d] = (price[d] * (105 + rnd() % 25)) / 100;
    sfx(7); press_a();
}
static void ev_factory(void) {
    page();
    say("\"Dude, I got some news for ya... a factory of one of de cities biggest suppliers of");
    market_event(1, 120);
    say(" "); say(rnd() % 2 == 1 ? "burned down" : "blew up");
    say(" last night.  I don't have to tell ya what this'll do on de street...\"");
    press_a();
}
static void ev_brownies(void) {
    if (inv[5] == 0) return;
    page();
    say("Ya come back to yer place tonight and find dat yer sexy main squeeze tought dat it wouldst be a good idea to use some of yer weed to make brownies!!!\nAll tings considered, yous take it pretty well.  After all, if ya were mad, you would've chopped up her body before dumping it in de river!");
    inv[5] = 0;
    press_a();
}
static void ev_mugger(void) {
    u8 scared = 0;
    page();
    say("Yer walking along de street at night, when from outta nowhere jumps dis goon! He waves a 9mm Beretta at yous and demands yer fork over all yer money.\n");
    if (status >= 10 && rnd() % 20 != 0) {
        say("Do ya wanna use yer status as a major drug dealer to get outta this?\n");
        if (yn()) {
            fill(1, 26, ' ', 30);
            say("Yous tell him whos he's dealin' wit.  He looks nervous, asks fer yer pardon, den runs off down an alley!");
            status -= 10; scared = 1;
        }
    }
    if (!scared) {
        fill(1, 26, ' ', 30);
        if (cash < 50) {
            say("\"This is all you got? &%&*! this, man!\" He looks at ya like a crazed animal, and pulls de trigger!\n* * *  B L A M  * * *");
            sfx(13);
            health -= rnd() % 6 + rnd() % 6 + 2;
        } else say("He laughs merrily as he takes yer cash and runs off down de street!");
        cash = 0;
    }
    press_a();
}
static void ev_trenchcoat(void) {
    i16 cap;
    i32 cost, base;
    if (hold >= 300) return;
    for (;;) {                                           /* the size of the coat on offer */
        cap = rnd() % 10 + 1;
        if (rnd() % 5 == 0) {
            cap += rnd() % 10 + 1;
            if (rnd() % 5 == 0) cap += rnd() % 10 + 1;
        }
        cap *= 10;
        if (!(hold + 50 < cap || cap <= hold)) break;
    }
    base = (i32)coat_base[cap / 10 - 1];
    cost = ((i32)(rnd() % 200 + 1) * base);              /* 32-bit wrap like the original */
    cost = cost / 10000 * 100;
    page();
    say("You are approached by a shady-lookin guy in a long dark trenchcoat.  He says:\n\"Hey, bud, I got a special trenchcoat for ya, just your style! It's got a capacity of ");
    say_num(cap); say(" and I won't take under "); say_money(cost); say(" for it.\"\nIs it a deal?\n");
    if (yn()) {
        fill(1, 26, ' ', 30);
        if (cash < cost) say("\"Sorry Charly, I don't take credit! Maybe when you're a little more solvent!\"");
        else { say("\"You bought yourself one hell of a trenchcoat!\""); cash -= cost; hold = cap; }
    } else { fill(1, 26, ' ', 30); say("\"OK, it's your loss, man.  What a shame!\""); }
    press_a();
}
static void ev_gun_smuggler(void) {
    i16 kind = rnd() % 5, qty;
    i32 each, total;
    if (rnd() % 10 == 0) qty = rnd() % 6 + 5; else qty = rnd() % 5 + 2;
    each = kind * 200 + 200; total = qty * each;
    page();
    say("Juan, one of the most powerful underground gun smugglers in the world, comes to you with an offer:\n\"I have a shipment of ");
    say(gun_long[kind]); say("'s coming in tonight.  I'm distributing them in shipments of ");
    say_num(qty); say(" at "); say_money(each); say(" each, for a total of "); say_money(total);
    say(".\"\nAre you interested in an order?\n");
    if (yn()) {
        fill(1, 26, ' ', 30);
        if (total > cash) say("\"I'm sorry, sir, but I only accept cash up-front.  Business is business.\"");
        else {
            say("\"Very well, they will be delivered by tomorrow.  Good doing business with you.\"");
            if (gun_type == kind + 1) num_guns += qty; else num_guns = qty;
            gun_type = kind + 1;
            cash -= total;
        }
    } else { fill(1, 26, ' ', 30); say("\"As you wish.  Perhaps next time we may do business.\""); }
    press_a();
}
static void ev_blackmail(void) {
    i32 amount;
    if (cash < 10000L) return;
    amount = (i32)(((u32)cash * (70 + rnd() % 30)) / 100UL / 1000UL) * 1000L;
    page();
    say("Yous get a note in de mail from a business associate of yers.  He says dat if yous don't deposit ");
    say_money(amount); say(" in his Swiss bank account, he's gonna rat to de cops everything dat he knows about you.\nDo you pay the blackmail?\n");
    if (yn()) {
        fill(1, 26, ' ', 30);
        say("You wire de money into his account.  That really ^$&?*@ sucks!!!");
        cash -= amount;
    } else {
        fill(1, 26, ' ', 30);
        say("Yeah, man, nobody pushes you around! I respect yer integrity!!!");
        if (rnd() % 5 != 0) { cop_flag = 1; done[0] = 0; }
    }
    press_a();
}
static void do_event(u8 ev) {
    if (done[ev]) return;
    switch (ev) {
    case 0: cop_fight(); break;
    case 1: ev_cops_sell(); break;
    case 2: ev_cheap(); break;
    case 3: ev_newspaper(); break;
    case 4: ev_factory(); break;
    case 5: ev_brownies(); break;
    case 6: ev_mugger(); break;
    case 7: ev_trenchcoat(); break;
    case 8: ev_gun_smuggler(); break;
    case 9: ev_blackmail(); break;
    }
    done[ev] = 1;
    if (!dead) draw_main();
}

/* ======================================================================== */
/*  Menu actions: each returns 1 when the day passes                        */
/* ======================================================================== */
static i16 pick_drug(const char* prompt) {
    i16 d;
    msg_area();
    say(prompt); say(" (A = pick, B = cancel)");
    d = choose(0, ROW_DRUGS, NUM_DRUGS, 0);
    msg_area();
    return d;
}
static u8 menu_buy(void) {
    i16 d;
    i32 max_buy, qty;
    d = pick_drug("What kinda drugs are ya interested in?");
    if (d < 0) return 0;
    max_buy = cash / price[d];
    msg_area();
    say("How many do ya wanna buy? ("); say_num(max_buy); say(" max.)  SELECT = most you can take");
    qty = pick_number(1, 26, 5, max_buy < space ? max_buy : space);
    msg_area();
    if (qty < 0) return 0;
    if (qty > max_buy) { say("Hey man, if ya don't got enough cash, don't come 'round here!"); sfx(3); msg_wait(); return 0; }
    if (qty > space) { say("Dude, you can't hold that much! Everyone's got a limit, right?"); sfx(3); msg_wait(); return 0; }
    if (qty > 0) {
        inv[d] += (i16)qty;
        cash -= qty * price[d];
        basis[d] = new_basis(basis[d], price[d]);
        sfx(4);
    }
    return 1;
}
static u8 menu_sell(void) {
    i16 d;
    i32 qty;
    d = pick_drug("What kinda drugs do ya want to unload?");
    if (d < 0) return 0;
    if (inv[d] == 0) { say("Give me a break, man, you ain't got any of that!"); sfx(3); msg_wait(); return 0; }
    msg_area();
    say("How many do ya wanna sell? SELECT = all");
    qty = pick_number(1, 26, 5, inv[d]);
    msg_area();
    if (qty < 0) return 0;
    if (qty > inv[d]) { say("I happen to know that you ain't that much on ya, so quit trying to burn me!"); sfx(3); msg_wait(); return 0; }
    if (qty > 0) {
        if (inv[d] - qty == 0) {
            i16 old = status;
            status += status_from_sale(d, price[d], basis[d]);
            total_deals++;
            if (status > old) good_deals++;
            basis[d] = 0;
        }
        inv[d] -= (i16)qty;
        cash += qty * price[d];
        sfx(4);
    }
    return 1;
}
static u8 menu_jet(void) {
    i16 c, i;
    i32 fare;
    msg_area(); say("Where ya wanna go?  (A = go, B = cancel)");
    for (i = 0; i < NUM_CITIES; i++) {
        at(2, ROW_DRUGS + i); ps_pad(city_name[i], 16);
        at(19, ROW_DRUGS + i);
        if (i == city) ps("here"); else { pc('$'); pn_r(travel_cost(i, city), 5); }
    }
    for (;;) {
        c = choose(0, ROW_DRUGS, NUM_CITIES, city);
        if (c < 0) { draw_status(); msg_area(); return 0; }
        if (c == city) { msg_area(); say("Are you having flashbacks? That's where you are now!"); sfx(3); msg_wait(); msg_area(); say("Where ya wanna go?"); continue; }
        break;
    }
    fare = travel_cost(c, city);
    if (fare > cash) {
        msg_area(); say("Start flappin' yer arms, cause you ain't got de dough!"); sfx(3); msg_wait();
        draw_status(); return 0;
    }
    cash -= fare;
    city = c;
    mkt_today = 0;
    gen_prices();
    for (i = 0; i < 10; i++) done[i] = 0;
    sfx(6);
    draw_status();
    return 1;
}
static u8 menu_bank(void) {
    i16 c;
    i32 amt;
    msg_area();
    say("You arrive at the First World Bank of America.");
    ps_at(3, 24, "Deposit some moola");
    ps_at(3, 25, "Withdraw some moola");
    ps_at(3, 26, "Leave");
    c = choose(1, 24, 3, 0);
    if (c < 0 || c == 2) { msg_area(); return 0; }
    msg_area();
    say(c == 0 ? "How much 'ya gonna rathole?" : "How much 'ya gonna pull out?");
    say(" SELECT = all");
    amt = pick_number(1, 26, 10, c == 0 ? cash : bank);
    msg_area();
    if (amt <= 0) return 0;
    if (c == 0) {
        if (amt > cash) { say("You ain't got that much to deposit!"); sfx(3); msg_wait(); }
        else { cash -= amt; bank += amt; sfx(2); draw_status(); }
    } else {
        if (amt > bank) { say("You ain't got that much to pull out!"); sfx(3); msg_wait(); }
        else { cash += amt; bank -= amt; sfx(2); draw_status(); }
    }
    return 0;                                            /* banking does not pass a day */
}
static u8 menu_loan(void) {
    i16 c;
    i32 amt;
    msg_area();
    say("Ya set foot in the 'sharks office.  His boys are lookin' tough, but you don't pay dem no never-mind, cause Guido is talkin...");
    ps_at(3, 24, "Borrow some cash");
    ps_at(3, 25, "Pay me back");
    ps_at(3, 26, "Leave");
    c = choose(1, 24, 3, 0);
    if (c < 0 || c == 2) { msg_area(); return 0; }
    msg_area();
    if (c == 0) {                                        /* borrow */
        if (angry == 1) { say("\"Man, yer pushin' yer luck luck! I ought'ta kill you just for askin'!!\""); sfx(3); msg_wait(); return 1; }
        if (status <= 0) { say("\"Look, whoever you are, you're NOBODY to me.  Get outta here!\""); sfx(3); msg_wait(); return 1; }
        if (debt == 0) say("\"My books show dat you don't owe me nut'in... would youse like to take out a little loan?\"\n");
        say("How much ya wanna borrow?");
        amt = pick_number(1, 26, 10, -1);
        msg_area();
        if (amt <= 0) return 0;
        if (amt < 1000) { say("\"Sorry, bud, but I got a $1000 minimum.  Got no time for penny-ante stuff!\""); sfx(3); msg_wait(); return 0; }
        if (amt / 10000 >= status) { say("\"No deal, too big a risk for me! I'm a businessman, ya'know!\""); sfx(3); msg_wait(); return 0; }
        {
            i16 days = loan_term(status, debt + amt);
            u8 refuse = 0;
            if ((debt + amt) / 10000 > status) refuse = 1;
            else if (days < loan_days) refuse = 0;
            else if (loan_flag == 1) refuse = 1;
            if (refuse) {
                say("\"Look, you already owe me $"); say_num(debt);
                say(" bucks, I'm not givin ya THAT much more!!! Christ, what do I look like, Santi-Claus?\"");
                sfx(3); msg_wait(); return 0;
            }
            debt += amt; cash += amt; loan_flag = 1; loan_days = days;
            say("\"Alright, I'll loan ya de money, but if you welch, my boys will come lookin for ya! I expect to see you here to pay me in full in ");
            say_num(days - 1); say(" days.\"");
            sfx(4); msg_wait();
            return 1;
        }
    }
    if (debt == 0) { say("\"My books show dat you don't owe me nut'in...\""); msg_wait(); return 0; }
    say("How much ya wanna pay back? SELECT = all you owe");
    amt = pick_number(1, 26, 10, debt < cash ? debt : cash);
    msg_area();
    if (amt <= 0) return 0;
    if (amt > cash) { say("'fraid you don't got that in cash, my man."); sfx(3); msg_wait(); return 0; }
    if (amt >= debt) debt = 0; else debt -= amt;
    cash -= amt;
    if (debt > 0) {
        say("\"OK, but you still owe me $"); say_num(debt); say(" bucks!\"");
    } else {
        loan_days = 0; loan_flag = 0; angry = 0; thugs = 0;
        say("\"Paid in full? Lucky for you, eh boys? Anytime yous need a loan, just come to me, I'll take care of ya...\"");
    }
    sfx(4); msg_wait();
    return 1;
}
static u8 menu_hospital(void) {
    i32 cost;
    i16 stay, n;
    msg_area();
    if (health == 100) {
        say("Ya have de doc check yous out, but aside from yer bunions on yer feet, youse is fit as a fiddle!");
        msg_wait(); return 0;
    }
    cost = (100 - health) * 10L;
    say("You drag yer sorry butt into de loc'l Hospital.  The docs check ye out, and come to the conclusion that it'll cost ");
    say_num(cost); say(" dollars to fix ya up.\nDo ya sign the consent forms?\n");
    if (yn()) {
        msg_area();
        if (cost > cash) { say("You don't have enough dinero up front to foot de bill!"); sfx(3); msg_wait(); }
        else {
            cash -= cost;
            stay = (i16)(cost / 15);
            for (n = 1; stay > n; n++) next_day();
            health = 100;
            say("OK, yer laid up for "); say_num(stay); say(" days recoup'ratin', and yous feel good as new!");
            sfx(2); msg_wait();
        }
    }
    return 1;
}
static u8 menu_quit(void) {
    msg_area();
    say("Are ya sure ya wanna chicken out an' QUIT?\n");
    if (yn()) { playing = 0; return 1; }
    msg_area();
    return 0;
}

/* ======================================================================== */
/*  Start, ending, title                                                    */
/* ======================================================================== */
static void init_game(void) {
    u8 i;
    for (i = 0; i < NUM_DRUGS; i++) { inv[i] = 0; basis[i] = 0; }
    for (i = 0; i < 10; i++) done[i] = 0;
    gen_prices();                                        /* (uses the previous city, as the original does) */
    city = rnd() % 8;
    cash = 500; hold = 10; bank = 0; debt = 0;
    loan_flag = 0; num_guns = 0; gun_type = 0; status = 1; health = 100;
    dead = 0; angry = 0; cop_flag = 0; thugs = 0; total_days = 0;
    good_deals = 0; total_deals = 0; mkt_today = 0; loan_days = 0;
    month = rnd() % 12 + 1; day = rnd() % 28 + 1; year = rnd() % 6 + 86;
#ifdef TEST_INIT
    TEST_INIT
#endif
}

static void show_death(void) {
    page();
    delay(60);
    ps_at(6, 8, "\x01\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x03");
    ps_at(6, 9, "\x04     R . I . P .    \x04");
    ps_at(6, 10, "\x04                  \x04");
    ps_at(6, 11, "\x04  You are dead.   \x04");
    ps_at(6, 12, "\x04                  \x04");
    ps_at(6, 13, "\x04");  ps_at(23, 13, "\x04");
    ps_at(6, 14, "\x05\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x06");
    colour_rows(8, 15, 2);
    delay(200);
    press_a();
}

static u32 score_days_at(u16 idx) {
    u32 v = SCORE_DAYS_START;
    u16 i;
    for (i = 0; i < idx; i++) v -= score_days_d[i];
    return v;
}

static u32 score_calc(i32 total, i16 pct) {
    u32 p1, p2 = 0, p3, p4;
    u8 k = 1, i;
    if (total < 0) k = 0;
    else for (i = 0; i < 9; i++) if ((u32)total >= score_wealth[i]) k = i + 2;
    p1 = (u32)k * 75000UL; if (p1 > 700000UL) p1 = 700000UL;
    if (total_days > 0) {
        if (total_days < SCORE_DAYS_FIRST) p2 = 500000UL;
        else if (total_days <= 700) p2 = score_days_at(total_days - SCORE_DAYS_FIRST);
        else p2 = score_days_at(700 - SCORE_DAYS_FIRST) * 490000UL / ((u32)total_days * total_days) * 1UL;   /* beyond the table: ~ days^-2 */
    }
    {
        i16 q = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
        p3 = score_deals[q];
    }
    p4 = (u32)((i32)gun_type * num_guns) * 1500UL;
    return p1 + p2 + p3 + p4;
}
static void show_win(void) {
    i16 pct = 0;
    u32 score;
    u8 rk = 0, f;
    for (f = 0; f < 5; f++) {                            /* "DRUGLORD!!!" flashes */
        page(); colour_rows(12, 13, 1 + (f & 1));
        ps_at(10, 12, "DRUGLORD!!!"); snd(HZ(500 + f * 150), 5);
        delay(25);
    }
    page();
    if (total_deals != 0) pct = (i16)(good_deals * 100) / total_deals;
    score = score_calc(cash + bank - debt, pct);
    while (rk < 10 && rank_thr[rk] <= score) rk++;
    ps_at(7, 1, "CONGRATULATIONS!!!");
    wx0 = 1; wx1 = 30; wy0 = 3; wy1 = 6; cx = 1; cy = 3;
    say("Yous has become a DRUGLORD!!!  I'm proud ta know you, man!  Anyway, check dis out --");
    ps_at(1, 8,  "Cash in Hand:"); at(16, 8); pn(cash);
    ps_at(1, 9,  "Cash in Bank:"); at(16, 9); pn(bank);
    ps_at(1, 10, "Still in Debt:"); at(16, 10); pn(debt);
    ps_at(1, 11, "Good Deals:"); at(16, 11); pn(good_deals);
    ps_at(1, 12, "Total Deals:"); at(16, 12); pn(total_deals);
    ps_at(1, 13, "DEAL %:"); at(16, 13); pn(pct); ps("%");
    ps_at(1, 14, gun_name[gun_type]); ps("'s:"); at(16, 14); pn(num_guns);
    ps_at(1, 15, "Total Days:"); at(16, 15); pn(total_days);
    for (f = 1; f < 31; f++) put(f, 17, '-');
    ps_at(1, 18, "OVERALL SCORE:"); at(16, 18); pn((i32)score);
    ps_at(1, 19, "RANK:"); at(16, 19); ps(rank_name[rk]);
    colour_rows(0, 1, 1); colour_rows(18, 19, 3);
    sfx(4);
    press_a();
}

static u8 ask_play_again(void) {
    page();
    ps_at(1, 12, "Would ya like ta");
    ps_at(1, 13, "play again?");
    return yn();
}
/* the NOTICE shown when you quit; A goes back to the title screen */
static void exit_screen(void) {
    u8 x;
    page();
    ps_at(0, 2, "\x01"); for (x = 1; x < 31; x++) put(x, 2, 2); put(31, 2, 3);
    for (x = 3; x < 22; x++) { put(0, x, 4); put(31, x, 4); }
    put(0, 22, 5); for (x = 1; x < 31; x++) put(x, 22, 2); put(31, 22, 6);
    ps_at(8, 4, "* * *  NOTICE  * * *");
    wx0 = 2; wx1 = 29; wy0 = 6; wy1 = 21; cx = 2; cy = 6;
    say("This game is intended simply as a diversion from everyday life. In no way am I condoning or encouraging drug dealing or drug use in any way, shape, or form.\n\nTo quote from an excellent anti-drug movie, New Jack City:\n\n\"The drug epidemic isn't a black thing, it isn't a white thing. It's a death thing. And death doesn't care what color you are.\"");
    colour_cols(4, 5, 4, 13, 2);                         /* only the heading is red, the frame stays white */
    ps_at(1, 25, "Thank you for playing DrugLord.");
    colour_rows(24, 25, 3);
    ppu_show();
    while (!(wait_btn() & BTN_A)) {}
}

/* the loan shark's thugs come for you */
static void thugs_scene(void) {
    u8 n;
    page();
    say("You're ambushed in a dark alley by Guido's thugs, must be 'bout ten guys! The boss-man say \"Oh, man, you should'na missed de deadline, now me and boys are gonna have to break your legs! Maybe next time you'll learn, eh?\"\nDey take yous from all directions, you ain't gotta chance...\nOne steps out with a baseball bat and...\n");
    delay(120);
    thugs++;
    for (n = 0; n < thugs; n++) {
        say("* * * C R A C K ! ! * * *\n");
        health -= 25; angry = 1;
        sfx(3);
        delay(60);
    }
    if (health > 0) {
        say("\"Oh, one more 'ting... you got a week to come up with dinero, or we'll be back, comprende?\"");
        loan_days = 7;
    } else dead = 1;
    press_a();
    draw_main();
}

#ifdef DEBUG
static void debug_menu(void);
#endif

/* one session: play games until the player does not want another */
static void game(void) {
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
            msg_area();
            ps_at(2, 21, "Buy drugs");
            ps_at(2, 22, "Sell drugs");
            ps_at(2, 23, "Jet to another city");
            ps_at(2, 24, "Visit de Bank");
            ps_at(2, 25, "See de Loan Shark");
            ps_at(2, 26, "Go to de Hospital");
            ps_at(2, 27, "Quit de game");
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

/* shown for 4 seconds before the title screen */
static void credits_screen(void) {
    u8 n;
    clear_screen(); set_palette();
    ps_at(5, 8, "Written by Phil Erwin");
    ps_at(7, 10, "Copyright 1991-93");
    ps_at(8, 12, "FantasyWare Inc.");
    ps_at(6, 15, "NES port by justz00t");
    colour_rows(8, 13, 1); colour_rows(14, 15, 3);
    ppu_show();
    for (n = 0; n < 240; n++) wait_vblank();
}

#define START_X 10
#define START_Y 18
static void title_screen(void) {
    clear_screen(); set_palette();
    ps_at(2, 9,  "\x01\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x03");
    ps_at(2, 10, "\x04                          \x04");
    ps_at(2, 11, "\x04     D R U G L O R D      \x04");
    ps_at(2, 12, "\x04                          \x04");
    ps_at(2, 13, "\x05\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x06");
    ps_at(START_X, START_Y, "PRESS START");
    colour_rows(8, 13, 2);
}
/* draw or erase "PRESS START" during vblank, without ever blanking the screen (no flicker) */
static void start_text(u8 on) {
    const char* s = "PRESS START";
    u8 i;
    wait_vblank();
    ppu_at(START_X, START_Y);
    for (i = 0; i < 11; i++) PPUDATA = on ? (u8)s[i] : ' ';
    PPUCTRL = 0; PPUSCROLL = 0; PPUSCROLL = 0;
}

/* the title screen with its music; returns when START (or A) is pressed */
static void title_wait(void) {
    u8 e, blink = 0;
    music_start();
    title_screen();
    ppu_show();
    for (;;) {
        if ((blink & 31) == 0) start_text(!(blink & 32));
        else wait_vblank();
        frames++; blink++;
        music_update();
        e = read_pad(); { u8 d = e & ~pad_old; pad_old = e; e = d; }
        if (e & (BTN_START | BTN_A)) break;
    }
    music_stop();
}

#ifdef DEBUG
/* ======================================================================== */
/*  Debug build only: SELECT+START in the main menu opens this menu         */
/* ======================================================================== */
static const char* const snd_name[] = {
    "Beep", "Denied", "Deal", "Siren", "News", "EXTRA!", "Hit",
    "Gun 1", "Gun 2", "Gun 3", "Gun 4", "Gun 5",
    "Menu click", "Title music", "Back"};
static const u8 snd_id[] = {2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 20};
#define SND_COUNT 15
static void sound_test(void) {
    u8 i;
    i16 s = 0;
    for (;;) {
        page();
        ps_at(7, 1, "SOUND TEST");
        for (i = 0; i < SND_COUNT; i++) ps_at(2, 4 + i, snd_name[i]);
        s = choose(0, 4, SND_COUNT, s);
        if (s < 0 || s == SND_COUNT - 1) return;
        if (s == SND_COUNT - 2) {                        /* title music until B */
            music_start();
            ppu_show();
            while (!(vsync_pad() & BTN_B)) {}
            music_stop();
        } else sfx(snd_id[s]);
    }
}

static void dbg_wait_release(void) { while (pad_old & (BTN_SELECT | BTN_START)) vsync_pad(); }

static const char* const dbg_name[] = {
    "Cop fight", "Cops sell", "Cheap", "Bust", "Factory",
    "Brownies", "Mugger 1", "Mugger 2", "Mugger 3", "Trenchcoat",
    "Smuggler", "Blackmail", "Thugs", "Death", "Win score",
    "Play again", "Notice", "Credits", "Title", "SOUND TEST", "Back"};
#define DBG_COUNT 21
static void debug_menu(void) {
    u8 i;
    i16 s = 0;
    dbg_wait_release();
    for (;;) {
        page();
        ps_at(6, 1, "DEBUG MENU");
        for (i = 0; i < DBG_COUNT; i++) ps_at(2, 3 + i, dbg_name[i]);
        s = choose(0, 3, DBG_COUNT, s);
        if (s < 0 || s == DBG_COUNT - 1) { dbg_wait_release(); return; }
        mkt_today = 0; dead = 0;
        if (status < 120) status = 120;                  /* so every option of every event is available */
        if (cash < 20000L) cash = 20000L;
        if (hold < 40) hold = 40;
        if (health < 50) health = 100;
        switch (s) {
        case 0: if (gun_type == 0) { gun_type = 2; num_guns = 3; } cop_fight(); break;
        case 1: ev_cops_sell(); break;
        case 2: ev_cheap(); break;
        case 3: ev_newspaper(); break;
        case 4: ev_factory(); break;
        case 5: inv[5] = 3; ev_brownies(); break;
        case 6: ev_mugger(); break;
        case 7: { i16 st = status; i32 ca = cash; status = 1; cash = 1000; ev_mugger(); status = st; cash = ca; } break;
        case 8: { i16 st = status; i32 ca = cash; status = 1; cash = 10; ev_mugger(); status = st; cash = ca; } break;
        case 9: hold = 40; ev_trenchcoat(); break;
        case 10: ev_gun_smuggler(); break;
        case 11: ev_blackmail(); break;
        case 12: thugs_scene(); break;
        case 13: show_death(); break;
        case 14: show_win(); break;
        case 15: ask_play_again(); break;
        case 16: exit_screen(); break;
        case 17: credits_screen(); break;
        case 18: title_wait(); break;
        case 19: sound_test(); break;
        }
        dead = 0;
        if (health <= 0) health = 100;
    }
}
#endif

void main(void) {
    wait_vblank(); wait_vblank();
    PPUCTRL = 0; PPUMASK = 0;
    credits_screen();
    for (;;) {                                           /* credits once, then title -> game -> notice -> title ... */
        title_wait();
        seed = ((u32)frames << 8) ^ 0x2A5Bu ^ (u32)frames * 977UL;
        game();
        exit_screen();
    }
}
