/* PRG bank 3: the MIDI player.  Every sound effect and the in-game music (the cop fight, the thugs, the event
 * pages, the death march, the notice, the easter-egg song) is a MIDI file, converted by tools/mid2nes.py into
 * rows of "what changes at this frame" (the format is described there).  Two of these play at once:
 *     player 0  the music          (seq_song / easter_music_start; stepped by music_frame() while m_song == 2)
 *     player 1  a sound effect     (seq_sfx; stepped by music_update() while sfx_busy)
 * A sound effect owns pulse 2 and the noise channel while it plays; it gets pulse 1 and the triangle too when no
 * music is playing.  The music gets pulse 2 and the noise back when the effect ends.
 * The data is read through seq_fetch() (crt0.s), which copies a row out of whichever PRG bank holds it: the
 * effects and the music are in this bank (sounds.bin), the easter-egg song has bank 6 to itself (easter.bin).
 * This file's variables live in the cartridge RAM at $6400 (segment XBSS). */
#include "dl.h"
#define SND_TABLE
#include "sounds.h"

extern u8 m_song, music_on, pend, sfx_busy;
extern const u8 Sounds[];                         /* sounds.bin: 16-bit offsets, then the blobs */
extern const u8 EasterSong[];                     /* easter.bin, PRG bank 6 */
extern const u8* seq_ptr;                         /* seq_fetch(bank): 16 bytes from seq_ptr to seq_buf */
#pragma zpsym ("seq_ptr")
extern u8 seq_buf[16];
#pragma zpsym ("seq_buf")
void __fastcall__ seq_fetch(u8 bank);
void apu_kick(void);                              /* druglord.c: makes p1()/p2() rewrite the high period byte */

u16 note_per(u8 k) { return note_per_t[k]; }      /* (the menu song and the victory tune use the table too) */

/* the instruments (tools/mid2nes.py picks one from the MIDI program): duty, how fast the volume drops (frames a
 * step; +0x80 = that many steps a frame; 0 = steady), how far it drops (15 = to silence), vibrato */
static const u8 i_duty[12] = {0xB0, 0x70, 0x30, 0x70, 0xB0, 0x70, 0x70, 0x30, 0xB0, 0x70, 0xB0, 0x30};
static const u8 i_rate[12] = {4,    4,    1,    4,    0,    0,    0x82, 0,    4,    2,    3,    4};
static const u8 i_drop[12] = {5,    5,    15,   15,   0,    0,    15,   0,    5,    15,   4,    5};
static const u8 i_vib[12]  = {0,    0,    0,    0,    0,    0,    0,    0,    1,    0,    1,    0};

static u8 q_on[2], q_bank[2], q_wait[2], q_loop[2];
static const u8* q_ptr[2];
static const u8* q_back[2];                       /* where a looping song starts again */
/* the pulse voices: player * 2 + (0 = pulse 1, 1 = pulse 2) */
static u8 v_on[4], v_peak[4], v_drop[4], v_cnt[4], v_age[4], v_inst[4];
static u16 v_per[4];
static u16 t_per[2];                              /* triangle period, 0 = off */
static u8 n_vol[2], n_per[2], n_dec[2], n_cnt[2];

static void q_begin(u8 pl, u8 bank, const u8* blob) {
    u8 v = pl << 1;
    seq_ptr = blob; seq_fetch(bank);
    q_loop[pl] = seq_buf[0] & 1;
    q_back[pl] = blob + 6 + (seq_buf[4] | ((u16)seq_buf[5] << 8));
    q_ptr[pl] = blob + 6; q_bank[pl] = bank; q_wait[pl] = 1;
    v_on[v] = v_on[v + 1] = 0; v_peak[v] = 12; v_peak[v + 1] = 9; v_inst[v] = 0; v_inst[v + 1] = 1;
    t_per[pl] = 0; n_vol[pl] = 0;
    q_on[pl] = 1;
}
static void q_note(u8 v, u8 b) {
    u8 n = b & 0x7F;
    if (!n) { v_on[v] = 0; return; }
    if ((b & 0x80) || !v_on[v]) { v_drop[v] = 0; v_cnt[v] = 0; v_age[v] = 0; }    /* struck */
    v_on[v] = 1; v_per[v] = note_per_t[n];
}
/* read the next row; returns 0 at the end of a sound that does not loop */
static u8 q_row(u8 pl) {
    u8 m, n, i = 2, v = pl << 1;
    seq_ptr = q_ptr[pl]; seq_fetch(q_bank[pl]);
    if (!seq_buf[0]) {                              /* the end */
        if (!q_loop[pl]) return 0;
        seq_ptr = q_back[pl]; seq_fetch(q_bank[pl]);
        if (!seq_buf[0]) return 0;
    }
    q_wait[pl] = seq_buf[0]; m = seq_buf[1];
    if (m & 0x40) {
        v_peak[v] = seq_buf[i] >> 4; v_peak[v + 1] = seq_buf[i] & 15; ++i;
        v_inst[v] = seq_buf[i] >> 4; v_inst[v + 1] = seq_buf[i] & 15; ++i;
    }
    if (m & 0x01) q_note(v, seq_buf[i++]);
    if (m & 0x02) q_note(v + 1, seq_buf[i++]);
    if (m & 0x04) { n = seq_buf[i++]; t_per[pl] = n ? (note_per_t[n] + 1) / 2 - 1 : 0; }
    if (m & 0x08) { n_per[pl] = seq_buf[i]; n_vol[pl] = seq_buf[i + 1] >> 4; n_dec[pl] = seq_buf[i + 1] & 15; n_cnt[pl] = 0; i += 2; }
    if (m & 0x10) { v_per[v] = seq_buf[i] | ((u16)seq_buf[i + 1] << 8); i += 2; }
    if (m & 0x20) { v_per[v + 1] = seq_buf[i] | ((u16)seq_buf[i + 1] << 8); i += 2; }
    if (m & 0x80) { t_per[pl] = seq_buf[i] | ((u16)seq_buf[i + 1] << 8); i += 2; }
    q_ptr[pl] = seq_ptr + i;
    return 1;
}
/* this frame's volume of a pulse voice (and the envelope moves on) */
static u8 q_env(u8 v) {
    u8 in = v_inst[v], r = i_rate[in], vol = v_peak[v] > v_drop[v] ? v_peak[v] - v_drop[v] : 0;
    if (r & 0x80) { v_drop[v] += r & 0x7F; if (v_drop[v] > i_drop[in]) v_drop[v] = i_drop[in]; }
    else if (r && ++v_cnt[v] >= r) { v_cnt[v] = 0; if (v_drop[v] < i_drop[in]) ++v_drop[v]; }
    if (v_age[v] != 255) ++v_age[v];
    return vol;
}
static u16 q_pitch(u8 v) {
    u16 p = v_per[v];
    if (i_vib[v_inst[v]] && v_age[v] > 14) { if ((v_age[v] >> 2) & 1) p += p >> 7; else p -= p >> 7; }
    return p;
}
/* one frame of a player: wide = it has all four voices, else only its own pair (the music: pulse 1 and the
 * triangle; an effect: pulse 2 and the noise) */
static void q_frame(u8 pl, u8 wide) {
    u8 v = pl << 1, x, d;
    u16 p;
    if (--q_wait[pl] == 0 && !q_row(pl)) { q_on[pl] = 0; return; }
    if (wide || pl == 0) {                          /* pulse 1 and the triangle */
        if (v_on[v]) { x = q_env(v); APU(0x00) = i_duty[v_inst[v]] | 0x30 | x; p1(q_pitch(v)); } else APU(0x00) = 0x30;
        p = t_per[pl];
        if (p) { APU(0x08) = 0xFF; APU(0x0A) = p & 255; APU(0x0B) = p >> 8; } else APU(0x08) = 0x80;
    }
    if (wide || pl == 1) {                          /* pulse 2 and the noise */
        ++v;
        if (v_on[v]) { x = q_env(v); APU(0x04) = i_duty[v_inst[v]] | 0x30 | x; p2(q_pitch(v)); } else APU(0x04) = 0x30;
        x = n_vol[pl];
        if (x) {
            APU(0x0E) = n_per[pl]; APU(0x0F) = 0x08; APU(0x0C) = 0x30 | x;
            d = n_dec[pl];
            if (d) {
                if (d < 8) n_vol[pl] = x > d ? x - d : 0;
                else if (++n_cnt[pl] >= d - 6) { n_cnt[pl] = 0; n_vol[pl] = x - 1; }
            }
        } else APU(0x0C) = 0x30;
    }
}

/* ---- the music ---- */
static void song_go(u8 bank, const u8* blob) {
    music_stop();
    APU(0x15) = 0x0F; APU(0x01) = 0x08; APU(0x05) = 0x08;
    apu_kick();
    q_begin(0, bank, blob);
    m_song = 2; pend = 0; music_on = 1;
}
void seq_song(u8 id) { song_go(3, Sounds + (Sounds[id * 2] | ((u16)Sounds[id * 2 + 1] << 8))); }
void easter_music_start(void) { song_go(6, EasterSong); }
void seq_frame(void) {
    q_frame(0, !sfx_busy);
    if (!q_on[0]) music_stop();                     /* a piece that plays once has ended */
}
/* ---- the sound effects ---- */
void seq_sfx(u8 id) {
    APU(0x15) = 0x0F; APU(0x01) = 0x08; APU(0x05) = 0x08;
    apu_kick();
    q_begin(1, 3, Sounds + (Sounds[id * 2] | ((u16)Sounds[id * 2 + 1] << 8)));
    sfx_busy = 1;
}
void seq_sfx_stop(void) {
    q_on[1] = 0; sfx_busy = 0;
    APU(0x04) = 0x30; APU(0x0C) = 0x30;
    if (!music_on) { APU(0x00) = 0x30; APU(0x08) = 0x80; }
    apu_kick();
}
void seq_sfx_frame(void) {
    q_frame(1, !music_on);
    if (!q_on[1]) seq_sfx_stop();
}
