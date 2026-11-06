; lanes_tick() from lanes.c, in assembly (same arithmetic; zsdcc's version
; took ~36000 T-states a frame). Lane layout (lanes.h, packed, little endian):
;   +0 row  +2 dir  +4 speed16  +6 tick_div  +8 tick_cnt  +10 num_objs
;   +12 kind (byte)  +13 is_river  +15 objs[6] of {x16, width}   = 39 bytes

SECTION code_user

PUBLIC _lanes_tick
EXTERN _lanes

NUM_LANES   equ 10
LANE_SIZE   equ 39
RIGHT_LIMIT equ (256 + 16) * 16           ; x16 > this wraps (moving right)
LEFT_LIMIT  equ -16 * 16                  ; x16 + width*16 < this wraps (moving left)
RESTART_L   equ 256 * 16                  ; where a left-mover re-enters

_lanes_tick:
    push ix
    ld ix,_lanes
    ld b,NUM_LANES
lane:
    push bc
    ; tick_cnt++; if (tick_cnt < tick_div) continue; tick_cnt = 0
    ld l,(ix+8)
    ld h,(ix+9)
    inc hl
    ld e,(ix+6)
    ld d,(ix+7)
    ld a,l
    sub e
    ld a,h
    sbc a,d                     ; tick_cnt - tick_div (both small, positive)
    jp m,store_cnt              ; still < tick_div
    ld hl,0
    ld (ix+8),l
    ld (ix+9),h
    ; objects
    ld a,(ix+10)
    or a
    jr z,next_lane
    ld b,a
    push ix
    pop iy
    ld de,15
    add iy,de                   ; IY -> objs[0]
obj:
    ld l,(iy+0)
    ld h,(iy+1)                 ; HL = x16
    ld e,(ix+4)
    ld d,(ix+5)                 ; DE = speed16
    ld a,(ix+2)
    dec a                       ; dir == 1 (right)?
    jr nz,move_left
    add hl,de
    ; right: if (x16 > (256+16)*16) x16 = -width*16
    push hl
    ld de,RIGHT_LIMIT
    or a
    sbc hl,de                   ; x16 - limit (signed)
    jr z,keep_r
    jp m,keep_r
    pop hl
    call width16
    ex de,hl                    ; DE = width*16
    ld hl,0
    or a
    sbc hl,de                   ; HL = -width*16
    jr store_x
keep_r:
    pop hl
    jr store_x
move_left:
    or a
    sbc hl,de
    ; left: if (x16 + width*16 < -16*16) x16 = 256*16
    push hl
    call width16                ; HL = width*16
    pop de
    push de
    add hl,de                   ; x16 + width*16
    ld de,LEFT_LIMIT
    or a
    sbc hl,de                   ; (x16 + w16) - limit
    pop hl
    jp p,store_x
    ld hl,RESTART_L
store_x:
    ld (iy+0),l
    ld (iy+1),h
    ld de,4
    add iy,de
    djnz obj
    jr next_lane
store_cnt:
    ld (ix+8),l
    ld (ix+9),h
next_lane:
    ld de,LANE_SIZE
    add ix,de
    pop bc
    dec b
    jp nz,lane
    pop ix
    ret

; HL = width * 16 for the object at IY (width is small and positive)
width16:
    ld l,(iy+2)
    ld h,(iy+3)
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ret
