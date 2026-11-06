; zx_log_s / zx_log_c: write to ZEsarUX's ZXI debug port register 1
; (HARDWARE_DEBUG_ASCII). See include/zxlog.h.

SECTION code_user

PUBLIC _zx_log_s
PUBLIC _zx_log_c

; void zx_log_s(const char *s) __z88dk_fastcall   ; HL = s
_zx_log_s:
    ld bc,0xCF3B            ; ZXI register select port
    ld a,1                  ; register 1 = debug ASCII
    out (c),a
    ld b,0xDF               ; BC = 0xDF3B, ZXI data port
log_s_loop:
    ld a,(hl)
    or a
    ret z
    out (c),a
    inc hl
    jr log_s_loop

; void zx_log_c(unsigned char c) __z88dk_fastcall  ; L = c
_zx_log_c:
    ld bc,0xCF3B
    ld a,1
    out (c),a
    ld b,0xDF
    out (c),l
    ret
