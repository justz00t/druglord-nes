; Start-up code for the NES port: mapper 2 (UxROM), 128 KB PRG, 8 KB CHR-RAM, 8 KB battery-backed WRAM at $6000.
;   PRG bank 0 = CODE_A/RODATA_A, bank 1 = CODE_B/RODATA_B, bank 2 = the font, bank 3 = the music, bank 4 = the high scores (hiscore.c), banks 5-6 = free, bank 7 = fixed (this file, the runtime,
;   the engine).  Functions in the switchable banks are called through _bankcall (cc65 "wrapped-call").

        .export         _exit, _bankcall, _bank_set, _curbank, _nmi_count, _ppu_copy
        .exportzp       _ppu_src
        .export         __STARTUP__ : absolute = 1
        .import         _main, zerobss, copydata, callptr4
        .import         __STACK_START__, __STACK_SIZE__
        .importzp       sp, tmp2, tmp3, tmp4

PPUCTRL   = $2000
PPUMASK   = $2001
PPUSTATUS = $2002

.segment "HEADER"
        .byte   $4E,$45,$53,$1A ; "NES",$1A
        .byte   8               ; 8 x 16 KB PRG (128 KB)
        .byte   0               ; no CHR-ROM (CHR-RAM)
        .byte   %00100011       ; mapper 2 (low nibble), battery, vertical mirroring
        .byte   %00001000       ; NES 2.0, mapper high nibble 0
        .byte   $00, $00
        .byte   $70             ; 8 KB battery-backed WRAM
        .byte   $07             ; 8 KB CHR-RAM
        .byte   0, 0, 0, 0

.segment "STARTUP"
start:
        sei
        cld
        ldx     #0
        stx     PPUCTRL
        stx     PPUMASK
        dex
        txs
        bit     PPUSTATUS
@w1:    bit     PPUSTATUS
        bpl     @w1
        jsr     zerobss
        jsr     copydata
        lda     #<(__STACK_START__ + __STACK_SIZE__)
        sta     sp
        lda     #>(__STACK_START__ + __STACK_SIZE__)
        sta     sp+1
@w2:    bit     PPUSTATUS
        bpl     @w2
        lda     #0
        jsr     _bank_set
        jsr     _main
_exit:  jmp     start

; NMI: count frames.  wait_vblank() waits for this count to change, so it always returns at the very start of
; vblank (polling PPUSTATUS could return late in vblank, and writes that spill past vblank corrupt the picture)
nmi:    inc _nmi_count
irq:    rti

; set the switchable PRG bank (A = 0..2); the value is written to a ROM byte that holds the same value (bus conflicts)
_bank_set:
        sta     _curbank
        tay
        sta     banktab,y
        rts

; trampoline for banked functions: tmp4 = bank, ptr4 = function.  Preserves A on entry and A/X on return.
_bankcall:
        pha                     ; A
        txa
        pha                     ; X
        lda     _curbank
        pha                     ; old bank
        lda     tmp4
        jsr     _bank_set
        tsx
        ldy     $0103,x         ; A
        lda     $0102,x         ; X
        tax
        tya
        jsr     callptr4
        sta     tmp2
        stx     tmp3
        pla
        jsr     _bank_set
        pla
        pla
        lda     tmp2
        ldx     tmp3
        rts

banktab: .byte  0, 1, 2, 3, 4, 5, 6, 7

.segment "BSS"
_curbank: .res 1

.segment "VECTORS"
        .word   nmi
        .word   start
        .word   irq

.segment "BSS"
_nmi_count: .res 1

; ppu_copy(n): copy n bytes (1-255) from ppu_src to PPUDATA, about 14 cycles a byte.  For vblank writes:
; a C loop costs 60-80 cycles a byte (its counter lives on the C stack), too slow for vblank's 2270 cycles.
.segment "ZEROPAGE"
_ppu_src: .res 2
.segment "STARTUP"
_ppu_copy:
        tax
        beq     @done
        ldy     #0
@loop:  lda     (_ppu_src),y
        sta     $2007
        iny
        dex
        bne     @loop
@done:  rts
