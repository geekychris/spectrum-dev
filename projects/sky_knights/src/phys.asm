; Sky Knights: the per-frame body physics of game.c in assembly (zsdcc's C
; for it took ~5000 T-states a knight; this takes ~900). Same steps, same
; results as the C it replaces (kept in game.c under #if PHYS_IN_C):
;
;   dy += gravity, at most MAX_FALL_VEL; x += dx; y += dy; wrap x;
;   ceiling at y 10; (players, rising) bump the underside of a ledge;
;   land on a ledge when falling; the floor at 236.
;
; Positions are 1/64 pixel (game.h). Ledges: game.h Platform {x, y, w}.

SECTION code_user

PUBLIC _body_step, _plat_scan, asm_fix_int
PUBLIC _phys_plats, _phys_nplats, _phys_above
PUBLIC _ps_px, _ps_py, _ps_w, _ps_first
EXTERN _gravity

DEFC MAX_FALL   = 256
DEFC PLAYER_H   = 20
DEFC PLAT_THICK = 8

; HL = HL >> 6 (arithmetic): the pixel of a 1/64 position. Uses A, C.
asm_fix_int:
fix_int:
    ld a,l
    rlca
    rlca
    and 3
    ld c,a                      ; bits 6,7 of L
    ld l,h
    ld a,h
    rla
    sbc a,a
    ld h,a                      ; HL = H, sign-extended
    add hl,hl
    add hl,hl
    ld a,l
    or c
    ld l,a
    ret

; int16_t plat_scan(void): the first ledge (from ps_first: 0 = with the
; ground, 1 = without) with ps_py in [y, y + PLAT_THICK] and ps_px .. ps_px +
; ps_w - 1 overlapping x .. x + w - 1; returns its y, or -1.
_plat_scan:
    push ix
    ld ix,(_phys_plats)
    ld a,(_phys_nplats)
    ld b,a
    ld a,(_ps_first)
    or a
    jr z,ps_from0
    dec b
    ld de,6
    add ix,de
ps_from0:
    ld hl,(_ps_px)              ; ps_pxw1 = px + w - 1
    ld a,(_ps_w)
    dec a
    ld e,a
    ld d,0
    add hl,de
    ld (ps_pxw1),hl
ps_loop:
    ld e,(ix+2)
    ld d,(ix+3)                 ; DE = ledge y
    ld hl,(_ps_py)
    or a
    sbc hl,de                   ; py - y
    jp m,ps_next
    ld a,h
    or a
    jr nz,ps_next
    ld a,l
    cp PLAT_THICK + 1           ; py <= y + PLAT_THICK
    jr nc,ps_next
    ld e,(ix+0)
    ld d,(ix+1)                 ; DE = ledge x
    ld hl,(ps_pxw1)
    or a
    sbc hl,de                   ; px + w - 1 - x >= 0
    jp m,ps_next
    ld l,(ix+4)
    ld h,(ix+5)
    add hl,de
    dec hl                      ; x + w - 1
    ld de,(_ps_px)
    or a
    sbc hl,de                   ; ... - px >= 0
    jp m,ps_next
    ld l,(ix+2)
    ld h,(ix+3)
    pop ix
    ret
ps_next:
    ld de,6
    add ix,de
    djnz ps_loop
    ld hl,-1
    pop ix
    ret

; uint8_t body_step(Body *b) __z88dk_fastcall: b = {x, y, dx, dy} (Player and
; Enemy start like that). Returns on_ground (1/0).
_body_step:
    push ix
    push hl
    pop ix
    ; dy += gravity, at most MAX_FALL_VEL
    ld l,(ix+6)
    ld h,(ix+7)
    ld de,(_gravity)
    add hl,de
    push hl
    ld de,MAX_FALL + 1
    or a
    sbc hl,de
    pop hl
    jp m,bs_dy_ok
    ld hl,MAX_FALL
bs_dy_ok:
    ld (ix+6),l
    ld (ix+7),h
    ; y += dy
    ex de,hl
    ld l,(ix+2)
    ld h,(ix+3)
    add hl,de
    ld (ix+2),l
    ld (ix+3),h
    ; x += dx, wrapped: px < -20 -> 320, px > 320 -> -19
    ld l,(ix+0)
    ld h,(ix+1)
    ld e,(ix+4)
    ld d,(ix+5)
    add hl,de
    push hl
    ld de,-20 * 64              ; x < -20 * 64
    or a
    sbc hl,de
    pop hl
    jp p,bs_not_left
    ld hl,320 * 64
    jr bs_x_done
bs_not_left:
    push hl
    ld de,321 * 64              ; x >= 321 * 64
    or a
    sbc hl,de
    pop hl
    jp m,bs_x_done
    ld hl,-19 * 64
bs_x_done:
    ld (ix+0),l
    ld (ix+1),h
    call fix_int
    ld (_ps_px),hl
    ld a,PLAYER_H
    ld (_ps_w),a
    ; ceiling: y < 10 * 64 -> y = 10 * 64, dy = 0
    ld l,(ix+2)
    ld h,(ix+3)
    ld de,10 * 64
    or a
    sbc hl,de
    jp p,bs_no_ceiling
    ld (ix+2),e
    ld (ix+3),d
    xor a
    ld (ix+6),a
    ld (ix+7),a
bs_no_ceiling:
    ; rising player: bump the underside of a ledge (not the ground)
    ld a,(_phys_above)
    or a
    jr z,bs_no_above
    bit 7,(ix+7)                ; dy < 0
    jr z,bs_no_above
    ld l,(ix+2)
    ld h,(ix+3)
    call fix_int
    ld (_ps_py),hl
    ld a,1
    ld (_ps_first),a
    call _plat_scan
    bit 7,h
    jr nz,bs_no_above
    ld de,PLAT_THICK
    add hl,de                   ; the ledge's bottom
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld (ix+2),l
    ld (ix+3),h
    xor a
    ld (ix+6),a
    ld (ix+7),a
bs_no_above:
    ; land on a ledge when falling
    ld l,(ix+2)
    ld h,(ix+3)
    call fix_int
    ld de,PLAYER_H
    add hl,de
    ld (_ps_py),hl
    xor a
    ld (_ps_first),a
    call _plat_scan
    ld c,0                      ; C = on_ground
    bit 7,h
    jr nz,bs_floor
    bit 7,(ix+7)
    jr nz,bs_floor              ; rising: no landing
    ld de,-PLAYER_H
    add hl,de
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld (ix+2),l
    ld (ix+3),h
    xor a
    ld (ix+6),a
    ld (ix+7),a
    ld c,1
bs_floor:
    ; floor: y + 20 >= 256 -> y = 236
    ld l,(ix+2)
    ld h,(ix+3)
    ld de,(256 - PLAYER_H) * 64
    or a
    sbc hl,de
    jp m,bs_done
    ld (ix+2),e
    ld (ix+3),d
    xor a
    ld (ix+6),a
    ld (ix+7),a
    ld c,1
bs_done:
    ld l,c
    pop ix
    ret

SECTION bss_user
_phys_plats:  defw 0
_phys_nplats: defb 0
_phys_above:  defb 0
_ps_px:       defw 0
_ps_py:       defw 0
_ps_w:        defb 0
_ps_first:    defb 0
ps_pxw1:      defw 0
