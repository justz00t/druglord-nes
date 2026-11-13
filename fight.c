/* PRG bank 5: the cop fight */
#include "dl.h"

/* ======================================================================== */
/*  Cop fight                                                               */
/* ======================================================================== */
i16 php[10], chp[12];
static u8 pmax[10], cmax[12];          /* each fighter's starting health (the 100% of the health colours) */
static u8 fm_y = 26;                    /* the row of fight_msg */
/* the fighters are on every other row (4, 6, ... 22) so each health value has its own attribute row (its colour) */
void fight_row(u8 side, u8 i, i16 hp) {
    u8 x = side ? 17 : 1, y = 4 + i * 2;
    ink = 0; at(x, y);
    if (!side) { if (i == 0) ps_pad("You", 8); else { ps("Mate "); pn_l(i, 3); } }
    else { ps("Cop "); pn_l(i + 1, 4); }
    at(x + 9, y);
    hp_put(hp, side ? cmax[i] : (i ? pmax[i] : 100), 4, 1);
    lflush();
}
void fight_msg(const char* s) {                         /* two rows of 30, padded (written live in the fight) */
    u8 r, n;
    ink = 2;
    for (r = 0; r < 2; r++) { at(1, fm_y + r); for (n = 0; n < 30; n++) pc(*s ? (u8)*s++ : ' '); }
    lflush(); ink = 0;
}

/* the shot messages are written over the old one, padded to the full row, in one vblank (live mode) */
static void mb_start(void) { ink = 2; at(1, 26); }
#define mb_s ps
#define mb_n pn
static void mb_show(void) { while (cx < 31) pc(' '); lflush(); ink = 0; }
/* a hit: a white star over the health value (centred on its first two digits), then the new value in its colour */
static void hit_star(u8 side, u8 i) {
    u8 x = side ? 26 : 10, y = 4 + i * 2;
    ink = 0; at(x, y); pc(0x98); pc(0x99); pc(' '); pc(' '); lflush();
    delay(16);
}
/* The "cops are on your tail" screen: police lights flash red on the left half, then blue on the right, back
 * and forth, half the screen at a time; the title flashes white in the middle; the choice menu is below.
 * Solid blocks (tile 8) fill the light bands - their colour (palette 0 left, palette 1 right) is flashed; the
 * text bands are left black with white text.  Returns 1 = alone, 2 = friend, 3 = posse. */
static i16 cops_tail(void) {
    static const char* const opt[3] = {"Go at it alone", "Call a friend (1 status pt.)", "Call a posse (5 status pts.)"};
    static const char deny[] = "Not enough status points!";
    u8 y, f = 0, sel = 0, osel, e, i, dn = 0, ddraw = 0;
    page();
    for (y = 0; y <= 11; y++) fill(0, y, 8, 32);         /* the police-light field: solid blocks */
    fill(0, 14, 8, 32); fill(0, 15, 8, 32);
    for (y = 24; y <= 29; y++) fill(0, y, 8, 32);
    ink = 0;
    ps_at(2, 13, "THE COPS ARE ON YOUR TAIL!!!");        /* 28 chars, centred */
    ps_at(10, 17, "Do you wanna");
    for (i = 0; i < 3; i++) ps_at(4, 19 + i, opt[i]);
    colour_cols(0, 11, 0, 7, 0);  colour_cols(0, 11, 8, 15, 1);   /* left half palette 0, right half palette 1 */
    colour_cols(14, 15, 0, 7, 0); colour_cols(14, 15, 8, 15, 1);
    colour_cols(24, 29, 0, 7, 0); colour_cols(24, 29, 8, 15, 1);
    colour_rows(12, 13, 3);                              /* the flashing title */
    colour_rows(16, 23, 2);                              /* the menu: steady white */
    ppu_off();
    latch = PPUSTATUS;
    PPUADDR = 0x3F; PPUADDR = 0x00; PPUDATA = 0x0F;      /* backdrop black */
    PPUADDR = 0x3F; PPUADDR = 0x09; PPUDATA = 0x30;      /* palette 2 colour 1: white (menu + title text) */
    ppu_at(2, 19); PPUDATA = 0x10;                       /* the arrow (drawn now, while the screen is off) */
    ppu_show();
    song(SONG_FIGHT);                                    /* the fight music, from here to the last shot */
    seq_sfx(SFX_SIREN);                                  /* the siren wails over it */
    for (;;) {
        wait_vblank(); frames++;
        latch = PPUSTATUS;
        if (f & 8) { PPUADDR = 0x3F; PPUADDR = 0x01; PPUDATA = 0x0F; PPUADDR = 0x3F; PPUADDR = 0x05; PPUDATA = 0x11; }  /* right half blue */
        else       { PPUADDR = 0x3F; PPUADDR = 0x01; PPUDATA = 0x16; PPUADDR = 0x3F; PPUADDR = 0x05; PPUDATA = 0x0F; }  /* left half red */
        PPUADDR = 0x3F; PPUADDR = 0x0D; PPUDATA = (f & 16) ? 0x0F : 0x30;   /* the title flashes white */
        if (ddraw) { ppu_at(3, 23); for (i = 0; i < 25; i++) PPUDATA = (ddraw == 1) ? (u8)deny[i] : ' '; ddraw = 0; }
        PPUCTRL = bgctl | 0x80; PPUSCROLL = 0; PPUSCROLL = 0;
        music_update();
        f++;
        if (dn && --dn == 0) ddraw = 2;
        e = read_pad(); { u8 d = e & ~pad_old; pad_old = e; e = d; }
        osel = sel;
        if (e & BTN_UP) sel = sel ? sel - 1 : 2;
        else if (e & BTN_DOWN) sel = sel < 2 ? sel + 1 : 0;
        else if (e & BTN_A) {
            if ((sel == 1 && status == 0) || (sel == 2 && status < 5)) { seq_sfx(SFX_DENIED); dn = 80; ddraw = 1; }
            else break;
        }
        if (osel != sel) { put2(2, 19 + osel, ' ', 2, 19 + sel, 0x10); seq_sfx(SFX_TICK); }
    }
    return sel + 1;
}

void cop_fight(void) {
    i16 choice, nf = 0, nc, ct, shots = 1, i, t, copsAlive, partyAlive, result = 0, att, si;
    u8 k;
    cop_flag = 0;
    if (gun_type >= 4) shots = 3;
    choice = cops_tail();
    if (choice == 2) { nf = 1; status--; }
    else if (choice == 3) {
        nf = rnd() % 4 + status / 100 + 4;
        if (nf > 8) nf = 8;
        status -= 5;
    }
    clear_screen();
    ink = 1; ps_at(1, 3, "YOU"); ps_at(17, 3, "COPS");
    ink = 0;
    php[0] = health;
    for (i = 1; i <= nf; i++) { php[i] = rnd() % 10 + dice(10, status / 50) + 25; pmax[i] = php[i] > 255 ? 255 : php[i]; }
    fm_y = 26;
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
    for (i = 0; i < nc; i++) { chp[i] = dice(15, 2) + 15; cmax[i] = chp[i]; }
    copsAlive = nc; partyAlive = nf + 1;
    for (i = 0; i <= nf; i++) fight_row(0, i, php[i]);
    for (i = 0; i < nc; i++) fight_row(1, i, chp[i]);
    at(1, 24); ps("Guns: "); pn(num_guns); ps(" "); ps(gun_name[gun_type]);
    at(1, 25); ps("Cops: "); ps(gun_name[ct]);
    ink = 0; ps_at(1, 28, "(F)ight or (R)un?  A=F B=R");
    colour_rows(26, 27, 2);                              /* the message rows red */
    live = 1;                                            /* from here on the screen stays on while it changes */
    for (;;) {
        for (;;) { k = wait_btn(); if (k & (BTN_A | BTN_B)) break; }
        if (k & BTN_B) {                                 /* run */
            fight_msg("You take off running...");
            sfx(SFX_RUN);
            delay(30);
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
                        sfx(si < num_guns ? SFX_GUN1 - 1 + gun_type : SFX_PUNCH);
                        mb_start(); mb_s(si < num_guns ? "Firing at cop " : "Punching cop "); mb_n(t + 1);
                        if (rnd() % 2 == 0) {
                            mb_s(" *Hit!*"); mb_show(); seq_sfx(SFX_HIT);
                            chp[t] -= (si < num_guns) ? dice(6, gun_dice[gun_type]) : dice(4, 1);
                            if (chp[t] <= 0) { chp[t] = 0; copsAlive--; }
                            hit_star(1, t);
                            fight_row(1, t, chp[t]);
                        } else { mb_s(" -Missed-"); mb_show(); }
                        delay(20);
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
                    sfx(SFX_GUN1 - 1 + ct);
                    mb_start(); mb_s("Cop "); mb_n(si + 1); mb_s(" shoots "); if (t == 0) mb_s("you"); else { mb_s("mate "); mb_n(t); }
                    if (rnd() % 2 == 0) {
                        mb_s(" *Hit!*"); mb_show(); seq_sfx(SFX_HIT);
                        php[t] -= dice(6, gun_dice[ct]);
                        if (php[t] <= 0) { status--; php[t] = 0; partyAlive--; }
                        hit_star(0, t);
                        fight_row(0, t, php[t]);
                    } else { mb_s(" -Missed-"); mb_show(); }
                    delay(20);
                    if (php[0] == 0 || partyAlive == 0) break;
                }
            }
        }
        if (!(copsAlive > 0 && partyAlive > 0 && php[0] > 0 && result != 1)) break;
    }
    music_stop();
    if (result == 1) { delay(60); live = 0; return; }
    health = php[0];
    if (health == 0) { dead = 1; delay(40); live = 0; return; }
    fight_msg("Yous wasted 'em!!! Way ta go, man!!!");
    sfx(SFX_WIN);
    delay(60);
    page();                                              /* the spoils, on a page of their own: the same look as the event pages */
    sayc(2, "Yous wasted 'em!!! Way ta go, man!!!");
    {
        u8 sell = 1;
        if (!(ct < gun_type && nc <= num_guns)) {
            gap();
            sayc(1, nc == 1 ? "Take his gun, man? (Y/N)" : "Take their guns, man? (Y/N)");
            if (yn()) {
                gap();
                sayc(0, "You want 'em, you got 'em! What a bunch'a chumps!");
                sell = 0;
                if (gun_type == ct) num_guns += nc; else { gun_type = ct; num_guns = nc; }
                sfx(SFX_COCK);
            } else { gap(); sayc(0, "OK, man, it's yer call."); }
        }
        if (sell) {
            i32 gold = (i32)nc * ct * 200;
            gap();
            sayc(0, "You sell the "); say_num(nc); say(" guns that were on the pigs you wasted for ");
            say_num(gold); say(" bucks! This'll teach 'em not to mess with ya, huh?");
            cash += gold; status += nc;
            sfx(SFX_DEAL);
        }
    }
    press_a();
}

