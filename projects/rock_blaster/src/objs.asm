; Rock Blaster: the per-frame movement loops of game.c in assembly (they
; were most of the logic's time in C). Struct layouts are in game.h:
;
;   +0 x, +2 y (1/128 px, unsigned)  +4 dx, +6 dy  +8 active
;   rocks / bullets: +9 px, +11 py (whole world pixels)
;   particles: +9 life
;
; Parameters in globals, set by game.c.

SECTION code_user

PUBLIC _mw_core, _mw_ptr, _mw_n, _mw_stride
PUBLIC _pm_core, _pm_ptr, _pm_n, _pm_live
PUBLIC _pr_core, _pr_rocks, _pr_want, _pr_sx, _pr_sy, _pr_polys
PUBLIC _pp_core, _pp_out, _pp_n, _pp_bullets, _pp_parts, _pp_parts_n
PUBLIC _bh_core, _bh_rocks, _bh_out, _bh_n, _mw_hits
PUBLIC _ship_move
EXTERN _rock_radius_tab

defc W7 = 0xA000                ; 320 px * 128
defc H7 = 0x7800                ; 240 px * 128

; x += dx, y += dy wrapped into the world, px / py updated, for mw_n objects
; mw_stride bytes apart
_mw_core:
    push ix
    ld hl,(_mw_ptr)
    ld de,8
    add hl,de                   ; HL = the first active flag
    ld de,(_mw_stride)
    ld a,(_mw_n)
    ld b,a
mw_scan:
    ld a,(hl)
    or a
    jr nz,mw_one
mw_skip:
    add hl,de
    djnz mw_scan
    pop ix
    ret
mw_one:
    ld a,(_mw_n)
    sub b
    ld (mw_slot),a
    push bc
    push de
    push hl
    pop ix
    ld de,-8
    add ix,de                   ; IX = the object
    push hl
    ld l,(ix+0)
    ld h,(ix+1)
    ld e,(ix+4)
    ld d,(ix+5)
    add hl,de
    ld de,W7
    call wrap
    ld (ix+0),l
    ld (ix+1),h
    call px7
    ld (ix+9),l
    ld (ix+10),h
    ld l,(ix+2)
    ld h,(ix+3)
    ld e,(ix+6)
    ld d,(ix+7)
    add hl,de
    ld de,H7
    call wrap
    ld (ix+2),l
    ld (ix+3),h
    call px7
    ld (ix+11),l
    ld (ix+12),h
    ld hl,(_mw_hits)            ; collecting the rocks' circles (hit48)?
    ld a,h
    or l
    call nz,mw_hit
    pop hl
    pop de
    pop bc
    jr mw_skip

; append {px, py, radius, slot} at mw_hits for the rock at IX (slot = 24 - B
; of the caller, which is on the stack above our return address and HL)
mw_hit:
    ld a,(ix+9)
    ld (hl),a
    inc hl
    ld a,(ix+10)
    ld (hl),a
    inc hl
    ld a,(ix+11)
    ld (hl),a
    inc hl
    ld a,(ix+12)
    ld (hl),a
    inc hl
    ld a,(ix+13)                ; size -> radius
    push hl
    ld hl,_rock_radius_tab
    add a,l
    ld l,a
    adc a,h
    sub l
    ld h,a
    ld a,(hl)
    pop hl
    ld (hl),a
    inc hl
    ld a,(mw_slot)
    ld (hl),a
    inc hl
    ld (_mw_hits),hl
    ld hl,_bh_n
    inc (hl)
    ret

; HL = v, DE = lim: v >= lim ? (v >= 0xF000 ? v + lim : v - lim) : v
; (a step left of 0 comes back as 0xF000 or more)
wrap:
    ld a,h
    cp 0xF0
    jr nc,wr_under
    or a
    sbc hl,de
    ret nc                      ; v >= lim: v - lim
    add hl,de
    ret
wr_under:
    add hl,de
    ret

; HL = HL >> 7 (unsigned)
px7:
    ld a,l
    rla
    ld a,h
    rla
    ld l,a
    ld a,0
    rla
    ld h,a
    ret

; particles: x += dx, y += dy; gone when outside the world; dx -= dx >> 4,
; dy -= dy >> 4; gone when life runs out
_pm_core:
    push ix
    xor a
    ld (_pm_live),a
    ld hl,(_pm_ptr)
    ld de,8
    add hl,de
    ld de,10
    ld a,(_pm_n)
    ld b,a
pm_scan:
    ld a,(hl)
    or a
    jr nz,pm_one
pm_skip:
    add hl,de
    djnz pm_scan
    pop ix
    ret
pm_one:
    push bc
    push hl
    push hl
    pop ix
    ld de,-8
    add ix,de
    ld l,(ix+0)
    ld h,(ix+1)
    ld e,(ix+4)
    ld d,(ix+5)
    add hl,de
    ld (ix+0),l
    ld (ix+1),h
    ld a,h
    cp W7 / 256
    jr nc,pm_kill
    ld l,(ix+2)
    ld h,(ix+3)
    ld e,(ix+6)
    ld d,(ix+7)
    add hl,de
    ld (ix+2),l
    ld (ix+3),h
    ld a,h
    cp H7 / 256
    jr nc,pm_kill
    ld l,(ix+4)
    ld h,(ix+5)
    call slow16
    ld (ix+4),l
    ld (ix+5),h
    ld l,(ix+6)
    ld h,(ix+7)
    call slow16
    ld (ix+6),l
    ld (ix+7),h
    dec (ix+9)
    jr z,pm_kill
    ld hl,_pm_live
    inc (hl)
    jr pm_done
pm_kill:
    ld (ix+8),0
pm_done:
    pop hl
    pop bc
    ld de,10
    jr pm_skip

; HL -= HL >> 4 (arithmetic)
slow16:
    ld e,l
    ld d,h
    sra d
    rr e
    sra d
    rr e
    sra d
    rr e
    sra d
    rr e
    or a
    sbc hl,de
    ret


; ---------------------------------------------------------------- drawing plans (main.c)

; the 24 rocks -> poly_at want[24] {screen x, y, compiled polygon}: x = sx[px],
; y = sy[py], polygon = pr_polys[size * 4 + shape]; none when inactive
_pr_core:
    push ix
    ld ix,(_pr_rocks)
    ld hl,(_pr_want)
    ld (pr_even),hl
    ld de,12 * 4
    add hl,de
    ld (pr_odd),hl
    ld b,24
pr_loop:
    ld hl,pr_even               ; even slots to want[0..11], odd to want[12..23]
    bit 0,b
    jr z,pr_slot
    ld hl,pr_odd
pr_slot:
    push hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld a,(ix+8)
    or a
    jr z,pr_none
    push hl
    ld e,(ix+9)
    ld d,(ix+10)
    ld hl,(_pr_sx)
    add hl,de
    ld c,(hl)
    ld e,(ix+11)
    ld d,(ix+12)
    ld hl,(_pr_sy)
    add hl,de
    ld a,(hl)
    pop hl
    ld (hl),c
    inc hl
    ld (hl),a
    inc hl
    push hl
    ld a,(ix+13)
    add a,a
    add a,a
    add a,(ix+14)
    add a,a
    ld e,a
    ld d,0
    ld hl,(_pr_polys)
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    jr pr_next
pr_none:
    inc hl
    inc hl
    xor a
    ld (hl),a
    inc hl
    ld (hl),a
    inc hl
pr_next:
    ex de,hl                    ; the next entry of this half
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    ld de,15
    add ix,de
    djnz pr_loop
    pop ix
    ret

; bullets (2x2) and particles (1 pixel) -> x,y pairs at pp_out, pp_n of them
_pp_core:
    push ix
    ld de,(_pp_out)
    ld c,0
    ld ix,(_pp_bullets)
    ld b,8
pb_loop:
    ld a,(ix+8)
    or a
    jr z,pb_next
    push bc
    push de
    ld e,(ix+9)
    ld d,(ix+10)
    ld hl,(_pr_sx)
    add hl,de
    ld b,(hl)
    ld e,(ix+11)
    ld d,(ix+12)
    ld hl,(_pr_sy)
    add hl,de
    ld c,(hl)
    pop hl
    ld (hl),b
    inc hl
    ld (hl),c
    inc hl
    ld a,b
    inc a
    ld (hl),a
    inc hl
    ld (hl),c
    inc hl
    ld (hl),b
    inc hl
    ld a,c
    inc a
    ld (hl),a
    inc hl
    ld a,b
    inc a
    ld (hl),a
    inc hl
    ld a,c
    inc a
    ld (hl),a
    inc hl
    ex de,hl
    pop bc
    ld a,c
    add a,4
    ld c,a
pb_next:
    push de
    ld de,14
    add ix,de
    pop de
    djnz pb_loop
    ld a,(_pp_parts_n)
    or a
    jr z,pq_done
    ld ix,(_pp_parts)
    ld b,64
pq_loop:
    ld a,(ix+8)
    or a
    jr z,pq_next
    push bc
    push de
    ld l,(ix+0)
    ld h,(ix+1)
    call px7
    ex de,hl
    ld hl,(_pr_sx)
    add hl,de
    ld b,(hl)
    ld l,(ix+2)
    ld h,(ix+3)
    call px7
    ex de,hl
    ld hl,(_pr_sy)
    add hl,de
    ld a,(hl)
    pop hl
    ld (hl),b
    inc hl
    ld (hl),a
    inc hl
    ex de,hl
    pop bc
    inc c
pq_next:
    push de
    ld de,10
    add ix,de
    pop de
    djnz pq_loop
pq_done:
    ld a,c
    ld (_pp_n),a
    pop ix
    ret

; the active rocks -> hit_circle list {px, py, radius, slot} (hit48.h), bh_n
_bh_core:
    ld hl,(_bh_out)
    ld (_mw_hits),hl
    ld hl,(_bh_rocks)
    ld de,8
    add hl,de
    ld de,15
    ld b,24
bh_scan:
    ld a,(hl)
    or a
    jr nz,bh_one
bh_skip:
    add hl,de
    djnz bh_scan
    ld hl,0
    ld (_mw_hits),hl
    ret
bh_one:
    ld a,24
    sub b
    ld (mw_slot),a
    push bc
    push de
    push hl
    push ix
    push hl
    pop ix
    ld de,-8
    add ix,de
    ld hl,(_mw_hits)
    call mw_hit
    pop ix
    pop hl
    pop de
    pop bc
    jr bh_skip

; ---------------------------------------------------------------- the ship (16.16, as the original)

; Ship at HL: +0 x, +4 y, +8 dx, +12 dy (int32), +21 px, +23 py (game.h).
; dx = (dx * 253) / 256, dy likewise (C's truncation), x += dx, y += dy,
; wrapped into 320 x 240, px / py = the whole parts
_ship_move:
    ld (sm_ship),hl
    ld de,8
    add hl,de
    call drag                   ; dx
    call drag                   ; dy
    ld hl,(sm_ship)
    ld de,8
    call add_wrap_x             ; x += dx
    ld hl,(sm_ship)
    ld de,4
    add hl,de
    ld de,8
    call add_wrap_y             ; y += dy
    ld hl,(sm_ship)
    ld de,2
    add hl,de
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; whole x
    ld de,4
    add hl,de
    ld e,(hl)
    dec hl
    ld d,(hl)                   ; whole y (high, low swapped below)
    ld hl,(sm_ship)
    push de
    ld de,21
    add hl,de
    pop de
    ld (hl),c                   ; px
    inc hl
    ld (hl),b
    inc hl
    ld (hl),d                   ; py
    inc hl
    ld (hl),e
    ret

; the int32 at HL = (v * 253) / 256 truncated: v - ceil(3v / 256) for v >= 0,
; the negative of that for |v|; HL += 4
drag:
    push hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BCDE = v
    bit 7,b
    push af                     ; Z: positive
    call nz,neg32
    ; t = 3v + 255 -> (t >> 8) in 24 bits: compute 3v in A:H:L:? ...
    push bc
    push de                     ; v
    ; HL:IX? use memory: v3 = v + v + v
    ld (dv),de
    ld (dv + 2),bc
    ex de,hl                    ; HL = low
    add hl,hl
    rl c
    rl b                        ; BC:HL = 2v
    ld de,(dv)
    add hl,de
    ex de,hl
    ld hl,(dv + 2)
    adc hl,bc
    ld b,h
    ld c,l                      ; BC:DE = 3v
    ld hl,255
    add hl,de
    ex de,hl
    ld hl,0
    adc hl,bc                   ; HL:DE = 3v + 255
    ; >> 8: q = H:L:D (24 bits)
    ld c,d
    ld d,l
    ld e,h                      ; q = E:D:C (high..low)
    pop hl                      ; low word of v
    or a
    ld a,l
    sub c
    ld l,a
    ld a,h
    sbc a,d
    ld h,a                      ; low word of v - q
    pop bc                      ; high word of v
    ld a,c
    sbc a,e
    ld c,a
    ld a,b
    sbc a,0
    ld b,a
    ex de,hl                    ; BCDE = v - q
    pop af
    call nz,neg32
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (hl),c
    inc hl
    ld (hl),b
    inc hl
    ret

; BCDE = -BCDE
neg32:
    xor a
    sub e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    ld a,0
    sbc a,c
    ld c,a
    ld a,0
    sbc a,b
    ld b,a
    ret

; the int32 at HL += the int32 at HL + DE, then wrapped into [0, lim)
add_wrap_x:
    ld bc,320
    jr add_wrap
add_wrap_y:
    ld bc,240
add_wrap:
    ld (aw_lim),bc
    push hl
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BCDE = velocity
    pop hl
    push hl
    ld a,(hl)
    add a,e
    ld (hl),a
    inc hl
    ld a,(hl)
    adc a,d
    ld (hl),a
    inc hl
    ld a,(hl)
    adc a,c
    ld e,a
    inc hl
    ld a,(hl)
    adc a,b
    ld d,a                      ; DE = the whole part (signed)
    bit 7,d
    jr z,aw_pos
    ld hl,(aw_lim)              ; < 0: += lim
    add hl,de
    ex de,hl
    jr aw_store
aw_pos:
    ld hl,(aw_lim)
    ex de,hl
    or a
    sbc hl,de                   ; whole - lim
    jr c,aw_keep
    ex de,hl                    ; >= lim: -= lim
    jr aw_store
aw_keep:
    add hl,de
    ex de,hl
aw_store:
    pop hl
    inc hl
    inc hl
    ld (hl),e
    inc hl
    ld (hl),d
    ret

SECTION bss_user
_mw_ptr:    defw 0
_mw_n:      defb 0
_mw_stride: defw 0
_pm_ptr:    defw 0
_pm_n:      defb 0
_pm_live:   defb 0
_pr_rocks:  defw 0
_pr_want:   defw 0
_pr_sx:     defw 0
_pr_sy:     defw 0
_pr_polys:  defw 0
_pp_out:    defw 0
_pp_n:      defb 0
_pp_bullets: defw 0
_pp_parts:  defw 0
_bh_rocks:  defw 0
_bh_out:    defw 0
_bh_n:      defb 0
_pp_parts_n: defb 0
_mw_hits:   defw 0
mw_slot:    defb 0
dv:         defs 4
aw_lim:     defw 0
sm_ship:    defw 0
pr_even:    defw 0
pr_odd:     defw 0
