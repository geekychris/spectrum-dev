; cellspr48 (see cellspr48.h): the per-frame sprite pass of cs_update.
; For each of the first cs_nspr sprites: if it moved, changed picture or
; colour, or was switched on/off, mark the cells it covered and covers
; (cs_mark_rc) and record what is drawn now (dx.. dsrc, dgfx = the pre-shifted
; copy for x & 7). Then the composer's list of drawn sprites (cs_act: c0, c1,
; y, h, stride, gfx, keep, ink) and cs_nact.
;
; cs_sprite (cellspr48.h, no padding): 0 x, 1 y, 2 gfx, 4 w, 5 h, 6 ink,
; 7 keep, 8 on, 9 dx, 10 dy, 11 dw, 12 dh, 13 don, 14 dink, 15 dgfx, 17 dsrc

SECTION code_user

PUBLIC _cs_sprite_pass
EXTERN _cs_spr, _cs_nspr, _cs_act, _cs_nact, _cs_mark_rc

defc S_X = 0
defc S_Y = 1
defc S_GFX = 2
defc S_W = 4
defc S_H = 5
defc S_INK = 6
defc S_KEEP = 7
defc S_ON = 8
defc S_DX = 9
defc S_DY = 10
defc S_DW = 11
defc S_DH = 12
defc S_DON = 13
defc S_DINK = 14
defc S_DGFX = 15
defc S_DSRC = 17
defc S_SIZE = 19

_cs_sprite_pass:
    push ix
    ld ix,_cs_spr
    ld a,(_cs_nspr)
    or a
    jp z,sp_list
    ld (sp_left),a
sp_one:
    ld a,(ix+S_ON)
    or a
    jr nz,sp_on
    ld a,(ix+S_DON)                 ; off: wipe it if it was drawn
    or a
    jp z,sp_next
    call sp_mark_drawn
    ld (ix+S_DON),0
    jp sp_next
sp_on:
    ld a,(ix+S_DON)
    or a
    jp z,sp_changed
    ld a,(ix+S_X)                   ; unchanged? x, y, picture, ink
    cp (ix+S_DX)
    jp nz,sp_moved
    ld a,(ix+S_Y)
    cp (ix+S_DY)
    jp nz,sp_moved
    ld a,(ix+S_GFX)
    cp (ix+S_DSRC)
    jp nz,sp_moved
    ld a,(ix+S_GFX+1)
    cp (ix+S_DSRC+1)
    jp nz,sp_moved
    ld a,(ix+S_INK)
    cp (ix+S_DINK)
    jp z,sp_next
sp_moved:
    call sp_mark_drawn
sp_changed:
    ld a,(ix+S_X)
    ld (ix+S_DX),a
    ld a,(ix+S_Y)
    ld (ix+S_DY),a
    ld a,(ix+S_W)
    ld (ix+S_DW),a
    ld a,(ix+S_H)
    ld (ix+S_DH),a
    ld a,(ix+S_INK)
    ld (ix+S_DINK),a
    ld (ix+S_DON),1
    ld l,(ix+S_GFX)
    ld h,(ix+S_GFX+1)
    ld (ix+S_DSRC),l
    ld (ix+S_DSRC+1),h
    ; dgfx = gfx + (x & 7) * (w + 1) * h
    ld a,(ix+S_X)
    and 7
    jr z,sp_shift_done
    ld b,a
    push hl
    ld hl,0                         ; HL = (w + 1) * h
    ld d,0
    ld e,(ix+S_H)
    ld a,(ix+S_W)
    inc a
sp_step:
    add hl,de
    dec a
    jr nz,sp_step
    ex de,hl
    pop hl
sp_shift:
    add hl,de
    djnz sp_shift
sp_shift_done:
    ld (ix+S_DGFX),l
    ld (ix+S_DGFX+1),h
    call sp_mark_drawn              ; the cells it covers now
sp_next:
    ld de,S_SIZE
    add ix,de
    ld hl,sp_left
    dec (hl)
    jp nz,sp_one

sp_list:                            ; cs_act: the drawn sprites
    ld ix,_cs_spr
    ld hl,_cs_act
    ld c,0                          ; C = cs_nact
    ld a,(_cs_nspr)
    or a
    jr z,sp_list_done
    ld b,a
sp_list_one:
    ld a,(ix+S_DON)
    or a
    jr z,sp_list_next
    inc c
    ld a,(ix+S_DX)
    ld e,a
    rrca
    rrca
    rrca
    and 0x1F
    ld (hl),a                       ; c0
    inc hl
    add a,(ix+S_DW)
    ld d,a
    ld a,e
    and 7
    jr nz,sp_c1
    dec d                           ; aligned: the extra byte is empty
sp_c1:
    ld (hl),d                       ; c1
    inc hl
    ld a,(ix+S_DY)
    ld (hl),a
    inc hl
    ld a,(ix+S_DH)
    ld (hl),a
    inc hl
    ld a,(ix+S_DW)
    inc a
    ld (hl),a                       ; stride
    inc hl
    ld a,(ix+S_DGFX)
    ld (hl),a
    inc hl
    ld a,(ix+S_DGFX+1)
    ld (hl),a
    inc hl
    ld a,(ix+S_KEEP)
    ld (hl),a
    inc hl
    ld a,(ix+S_DINK)
    ld (hl),a
    inc hl
sp_list_next:
    ld de,S_SIZE
    add ix,de
    djnz sp_list_one
sp_list_done:
    ld a,c
    ld (_cs_nact),a
    pop ix
    ret

; mark the cells under the drawn sprite at IX (dx, dy, dw, dh)
sp_mark_drawn:
    ld a,(ix+S_DX)
    ld e,a
    rrca
    rrca
    rrca
    and 0x1F
    ld (mk_c0),a
    add a,(ix+S_DW)
    ld d,a
    ld a,e
    and 7
    jr nz,mk_1
    dec d
mk_1:
    ld a,d
    ld (mk_c1),a
    ld a,(ix+S_DY)
    ld e,a
    add a,(ix+S_DH)
    dec a
    rrca
    rrca
    rrca
    and 0x1F
    ld (mk_r1),a
    ld a,e
    rrca
    rrca
    rrca
    and 0x1F                        ; A = row
mk_row:
    ld h,a
    ld a,(mk_c0)
mk_col:
    ld l,a
    push hl
    call _cs_mark_rc
    pop hl
    ld a,(mk_c1)
    cp l
    jr z,mk_col_done
    ld a,l
    inc a
    jr mk_col
mk_col_done:
    ld a,(mk_r1)
    cp h
    ret z
    ld a,h
    inc a
    jr mk_row

SECTION bss_user
sp_left: defs 1
mk_c0:   defs 1
mk_c1:   defs 1
mk_r1:   defs 1
