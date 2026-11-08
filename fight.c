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
}
void fight_msg(const char* s) { fill(1, fm_y, ' ', 30); fill(1, fm_y + 1, ' ', 30); at(1, fm_y); ink = 2; ps(s); ink = 0; }

void cop_fight(void) {
    i16 choice, nf = 0, nc, ct, shots = 1, i, t, copsAlive, partyAlive, result = 0, att, si;
    u8 k;
    cop_flag = 0;
    if (gun_type >= 4) shots = 3;
    page();
    ink = 2; ps_at(1, 1, "The cops are on your tail!!!");
    sfx(5);
    ink = 3; ps_at(1, 3, "Do you wanna");
    ps_at(3, 4, "Go at it alone");
    ps_at(3, 5, "Call a friend (1 status pt.)");
    ps_at(3, 6, "Call a posse (5 status pts.)");
    for (;;) {
        choice = choose(1, 4, 3, 0);
        if (choice < 0) continue;
        choice++;
        if ((choice == 2 && status == 0) || (choice == 3 && status < 5)) {
            ink = 2; ps_at(1, 8, "You don't have enough points!");
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
    ink = 2; ps_at(2, 1, "* * *  D R U G L O R D  * * *");
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
    for (;;) {
        fill(1, 28, ' ', 30);
        ink = 0; ps_at(1, 28, "(F)ight or (R)un?  A=F B=R");
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
                        fill(1, 26, ' ', 30); fill(1, 27, ' ', 30); at(1, 26); ink = 2;
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
                    fill(1, 26, ' ', 30); fill(1, 27, ' ', 30); at(1, 26); ink = 2;
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
    delay(90);
    page();                                              /* the spoils, on a page of their own */
    fm_y = 1; fight_msg("Yous wasted 'em!!! Way ta go, man!!!");
    fm_y = 8;
    {
        u8 sell = 1;
        if (!(ct < gun_type && nc <= num_guns)) {
            ink = 0; ps_at(1, 4, nc == 1 ? "Take his gun, man? (Y/N)" : "Take their guns, man? (Y/N)");
            ink = 1; ps_at(1, 6, "A = Yes     B = No");
            for (;;) { k = wait_btn(); if (k & (BTN_A | BTN_B)) break; }
            if (k & BTN_A) {
                fight_msg("You want 'em, you got 'em! What a bunch'a chumps!");
                sell = 0;
                if (gun_type == ct) num_guns += nc; else { gun_type = ct; num_guns = nc; }
            } else { fight_msg("OK, man, it's yer call."); delay(90); }
        }
        if (sell) {
            i32 gold = (i32)nc * ct * 200;
            at(1, 11); wx0 = 1; wx1 = 30; wy0 = 11; wy1 = 26;
            sayc(3, "You sell the "); say_num(nc); say(" guns that were on the pigs you wasted for ");
            say_num(gold); say(" bucks! This'll teach 'em not to mess with ya, huh?");
            cash += gold; status += nc;
            delay(200);
        }
    }
    press_a();
}

