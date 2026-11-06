; Draw one lane object (see main.c draw_lane). A lane is two character rows
; (16 lines, aligned); objects use lines 1..14: 7 lines in each character
; row, so the next line is D+1 and there are only two table lookups.
; Per line: zero lo_w1 bytes at column lo_c1 and lo_w2 at lo_c2 (the trail
; of the old footprint), then copy lo_n image bytes to column lo_col
; (lo_src advances lo_n per line). lo_n = 0: clear only. lo_y = the lane's
; first line + 1.

SECTION code_user

PUBLIC _lane_obj
PUBLIC _lo_y, _lo_col, _lo_n, _lo_src, _lo_c1, _lo_w1, _lo_c2, _lo_w2
EXTERN _zx_row

_lane_obj:
    ld hl,(_lo_src)
    ld (lo_p),hl
    ld a,(_lo_y)
    call segment                ; lines 1..7
    ld a,(_lo_y)
    add a,7
    ; fall through: lines 8..14

; 7 lines starting at screen line A (all in one character row)
segment:
    ld l,a
    ld h,0
    add hl,hl
    ld de,_zx_row
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = first line's address
    ld a,7
seg_line:
    push af
    push de
    ld a,(_lo_w1)
    or a
    jr z,c1_done
    ld b,a
    ld a,(_lo_c1)
    add a,e
    ld l,a
    ld h,d
    xor a
c1_loop:
    ld (hl),a
    inc l
    djnz c1_loop
c1_done:
    ld a,(_lo_w2)
    or a
    jr z,c2_done
    ld b,a
    ld a,(_lo_c2)
    add a,e
    ld l,a
    ld h,d
    xor a
c2_loop:
    ld (hl),a
    inc l
    djnz c2_loop
c2_done:
    ld a,(_lo_n)
    or a
    jr z,img_done
    ld c,a
    ld b,0
    ld a,(_lo_col)
    add a,e
    ld e,a
    ld hl,(lo_p)
    ldir
    ld (lo_p),hl
img_done:
    pop de
    inc d                       ; next line in the character row
    pop af
    dec a
    jr nz,seg_line
    ret

SECTION bss_user
_lo_y:     defb 0
_lo_col:   defb 0
_lo_n:     defb 0
_lo_src:   defw 0
_lo_c1:    defb 0
_lo_w1:    defb 0
_lo_c2:    defb 0
_lo_w2:    defb 0
lo_p:      defw 0
