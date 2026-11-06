; asm48 - sjasmplus example: 48K snapshot, border stripes, zx_log, key test.
        DEVICE ZXSPECTRUM48
        INCLUDE "zxlog.inc"
        ORG 0x8000

start:
        di
        ld sp,0xFF00
        ei
        ZXLOG "asm48: ready\n"
        xor a
        ld (frames),a
main_loop:
        halt                    ; 50 Hz (ROM IM1 interrupt)
        ld hl,frames
        inc (hl)
        ld a,(hl)
        and 7
        out (0xFE),a            ; cycle the border colour
        ; SPACE is bit 0 of half-row 0x7FFE (0 = pressed)
        ld a,0x7F
        in a,(0xFE)
        rra
        jr c,main_loop
        ZXLOG "asm48: space\n"
.wait:  ld a,0x7F               ; wait for release
        in a,(0xFE)
        rra
        jr nc,.wait
        jr main_loop

frames: db 0

        SAVESNA "build/asm48.sna", start
