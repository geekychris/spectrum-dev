; tgrid.asm - one row of the terrain grid into camera space and onto the
; screen (render.c draw_terrain), Z80N.
;
; For vertex i of the row (i = 0 .. n-1), with h its height (raised to the
; sea), all accumulators 32-bit Q12 (4096 = one world unit):
;     x = X >> 12                      X += AX after each vertex
;     y = (Y + h * cp) >> 12           Y += AY
;     z = (Z - h * sp) >> 12           Z += AZ
; (the camera's rotation is linear, so the C sets up X, Y, Z for the row's
; first vertex with the camera's height folded in). Then, when z >= NEAR_Z:
;     s  = recip(z)                    (render.c's tables)
;     px = 128 + ((x * s) >> 12)       clamped to +-16000
;     py = (77 - ((y * s) >> 12)) >> 1 (drawn rows: half height)
; Results go to gx, gy, gz, gs_x, gs_y at index tg_k (advanced by n).

SECTION BANK_24

PUBLIC _tg_row
EXTERN _gx, _gy, _gz, _rnear, _rfar
PUBLIC _tg_X, _tg_Y, _tg_Z, _tg_AX, _tg_AY, _tg_AZ, _tg_cp, _tg_sp
PUBLIC _tg_hrow, _tg_col, _tg_k, _tg_n

NEAR_Z  equ 24
SEA     equ 160
RFAR_N  equ 114                         ; render.c: FAR_Z / 32 + 2
_gs_x   equ 0x4580                      ; render.c: in the draw list's page
_gs_y   equ 0x4780
_graw   equ 0x4980                      ; the vertices' raw heights

_tg_row:
    push ix
    ld a,(_tg_n)
    ld (cnt),a
tr_loop:
    ; ---- h: the row's heights, column (tg_col & 63)
    ld a,(_tg_col)
    and 63
    ld l,a
    ld h,0
    add hl,hl
    ld de,(_tg_hrow)
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                           ; DE = h
    ld hl,(_tg_k)                       ; graw[k] = h (tq_row's colours)
    add hl,hl
    ld bc,_graw
    add hl,bc
    ld (hl),e
    inc hl
    ld (hl),d
    ld hl,SEA
    or a
    sbc hl,de
    jp m,tr_land                        ; h > SEA
    ld de,SEA                           ; under the sea: the sea's level
tr_land:
    ld (hgt),de

    ; ---- x = X >> 12
    ld hl,_tg_X
    call shr12                          ; HL = (int16)(acc >> 12)
    ld (vx),hl

    ; ---- y = (Y + h cp) >> 12
    ld de,(hgt)
    ld bc,(_tg_cp)
    call umul16                         ; DEHL = h * cp (both positive)
    ld ix,_tg_Y
    call add_acc                        ; tmp = acc + DEHL
    ld hl,tmp
    call shr12
    ld (vy),hl

    ; ---- z = (Z - h sp) >> 12
    ld de,(hgt)
    ld bc,(_tg_sp)
    call umul16
    call neg32                          ; DEHL = -(h * sp)
    ld ix,_tg_Z
    call add_acc
    ld hl,tmp
    call shr12
    ld (vz),hl

    ; ---- store x, y, z at k
    ld hl,(_tg_k)
    add hl,hl
    ld (k2),hl
    ld de,_gx
    call store_vx
    ld de,_gy
    ld bc,(vy)
    call store_bc
    ld de,_gz
    ld bc,(vz)
    call store_bc

    ; ---- project if z >= NEAR_Z
    ld hl,(vz)
    bit 7,h
    jr nz,tr_next
    ld de,NEAR_Z
    or a
    sbc hl,de
    jr c,tr_next
    call recip                          ; HL = s
    ld (sc),hl
    ; px = 128 + clamp((x * s) >> 12)
    ld de,(vx)
    ld bc,(sc)
    call smul16                         ; DEHL = x * s
    call sh12_clamp                     ; HL = clamp(DEHL >> 12)
    ld de,128
    add hl,de
    ld b,h
    ld c,l
    ld de,_gs_x
    call store_bc
    ; py = (77 - clamp((y * s) >> 12)) >> 1
    ld de,(vy)
    ld bc,(sc)
    call smul16
    call sh12_clamp
    ex de,hl
    ld hl,77
    or a
    sbc hl,de
    sra h
    rr l
    ld b,h
    ld c,l
    ld de,_gs_y
    call store_bc

tr_next:
    ; ---- step: X += AX, Y += AY, Z += AZ, col + 1, k + 1
    ld hl,_tg_X
    ld de,_tg_AX
    call acc_add
    ld hl,_tg_Y
    ld de,_tg_AY
    call acc_add
    ld hl,_tg_Z
    ld de,_tg_AZ
    call acc_add
    ld hl,_tg_col
    inc (hl)
    ld hl,(_tg_k)
    inc hl
    ld (_tg_k),hl
    ld hl,cnt
    dec (hl)
    jp nz,tr_loop
    pop ix
    ret

; ------------------------------------------------------------ helpers

; store (vx) at DE + k2
store_vx:
    ld bc,(vx)
; store BC at DE + k2
store_bc:
    ld hl,(k2)
    add hl,de
    ld (hl),c
    inc hl
    ld (hl),b
    ret

; HL -> a 32-bit little-endian value: HL = (int16)(value >> 12)
shr12:
    inc hl                              ; bytes 1, 2: value >> 8
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)                           ; byte 3
    ; (A:D:E) >> 4 -> D:E
    ld b,4
sh_l:
    sra a
    rr d
    rr e
    djnz sh_l
    ex de,hl
    ret

; (HL) += (DE), 32-bit
acc_add:
    ld b,4
    or a
aa_l:
    ld a,(de)
    adc a,(hl)
    ld (hl),a
    inc hl
    inc de
    djnz aa_l
    ret

; tmp = (IX) + DEHL
add_acc:
    ld a,(ix+0)
    add a,l
    ld (tmp),a
    ld a,(ix+1)
    adc a,h
    ld (tmp+1),a
    ld a,(ix+2)
    adc a,e
    ld (tmp+2),a
    ld a,(ix+3)
    adc a,d
    ld (tmp+3),a
    ret

; DEHL = -DEHL
neg32:
    xor a
    sub l
    ld l,a
    ld a,0
    sbc a,h
    ld h,a
    ld a,0
    sbc a,e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    ret

; DEHL = DE * BC, unsigned 16 x 16 (Z80N MUL)
umul16:                                 ; DEHL = DE * BC, unsigned (BC lost)
    ld a,e                              ; A = al
    ld h,d
    ld l,b
    push hl                             ; ah, bh
    ld h,d
    ld l,c
    push hl                             ; ah, bl
    ld d,c
    mul d,e                             ; al * bl
    push de
    ld d,a
    ld e,b
    mul d,e                             ; al * bh
    ex de,hl
    pop bc                              ; BC = the low product
    pop de
    mul d,e                             ; ah * bl
    add hl,de                           ; the middle (17 bits)
    ld a,0
    rla
    ld e,a                              ; its carry
    ld a,b
    add a,l
    ld b,a                              ; byte 1
    ld l,h
    ld h,e                              ; HL = the middle >> 8
    ld de,0
    adc hl,de
    pop de
    mul d,e                             ; ah * bh
    add hl,de
    ex de,hl
    ld h,b
    ld l,c
    ret

; DEHL = DE * BC, DE signed, BC unsigned (< 32768)
smul16:
    bit 7,d
    jr z,umul16
    xor a                               ; DE = -DE
    sub e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    call umul16
    jr neg32

; HL = clamp((DEHL >> 12), -16000, 16000)
sh12_clamp:
    ; DEHL >> 12: take E:H:L >> 4 for the low 16 bits; D:E gives the range
    ld a,d
    ld b,4
sc_l:
    sra a
    rr e
    rr h
    djnz sc_l                           ; A:E:H = value >> 12 (24 bits), with L dropped
    ; A:E:H is the result (24-bit); HL = its low 16 bits, clamped
    ld l,h
    ld h,e
    bit 7,a
    jr nz,sc_neg
    or a                                ; positive: A must be 0, HL <= 16000
    jr nz,sc_hi
    bit 7,h
    jr nz,sc_hi
    push hl
    ld de,16001
    or a
    sbc hl,de
    pop hl
    ret c
sc_hi:
    ld hl,16000
    ret
sc_neg:
    inc a                               ; negative: A must be 0xFF, HL >= -16000
    jr nz,sc_lo
    bit 7,h
    jr z,sc_lo
    push hl
    ld de,-16000
    or a
    sbc hl,de
    pop hl
    ret nc
sc_lo:
    ld hl,-16000
    ret

; HL = recip(z) for z in (vz), z >= NEAR_Z
recip:
    ld hl,(vz)
    ld a,h
    cp 4                                ; z < 1024: rnear[z >> 2]
    jr nc,rc_far
    srl h
    rr l
    srl h
    rr l
    add hl,hl
    ld de,_rnear
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ex de,hl
    ret
rc_far:
    ; i = z >> 5, capped at the table's end (the far corners of tiles partly
    ; in range: a little nearer than they are)
    ld b,5
rf_l:
    srl h
    rr l
    djnz rf_l
    ld de,RFAR_N - 1
    or a
    sbc hl,de
    add hl,de
    jr c,rf_ok
    ex de,hl
rf_ok:
    add hl,hl
    ld de,_rfar
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ex de,hl
    ret

SECTION BANK_24
_tg_X:   defs 4
_tg_Y:   defs 4
_tg_Z:   defs 4
_tg_AX:  defs 4
_tg_AY:  defs 4
_tg_AZ:  defs 4
_tg_cp:  defs 2
_tg_sp:  defs 2
_tg_hrow: defs 2
_tg_col: defs 1
_tg_k:   defs 2
_tg_n:   defs 1
cnt:     defs 1
hgt:     defs 2
vx:      defs 2
vy:      defs 2
vz:      defs 2
k2:      defs 2
sc:      defs 2
tmp:     defs 4

; ------------------------------------------------------------ tq_row
; One row j of terrain tiles (render.c draw_terrain): for tile i = 0 .. 14
; with corners a = (i, j), b = (i+1, j), c = (i+1, j+1), d = (i, j+1):
;   tdepth[j * 15 + i] = (za + zb + zc + zd) >> 2
;   culled when all four are nearer than NEAR_Z, all beyond FAR_Z, all
;   right of x = z or all left of x = -z (the original's tests)
;   the pen (the original's): the pad's checker, the sea's two shades, or
;   the height band of the corners' mean with the b - d slope's shade
;   all corners in front: a quad (the corners' screen points) appended to
;   its depth bucket; else its i goes to tq_clip[] for the C to clip
; Inputs: tq_j, tq_tx (gx0 & 63), tq_tz ((gz0 + j) & 63), tq_padx, tq_padz.

PUBLIC _tq_row, _tq_n, _tq_j, _tq_tx, _tq_tz, _tq_padx, _tq_padz, _tq_clip, _tq_nclip
EXTERN _arena_used, _render_stats

FAR_Z   equ 3600
NBUCKET equ 232
ARENA   equ 0x4000
_bhead  equ 0x4000
_btail  equ 0x4000 + 2 * NBUCKET
_tdepth equ 0x4000 + 4 * NBUCKET

_tq_row:
    push ix
    xor a
    ld (qi),a
tq_loop:
    ; ---- k = j * 16 + i (vertex a); kk = 2k
    ld a,(_tq_j)
    add a,a
    add a,a
    add a,a
    add a,a
    ld hl,qi
    add a,(hl)
    ld l,a
    ld h,0
    add hl,hl
    ld (qk2),hl

    ; ---- the four z, x
    ld de,_gz
    call corners
    ld hl,(c0)
    ld (za),hl
    ld hl,(c1)
    ld (zb),hl
    ld hl,(c2)
    ld (zc),hl
    ld hl,(c3)
    ld (zd),hl
    ld de,_gx
    call corners

    ; ---- depth = sum >> 2, tdepth[j * 15 + i]
    ld hl,(za)
    ld de,(zb)
    add hl,de
    ld de,(zc)
    add hl,de
    ld de,(zd)
    add hl,de
    sra h
    rr l
    sra h
    rr l
    ld (dep),hl
    ld a,(_tq_j)                        ; j * 15 + i
    ld e,a
    add a,a
    add a,a
    add a,a
    add a,a
    sub e
    ld hl,qi
    add a,(hl)
    ld l,a
    ld h,0
    add hl,hl
    ld de,_tdepth
    add hl,de
    ld de,(dep)
    ld (hl),e
    inc hl
    ld (hl),d

    ; ---- culls; nearmask = which corners are nearer than NEAR_Z
    ld ix,za
    ld b,0                              ; near count
    ld c,0                              ; far count
    ld hl,qflags
    ld (hl),0                           ; bit 0: some corner near
    ld e,0                              ; right-of count, d: left-of count
    ld d,0
    ld iy,c0
    ld a,4
    ld (qn),a
cul_l:
    push de
    ld l,(ix+0)
    ld h,(ix+1)                         ; z
    ; near?
    push hl
    ld de,NEAR_Z
    or a
    sbc hl,de
    pop hl
    jp p,cul_nn
    inc b
cul_nn:
    ; far?
    push hl
    ex de,hl
    ld hl,FAR_Z
    or a
    sbc hl,de                           ; FAR_Z - z < 0: beyond
    pop hl
    jp p,cul_nf
    inc c
cul_nf:
    pop de
    ; x > z ?
    push de
    ld e,(iy+0)
    ld d,(iy+1)                         ; DE = x, HL = z
    push hl
    or a
    sbc hl,de                           ; z - x < 0: x > z
    pop hl
    pop de
    jp p,cul_nr
    inc e
cul_nr:
    ; x < -z  <=>  x + z < 0
    push de
    ld e,(iy+0)
    ld d,(iy+1)
    add hl,de
    pop de
    bit 7,h
    jr z,cul_nl
    inc d
cul_nl:
    inc ix
    inc ix
    inc iy
    inc iy
    ld hl,qn
    dec (hl)
    jr nz,cul_l
    ld a,b
    cp 4
    jp z,tq_next                        ; all near
    or a
    jr z,cul_front
    ld a,1
    ld (qflags),a                       ; some near: clip in C
cul_front:
    ld a,c
    cp 4
    jp z,tq_next                        ; all far
    ld a,e
    cp 4
    jp z,tq_next                        ; all right
    ld a,d
    cp 4
    jp z,tq_next                        ; all left

    ; ---- the pen
    ld a,(_tq_tx)                       ; chk = (tx + i + tz) & 1
    ld hl,qi
    add a,(hl)
    ld hl,_tq_tz
    add a,(hl)
    and 1
    ld (chk),a
    ; on the pad: (tx + i - padx) & 63 < 2 and (tz - padz) & 63 < 2
    ld a,(_tq_tx)
    ld hl,qi
    add a,(hl)
    ld hl,_tq_padx
    sub (hl)
    and 63
    cp 2
    jr nc,pen_np
    ld a,(_tq_tz)
    ld hl,_tq_padz
    sub (hl)
    and 63
    cp 2
    jr nc,pen_np
    ld a,(chk)
    or a
    ld a,3                              ; PEN_GREY
    jp nz,pen_set
    ld a,18                             ; PEN_YELLOW
    jp pen_set
pen_np:
    ld de,_graw
    call corners                        ; c0..c3 = ha, hb, hc, hd
    ; all <= SEA: the sea
    ld ix,c0
    ld b,4
pen_sea:
    ld l,(ix+0)
    ld h,(ix+1)
    ld de,SEA + 1
    or a
    sbc hl,de
    jp p,pen_land
    inc ix
    inc ix
    djnz pen_sea
    ld a,(chk)
    add a,4                             ; PEN_TERRAIN + chk
    jp pen_set
pen_land:
    ld hl,(c0)
    ld de,(c1)
    add hl,de
    ld de,(c2)
    add hl,de
    ld de,(c3)
    add hl,de
    srl h
    rr l
    srl h
    rr l                                ; avg (heights are 0..1100)
    ; band: <= SEA -> sand (avg raised to SEA + 1), < 210 sand, < 420 grass,
    ; < 650 forest, < 880 rock, else snow
    ld b,1
    ld de,210
    call cmp_hl_de
    jr c,pen_band
    inc b
    ld de,420
    call cmp_hl_de
    jr c,pen_band
    inc b
    ld de,650
    call cmp_hl_de
    jr c,pen_band
    inc b
    ld de,880
    call cmp_hl_de
    jr c,pen_band
    inc b
pen_band:
    ; shade: s = hb - hd: > 30 -> 1, < -30 -> 0, else chk
    ld hl,(c1)
    ld de,(c3)
    or a
    sbc hl,de
    ld de,31
    push hl
    or a
    sbc hl,de
    pop hl
    ld c,1
    jp p,pen_sh                         ; s >= 31
    ld de,-30
    or a
    sbc hl,de
    ld c,0
    jp m,pen_sh                         ; s < -30
    ld a,(chk)
    ld c,a
pen_sh:
    ld a,b
    add a,a
    add a,4
    add a,c                             ; PEN_TERRAIN + band * 2 + shade
pen_set:
    ld (pen),a

    ; ---- some corner near: leave it to the C
    ld a,(qflags)
    or a
    jr z,tq_quad
    ld a,(_tq_nclip)
    ld e,a
    ld d,0
    ld hl,_tq_clip
    add hl,de
    ld a,(qi)
    ld (hl),a
    inc hl                              ; (its pen next to it)
    ld a,(pen)
    ld (hl),a
    ld a,e
    add a,2
    ld (_tq_nclip),a
    jr tq_count

tq_quad:
    call slot                           ; HL = the slot's points, or carry: no room
    jr c,tq_next
    ex de,hl
    ld bc,0x4580                        ; gs_x
    call put_xy                         ; a
    ld bc,0x4580
    call put_xy
    ld bc,0x4580
    call put_xy
    ld bc,0x4580
    call put_xy
tq_count:
    ld hl,(_render_stats)               ; render_stats.tiles++
    inc hl
    ld (_render_stats),hl
tq_next:
    ld hl,qi
    inc (hl)
    ld a,(_tq_n)
    cp (hl)
    jp nz,tq_loop
    pop ix
    ret

; carry if HL < DE (unsigned)
cmp_hl_de:
    push hl
    or a
    sbc hl,de
    pop hl
    ret

; c0..c3 = the corners a, b, c, d of array DE (k = qk2 / 2)
corners:
    ld hl,(qk2)
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (c0),de                          ; a
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (c1),de                          ; b = a + 1
    ld de,31
    add hl,de                           ; c = a + 17 (byte 34)
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (c2),de
    dec hl
    dec hl                              ; d = a + 16 (bytes 32, 33)
    ld d,(hl)
    dec hl
    ld e,(hl)
    ld (c3),de
    ret

; the next corner's screen point: DE = destination; the corner order a, b,
; c, d (offsets 0, 2, 34, 32 from qk2) comes from qcorner
put_xy:
    push de
    ld a,(qcorner)
    ld l,a
    ld h,0
    ld de,corner_off
    add hl,de
    ld a,(hl)
    ld e,a
    ld d,0
    ld hl,(qk2)
    add hl,de
    ld (qtmp),hl
    ld hl,qcorner
    inc (hl)
    pop de
    ld hl,(qtmp)
    add hl,bc                           ; gs_x + offset
    ldi
    ldi
    ld hl,(qtmp)
    ld bc,0x4780                        ; gs_y
    add hl,bc
    ldi
    ldi
    ret
corner_off:
    defb 0, 2, 34, 32

; a 4-point slot in the draw list (render.c add_poly_slot): HL = its points;
; carry when the arena is full (render_stats.dropped++)
slot:
    xor a
    ld (qcorner),a
    ld hl,(_arena_used)
    ld de,20
    add hl,de
    ld de,8193
    or a
    sbc hl,de
    jr c,sl_room
    ld hl,(_render_stats + 6)          ; dropped
    inc hl
    ld (_render_stats + 6),hl
    scf
    ret
sl_room:
    ; bucket b = depth <= 0 ? 0 : min(depth >> 4, NBUCKET - 1)
    ld hl,(dep)
    bit 7,h
    jr nz,sl_b0
    ld a,h
    or l
    jr z,sl_b0
    ld b,4
sl_sh:
    srl h
    rr l
    djnz sl_sh
    ld de,NBUCKET - 1
    call cmp_hl_de
    jr c,sl_bok
    ex de,hl
    jr sl_bok
sl_b0:
    ld hl,0
sl_bok:
    add hl,hl
    ld (qb2),hl
    ; p = ARENA + arena_used; next = 0; link from the tail or the head
    ld de,(_arena_used)
    inc de                              ; offset + 1 (0 = none)
    ld (qoff),de
    ld hl,_btail
    ld bc,(qb2)
    add hl,bc
    ld a,(hl)
    inc hl
    or (hl)
    jr z,sl_head
    ld d,(hl)                           ; the tail's offset + 1
    dec hl
    ld e,(hl)
    ld hl,ARENA - 1
    add hl,de                           ; the tail's link
    ld de,(qoff)
    ld (hl),e
    inc hl
    ld (hl),d
    jr sl_tail
sl_head:
    ld hl,_bhead
    add hl,bc
    ld de,(qoff)
    ld (hl),e
    inc hl
    ld (hl),d
sl_tail:
    ld hl,_btail
    add hl,bc
    ld de,(qoff)
    ld (hl),e
    inc hl
    ld (hl),d
    ld hl,(_arena_used)
    ld de,ARENA
    add hl,de
    xor a
    ld (hl),a                           ; next = 0
    inc hl
    ld (hl),a
    inc hl
    ld (hl),4                           ; n
    inc hl
    ld a,(pen)
    ld (hl),a                           ; colour
    inc hl
    push hl
    ld hl,(_arena_used)
    ld de,20
    add hl,de
    ld (_arena_used),hl
    ld hl,(_render_stats + 4)           ; polys
    inc hl
    ld (_render_stats + 4),hl
    pop hl
    or a
    ret

SECTION BANK_24
_tq_n:    defs 1
_tq_j:    defs 1
_tq_tx:   defs 1
_tq_tz:   defs 1
_tq_padx: defs 1
_tq_padz: defs 1
_tq_nclip: defs 1
_tq_clip: defs 64
qi:       defs 1
qk2:      defs 2
qn:       defs 1
qflags:   defs 1
chk:      defs 1
pen:      defs 1
dep:      defs 2
za:       defs 2
zb:       defs 2
zc:       defs 2
zd:       defs 2
c0:       defs 2
c1:       defs 2
c2:       defs 2
c3:       defs 2
qb2:      defs 2
qoff:     defs 2
qcorner:  defs 1
qtmp:     defs 2

; ------------------------------------------------------------ xf_model
; A model's vertices into camera space and onto the screen (render.c
; add_model). For vertex (x, y, z) (int16, from xf_src, xf_n of them):
;     x1 = (x m00 + z m01) >> 12          the model's yaw and the camera's
;     z1 = (x m10 + z m11) >> 12          in one matrix
;     cx = x1 + xf_cx
;     cy = ((y cp + z1 sp) >> 12) + xf_cy  (y = 0 when xf_flat)
;     cz = ((z1 cp - y sp) >> 12) + xf_cz
; to mvx, mvy, mvz; projected (as tg_row) into msx, msy when cz >= NEAR_Z.

PUBLIC _xf_model, _xf_src, _xf_n, _xf_m00, _xf_m01, _xf_m10, _xf_m11
PUBLIC _xf_cx, _xf_cy, _xf_cz, _xf_flat, _xf_wide
PUBLIC _mvx, _mvy, _mvz, _msx, _msy

_xf_model:
    push ix
    ld hl,(_xf_src)
    ld (xsrc),hl
    xor a
    ld (xk),a
xm_loop:
    ; ---- the vertex
    ld hl,(xsrc)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (ox),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (oy),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (oz),de
    ld (xsrc),hl
    ld a,(_xf_flat)
    or a
    jr z,xm_noflat
    ld hl,0
    ld (oy),hl
xm_noflat:
    ; ---- x1 = (x m00 + z m01) >> 12 + cx
    ld de,(ox)
    ld bc,(_xf_m00)
    call vmul
    ld (tmp),hl
    ld (tmp+2),de
    ld de,(oz)
    ld bc,(_xf_m01)
    call vmul
    call add_tmp
    call shr12_dehl
    ld de,(_xf_cx)
    add hl,de
    ld (vx),hl
    ; ---- z1 = (x m10 + z m11) >> 12
    ld de,(ox)
    ld bc,(_xf_m10)
    call vmul
    ld (tmp),hl
    ld (tmp+2),de
    ld de,(oz)
    ld bc,(_xf_m11)
    call vmul
    call add_tmp
    call shr12_dehl
    ld (z1),hl
    ; ---- cy = ((y cp + z1 sp) >> 12) + cy0
    ld de,(oy)
    ld bc,(_tg_cp)
    call vmul
    ld (tmp),hl
    ld (tmp+2),de
    ld de,(z1)
    ld bc,(_tg_sp)
    call smul16
    call add_tmp
    call shr12_dehl
    ld de,(_xf_cy)
    add hl,de
    ld (vy),hl
    ; ---- cz = ((z1 cp - y sp) >> 12) + cz0
    ld de,(oy)
    ld bc,(_tg_sp)
    call vmul
    call neg32
    ld (tmp),hl
    ld (tmp+2),de
    ld de,(z1)
    ld bc,(_tg_cp)
    call smul16
    call add_tmp
    call shr12_dehl
    ld de,(_xf_cz)
    add hl,de
    ld (vz),hl
    ; ---- store at k
    ld a,(xk)
    ld l,a
    ld h,0
    add hl,hl
    ld (k2),hl
    ld de,_mvx
    call store_vx
    ld de,_mvy
    ld bc,(vy)
    call store_bc
    ld de,_mvz
    ld bc,(vz)
    call store_bc
    ; ---- project if cz >= NEAR_Z
    ld hl,(vz)
    bit 7,h
    jr nz,xm_next
    ld de,NEAR_Z
    or a
    sbc hl,de
    jr c,xm_next
    call recip
    ld (sc),hl
    ld de,(vx)
    ld bc,(sc)
    call smul16
    call sh12_clamp
    ld de,128
    add hl,de
    ld b,h
    ld c,l
    ld de,_msx
    call store_bc
    ld de,(vy)
    ld bc,(sc)
    call smul16
    call sh12_clamp
    ex de,hl
    ld hl,77
    or a
    sbc hl,de
    sra h
    rr l
    ld b,h
    ld c,l
    ld de,_msy
    call store_bc
xm_next:
    ld hl,xk
    inc (hl)
    ld a,(_xf_n)
    cp (hl)
    jp nz,xm_loop
    pop ix
    ret

; DEHL = DE * BC, both signed: a model's vertex (|DE| <= 255) by
; smul8x16, else (xf_wide: xform's world offsets) by smul16s
vmul:
    ld a,(_xf_wide)
    or a
    jp nz,smul16s
smul8x16:
    ld a,d
    xor b
    push af                             ; the product's sign in bit 7
    bit 7,d
    jr z,s8_pa
    xor a
    sub e
    ld e,a                              ; E = |DE|
s8_pa:
    bit 7,b
    jr z,s8_pb
    xor a
    sub c
    ld c,a
    ld a,0
    sbc a,b
    ld b,a                              ; BC = |BC|
s8_pb:
    ld d,c
    ld l,e
    mul d,e                             ; |v| * low byte
    ld a,l
    ld l,e
    ld h,d
    ld d,b
    ld e,a
    mul d,e                             ; |v| * high byte (<< 8)
    ld a,h
    add a,e
    ld h,a
    ld a,d
    adc a,0
    ld e,a
    ld d,0
    pop af
    ret p
    jp neg32

; DEHL = DE * BC, both signed
smul16s:
    bit 7,b
    jp z,smul16
    xor a                               ; BC = -BC, then negate the product
    sub c
    ld c,a
    ld a,0
    sbc a,b
    ld b,a
    call smul16
    jp neg32

; DEHL += tmp
add_tmp:
    ld a,(tmp)
    add a,l
    ld l,a
    ld a,(tmp+1)
    adc a,h
    ld h,a
    ld a,(tmp+2)
    adc a,e
    ld e,a
    ld a,(tmp+3)
    adc a,d
    ld d,a
    ret

; HL = (int16)(DEHL >> 12)
shr12_dehl:
    ld a,d
    ld b,4
s12_l:
    sra a
    rr e
    rr h
    djnz s12_l
    ld l,h
    ld h,e
    ret

SECTION BANK_24
_xf_src:  defs 2
_xf_n:    defs 1
_xf_m00:  defs 2
_xf_m01:  defs 2
_xf_m10:  defs 2
_xf_m11:  defs 2
_xf_cx:   defs 2
_xf_cy:   defs 2
_xf_cz:   defs 2
_xf_flat: defs 1
_xf_wide: defs 1
_mvx:     defs 64
_mvy:     defs 64
_mvz:     defs 64
_msx:     defs 64
_msy:     defs 64
xsrc:     defs 2
xk:       defs 1
ox:       defs 2
oy:       defs 2
oz:       defs 2
z1:       defs 2
