/* the victory tune: plays on the initials screen after a win and on into the high score table that follows it
 * (never when the table is reached any other way).  An original 4-bar loop in C major, about 138 BPM:
 * C - F - G - C, the triangle bouncing root and octave, chord stabs as a fast arpeggio on pulse 2, the tune on
 * pulse 1, drums on the noise channel.  This file lives in PRG bank 2 with the other song data (code and data);
 * music_frame() reaches vic_frame() through the bank trampoline. */
#include "dl.h"

extern u8 m_song, music_on, pend, sfx_busy;
extern const u8 EasterSong[];                         /* its pulse period table (MIDI note 0-127) is shared */
static u16 vper(u8 k) { const u8* p = EasterSong + 6 + (u16)k * 2; return p[0] | (p[1] << 8); }

/* 64 steps (4 bars of 16ths) */
#define C 0
static const u8 v_bn[64] = {                          /* bass note at each step (0 = none) */
    36,0,48,0, 36,0,48,0, 36,0,48,0, 36,0,48,0,     41,0,53,0, 41,0,53,0, 41,0,53,0, 41,0,53,0,
    43,0,55,0, 43,0,55,0, 43,0,55,0, 43,0,55,0,     36,0,48,0, 36,0,48,0, 43,0,55,0, 36,0,0,0};
static const u8 v_st[64] = {                          /* chord stab: 1 C, 2 F, 3 G */
    1,0,0,0, 0,0,1,0, 0,0,0,0, 1,0,0,0,   2,0,0,0, 0,0,2,0, 0,0,0,0, 2,0,0,0,
    3,0,0,0, 0,0,3,0, 0,0,0,0, 3,0,0,0,   1,0,0,0, 0,0,1,0, 0,0,0,0, 1,0,0,0};
static const u8 v_ch[12] = {60, 64, 67, 72,  65, 69, 72, 77,  67, 71, 74, 79};
/* the tune: (step, note, length in steps) */
static const u8 v_tune[] = {
     0, 72, 2,   2, 76, 2,   4, 79, 2,   6, 84, 6,  12, 79, 2,  14, 81, 2,        /* C E G C' . G A */
    16, 77, 2,  18, 81, 2,  20, 84, 2,  22, 89, 6,  28, 88, 2,  30, 86, 2,        /* F A C F' . E D */
    32, 86, 4,  36, 83, 2,  38, 79, 2,  40, 86, 4,  44, 89, 4,                    /* D B G D F */
    48, 88, 8,  56, 84, 2,  58, 86, 2,  60, 88, 4,                                /* E . C D E */
    255};
/* drums, 16 steps (bar 4 adds a snare fill): bit 0 kick, bit 2 snare, bits 4-6 hi-hat volume */
static const u8 v_dr[16] = {0x31, 0, 0x30, 0, 0x34, 0, 0x30, 0, 0x31, 0, 0x30, 0x01, 0x34, 0, 0x30, 0};
static const u8 v_len[2] = {7, 6};                    /* swung a hair: 13 frames per 2 steps */

static u8 st, fl, ti;                                 /* step, frames left in it, next tune entry */
static u8 tri, tl, thump, chd, sl, stt, hk, hl, ht, nv, ni, nm, nd;
static const u8 thump_n[5] = {60, 50, 43, 38, 34};
static void noise(u8 v, u8 i, u8 m, u8 d) { nv = v; ni = i; nm = m; nd = d; }

void vic_start(void) {
    music_on = 0;
    APU(0x15) = 0x0F; APU(0x01) = 0x08; APU(0x05) = 0x08;
    st = 63; fl = 1; ti = 0; tl = 0; thump = 5; sl = 0; hl = 0; nv = 0;
    m_song = 3; pend = 0; music_on = 1;
}
void vic_frame(void) {
    u8 x, d;
    u16 p;
    if (--fl == 0) {                                   /* a new step */
        if (++st >= 64) { st = 0; ti = 0; }
        fl = v_len[st & 1];
        if (v_bn[st]) { tri = v_bn[st]; tl = 5; }
        if (v_st[st]) { chd = v_st[st]; sl = 10; stt = 0; }
        if (v_tune[ti] == st) { hk = v_tune[ti + 1]; hl = v_tune[ti + 2] * 6 + v_tune[ti + 2] / 2 - 1; ht = 0; ti += 3; }
        d = v_dr[st & 15];
        if (st >= 48 && (st & 15) >= 13) d |= 4;     /* the fill */
        if (d & 0x70) noise(d >> 4, 1, 0x80, 2);
        if (d & 1) { noise(10, 12, 0, 4); thump = 0; }
        if (d & 4) noise(12, 6, 0, 2);
    }
    /* triangle */
    if (thump < 5) { p = (vper(thump_n[thump]) + 1) / 2 - 1; thump++; }
    else if (tl) { p = (vper(tri) + 1) / 2 - 1; tl--; }
    else p = 0;
    if (p) { APU(0x08) = 0xFF; APU(0x0A) = p & 255; APU(0x0B) = p >> 8; } else APU(0x08) = 0x80;
    /* pulse 1: the tune, 50% duty, bright then settling, vibrato on long notes */
    if (hl) {
        p = vper(hk);
        if (ht > 8) { if ((ht >> 2) & 1) p += p >> 8; else p -= p >> 8; }
        x = ht >> 2; x = x < 5 ? 12 - x : 7;
        APU(0x00) = 0xB0 | x; p1(p);
        ht++; hl--;
    } else APU(0x00) = 0x30;
    if (sfx_busy) return;
    /* pulse 2: the stab, a 25% arpeggio dying away */
    if (sl) {
        x = stt >> 1; x = x < 7 ? 7 - x : 0;
        APU(0x04) = 0x70 | x; p2(vper(v_ch[(chd - 1) * 4 + (stt & 3)]));
        stt++; sl--;
    } else APU(0x04) = 0x30;
    if (nv) { APU(0x0E) = ni | nm; APU(0x0F) = 0x08; APU(0x0C) = 0x30 | nv; nv = nv > nd ? nv - nd : 0; } else APU(0x0C) = 0x30;
}
