/* PRG bank 0: random events, the menus */
#include "dl.h"
#define TBL_COAT
#include "tables.h"

/* ======================================================================== */
/*  Other random events                                                     */
/* ======================================================================== */
u8 market_event(u8 count, i16 mul_pct) {
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
void ev_cops_sell(void) {
    if (mkt_today == 1) return;
    page();
    sayc(3, "\"Hey man, check dis out!  Word has it on de street dat deres des cops, see? They musta decided that workin' for 25 G's a year ain't enough, so de've decided ta sell some of de take drugs confiscated on de side! De market is flooded with");
    market_event(3, 70);
    say("!\"");
    sfx(6); press_a();
}
void ev_cheap(void) {
    page();
    sayc(0, "\"Hey, I heard it through de grapevine dat deres some cheap homemade stuff available from some of the homeboys downtown.  Real primo stuff, dey say! All I hear on de streets is 'Cheap");
    market_event(3, 60);
    say("!'\"");
    press_a();
}
void ev_newspaper(void) {
    u8 d;
    page();
    sayc(1, "EXTRA! EXTRA! Dope prices soar after a big drug bust!!!");
    for (d = 0; d < NUM_DRUGS; d++) price[d] = (price[d] * (105 + rnd() % 25)) / 100;
    sfx(7); press_a();
}
void ev_factory(void) {
    page();
    sayc(2, "\"Dude, I got some news for ya... a factory of one of de cities biggest suppliers of");
    market_event(1, 120);
    say(" "); say(rnd() % 2 == 1 ? "burned down" : "blew up");
    say(" last night.  I don't have to tell ya what this'll do on de street...\"");
    press_a();
}
void ev_brownies(void) {
    if (inv[5] == 0) return;
    page();
    sayc(1, "Ya come back to yer place tonight and find dat yer sexy main squeeze tought dat it wouldst be a good idea to use some of yer weed to make brownies!!!\nAll tings considered, yous take it pretty well.  After all, if ya were mad, you would've chopped up her body before dumping it in de river!");
    inv[5] = 0;
    press_a();
}
void ev_mugger(void) {
    u8 scared = 0;
    page();
    sayc(2, "Yer walking along de street at night, when from outta nowhere jumps dis goon! He waves a 9mm Beretta at yous and demands yer fork over all yer money.\n");
    if (status >= 10 && rnd() % 20 != 0) {
        sayc(1, "Do ya wanna use yer status as a major drug dealer to get outta this?\n");
        if (yn()) {
            fill(1, 26, ' ', 30);
            sayc(2, "Yous tell him whos he's dealin' wit.  He looks nervous, asks fer yer pardon, den runs off down an alley!");
            status -= 10; scared = 1;
        }
    }
    if (!scared) {
        fill(1, 26, ' ', 30);
        if (cash < 50) {
            sayc(3, "\"This is all you got? &%&*! this, man!\" He looks at ya like a crazed animal, and pulls de trigger!\n* * *  B L A M  * * *");
            sfx(13);
            health -= rnd() % 6 + rnd() % 6 + 2;
        } else sayc(2, "He laughs merrily as he takes yer cash and runs off down de street!");
        cash = 0;
    }
    press_a();
}
void ev_trenchcoat(void) {
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
    sayc(3, "You are approached by a shady-lookin guy in a long dark trenchcoat.  He says:\n\"Hey, bud, I got a special trenchcoat for ya, just your style! It's got a capacity of ");
    say_num(cap); say(" and I won't take under "); say_money(cost); say(" for it.\"\nIs it a deal?\n");
    if (yn()) {
        fill(1, 26, ' ', 30);
        if (cash < cost) sayc(3, "\"Sorry Charly, I don't take credit! Maybe when you're a little more solvent!\"");
        else { sayc(3, "\"You bought yourself one hell of a trenchcoat!\""); cash -= cost; hold = cap; }
    } else { fill(1, 26, ' ', 30); sayc(3, "\"OK, it's your loss, man.  What a shame!\""); }
    press_a();
}
void ev_gun_smuggler(void) {
    i16 kind = rnd() % 5, qty;
    i32 each, total;
    if (rnd() % 10 == 0) qty = rnd() % 6 + 5; else qty = rnd() % 5 + 2;
    each = kind * 200 + 200; total = qty * each;
    page();
    sayc(2, "Juan, one of the most powerful underground gun smugglers in the world, comes to you with an offer:\n\"I have a shipment of ");
    say(gun_long[kind]); say("'s coming in tonight.  I'm distributing them in shipments of ");
    say_num(qty); say(" at "); say_money(each); say(" each, for a total of "); say_money(total);
    say(".\"\nAre you interested in an order?\n");
    if (yn()) {
        fill(1, 26, ' ', 30);
        if (total > cash) sayc(0, "\"I'm sorry, sir, but I only accept cash up-front.  Business is business.\"");
        else {
            sayc(0, "\"Very well, they will be delivered by tomorrow.  Good doing business with you.\"");
            if (gun_type == kind + 1) num_guns += qty; else num_guns = qty;
            gun_type = kind + 1;
            cash -= total;
        }
    } else { fill(1, 26, ' ', 30); sayc(0, "\"As you wish.  Perhaps next time we may do business.\""); }
    press_a();
}
void ev_blackmail(void) {
    i32 amount;
    if (cash < 10000L) return;
    amount = (i32)(((u32)cash * (70 + rnd() % 30)) / 100UL / 1000UL) * 1000L;
    page();
    sayc(3, "Yous get a note in de mail from a business associate of yers.  He says dat if yous don't deposit ");
    say_money(amount); say(" in his Swiss bank account, he's gonna rat to de cops everything dat he knows about you.\nDo you pay the blackmail?\n");
    if (yn()) {
        fill(1, 26, ' ', 30);
        sayc(2, "You wire de money into his account.  That really ^$&?*@ sucks!!!");
        cash -= amount;
    } else {
        fill(1, 26, ' ', 30);
        sayc(2, "Yeah, man, nobody pushes you around! I respect yer integrity!!!");
        if (rnd() % 5 != 0) { cop_flag = 1; done[0] = 0; }
    }
    press_a();
}
void do_event(u8 ev) {
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
i16 pick_drug(const char* prompt) {
    i16 d;
    msg_area();
    ink = 2; say(prompt); say(" (A = pick, B = cancel)");
    d = choose(0, ROW_DRUGS, NUM_DRUGS, 0);
    msg_area();
    return d;
}
u8 menu_buy(void) {
    i16 d;
    i32 max_buy, qty;
    d = pick_drug("What kinda drugs are ya interested in?");
    if (d < 0) return 0;
    max_buy = cash / price[d];
    msg_area();
    sayc(2, "How many do ya wanna buy? ("); say_num(max_buy); say(" max.)  SELECT = most you can take");
    qty = pick_number(1, 26, 5, max_buy < space ? max_buy : space);
    msg_area();
    if (qty < 0) return 0;
    if (qty > max_buy) { sayc(2, "Hey man, if ya don't got enough cash, don't come 'round here!"); sfx(3); msg_wait(); return 0; }
    if (qty > space) { sayc(2, "Dude, you can't hold that much! Everyone's got a limit, right?"); sfx(3); msg_wait(); return 0; }
    if (qty > 0) {
        inv[d] += (i16)qty;
        cash -= qty * price[d];
        basis[d] = new_basis(basis[d], price[d]);
        sfx(4);
    }
    return 1;
}
u8 menu_sell(void) {
    i16 d;
    i32 qty;
    d = pick_drug("What kinda drugs do ya want to unload?");
    if (d < 0) return 0;
    if (inv[d] == 0) { sayc(2, "Give me a break, man, you ain't got any of that!"); sfx(3); msg_wait(); return 0; }
    msg_area();
    sayc(2, "How many do ya wanna sell? SELECT = all");
    qty = pick_number(1, 26, 5, inv[d]);
    msg_area();
    if (qty < 0) return 0;
    if (qty > inv[d]) { sayc(2, "I happen to know that you ain't that much on ya, so quit trying to burn me!"); sfx(3); msg_wait(); return 0; }
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
        /* the easter egg: over 100 cocaine in one sale switches the main-screen music for the rest of the game */
        if (d == 0 && qty > 100 && !easter_on) { easter_on = 1; easter_music_start(); }
    }
    return 1;
}
/* air fare = distance on the map * 60 + 25.  floor(sqrt(3600 * (dx^2 + dy^2))) comes from a table: the grid is
 * 6 x 5, so dx^2 + dy^2 is at most 41 (the loop it replaces took a quarter of a second per city).
 * Only the jet menu uses it, so it lives here in bank A. */
static const u16 dist60[42] = {0,60,84,103,120,134,146,158,169,180,189,198,207,216,224,232,240,247,254,261,268,274,281,287,293,300,305,311,317,323,328,334,339,344,349,354,360,364,369,374,379,384};
static i32 travel_cost(u8 a, u8 b) {
    i16 dx = (i16)city_x[a] - city_x[b], dy = (i16)city_y[a] - city_y[b];
    return (i32)dist60[dx * dx + dy * dy] + 25;
}
/* a rough map of the USA for the jet menu (rows 21-27); the cities sit at their city_x/city_y grid spots */
static const char* const usa_map[7] = {
    "  ..............-....\\         ",
    " |                    \\______  ",
    " |                           \\ ",
    "  \\                         |  ",
    "   |                     __.'   ",
    "   '-._____   ______.--'  |     ",
    "           '-'              \\.  "};
static const char* const city_code[NUM_CITIES] = {"CHI", "DET", "LVS", "LA", "MIA", "NYC", "SD", "DC"};
static void draw_map(void) {
    u8 i, x, y;
    ink = 3;
    for (i = 0; i < 7; i++) ps_at(0, 21 + i, usa_map[i]);
    for (i = 0; i < NUM_CITIES; i++) {
        x = 3 + city_x[i] * 5; y = 20 + city_y[i] * 2 - 1;
        if (i == 6 || i == 7) y++;                       /* San Diego sits under Los Angeles, D.C. under New York */
        ink = i == city ? 1 : 0;
        ps_at(x, y, city_code[i]);
    }
    ink = 0;
}
u8 menu_jet(void) {
    i16 c, i;
    i32 fare;
    msg_area(); draw_map();
    ink = 3; ps_at(1, 20, "Where ya wanna go?"); ink = 0;
    for (i = 0; i < NUM_CITIES; i++) {
        at(2, ROW_DRUGS + i); ps_pad(city_name[i], 16);
        at(19, ROW_DRUGS + i);
        if (i == city) ps("here "); else { pc('$'); pn_r(travel_cost(i, city), 5); }
    }
    for (;;) {
        c = choose(0, ROW_DRUGS, NUM_CITIES, city);
        if (c < 0) { draw_status(); msg_area(); return 0; }
        if (c == city) { msg_area(); sayc(2, "Are you having flashbacks? That's where you are now!"); sfx(3); msg_wait(); msg_area(); draw_map(); ink = 3; ps_at(1, 20, "Where ya wanna go?"); ink = 0; continue; }
        break;
    }
    fare = travel_cost(c, city);
    if (fare > cash) {
        msg_area(); sayc(2, "Start flappin' yer arms, cause you ain't got de dough!"); sfx(3); msg_wait();
        draw_status(); return 0;
    }
    cash -= fare;
    city = c;
    mkt_today = 0;
    gen_prices();
    for (i = 0; i < 10; i++) done[i] = 0;
    sfx(9); sfx(10);
    draw_status();
    return 1;
}
u8 menu_bank(void) {
    i16 c;
    i32 amt;
    msg_area();
    sayc(0, "You arrive at the First World Bank of America.");
    ps_at(3, 24, "Deposit some moola");
    ps_at(3, 25, "Withdraw some moola");
    ps_at(3, 26, "Leave");
    c = choose(1, 24, 3, 0);
    if (c < 0 || c == 2) { msg_area(); return 0; }
    msg_area();
    ink = 3; say(c == 0 ? "How much 'ya gonna rathole?" : "How much 'ya gonna pull out?");
    say(" SELECT = all");
    amt = pick_number(1, 26, 10, c == 0 ? cash : bank);
    msg_area();
    if (amt <= 0) return 0;
    if (c == 0) {
        if (amt > cash) { sayc(2, "You ain't got that much to deposit!"); sfx(3); msg_wait(); }
        else { cash -= amt; bank += amt; sfx(2); draw_status(); }
    } else {
        if (amt > bank) { sayc(2, "You ain't got that much to pull out!"); sfx(3); msg_wait(); }
        else { cash += amt; bank -= amt; sfx(2); draw_status(); }
    }
    return 0;                                            /* banking does not pass a day */
}
u8 menu_loan(void) {
    i16 c;
    i32 amt;
    msg_area();
    sayc(3, "Ya set foot in the 'sharks office.  His boys are lookin' tough, but you don't pay dem no never-mind, cause Guido is talkin...");
    ps_at(3, 24, "Borrow some cash");
    ps_at(3, 25, "Pay me back");
    ps_at(3, 26, "Leave");
    c = choose(1, 24, 3, 0);
    if (c < 0 || c == 2) { msg_area(); return 0; }
    msg_area();
    if (c == 0) {                                        /* borrow */
        if (angry == 1) { sayc(2, "\"Man, yer pushin' yer luck luck! I ought'ta kill you just for askin'!!\""); sfx(3); msg_wait(); return 1; }
        if (status <= 0) { sayc(3, "\"Look, whoever you are, you're NOBODY to me.  Get outta here!\""); sfx(3); msg_wait(); return 1; }
        if (debt == 0) sayc(3, "\"My books show dat you don't owe me nut'in... would youse like to take out a little loan?\"\n");
        sayc(3, "How much ya wanna borrow?");
        amt = pick_number(1, 26, 10, -1);
        msg_area();
        if (amt <= 0) return 0;
        if (amt < 1000) { sayc(2, "\"Sorry, bud, but I got a $1000 minimum.  Got no time for penny-ante stuff!\""); sfx(3); msg_wait(); return 0; }
        if (amt / 10000 >= status) { sayc(2, "\"No deal, too big a risk for me! I'm a businessman, ya'know!\""); sfx(3); msg_wait(); return 0; }
        {
            i16 days = loan_term(status, debt + amt);
            u8 refuse = 0;
            if ((debt + amt) / 10000 > status) refuse = 1;
            else if (days < loan_days) refuse = 0;
            else if (loan_flag == 1) refuse = 1;
            if (refuse) {
                sayc(3, "\"Look, you already owe me $"); say_num(debt);
                say(" bucks, I'm not givin ya THAT much more!!! Christ, what do I look like, Santi-Claus?\"");
                sfx(3); msg_wait(); return 0;
            }
            debt += amt; cash += amt; loan_flag = 1; loan_days = days;
            sayc(2, "\"Alright, I'll loan ya de money, but if you welch, my boys will come lookin for ya! I expect to see you here to pay me in full in ");
            say_num(days - 1); say(" days.\"");
            sfx(4); msg_wait();
            return 1;
        }
    }
    if (debt == 0) { sayc(3, "\"My books show dat you don't owe me nut'in...\""); msg_wait(); return 0; }
    sayc(3, "How much ya wanna pay back? SELECT = all you owe");
    amt = pick_number(1, 26, 10, debt < cash ? debt : cash);
    msg_area();
    if (amt <= 0) return 0;
    if (amt > cash) { sayc(2, "'fraid you don't got that in cash, my man."); sfx(3); msg_wait(); return 0; }
    if (amt >= debt) debt = 0; else debt -= amt;
    cash -= amt;
    if (debt > 0) {
        sayc(3, "\"OK, but you still owe me $"); say_num(debt); say(" bucks!\"");
    } else {
        loan_days = 0; loan_flag = 0; angry = 0; thugs = 0;
        sayc(3, "\"Paid in full? Lucky for you, eh boys? Anytime yous need a loan, just come to me, I'll take care of ya...\"");
    }
    sfx(4); msg_wait();
    return 1;
}
u8 menu_hospital(void) {
    i32 cost;
    i16 stay, n;
    msg_area();
    if (health == 100) {
        sayc(0, "Ya have de doc check yous out, but aside from yer bunions on yer feet, youse is fit as a fiddle!");
        msg_wait(); return 0;
    }
    cost = (100 - health) * 10L;
    sayc(0, "You drag yer sorry butt into de loc'l Hospital.  The docs check ye out, and come to the conclusion that it'll cost ");
    say_num(cost); say(" dollars to fix ya up.\nDo ya sign the consent forms?\n");
    if (yn()) {
        msg_area();
        if (cost > cash) { sayc(2, "You don't have enough dinero up front to foot de bill!"); sfx(3); msg_wait(); }
        else {
            cash -= cost;
            stay = (i16)(cost / 15);
            for (n = 1; stay > n; n++) next_day();
            health = 100;
            sayc(1, "OK, yer laid up for "); say_num(stay); say(" days recoup'ratin', and yous feel good as new!");
            sfx(2); msg_wait();
        }
    }
    return 1;
}
u8 menu_quit(void) {
    msg_area();
    sayc(0, "Are ya sure ya wanna chicken out an' QUIT?\n");
    if (yn()) { playing = 0; return 1; }
    msg_area();
    return 0;
}

/* the loan shark's thugs come for you */
static void thug_hp(u8 label) {                         /* your health at the bottom of the page (the text cursor is kept) */
    u8 sx = cx, sy = cy, si = ink;
    if (label) { ink = 1; ps_at(16, 28, "Health:"); }
    at(24, 28); hp_put(health, 100, 4, 0);
    cx = sx; cy = sy; ink = si;
}
void thugs_scene(void) {
    u8 n;
    page();
    sayc(2, "You're ambushed in a dark alley by Guido's thugs, must be 'bout ten guys! The boss-man say \"Oh, man, you should'na missed de deadline, now me and boys are gonna have to break your legs! Maybe next time you'll learn, eh?\"\nDey take yous from all directions, you ain't gotta chance...\nOne steps out with a baseball bat and...\n");
    delay(120);
    thugs++;
    thug_hp(1);                                          /* your health, in its colour, at the bottom */
    for (n = 0; n < thugs; n++) {
        sayc(2, "* * * C R A C K ! ! * * *\n");
        health -= 25; angry = 1;
        thug_hp(0);
        sfx(17);
        delay(60);
    }
    if (health > 0) {
        sayc(2, "\"Oh, one more 'ting... you got a week to come up with dinero, or we'll be back, comprende?\"");
        loan_days = 7;
    } else dead = 1;
    press_a();
    draw_main();
}

