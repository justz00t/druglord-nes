/* PRG bank 1: the high score table (kept in the battery-backed RAM at $6000), the initials entry and the
 * high score screen. */
#include "dl.h"

typedef struct { char n[3]; u8 rk; u32 sc; } hs_t;     /* 8 bytes: initials, rank number, score */
#define HS    ((hs_t*)0x6010)
#define HS_ID ((u8*)0x6000)                            /* 'D', 'L', version 1, checksum */
#define NHS   10

/* the table a fresh cartridge starts with */
const char def_names[] = "TNYSALVICMAXRAYJOELEEPATBUDSAM";
const u32 def_scores[NHS] = {1100000UL, 950000UL, 850000UL, 750000UL, 650000UL, 550000UL, 450000UL, 350000UL, 250000UL, 150000UL};

static u8 hs_sum(void) {
    const u8* p = (const u8*)HS;
    u8 i, s = 0x5A;
    for (i = 0; i < NHS * 8; i++) s = (u8)(s * 3 + p[i]);
    return s;
}
static u8 rank_of(u32 s) {
    u8 k = 0;
    while (k < 10 && rank_thr[k] <= s) k++;
    return k;
}
static void hs_seal(void) { HS_ID[0] = 'D'; HS_ID[1] = 'L'; HS_ID[2] = 1; HS_ID[3] = hs_sum(); }

void hs_reset(void) {
    u8 i, j;
    for (i = 0; i < NHS; i++) {
        for (j = 0; j < 3; j++) HS[i].n[j] = def_names[i * 3 + j];
        HS[i].sc = def_scores[i];
        HS[i].rk = rank_of(def_scores[i]);
    }
    hs_seal();
}
void hs_init(void) {                                   /* called once at power-on */
    if (HS_ID[0] != 'D' || HS_ID[1] != 'L' || HS_ID[2] != 1 || HS_ID[3] != hs_sum()) hs_reset();
}
u8 hs_place(u32 s) {                                   /* 0..9 = would enter at that place, 10 = no */
    u8 i;
    for (i = 0; i < NHS; i++) if (s > HS[i].sc) return i;
    return NHS;
}
static void hs_insert(u8 pos, const char* nm, u32 s) {
    u8 i, j;
    for (i = NHS - 1; i > pos; i--) HS[i] = HS[i - 1];
    for (j = 0; j < 3; j++) HS[pos].n[j] = nm[j];
    HS[pos].sc = s;
    HS[pos].rk = rank_of(s);
    hs_seal();
}

/* ---- palette flashing: palettes 1..3 hold yellow, red, cyan; they are rotated through a rainbow ---- */
static const u8 rainbow[7] = {0x28, 0x16, 0x2C, 0x2A, 0x30, 0x24, 0x27};
static void pal_set3(u8 a, u8 b, u8 c) {               /* call right after wait_vblank() */
    latch = PPUSTATUS;
    PPUADDR = 0x3F; PPUADDR = 0x05; PPUDATA = a;
    PPUADDR = 0x3F; PPUADDR = 0x09; PPUDATA = b;
    PPUADDR = 0x3F; PPUADDR = 0x0D; PPUDATA = c;
    PPUADDR = 0x3F; PPUADDR = 0x00; PPUADDR = 0; PPUADDR = 0;
    PPUCTRL = 0; PPUSCROLL = 0; PPUSCROLL = 0;
}
static void pal_default(void) { wait_vblank(); pal_set3(0x28, 0x16, 0x2C); }

/* ---- the table ---- */
static const u8 hs_ink[NHS] = {1, 3, 0, 1, 3, 0, 1, 3, 0, 1};
static void hs_draw(u8 hl) {
    u8 i, j, y;
    page();
    ink = 2; ps_at(5, 1, "H I G H   S C O R E S");
    ink = 0; ps_at(1, 3, "NO"); ps_at(5, 3, "NAME"); ps_at(12, 3, "SCORE"); ps_at(18, 3, "RANK");
    for (i = 0; i < NHS; i++) {
        y = 5 + 2 * i;
        ink = (i == hl) ? 2 : hs_ink[i];
        at(1, y); pn_r(i + 1, 2); pc('.');
        at(5, y);
        for (j = 0; j < 3; j++) pc((u8)HS[i].n[j] | (i == hl ? 0x80 : 0));
        at(9, y); pn_r((i32)HS[i].sc, 8);
        at(18, y); ps(rank_name[HS[i].rk]);
    }
}
/* hl = place to highlight (0xFF none); waits for A */
void hs_show(u8 hl) {
    u8 k = 0, f = 0, e;
    hs_draw(hl);
    ink = 3; ps_at(1, 27, "Press A to continue");
    ppu_show();
    for (;;) {
        wait_vblank(); frames++; music_update();
        if (!(f++ & 7)) { k++; pal_set3(rainbow[k % 7], rainbow[(k + 2) % 7], rainbow[(k + 4) % 7]); }
        e = read_pad(); { u8 d = e & ~pad_old; pad_old = e; e = d; }
        if (e & BTN_A) break;
    }
    pal_default();
}
/* shown for a second before the title screen */
void hs_flash(void) {
    u8 f, k = 0;
    hs_draw(0xFF);
    ppu_show();
    for (f = 0; f < 66; f++) {
        wait_vblank(); frames++;
        if (!(f & 3)) { k++; pal_set3(rainbow[k % 7], rainbow[(k + 2) % 7], rainbow[(k + 4) % 7]); }
    }
    pal_default();
}

/* ---- initials entry after a win ---- */
static void entry_letters(char* nm, u8 cur) {
    u8 i;
    for (i = 0; i < 3; i++) put2(12 + i * 3, 15, (u8)nm[i] | (i == cur ? 0x80 : 0), 12 + i * 3, 16, i == cur ? '^' : ' ');
}
void hs_entry(u8 pos, u32 score, u8 rk) {
    char nm[3];
    u8 cur = 0, e, rep = 0, f = 0, k = 0, go = 0;
    nm[0] = nm[1] = nm[2] = 'A';
    page();
    ink = 1; ps_at(6, 1, "NEW  HIGH  SCORE!!!");
    ink = 3; ps_at(5, 5, "You made place "); pn(pos + 1); ps(" of 10");
    ink = 2; ps_at(5, 8, "SCORE"); at(12, 8); pn((i32)score);
    ink = 1; ps_at(5, 10, "RANK"); at(12, 10); ps(rank_name[rk]);
    ink = 0; ps_at(6, 13, "ENTER YOUR INITIALS");
    ink = 1; ps_at(12, 15, "A  A  A");
    ink = 3; ps_at(2, 20, "UP/DOWN letter  L/R move");
    ps_at(2, 22, "A next / OK   B back");
    ps_at(2, 24, "START = done");
    ppu_show();
    sfx(7);
    entry_letters(nm, cur);
    while (!go) {
        wait_vblank(); frames++; music_update();
        if (!(f++ & 7)) { k++; pal_set3(rainbow[k % 7], rainbow[(k + 2) % 7], rainbow[(k + 4) % 7]); }
        e = read_pad();
        { u8 d = e & ~pad_old; pad_old = e; if ((e & (BTN_UP | BTN_DOWN)) && ++rep > 16 && (rep & 3) == 0) d |= e & (BTN_UP | BTN_DOWN); if (!(e & (BTN_UP | BTN_DOWN))) rep = 0; e = d; }
        if (e & BTN_UP) { nm[cur] = nm[cur] == 'Z' ? 'A' : nm[cur] + 1; entry_letters(nm, cur); }
        if (e & BTN_DOWN) { nm[cur] = nm[cur] == 'A' ? 'Z' : nm[cur] - 1; entry_letters(nm, cur); }
        if ((e & BTN_RIGHT) && cur < 2) { cur++; entry_letters(nm, cur); }
        if ((e & BTN_LEFT) && cur > 0) { cur--; entry_letters(nm, cur); }
        if (e & BTN_B) { if (cur > 0) { cur--; entry_letters(nm, cur); } }
        if (e & BTN_A) { if (cur < 2) { cur++; entry_letters(nm, cur); } else go = 1; }
        if (e & BTN_START) go = 1;
    }
    pal_default();
    sfx(4);
    hs_insert(pos, nm, score);
    hs_show(pos);
}

/* called after the win screen: asks for initials if the score made the table */
void hs_after_win(u32 score, u8 rk) {
    u8 pos = hs_place(score);
    if (pos < NHS) hs_entry(pos, score, rk);
}
