; grid_move in assembly (gridmove.c has the same in C, for reading and for
; host tests; this one is linked): see gridmove.h.
;
; void grid_move(grid_body *b, int16_t dir, int16_t speed, int16_t wrap_w)
; grid_body: tile_x +0, tile_y +2, pixel_x +4, pixel_y +6, sub_x +8, sub_y +10

SECTION code_user

PUBLIC _grid_move

_grid_move:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = b
    inc hl
    ld a,(hl)                   ; dir
    inc hl
    inc hl
    bit 7,a
    ret nz                      ; GRID_NONE
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = speed
    inc hl
    push ix
    push de
    pop ix                      ; IX = b
    push af
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (gm_wrap),hl
    pop af
    ld (gm_dir),a
    cp 2
    jr c,gm_forward
    xor a                       ; left / up: BC = -speed
    sub c
    ld c,a
    sbc a,a
    sub b
    ld b,a
gm_forward:
    ld a,(gm_dir)
    and 1                       ; the axis: DE = b + 4 (x) or b + 6 (y)
    add a,a
    add a,4
    add a,e
    ld e,a
    jr nc,gm_axis
    inc d
gm_axis:
    ex de,hl                    ; HL = &pixel
    push hl
    ld de,4
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = sub
    ex de,hl
    add hl,bc                   ; v = sub + speed (signed)
    ld a,l
    ld (gm_sub),a               ; new sub = v & 255
    ld a,h                      ; whole units: v >> 8, signed
    pop hl                      ; HL = &pixel
    ld e,(hl)
    inc hl
    ld d,(hl)
    dec hl                      ; DE = pixel
    push hl
    ld l,a
    rla
    sbc a,a
    ld h,a
    add hl,de                   ; HL = t = pixel + units
    ; a step over a tile centre stops on it
    or a
    sbc hl,de
    jr z,gm_store_same
    jp m,gm_back
    add hl,de                   ; forward: c = (pixel | 7) + 1
    push hl
    ld a,e
    or 7
    ld l,a
    ld h,d
    inc hl                      ; HL = c
    pop bc                      ; BC = t
    push hl
    or a
    sbc hl,bc                   ; c - t
    pop hl
    jp m,gm_clamp               ; c < t: stop on c
    ld h,b
    ld l,c
    jr gm_store
gm_back:
    add hl,de                   ; back: c = (pixel - 1) & ~7
    push hl
    ld h,d
    ld l,e
    dec hl
    ld a,l
    and 0xF8
    ld l,a                      ; HL = c
    pop bc                      ; BC = t
    push hl
    or a
    sbc hl,bc                   ; c - t
    pop hl
    jr z,gm_t
    jp p,gm_clamp               ; c > t: stop on c
gm_t:
    ld h,b
    ld l,c
    jr gm_store
gm_clamp:
    xor a
    ld (gm_sub),a
    jr gm_store
gm_store_same:
    add hl,de
gm_store:
    ex de,hl                    ; DE = new pixel
    pop hl                      ; &pixel
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    inc hl
    inc hl
    ld a,(gm_sub)
    ld (hl),a                   ; sub
    inc hl
    ld (hl),0
    ; tiles: (pixel + 4) / 8, toward zero
    ld l,(ix+4)
    ld h,(ix+5)
    call gm_tile
    ld (ix+0),l
    ld (ix+1),h
    ld l,(ix+6)
    ld h,(ix+7)
    call gm_tile
    ld (ix+2),l
    ld (ix+3),h
    ; the tunnel
    ld de,(gm_wrap)
    ld a,d
    or e
    jr z,gm_done
    ld l,(ix+4)
    ld h,(ix+5)
    ld bc,8
    add hl,bc                   ; pixel_x + 8 < 0: to the right side
    bit 7,h
    jr z,gm_right
    ld (ix+4),e
    ld (ix+5),d
    jr gm_wrapped
gm_right:
    or a
    sbc hl,bc                   ; pixel_x
    or a
    sbc hl,de                   ; pixel_x - wrap_w >= 0: to the left side
    jp m,gm_done
    ld (ix+4),0xF8
    ld (ix+5),0xFF              ; -8
gm_wrapped:
    ld (ix+8),0
    ld (ix+9),0
gm_done:
    pop ix
    ret

; HL = (HL + 4) / 8 rounded toward zero
gm_tile:
    ld de,4
    add hl,de
    bit 7,h
    jr nz,gm_tile_neg
    ld a,l                      ; HL >> 3
    srl h
    rra
    srl h
    rra
    srl h
    rra
    ld l,a
    ret
gm_tile_neg:
    xor a                       ; -((-HL) >> 3)
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    call gm_tile_shift
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret
gm_tile_shift:
    ld a,l
    srl h
    rra
    srl h
    rra
    srl h
    rra
    ld l,a
    ret

SECTION bss_user
gm_wrap: defw 0
gm_sub:  defb 0
gm_dir:  defb 0
