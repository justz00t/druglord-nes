/* PRG bank 3: the music played in the game - the main-menu song ("boom bap", menu.h from tools/menu_song.py) and
 * the easter-egg song (easter.bin from tools/mid2nes.py), with their data.  The victory tune (victory.c) lives in
 * this bank too.  music_frame() in the fixed bank calls in here through the bank trampoline; the title tune stays
 * in the fixed bank. */
#include "dl.h"
#include "menu.h"

extern u8 m_song, music_on, pend, sfx_busy, easter_on;
extern const u8 EasterSong[];
static u16 note_per(u8 k) {                      /* the pulse period of MIDI note k, from the easter song's table */
    const u8* p = EasterSong + 6 + (u16)k * 2;
    return p[0] | (p[1] << 8);
}
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
/* ---- the easter-egg song (tools/mid2nes.py -> easter.bin, kept in this bank): events of
 *      (wait in 16ths, pulse 1, pulse 2, triangle), with a kick/snare/hat beat added on the noise channel.  Plays
 *      after the player sells over 100 cocaine at once, as the main-screen music for the rest of that game. ---- */
static u16 es_n, es_i, es_acc, es_rate;          /* event count, next event, 16th clock (8.8), 16ths per frame */
static u8 es_wait, es_beat;                      /* 16ths to the next event, 16th in the bar (the drums) */
static u8 es_n1, es_n2, es_t, es_a1, es_a2;
#define es_per note_per
static void es_event(void) {                     /* read the next event  */
    const u8* e = EasterSong + 6 + 256 + es_i * 4;
    es_wait = e[0];
    if (e[1]) { if (e[1] & 0x80 || (e[1] & 0x7F) != es_n1) { es_a1 = 0; } es_n1 = e[1] & 0x7F; } else es_n1 = 0;
    if (e[2]) { if (e[2] & 0x80 || (e[2] & 0x7F) != es_n2) { es_a2 = 0; } es_n2 = e[2] & 0x7F; } else es_n2 = 0;
    es_t = e[3];
    if (++es_i >= es_n) es_i = 0;
}
void easter_music_start(void) {
    music_stop();
    APU(0x15) = 0x0F; APU(0x01) = 0x08; APU(0x05) = 0x08;
    es_rate = EasterSong[0] | (EasterSong[1] << 8);
    es_n = EasterSong[2] | (EasterSong[3] << 8);
    es_i = 0; es_acc = 0; es_beat = 0; es_n1 = es_n2 = es_t = 0; es_a1 = es_a2 = 0;
    es_event();
    mn_nv = 0; mn_nd = 1;
    m_song = 2; pend = 0; music_on = 1;
}
void easter_frame(void) {
    u8 d, v;
    u16 p1p = 0, p2p = 0, tp = 0;
    es_acc += es_rate;
    while (es_acc >= 0x100) {                    /* each 16th: the drums, then the next event when it is due */
        es_acc -= 0x100;
        d = es_beat & 15;
        if (d == 0 || d == 8 || d == 10) mn_noise(10, 12, 0, 4);
        else if (d == 4 || d == 12) mn_noise(12, 6, 0, 2);
        else if (!(d & 1)) mn_noise(4, 1, 0x80, 2);
        es_beat++;
        if (--es_wait == 0) es_event();
    }
    if (es_n1) p1p = es_per(es_n1);
    if (es_n2) p2p = es_per(es_n2);
    if (es_t) tp = (es_per(es_t + 12 < 128 ? es_t + 12 : 127) + 0);
    /* triangle: the bass */
    if (tp) { APU(0x08) = 0xFF; APU(0x0A) = tp & 255; APU(0x0B) = tp >> 8; } else APU(0x08) = 0x80;
    /* pulse 1: the tune, 50% duty, struck at 12 and settling at 7 */
    if (p1p) { v = es_a1 >> 2; v = v < 5 ? 12 - v : 7; APU(0x00) = 0xB0 | v; p1(p1p); es_a1++; } else APU(0x00) = 0x30;
    if (sfx_busy) return;
    /* pulse 2: the harmony, 25% duty, quieter */
    if (p2p) { v = es_a2 >> 2; v = v < 5 ? 9 - v : 4; APU(0x04) = 0x70 | v; p2(p2p); es_a2++; } else APU(0x04) = 0x30;
    if (mn_nv) { APU(0x0E) = mn_ni | mn_nm; APU(0x0F) = 0x08; APU(0x0C) = 0x30 | mn_nv; mn_nv = mn_nv > mn_nd ? mn_nv - mn_nd : 0; } else APU(0x0C) = 0x30;
}
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
