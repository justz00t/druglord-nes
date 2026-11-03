; Start-up code for the NES port: mapper 2 (UxROM), 64 KB PRG, 8 KB CHR-RAM, 8 KB battery-backed WRAM at $6000.
;   PRG bank 0 = CODE_A/RODATA_A, bank 1 = CODE_B/RODATA_B, bank 2 = the font, bank 3 = fixed (this file, the runtime,
;   the engine).  Functions in the switchable banks are called through _bankcall (cc65 "wrapped-call").

        .export         _exit, _bankcall, _bank_set
        .export         __STARTUP__ : absolute = 1
        .import         _main, zerobss, copydata, callptr4
        .import         __STACK_START__, __STACK_SIZE__
        .importzp       sp, tmp2, tmp3, tmp4

PPUCTRL   = $2000
PPUMASK   = $2001
PPUSTATUS = $2002

.segment "HEADER"
        .byte   $4E,$45,$53,$1A ; "NES",$1A
        .byte   4               ; 4 x 16 KB PRG
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

nmi:
irq:    rti

; set the switchable PRG bank (A = 0..2); the value is written to a ROM byte that holds the same value (bus conflicts)
_bank_set:
        sta     curbank
        tay
        sta     banktab,y
        rts

; trampoline for banked functions: tmp4 = bank, ptr4 = function.  Preserves A on entry and A/X on return.
_bankcall:
        pha                     ; A
        txa
        pha                     ; X
        lda     curbank
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

banktab: .byte  0, 1, 2, 3

.segment "BSS"
curbank: .res 1

.segment "VECTORS"
        .word   nmi
        .word   start
        .word   irq
