/* DRUGLORD for the NES - a separate port, NOT part of the byte-matched c/ folder.
 * Built with cc65:  see build.sh.  Mapper 0 (NROM), 32 KB PRG, 8 KB CHR.
 * Text uses the PC's CP437 8x8 font as the tile set, so tile number == DOS character code. */
#include <stdint.h>
#include "music.h"

#define PPUCTRL   (*(volatile uint8_t*)0x2000)
#define PPUMASK   (*(volatile uint8_t*)0x2001)
#define PPUSTATUS (*(volatile uint8_t*)0x2002)
#define PPUSCROLL (*(volatile uint8_t*)0x2005)
#define PPUADDR   (*(volatile uint8_t*)0x2006)
#define PPUDATA   (*(volatile uint8_t*)0x2007)
#define APU(a)    (*(volatile uint8_t*)(0x4000+(a)))
#define JOY1      (*(volatile uint8_t*)0x4016)

#define BTN_A 0x80
#define BTN_B 0x40
#define BTN_SELECT 0x20
#define BTN_START 0x10
#define BTN_UP 0x08
#define BTN_DOWN 0x04
#define BTN_LEFT 0x02
#define BTN_RIGHT 0x01

/* ---------- PPU helpers (rendering is switched off while the screen is drawn) ---------- */
static volatile uint8_t latch;   /* a real read of $2002 resets the PPUADDR write latch */
static void wait_vblank(void) { while (!(PPUSTATUS & 0x80)) {} }

static void ppu_at(uint8_t x, uint8_t y) {
    uint16_t a = 0x2000 + (uint16_t)y * 32 + x;
    latch = PPUSTATUS; PPUADDR = a >> 8; PPUADDR = a & 255;
}
static void put_str(uint8_t x, uint8_t y, const char* s) {
    ppu_at(x, y);
    while (*s) PPUDATA = (uint8_t)*s++;
}
static void put_rep(uint8_t x, uint8_t y, uint8_t ch, uint8_t n) {
    ppu_at(x, y);
    while (n--) PPUDATA = ch;
}
static void clear_screen(void) {
    uint16_t i;
    ppu_at(0, 0);
    for (i = 0; i < 960 + 64; i++) PPUDATA = 0;      /* tiles + attributes */
}
static void load_palette(void) {
    static const uint8_t pal[8] = {0x0F, 0x30, 0x10, 0x00, 0x0F, 0x16, 0x28, 0x2A};
    uint8_t i;
    latch = PPUSTATUS; PPUADDR = 0x3F; PPUADDR = 0x00;
    for (i = 0; i < 32; i++) PPUDATA = pal[(i & 3) | ((i & 4) ? 0 : 0)] ;
}
static void screen_on(void)  { wait_vblank(); PPUCTRL = 0x00; PPUSCROLL = 0; PPUSCROLL = 0; PPUMASK = 0x0A; }
static void screen_off(void) { wait_vblank(); PPUMASK = 0x00; }

static uint8_t read_pad(void) {
    uint8_t i, v = 0;
    JOY1 = 1; JOY1 = 0;
    for (i = 0; i < 8; i++) v = (v << 1) | (JOY1 & 1);
    return v;
}

/* ---------- music: pulse 1 = melody, noise = drums, 1 step (16th note) = 10 frames ---------- */
static uint8_t m_step, m_tick, m_vol, m_dvol, m_dkind;
static void music_init(void) {
    APU(0x15) = 0x09;               /* pulse 1 + noise on */
    m_step = 0; m_tick = 0; m_vol = 0; m_dvol = 0;
}
static void music_stop(void) { APU(0x00) = 0x30; APU(0x0C) = 0x30; }
static void music_frame(void) {
    if (m_tick == 0) {
        uint8_t n = tune_mel[m_step], d = tune_drm[m_step];
        if (n) {
            uint16_t p = note_period[n];
            APU(0x02) = p & 255; APU(0x03) = 0x08 | (p >> 8);
            m_vol = 12;
        }
        if (d) {
            m_dkind = d;
            m_dvol = (d == 1) ? 5 : (d == 2) ? 9 : 11;
            APU(0x0E) = (d == 1) ? 3 : (d == 2) ? 6 : 9;     /* noise period */
            APU(0x0F) = 0x08;
        }
    }
    APU(0x00) = 0xB0 | m_vol;       /* 50% duty, constant volume */
    APU(0x0C) = 0x30 | m_dvol;
    if (m_vol) m_vol -= (m_tick & 1) ? 1 : 0;
    if (m_dvol) m_dvol -= (m_dkind == 1 || (m_tick & 1)) ? 1 : 0;
    if (++m_tick >= 10) {
        m_tick = 0;
        if (++m_step >= TUNE_STEPS) m_step = 0;
    }
}

/* ---------- title screen ---------- */
static void title_screen(void) {

    screen_off(); PPUCTRL = 0x00; clear_screen();
    put_rep(2, 3, 0xCD, 28);
    put_str(10, 5,  "D R U G L O R D");
    put_str(7, 7,   "Version 2.01 (NES port)");
    put_rep(2, 9, 0xCD, 28);
    put_str(5, 12,  "Written by Phil Erwin");
    put_str(5, 13,  "FantasyWare Inc. 1991-93");
    put_str(5, 16,  "Registered to: Phil Erwin");
    put_str(5, 17,  "Serial number: 000001");
    put_str(3, 21,  "NES port by justz00t");
    load_palette();
    screen_on();
}

void main(void) {
    uint8_t pad, old = 0, blink = 0;
    wait_vblank(); wait_vblank();
    music_init();
    title_screen();
    for (;;) {
        wait_vblank();
        music_frame();
        pad = read_pad();
        if ((pad & BTN_START) && !(old & BTN_START)) { music_stop(); }
        old = pad;
    }
}
