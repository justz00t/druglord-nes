/* PRG bank 3: the main-menu song ("boom bap", menu.h from tools/menu_song.py).  The victory tune (victory.c) and
 * the MIDI player (seq.c: the sound effects and the rest of the music) live in this bank too.  music_frame() in the
 * fixed bank calls in here through the bank trampoline; the title tune stays in the fixed bank. */
#include "dl.h"
#include "menu.h"

extern u8 m_song, music_on, pend, sfx_busy, easter_on;
u16 note_per(u8 k);                             /* seq.c: the pulse period of MIDI note k */
/* ---- the main-menu song: "boom bap" (tools/menu_song.py), 104 BPM.  Swung 16ths (mn_len); the triangle plays the
 *      bass (and a pitch-drop "thump" under each kick), pulse 2 the chord stabs (a 4-note arpeggio, one note a
 *      frame), pulse 1 the hook in bars 5-8, the noise the drums. ---- */
static u8 mn_step, mn_fl, mn_sw;                  /* step 0-127, frames left in it, place in the swing pattern */
/* step lengths: 52 frames per 6 steps = 104 BPM (90 BPM + 15%), still a little swung */
static const u8 mn_len[6] = {9, 8, 9, 8, 10, 8};
static u8 mn_tri, mn_tl, mn_thump;                /* bass note, frames left, kick thump frame (5 = none) */
static u8 mn_chd, mn_sl, mn_stt;                  /* stab chord (0 = none), frames left, age */
static u8 mn_hk, mn_hleft, mn_ht;                 /* hook note, frames left, age */
static u8 mn_nv, mn_ni, mn_nm, mn_nd;             /* noise volume, period, mode, volume lost per frame */
static const u8 mn_thump_n[5] = {60, 50, 43, 38, 34};
/* a drum hit always cuts off the one before it (a long tail under the next hit smears into a crash) */
static void mn_noise(u8 v, u8 i, u8 md, u8 dec) { mn_nv = v; mn_ni = i; mn_nm = md; mn_nd = dec; }
void menu_music_start(void) {
    if (easter_on) { easter_music_start(); return; }
    music_stop();
    APU(0x15) = 0x0F; APU(0x01) = 0x08; APU(0x05) = 0x08;
    mn_step = 127; mn_fl = 1; mn_sw = 0; mn_nd = 1; mn_tl = 0; mn_thump = 5; mn_sl = 0; mn_hleft = 0; mn_nv = 0;
    m_song = 1; pend = 0; music_on = 1;
}
void menu_frame(void) {
    u8 x, d;
    u16 p;
    if (--mn_fl == 0) {                            /* a new step */
        if (++mn_step >= 128) mn_step = 0;
        mn_fl = mn_len[mn_sw]; if (++mn_sw >= 6) mn_sw = 0;
        x = mn_step & 63;
        if (mn_bn[x]) { mn_tri = mn_bn[x]; mn_tl = mn_bl[x] * 9 - 1; }
        if (mn_st[x]) { mn_chd = mn_st[x]; mn_sl = 16; mn_stt = 0; }
        if (mn_step >= 64 && mn_hn[x]) { mn_hk = mn_hn[x]; mn_hleft = mn_hl[x] * 9 - 1; mn_ht = 0; }
        d = mn_dr[mn_step & 31];
        if (d & 0x70) mn_noise(d >> 4, 1, 0x80, 2);           /* hi-hat: 2-3 frames */
        if (d & 3) mn_noise(10, 12, 0, 4);                    /* kick: a 3-frame click, the triangle does the rest */
        if (d & 1) mn_thump = 0;
        if (d & 4) mn_noise(12, 6, 0, 2);                     /* snare: 6 frames */
    }
    /* triangle: the thump, else the bass */
    if (mn_thump < 5) { p = (note_per(mn_thump_n[mn_thump]) + 1) / 2 - 1; mn_thump++; }
    else if (mn_tl) { p = (note_per(mn_tri) + 1) / 2 - 1; mn_tl--; }
    else p = 0;
    if (p) { APU(0x08) = 0xFF; APU(0x0A) = p & 255; APU(0x0B) = p >> 8; } else APU(0x08) = 0x80;
    /* pulse 1: the hook, a 25% pulse that fades to a floor, with vibrato */
    if (mn_hleft) {
        p = note_per(mn_hk);
        if (mn_ht > 6) { d = (mn_ht - 6) % 10; if (d < 5) p += p >> 8; else p -= p >> 8; }
        x = mn_ht / 6; x = x < 6 ? 10 - x : 4;
        APU(0x00) = 0x70 | x; p1(p);
        mn_ht++; mn_hleft--;
    } else APU(0x00) = 0x30;
    if (sfx_busy) return;
    /* pulse 2: the stab, a 25% arpeggio that dies away */
    if (mn_sl) {
        x = mn_stt >> 1; x = x < 10 ? 10 - x : 0;
        APU(0x04) = 0x70 | x; p2(note_per(mn_ch[(mn_chd - 1) * 4 + (mn_stt & 3)]));
        mn_stt++; mn_sl--;
    } else APU(0x04) = 0x30;
    /* noise: each hit dies away one step a frame */
    if (mn_nv) { APU(0x0E) = mn_ni | mn_nm; APU(0x0F) = 0x08; APU(0x0C) = 0x30 | mn_nv; mn_nv = mn_nv > mn_nd ? mn_nv - mn_nd : 0; } else APU(0x0C) = 0x30;
}
