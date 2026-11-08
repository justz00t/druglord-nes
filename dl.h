/* DRUGLORD for the NES - a separate port, NOT part of the byte-matched c/ folder.
 *
 * Written for cc65 (mapper 2 / UxROM, 64 KB PRG, CHR-RAM, battery WRAM).  The rules, odds and
 * formulas are those of the DOS original (see "GAME RULES AND FORMULAS" in c/DRUGLORD.C);
 * the NES has no floating point, so every formula is redone with integers, in a way that
 * gives the same numbers.  The screen is 32 x 30 tiles of text, so the layout and the
 * input (a controller instead of a keyboard) are different from the PC version.
 *
 * Tiles: 0x20-0x7E are ASCII, 1-6 box pieces, 7 a shade; tile + 0x80 is the same glyph
 * inverted (the highlight).  See tools/mkdata.py.
 */
#include <stdint.h>

typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;

#define PPUCTRL   (*(volatile u8*)0x2000)
#define PPUMASK   (*(volatile u8*)0x2001)
#define PPUSTATUS (*(volatile u8*)0x2002)
#define PPUSCROLL (*(volatile u8*)0x2005)
#define PPUADDR   (*(volatile u8*)0x2006)
#define PPUDATA   (*(volatile u8*)0x2007)
#define APU(a)    (*(volatile u8*)(0x4000+(a)))
#define JOY1      (*(volatile u8*)0x4016)

#define BTN_A 0x80
#define BTN_B 0x40
#define BTN_SELECT 0x20
#define BTN_START 0x10
#define BTN_UP 0x08
#define BTN_DOWN 0x04
#define BTN_LEFT 0x02
#define BTN_RIGHT 0x01

#define NUM_DRUGS 8
#define NUM_CITIES 8
#define WIN_STATUS 300


#define HZ(f) ((u16)(111860UL / (f)))
#define ROW_DRUGS 12
#define START_X 10
#define START_Y 21         /* "PRESS START" on the title screen */
extern unsigned char m_loops;   /* times the title tune has played through */
extern u8 easter_on;
#define LOGO_Y 10          /* first row of the logo: rows 10-15 lie in attribute rows of their own (rows 8-15) */
#define SND_COUNT 18
#define DBG_COUNT 24

/* ---- shared state (defined in druglord.c) ---- */
extern volatile u8 latch;
extern u8 ppu_on;
extern u8 cx, cy;
extern u8 wx0, wx1;
extern u8 wy0, wy1;
extern u8 bgctl;
extern u8 ink, attr_dirty;
extern u8 attr[64];
extern u8 pend;
extern char nb[12];
extern u8 pad_old, pend_btn;
extern u16 frames;
extern u32 seed;
extern const char* const drug_name[NUM_DRUGS];
extern const char* const gun_name[6];
extern const char* const gun_long[5];
extern const char* const city_name[NUM_CITIES];
extern const char* const rank_name[11];
extern const u16 price_lo[NUM_DRUGS];
extern const u16 price_hi[NUM_DRUGS];
extern const u8 mult_pct[NUM_CITIES][NUM_DRUGS];
extern const u8 city_x[NUM_CITIES];
extern const u8 city_y[NUM_CITIES];
extern const u8 gun_dice[6];
extern const u8 days_in_month[12];
extern const u32 rank_thr[10];
extern i32 cash, bank, debt;
extern u8 city, month, day, year;
extern i16 hold, space;
extern i16 inv[NUM_DRUGS];
extern i32 basis[NUM_DRUGS], price[NUM_DRUGS];
extern i16 num_guns, gun_type, status, health;
extern u8 dead, loan_flag, angry, thugs, cop_flag, playing, mkt_today;
extern i16 loan_days;
extern u8 done[10];
extern i16 total_days, good_deals, total_deals;

/* ---- functions of the fixed bank (druglord.c) ---- */
void wait_vblank(void);
void ppu_off(void);
void ppu_show(void);
void ppu_at(u8 x, u8 y);
void put(u8 x, u8 y, u8 ch);
void put2(u8 x1, u8 y1, u8 c1, u8 x2, u8 y2, u8 c2);
void fill(u8 x, u8 y, u8 ch, u8 n);
void clear_rows(u8 y0, u8 y1);
void set_palette(void);
void clear_screen(void);
void colour_cols(u8 y0, u8 y1, u8 bx0, u8 bx1, u8 pal);
void pal3(u8 a, u8 b);
void colour_rows(u8 y0, u8 y1, u8 pal);
void pc(u8 ch);
void at(u8 x, u8 y);
void ps(const char* s);
void ps_at(u8 x, u8 y, const char* s);
void ltoa_s(i32 v);
void pn(i32 v);
void hp_put(i16 hp, i16 max, u8 w, u8 dw);
void pn_r(i32 v, u8 w);
void pn_l(i32 v, u8 w);
void ps_pad(const char* s, u8 w);
u8 read_pad(void);
u8 vsync_pad(void);
void delay(u8 n);
u8 wait_btn(void);
void snd(u16 per, u8 n);
void tone(u16 per, u8 dv, u8 n);
void p1(u16 per);
void p2(u16 per);
void noi(u8 idx, u8 vol, u8 n);
void sfx(u8 n);
void music_stop(void);
void music_start(void);
void music_note(u8 ab, u8 c);
void music_frame(void);
void music_update(void);
u16 rnd(void);
i16 dice(i16 sides, i16 count);
i32 roll_price(u8 d);
void gen_prices(void);
i16 loan_term(i16 stat, i32 total);
void interest_one_day(void);
void next_day(void);
u8 sell_weight(u8 d);
i16 status_from_sale(u8 d, i32 sell, i32 paid);
i32 new_basis(i32 old, i32 p);
void nl(void);
void say(const char* s);
void sayc(u8 col, const char* s);
void say_num(i32 v);
void say_money(i32 v);
void press_a(void);
void page(void);
u8 yn(void);
void draw_status(void);
void draw_main(void);
i16 choose(u8 x, u8 y, u8 n, i16 start);
i32 pick_number(u8 x, u8 y, u8 width, i32 maxv);
void msg_area(void);
void msg_wait(void);
void game(void);
void load_font(void);
void load_pt1(u8 set);
void ppu_write2(const u8* src, u8 n);
extern const u8* ppu_src;               /* ppu_copy: n bytes from ppu_src to the PPU, fast enough for vblank (crt0.s) */
#pragma zpsym ("ppu_src")
void __fastcall__ ppu_copy(u8 n);
extern u8 pt1_set;

/* ---- functions in the switchable PRG banks: every call goes through _bankcall (see crt0.s) ---- */
void bankcall(void);
void __fastcall__ bank_set(u8 b);
#pragma wrapped-call (push, bankcall, 0)
/* bank_a.c: events, the menus */
u8 market_event(u8 count, i16 mul_pct);
void ev_cops_sell(void);
void ev_cheap(void);
void ev_newspaper(void);
void ev_factory(void);
void ev_brownies(void);
void ev_mugger(void);
void ev_trenchcoat(void);
void ev_gun_smuggler(void);
void ev_blackmail(void);
void do_event(u8 ev);
i16 pick_drug(const char* prompt);
u8 menu_buy(void);
u8 menu_sell(void);
u8 menu_jet(void);
u8 menu_bank(void);
u8 menu_loan(void);
u8 menu_hospital(void);
u8 menu_quit(void);
void thugs_scene(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, bankcall, 5)
/* fight.c, PRG bank 5: the cop fight */
void fight_row(u8 side, u8 i, i16 hp);
void fight_msg(const char* s);
void cop_fight(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, bankcall, 4)
/* hiscore.c, PRG bank 4: the high-score table (kept in the battery-backed WRAM at $6000) */
void hs_reset(void); void hs_init(void); u8 hs_place(u32 s); void hs_show(u8 hl); void hs_flash(void); u8 hs_attract(void);
void hs_entry(u8 pos, u32 score, u8 rk); void hs_after_win(u32 score, u8 rk);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, bankcall, 1)
/* bank_b.c: start/end screens, title, high scores, debug */
void init_game(void);
void show_death(void);
u32 score_days_at(u16 idx);
u32 score_calc(i32 total, i16 pct);
void show_win(void);
u8 ask_play_again(void);
void exit_screen(void);
void credits_screen(void);
void title_screen(void);
void title_wait(void);
void sound_test(void);
void dbg_wait_release(void);
void debug_menu(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, bankcall, 3)
/* PRG bank 3, the music: music.c (main-menu and easter-egg songs) and victory.c (the victory tune) */
void menu_music_start(void); void menu_frame(void);
void easter_music_start(void); void easter_frame(void);
void vic_start(void); void vic_frame(void);
#pragma wrapped-call (pop)
