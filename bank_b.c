/* PRG bank 1: start/end screens, score, title, high scores, debug menu */
#include "dl.h"
#define TBL_SCORE
#include "tables.h"
#include "title.h"
#include "scene.h"

void init_game(void) {
    u8 i;
    easter_on = 0;                                       /* the easter egg lasts one game */
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

/* a box of tiles 1..6 (corners and edges), w x h tiles */
static void box(u8 x, u8 y, u8 w, u8 h) {
    u8 i;
    at(x, y); pc(1); for (i = 2; i < w; i++) pc(2); pc(3);
    for (i = 1; i + 1 < h; i++) { at(x, y + i); pc(4); at(x + w - 1, y + i); pc(4); }
    at(x, y + h - 1); pc(5); for (i = 2; i < w; i++) pc(2); pc(6);
}
/* the DOS death screen: a framed message with a few words picked out in colour */
void show_death(void) {
    page();
    delay(90);
    sfx(16);
    pal3(0x2A, 0x1A);
    ink = 2; box(1, 2, 30, 24);
    wx0 = 3; wx1 = 28; wy0 = 4; wy1 = 22; cx = 3; cy = 4;
    sayc(1, "Everything has a strange misty look to it...\n\n");
    sayc(0, "You are standing next to your dead body.  As you decide what to do next, you start to feel heat under your feet.  The next thing you know, you see flames licking all around your body.\n\n");
    sayc(0, "I guess this is "); sayc(3, "the end."); ink = 0;
    delay(240);
    press_a();
}

u32 score_days_at(u16 idx) {
    u32 v = SCORE_DAYS_START;
    u16 i, d = SCORE_DAYS_D0;
    for (i = 0; i < idx; i++) { v -= d; d += score_days_s[i]; }
    return v;
}

u32 score_calc(i32 total, i16 pct) {
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
        p3 = 0;
        for (i = SCORE_DEALS_Z; i < q; i++) p3 += score_deals_d[i - SCORE_DEALS_Z];
    }
    p4 = (u32)((i32)gun_type * num_guns) * 1500UL;
    return p1 + p2 + p3 + p4;
}
/* ---- the end scene: the DOS picture (the table, the lamp, the coins) with a gun for each gun owned and
 *      stacks of money that pile up with rising beeps, then the score in a box you scroll through ---- */
static const u8 gun_x[10] = {20, 23, 26, 29, 0, 2, 5, 7, 10, 12};
static const u8 gun_y[10] = {2, 5, 2, 5, 2, 5, 2, 5, 2, 5};
/* write two tiles and one attribute byte during vblank (the screen stays on) */
/* give tile (x, y) palette 2 in the attribute shadow; returns the attribute byte's index */
static u8 bar_attr(u8 x, u8 y) {
    u8 i = (y >> 2) * 8 + (x >> 2), sh = ((y & 2) ? 4 : 0) + ((x & 2) ? 2 : 0);
    attr[i] = (attr[i] & ~(3 << sh)) | (2 << sh);
    return i;
}
static void vb_bar(u8 x, u8 y) {
    /* a stack is two tiles wide: each tile gets palette 2 for its own 16-pixel block (the two tiles can lie in
     * different blocks, and even in different attribute bytes) */
    u8 i = bar_attr(x, y), j = bar_attr(x + 1, y);
    wait_vblank(); frames++;
    ppu_at(x, y); PPUDATA = bar_tiles[0]; PPUDATA = bar_tiles[1];
    PPUADDR = 0x23; PPUADDR = 0xC0 + i; PPUDATA = attr[i];
    if (j != i) { PPUADDR = 0x23; PPUADDR = 0xC0 + j; PPUDATA = attr[j]; }
    PPUCTRL = bgctl; PPUSCROLL = 0; PPUSCROLL = 0;
}
static void end_scene(void) {
    u8 x, y, i, k, idx, layer, nl, guns;
    const u8* m = scene_map;
    clear_screen();
    load_pt1(2);
    ppu_off();
    latch = PPUSTATUS; PPUADDR = 0x3F; PPUADDR = 0x00;
    PPUDATA = 0x0F; PPUDATA = 0x07; PPUDATA = 0x28; PPUDATA = 0x17;      /* 0: dark brown, yellow, brown */
    PPUDATA = 0x0F; PPUDATA = 0x00; PPUDATA = 0x10; PPUDATA = 0x17;      /* 1: dark grey, grey, brown */
    PPUDATA = 0x0F; PPUDATA = 0x1A; PPUDATA = 0x28; PPUDATA = 0x17;      /* 2: green, yellow, brown */
    PPUDATA = 0x0F; PPUDATA = 0x30; PPUDATA = 0x2C; PPUDATA = 0x28;      /* 3: white, cyan, yellow (the score box) */
    bgctl = 0x10;
    for (y = 0; y < SCENE_H; y++) { ppu_at(1, y); for (x = 0; x < SCENE_W; x++) PPUDATA = *m++; }
    colour_cols(0, 7, 7, 9, 1);                                          /* the lamp */
    guns = num_guns < 10 ? (u8)num_guns : 10;
    for (i = 0; i < guns; i++) {
        x = gun_x[i]; y = gun_y[i];
        for (k = 0; k < 9; k++) put(x + k % 3, y + k / 3, gun_tiles[k]);
        colour_cols(y, y + 2, x >> 1, (x + 2) >> 1, 1);
    }
    ppu_show();
    delay(30);
    /* the stacks: one for every $100000 in hand, a new layer every $2000000, each with two rising beeps */
    nl = (u8)((cash + bank - 1) / 2000000L) + 1;
    if (nl > 4) nl = 4;
    for (idx = 0; idx < 4; idx++)
        for (k = 0; k < 5; k++)
            for (layer = 0; layer < nl; layer++) {
                if ((i32)(idx * 5 + k + 1) * 100000L + (i32)layer * 2000000L > cash) break;
                vb_bar(6 + k * 3 - idx, 12 + idx - layer);
                snd(HZ(1000 + layer * 500), 1); snd(HZ(3000 + layer * 500), 1);
            }
}
/* the score list, 18 lines, 5 at a time in a box over the bottom of the scene */
static const char* const end_line[18] = {
    "   CONGRATULATIONS!!!", "", "Yous has become a", "DRUGLORD!!!  I'm proud ta", "know you, man!  Anyway,",
    "check dis out --", "", "   Cash in Hand:", "   Cash in Bank:", "  Still in Debt:", "     Good Deals:",
    "    Total Deals:", "         DEAL %:", "", "     Total Days:", "----------------  ----------", " OVERALL SCORE:", "          RANK:"};
static i32 end_val[18];
/* one line of the score list, built into row_b[]: 16 + 12 characters for columns 2-29, clear of the scroll
 * arrows (30) and the frame (31) */
static char row_b[28];
static u8 row_l;
static void rb_c(char c) { if (row_l < 28) row_b[row_l++] = c; }
static void rb_s(const char* t) { while (*t) rb_c(*t++); }
static void rb_pad(u8 to) { while (row_l < to) row_b[row_l++] = ' '; }
static void end_row(u8 n) {
    u8 l;
    row_l = 0;
    if (n == 15) { rb_s("----------------  ----------"); return; }   /* the rule: exactly 28 */
    if (n == 13) { rb_s(gun_name[gun_type]); rb_pad(13); rb_s("'s:"); }
    else rb_s(end_line[n]);
    if (n == 17) { rb_pad(16); rb_s(rank_name[(u8)end_val[17]]); rb_pad(28); return; }
    if ((n >= 7 && n <= 14) || n == 16) {            /* a label (16) and a number (12, right-aligned) */
        rb_pad(16);
        ltoa_s(end_val[n]);
        if (n == 12) { for (l = 0; nb[l]; l++) {} nb[l] = '%'; nb[l + 1] = 0; }
        for (l = 0; nb[l]; l++) {}
        rb_pad(28 - l); rb_s(nb);
    }
    rb_pad(28);                                       /* a line of the message: padded to 28, never longer */
}
/* write row_b[] to screen row y during vblank, the picture staying on (no flicker) */
static void vb_row(u8 y) {
    u8 i;
    wait_vblank(); frames++;
    latch = PPUSTATUS;
    ppu_at(2, y);
    for (i = 0; i < 28; i++) PPUDATA = (u8)row_b[i];
    PPUCTRL = bgctl; PPUSCROLL = 0; PPUSCROLL = 0;
}
static void vb_arrows(u8 top) {
    wait_vblank(); frames++;
    latch = PPUSTATUS;
    ppu_at(30, 19); PPUDATA = top ? 0x1E : ' ';
    ppu_at(30, 26); PPUDATA = top < 12 ? 0x1F : ' ';
    PPUCTRL = bgctl; PPUSCROLL = 0; PPUSCROLL = 0;
}
static void end_box(u32 score, u8 rk) {
    u8 top = 0, i, e;
    ppu_off();
    clear_rows(18, 27);
    colour_rows(18, 27, 3);
    end_val[7] = cash; end_val[8] = bank; end_val[9] = debt; end_val[10] = good_deals; end_val[11] = total_deals;
    end_val[12] = total_deals ? (i32)good_deals * 100 / total_deals : 0;
    end_val[13] = num_guns; end_val[14] = total_days; end_val[16] = (i32)score; end_val[17] = rk;
    ink = 3; box(0, 18, 32, 10);
    ps_at(2, 27, "UP/DOWN scroll    A = done");
    for (i = 0; i < 6; i++) {                        /* the first page: drawn with the screen off */
        end_row(i); at(2, 20 + i);
        for (e = 0; e < 28; e++) pc((u8)row_b[e]);
    }
    put(30, 19, ' '); put(30, 26, 0x1F);
    for (;;) {
        do {
            e = wait_btn();
            if ((e & BTN_UP) && top) { top--; sfx(20); break; }
            if ((e & BTN_DOWN) && top < 12) { top++; sfx(20); break; }
        } while (!(e & BTN_A));
        if (e & BTN_A) break;
        for (i = 0; i < 6; i++) { end_row(top + i); vb_row(20 + i); }   /* scrolling: a row a frame, screen on */
        vb_arrows(top);
    }
    ink = 0;
}
/* ---- "DRUGLORD!!!" on one line, growing with each flash until it spans the screen edge to edge, the colour
 *      going from white to dark red.  The letters for each size are drawn into pattern table 1 at run time
 *      (scaled from the 8x8 glyphs, nearest pixel), with the screen off; the screen comes back on in vblank. ---- */
static void vb_pal(u8 a, u8 v);
static void cr_wait(u8 n);
static const char big_text[] = "DRUGLORD!!!";
static u8 big_idx(char c) { u8 i; for (i = 0; i < 7; i++) if (BIG_CHARS[i] == c) return i; return 0; }
static u8 big_g[11];                                  /* offset of each letter's glyph in big_glyph */
static u8 big_cb[256];                                /* [tile column * 8 + glyph row]: that column's 8 pixels */
#define big_sx ((u8*)0x0200)                         /* the source column (0-87) of each screen pixel column:
                                                      * borrows the credits' sprite page, unused by then */
static u8 big_sy[24];                                 /* the glyph row (0-7) of each screen pixel row */
/* draw the line w tiles wide and h tiles high, centred; tiles 1.. of pattern table 1 (tile 0 stays blank).
 * The 6502 has no divide, so the scaling uses running counters instead of division. */
static void big_line(u8 w, u8 h) {
    u8 tx, ty, py, sy, b, k, x0, y0, t, sx, i;
    u16 wp = (u16)w * 8, acc, px;
    u8* cb;
    for (px = 0, acc = 0, sx = 0; px < wp; px++) {     /* source column = px * 88 / wp */
        big_sx[px] = sx;
        acc += 88; while (acc >= wp) { acc -= wp; sx++; }
    }
    for (py = 0, i = 0, sy = 0; py < h * 8; py++) {    /* glyph row = py / h */
        big_sy[py] = sy;
        if (++i == h) { i = 0; sy++; }
    }
    cb = big_cb;
    for (tx = 0; tx < w; tx++) {                      /* each tile column's bytes for the 8 glyph rows */
        const u8* xs = big_sx + tx * 8;
        for (sy = 0; sy < 8; sy++) {
            b = 0;
            for (k = 0; k < 8; k++) {
                sx = xs[k];
                if (big_glyph[big_g[sx >> 3] + sy] & (0x80 >> (sx & 7))) b |= 0x80 >> k;
            }
            *cb++ = b;
        }
    }
    t = 1;
    for (ty = 0; ty < h; ty++)
        for (tx = 0; tx < w; tx++, t++) {
            const u8* c = big_cb + tx * 8;
            const u8* r = big_sy + ty * 8;
            latch = PPUSTATUS; PPUADDR = 0x10 | (t >> 4); PPUADDR = t << 4;
            for (k = 0; k < 8; k++) PPUDATA = c[r[k]];
            for (k = 0; k < 8; k++) PPUDATA = 0;      /* colour 1 only */
        }
    x0 = (32 - w) / 2; y0 = 14 - h / 2;
    t = 1;
    for (ty = 0; ty < h; ty++) { ppu_at(x0, y0 + ty); for (tx = 0; tx < w; tx++) PPUDATA = t++; }
}
static void big_step(u8 w, u8 h, u8 col) {
    clear_screen();                                   /* screen off */
    bgctl = 0x10; pt1_set = 0;                        /* pattern table 1 is overwritten: the next load_pt1 reloads */
    big_line(w, h);
    latch = PPUSTATUS; PPUADDR = 0x3F; PPUADDR = 0x01; PPUDATA = col;
    ppu_show();                                       /* on again, in vblank */
}
/* the sound for each flash of DRUGLORD!!! (f = 0..4) */
static void druglord_sfx(u8 f) {
    static const u16 n[5] = {HZ(523), HZ(659), HZ(784), HZ(1047), HZ(1319)};
    snd(n[f], 6);
}
void show_win(void) {
    i16 pct = 0;
    u32 score;
    u8 rk = 0, f;
    /* "DRUGLORD!!!" 27 tiles wide in white, flashing on and off five times, as the DOS version flashes it */
    music_stop();
    for (f = 0; f < 11; f++) big_g[f] = big_idx(big_text[f]) * 8;
    big_step(27, 3, 0x0F);                            /* drawn while black */
    for (f = 0; f < 5; f++) {
        vb_pal(0x01, 0x30);                           /* on, with the sound */
        druglord_sfx(f);
        cr_wait(16);
        vb_pal(0x01, 0x0F);                           /* off */
        cr_wait(8);
    }
    vb_pal(0x01, 0x30); cr_wait(40);
    if (total_deals != 0) pct = (i16)(good_deals * 100) / total_deals;
    score = score_calc(cash + bank - debt, pct);
    while (rk < 10 && rank_thr[rk] <= score) rk++;
    end_scene();
    sfx(4);
    end_box(score, rk);
    set_palette();                                       /* the scene changed the game palettes */
    hs_after_win(score, rk);
}

u8 ask_play_again(void) {
    page();
    ps_at(1, 12, "Would ya like ta");
    ps_at(1, 13, "play again?");
    return yn();
}
/* the NOTICE shown when you quit; A goes back to the title screen */
void exit_screen(void) {
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
    pal3(0x2A, 0x1A); colour_rows(24, 25, 3);
    ppu_show();
    while (!(wait_btn() & BTN_A)) {}
}

#ifdef DEBUG
void debug_menu(void);
#endif


/* shown for 4 seconds before the title screen */
/* ---- the credits at boot: "NES port by justz00t" falls into place letter by letter in a random order (sprites,
 *      each turned into a background tile as it lands), flashes once; then the three red lines fade in from black
 *      and flash once. ---- */
#define OAM ((u8*)0x0200)            /* the sprite buffer, copied to the PPU each frame by DMA ($0200-$02FF is free RAM) */
#define CR_ROW 16                    /* the falling line */
#define CR_X 6
static const char cr_text[] = "NES port by justz00t";
static void vb_pal(u8 a, u8 v) {     /* set one palette entry during vblank, the screen staying on */
    wait_vblank(); frames++;
    latch = PPUSTATUS; PPUADDR = 0x3F; PPUADDR = a; PPUDATA = v;
    PPUADDR = 0x3F; PPUADDR = 0; PPUADDR = 0; PPUADDR = 0;
    PPUCTRL = bgctl; PPUSCROLL = 0; PPUSCROLL = 0;
}
static void cr_wait(u8 n) { while (n--) { wait_vblank(); frames++; } }
void credits_screen(void) {
    u8 ord[20], n = 0, i, j, t, k, done = 0, land_n, land[4];
    u16 y[20], v[20];                /* each letter's top in 1/16 pixels, its speed in 1/16 pixels a frame */
    u8 st[20];                       /* 0 waiting, 1 falling, 2 landed */
    clear_screen(); set_palette();
    ps_at(5, 8, "Written by Phil Erwin");
    ps_at(7, 10, "Copyright 1991-93");
    ps_at(8, 12, "FantasyWare Inc.");
    colour_rows(8, 13, 1); colour_rows(16, 17, 3);
    ppu_off();
    latch = PPUSTATUS;
    PPUADDR = 0x3F; PPUADDR = 0x05; PPUDATA = 0x0F;      /* palette 1 (the red lines): black for now */
    PPUADDR = 0x3F; PPUADDR = 0x0D; PPUDATA = 0x2A;      /* palette 3: green */
    PPUADDR = 0x3F; PPUADDR = 0x11; PPUDATA = 0x2A;      /* sprite palette 0: green */
    for (i = 0; i < 20; i++) if (cr_text[i] != ' ') ord[n++] = i;
    for (i = n - 1; i > 0; i--) { j = rnd() % (i + 1); t = ord[i]; ord[i] = ord[j]; ord[j] = t; }   /* a random order */
    for (i = 0; i < 20; i++) { st[i] = 0; y[i] = 0; v[i] = 0; }
    for (i = 0; i < 64; i++) OAM[i * 4] = 0xFF;
    APU(0x14) = 0x02;                                    /* no stray sprites on the first frame */
    ppu_show();
    PPUMASK = 0x1E;                                      /* sprites on */
    for (t = 0; done < n; t++) {
        /* start a letter every 6 frames; move the falling ones (gravity: half a pixel a frame, every frame) */
        if (t % 6 == 0 && t / 6 < n) st[ord[t / 6]] = 1;
        land_n = 0;
        for (k = 0; k < n; k++) {
            i = ord[k];
            if (st[i] != 1) { OAM[k * 4] = 0xFF; continue; }
            v[i] += 8; y[i] += v[i];
            if (y[i] >= (u16)(CR_ROW * 8 + 8) * 16) {     /* landed (the letter started 8 pixels above the screen) */
                st[i] = 2; done++; OAM[k * 4] = 0xFF;
                if (land_n < 4) land[land_n++] = i;
                continue;
            }
            OAM[k * 4] = (u8)(y[i] >> 4) < 8 ? 0xFF : (u8)((y[i] >> 4) - 8 - 1);
            OAM[k * 4 + 1] = (u8)cr_text[i]; OAM[k * 4 + 2] = 0; OAM[k * 4 + 3] = (CR_X + i) * 8;
        }
        wait_vblank(); frames++;
        APU(0x14) = 0x02;                                /* sprite DMA from $0200 */
        latch = PPUSTATUS;
        for (j = 0; j < land_n; j++) {
            PPUADDR = 0x22; PPUADDR = (CR_ROW - 16) * 32 + CR_X + land[j]; PPUDATA = (u8)cr_text[land[j]];
        }
        PPUCTRL = bgctl; PPUSCROLL = 0; PPUSCROLL = 0;
    }
    PPUMASK = 0x0A;                                      /* sprites off again */
    cr_wait(15);
    vb_pal(0x0D, 0x30); cr_wait(6); vb_pal(0x0D, 0x2A);  /* the green line flashes */
    cr_wait(20);
    vb_pal(0x05, 0x0F); cr_wait(8);                      /* the red lines fade in, then flash */
    vb_pal(0x05, 0x06); cr_wait(8);
    vb_pal(0x05, 0x16); cr_wait(14);
    vb_pal(0x05, 0x30); cr_wait(6); vb_pal(0x05, 0x16);
    cr_wait(120);
}

/* ---- the title screen: the DOS logo and PRESS START, nothing else.  The logo is white; a red snake crawls
 *      across it and back (a bright red head, its tail fading to pink), then the whole logo flashes
 *      red, white, red, white, and the snake sets off again.  The snake moves by changing the logo's attribute
 *      bytes (one 16-pixel column at a time); the four palettes are fixed:
 *        0: white face   (the logo, and the text)     1: bright red (the head)
 *        2: lighter red  (the tail)                   3: pink       (the tip of the tail)
 *      Each face is a checker of colours 1 and 3 (from the DOS shade character; both the same here, so the faces
 *      are solid), shadow colour 2.  Each part of the snake is two columns (32 pixels) long. ---- */
static const u8 title_pal[16] = {0x0F, 0x30, 0x00, 0x30,  0x0F, 0x16, 0x00, 0x16,  0x0F, 0x26, 0x00, 0x26,  0x0F, 0x36, 0x00, 0x36};
static u8 logo_q[16];                /* the palette of each 16-pixel column of the logo */
void title_screen(void) {
    u8 x, y;
    const u8* m = logo_map;
    clear_screen();
    load_pt1(1);
    ppu_off();
    latch = PPUSTATUS; PPUADDR = 0x3F; PPUADDR = 0x00;
    for (x = 0; x < 16; x++) PPUDATA = title_pal[x];
    bgctl = 0x10;
    for (y = 0; y < LOGO_H; y++) { ppu_at(0, LOGO_Y + y); for (x = 0; x < LOGO_W; x++) PPUDATA = *m++; }
    ps_at(START_X, START_Y, "PRESS START");
    for (x = 0; x < 16; x++) logo_q[x] = 0;
}
/* the logo's attribute bytes from logo_q[] (rows 8-15 of the screen; the logo is on rows 10-15 = attribute bytes 16-31); worked out
 * before vblank, because cc65 code is too slow to do it inside the 20 lines of vblank */
static void logo_attr_calc(void) {
    u8 i, v;
    for (i = 0; i < 8; i++) {
        v = logo_q[i * 2] | (logo_q[i * 2 + 1] << 2);
        v |= v << 4;
        attr[16 + i] = attr[24 + i] = v;
    }
}
static void logo_attr_put(void) {    /* in vblank: 16 bytes, unrolled */
    PPUADDR = 0x23; PPUADDR = 0xC0 + 16;
    PPUDATA = attr[16]; PPUDATA = attr[17]; PPUDATA = attr[18]; PPUDATA = attr[19];
    PPUDATA = attr[20]; PPUDATA = attr[21]; PPUDATA = attr[22]; PPUDATA = attr[23];
    PPUDATA = attr[24]; PPUDATA = attr[25]; PPUDATA = attr[26]; PPUDATA = attr[27];
    PPUDATA = attr[28]; PPUDATA = attr[29]; PPUDATA = attr[30]; PPUDATA = attr[31];
}
/* the snake: the head at column h, the tail behind it (behind = against the direction it moves) */
static void snake(signed char h, signed char dir) {
    signed char c, d;
    for (c = 0; c < 16; c++) {
        d = dir > 0 ? h - c : c - h;
        logo_q[c] = d < 0 || d > 5 ? 0 : 1 + (d >> 1);
    }
}
/* the title screen with its music; returns when START (or A) is pressed.  After 60 seconds the high score table
 * is shown for 2 seconds, then the title again. */
#ifndef TITLE_FRAMES
#define TITLE_FRAMES 3600            /* 60 seconds of the title screen before the score table */
#endif
#define SNAKE_SPEED 3                /* frames per column */
#define FLASH_TIME 10                /* frames per colour of the flash */
void title_wait(void) {
    u8 e, blink, t;
    u16 tf;
    signed char h, dir;
    u8 flash;
    for (;;) {
        music_start();
        title_screen();
        ppu_show();
        blink = 0; t = 0; h = 0; dir = 1; flash = 0; tf = 0;
        snake(h, dir);
        while (++tf < TITLE_FRAMES) {
            /* one step of the logo animation */
            if (++t >= (flash ? FLASH_TIME : SNAKE_SPEED)) {
                t = 0;
                if (flash) {                                     /* red, white, red, white, then the snake */
                    if (++flash > 4) { flash = 0; h = 0; dir = 1; snake(h, dir); }
                    else { for (e = 0; e < 16; e++) logo_q[e] = (flash & 1) ? 1 : 0; }
                } else {
                    h += dir;
                    if (dir > 0 && h > 15) { h = 14; dir = -1; }  /* the bounce at the right end */
                    if (dir < 0 && h < -6) {                      /* back home, tail and all: flash */
                        flash = 1; for (e = 0; e < 16; e++) logo_q[e] = 1;
                    } else snake(h, dir);
                }
            }
            /* this frame's vblank: "PRESS START" on or off every half second, the logo colours on the other frames */
            if ((blink & 31) == 0) {
                const char* s = (blink & 32) ? "           " : "PRESS START";
                wait_vblank();
                latch = PPUSTATUS;
                PPUADDR = 0x20 + (START_Y >> 3); PPUADDR = ((START_Y & 7) << 5) + START_X;
                for (e = 0; e < 11; e++) PPUDATA = (u8)s[e];
            } else {
                logo_attr_calc();
                wait_vblank();
                latch = PPUSTATUS;
                logo_attr_put();
            }
            PPUCTRL = bgctl; PPUSCROLL = 0; PPUSCROLL = 0;
            frames++; blink++;
            music_update();
            e = read_pad(); { u8 d = e & ~pad_old; pad_old = e; e = d; }
            if (e & (BTN_START | BTN_A)) { music_stop(); return; }
        }
        music_stop();
        if (hs_attract()) return;                                /* START on the score table starts a game too */
    }
}

#ifdef DEBUG
/* ======================================================================== */
/*  Debug build only: SELECT+START in the main menu opens this menu         */
/* ======================================================================== */
const char* const snd_name[] = {
    "Beep", "Denied", "Deal", "Siren", "News", "EXTRA!", "Hit", "Jet", "Take-off",
    "Gun 1", "Gun 2", "Gun 3", "Gun 4", "Gun 5", "Death", "Punch", "Tick", "Music"};
const u8 snd_id[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20};
void sound_test(void) {
    u8 i;
    i16 s = 0;
    for (;;) {
        page();
        for (i = 0; i < SND_COUNT; i++) ps_at(2, 4 + i, snd_name[i]);
        s = choose(0, 4, SND_COUNT, s);
        if (s < 0) return;
        if (s == SND_COUNT - 1) {                        /* title music until B */
            music_start();
            ppu_show();
            while (!(vsync_pad() & BTN_B)) {}
            music_stop();
        } else sfx(snd_id[s]);
    }
}

void dbg_wait_release(void) { while (pad_old & (BTN_SELECT | BTN_START)) vsync_pad(); }

const char* const dbg_name[] = {
    "Cop fight", "Cops", "Cheap", "Bust", "Factory",
    "Brownies", "Mug 1", "Mug 2", "Mug 3", "Coat",
    "Guns", "Blackmail", "Thugs", "Death", "Win",
    "Replay", "Notice", "Credit", "Title", "Sounds", "Scores", "Entry", "ClrHi", "Coke+200"};
void debug_menu(void) {
    u8 i;
    i16 s = 0;
    dbg_wait_release();
    for (;;) {
        page();
        for (i = 0; i < DBG_COUNT; i++) ps_at(2, 3 + i, dbg_name[i]);
        s = choose(0, 3, DBG_COUNT, s);
        if (s < 0) { dbg_wait_release(); return; }
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
        case 14: cash = 30000000L; total_days = 100; good_deals = total_deals = 50; gun_type = 4; num_guns = 7; show_win(); break;
        case 15: ask_play_again(); break;
        case 16: exit_screen(); break;
        case 17: credits_screen(); break;
        case 18: title_wait(); break;
        case 19: sound_test(); break;
        case 20: hs_show(0xFF); break;
        case 21: hs_entry(hs_place(777777UL), 777777UL, 6); break;
        case 22: hs_reset(); break;
        case 23: inv[0] += 200; if (hold < 400) hold = 400; break;   /* enough cocaine to try the easter egg */
        }
        dead = 0;
        if (health <= 0) health = 100;
    }
}
#endif

