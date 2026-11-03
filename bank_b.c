/* PRG bank 1: start/end screens, score, title, high scores, debug menu */
#include "dl.h"
#define TBL_SCORE
#include "tables.h"

void init_game(void) {
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

/* a box of tiles 1..6 (corners and edges), w x h tiles */
static void box(u8 x, u8 y, u8 w, u8 h) {
    u8 i;
    at(x, y); pc(1); for (i = 2; i < w; i++) pc(2); pc(3);
    for (i = 1; i + 1 < h; i++) { at(x, y + i); pc(4); at(x + w - 1, y + i); pc(4); }
    at(x, y + h - 1); pc(5); for (i = 2; i < w; i++) pc(2); pc(6);
}
void show_death(void) {
    page();
    delay(60);
    ink = 2; box(5, 7, 22, 9);
    ink = 1; ps_at(10, 9, "R . I . P .");
    ink = 0; ps_at(9, 12, "You are dead.");
    sfx(3);
    delay(200);
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
void show_win(void) {
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
    pal3(0x2A, 0x1A); colour_rows(0, 1, 1); colour_rows(18, 19, 3);
    sfx(4);
    press_a();
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
void credits_screen(void) {
    u8 n;
    clear_screen(); set_palette();
    ps_at(5, 8, "Written by Phil Erwin");
    ps_at(7, 10, "Copyright 1991-93");
    ps_at(8, 12, "FantasyWare Inc.");
    ps_at(6, 15, "NES port by justz00t");
    pal3(0x2A, 0x1A); colour_rows(8, 13, 1); colour_rows(14, 15, 3);
    ppu_show();
    for (n = 0; n < 240; n++) wait_vblank();
}

void title_screen(void) {
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
void start_text(u8 on) {
    const char* s = "PRESS START";
    u8 i;
    wait_vblank();
    ppu_at(START_X, START_Y);
    for (i = 0; i < 11; i++) PPUDATA = on ? (u8)s[i] : ' ';
    PPUCTRL = 0; PPUSCROLL = 0; PPUSCROLL = 0;
}

/* the title screen with its music; returns when START (or A) is pressed */
void title_wait(void) {
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
const char* const snd_name[] = {
    "Beep", "Denied", "Deal", "Siren", "News", "EXTRA!", "Hit",
    "Gun 1", "Gun 2", "Gun 3", "Gun 4", "Gun 5", "Music"};
const u8 snd_id[] = {2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15};
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
    "Replay", "Notice", "Credit", "Title", "Sounds", "Scores", "Entry", "ClrHi"};
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
        case 14: cash = 30000000L; total_days = 100; good_deals = total_deals = 50; show_win(); break;
        case 15: ask_play_again(); break;
        case 16: exit_screen(); break;
        case 17: credits_screen(); break;
        case 18: title_wait(); break;
        case 19: sound_test(); break;
        case 20: hs_show(0xFF); break;
        case 21: hs_entry(hs_place(777777UL), 777777UL, 6); break;
        case 22: hs_reset(); break;
        }
        dead = 0;
        if (health <= 0) health = 100;
    }
}
#endif

