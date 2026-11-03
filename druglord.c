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

u8 ink, attr_dirty;      /* current text colour (palette 0..3); pc() colours its 2x2-tile attribute block with it */
u8 attr[64];
u8 pend;
void wait_vblank(void) { while (!(PPUSTATUS & 0x80)) {} if (pend < 6) pend++; }
void music_update(void);
void ppu_off(void) { if (ppu_on) { wait_vblank(); PPUMASK = 0; ppu_on = 0; } }
void ppu_show(void) {
    if (!ppu_on) {
        if (attr_dirty) { u8 i; attr_dirty = 0; latch = PPUSTATUS; PPUADDR = 0x23; PPUADDR = 0xC0; for (i = 0; i < 64; i++) PPUDATA = attr[i]; }
        wait_vblank();
        PPUCTRL = 0; latch = PPUSTATUS; PPUSCROLL = 0; PPUSCROLL = 0;
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
    if (ppu_on) { PPUCTRL = 0; PPUSCROLL = 0; PPUSCROLL = 0; }
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
    static const u8 pal[16] = {0x0F, 0x30, 0x10, 0x00,  0x0F, 0x28, 0x18, 0x00,
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
    ink = 0; attr_dirty = 0;
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
    while (u) { t[n++] = '0' + (u % 10); u /= 10; }
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
u8 wait_btn(void) {          /* wait for any new button, return it */
    u8 e;
    ppu_show();
    for (;;) { e = vsync_pad(); if (e) return e; }
}

/* ---- sound effects (pulse 2 + noise, played while waiting) ---- */
void snd(u16 per, u8 n) {
    APU(0x04) = 0xBC; APU(0x06) = per & 255; APU(0x07) = 0x08 | (per >> 8);
    ppu_show();
    while (n--) { wait_vblank(); music_update(); }
    APU(0x04) = 0xB0;
}
void noi(u8 idx, u8 vol, u8 n) {
    APU(0x0C) = 0x30 | vol; APU(0x0E) = idx; APU(0x0F) = 0x08;
    ppu_show();
    while (n--) { wait_vblank(); music_update(); if (vol) APU(0x0C) = 0x30 | --vol; }
    APU(0x0C) = 0x30;
}
void sfx(u8 n) {
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
const u8* m_d; static const u8* m_ab; static const u8* m_c;
const u16* m_pp; static const u16* m_tp;
u16 m_n, m_rate, m_ev, m_acc;
u8 m_wait, m_v1, m_v2, m_tri, m_tage, m_cnt, m_use2, music_on;
void music_stop(void) {
    music_on = 0;
    APU(0x00) = 0x30; APU(0x04) = 0x30; APU(0x08) = 0x80; APU(0x0C) = 0x30;
}
void music_start(void) {
    APU(0x15) = 0x0F;               /* pulse 1, pulse 2, triangle, noise */
    APU(0x01) = 0x08; APU(0x05) = 0x08;
    m_d = title_d; m_ab = title_ab; m_c = title_c; m_pp = title_pp; m_tp = title_tp; m_n = TITLE_EVENTS; m_rate = TITLE_RATE; m_use2 = 1;
    m_ev = 0; m_acc = 0; m_wait = m_d[0]; m_v1 = m_v2 = 0; m_tri = 0; m_tage = 0; m_cnt = 0;
    pend = 0; music_on = 1;
}
void music_note(u8 ab, u8 c) {
    u8 a = ab >> 4, b = ab & 15;
    if (a) { u16 p = m_pp[a]; APU(0x02) = p & 255; APU(0x03) = 0x08 | (p >> 8); m_v1 = 12; }
    if (b && m_use2) { u16 p = m_pp[b]; APU(0x06) = p & 255; APU(0x07) = 0x08 | (p >> 8); m_v2 = 9; }
    if (c) { u16 p = m_tp[c]; APU(0x0A) = p & 255; APU(0x0B) = 0x08 | (p >> 8); APU(0x08) = 0xFF; m_tage = 0; m_tri = 1; }
}
void music_frame(void) {
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
u16 isqrt32(u32 n) {
    u16 r = 0;
    while ((u32)(r + 1) * (r + 1) <= n) r++;
    return r;
}
/* air fare = distance on the map * 60 + 25 */
i32 travel_cost(u8 a, u8 b) {
    i16 dx = (i16)city_x[a] - city_x[b], dy = (i16)city_y[a] - city_y[b];
    return (i32)isqrt32(3600UL * (dx * dx + dy * dy)) + 25;
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

/* ---- main screen ---- */
void draw_status(void) {
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
void draw_main(void) {
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
        else if (e & BTN_A) { put(x, y + s, ' '); return s; }
        else if (e & BTN_B) { for (i = 0; i < n; i++) put(x, y + i, ' '); return -1; }
        if (o != s) put2(x, y + o, ' ', x, y + s, 0x10);
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
void load_font(void) {
    const u8* p = NESfont;
    u16 n;
    bank_set(2);
    latch = PPUSTATUS; PPUADDR = 0; PPUADDR = 0;
    for (n = 0; n < 8192; n++) PPUDATA = *p++;
    bank_set(0);
}

void main(void) {
    PPUCTRL = 0; PPUMASK = 0;
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
