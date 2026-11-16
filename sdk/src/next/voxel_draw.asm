; voxel.c's inner loops on the Next (see voxel.h): one slice across every
; column (vox_slice), and filling a column's rows on Layer 2's back buffer
; (vox_fill). Same arithmetic as voxel.c's C reference.

SECTION code_user

PUBLIC _vox_slice
PUBLIC _vox_fill
PUBLIC _vox_l2_begin
PUBLIC _vox_l2_end
EXTERN _vox_s
EXTERN _vox_ybuf
EXTERN _l2_back

; vox_s's layout (voxel.c struct VoxState)
VS_HEIGHTS equ 0
VS_COLOURS equ 2
VS_MID     equ 4
VS_STRIPE  equ 6
VS_X       equ 8
VS_Z       equ 10
VS_SX      equ 12
VS_SZ      equ 14
VS_A       equ 16
VS_B       equ 18
VS_CSH     equ 20
VS_NBANDS  equ 21
VS_BANDS   equ 22                   ; 5 bytes each: half (2), pen0, pen1, sided

; ---- Layer 2 through MMU slot 3 ----

_vox_l2_begin:
    ld bc,0x243B
    ld a,0x53
    out (c),a
    inc b
    in a,(c)
    ld (saved53),a
    ld a,0xFF
    ld (curpage),a
    ld a,(_l2_back)
    add a,a
    ld (l2base),a
    ret

_vox_l2_end:
    ld a,(saved53)
    nextreg 0x53,a
    ret

; fill rows y0 <= y < y1 of pixel columns 2c, 2c + 1 with pen
; in: c = column, d = y0, e = y1, a = pen (preserves nothing else)
fill_core:
    ld (fpen),a
    ld a,c
    add a,a
    ld (fcol),a
fill_chunk:
    ld a,d
    cp e
    ret nc
    ; the page holding row d: base + d / 32
    rlca
    rlca
    rlca
    and 7
    ld hl,l2base
    add a,(hl)
    ld hl,curpage
    cp (hl)
    jr z,fill_mapped
    ld (hl),a
    nextreg 0x53,a
fill_mapped:
    ; rows to fill in this page: min(e, (d | 31) + 1) - d
    ld a,d
    or 31
    inc a
    cp e
    jr c,fill_lim
    ld a,e
fill_lim:
    sub d
    ld b,a                          ; rows
    ld a,d
    and 31
    add a,0x60
    ld h,a
    ld a,(fcol)
    ld l,a
    ld a,d
    add a,b
    ld d,a                          ; y0 for the next chunk
    ld a,(fpen)
fill_row:
    ld (hl),a
    inc l
    ld (hl),a
    dec l
    inc h
    djnz fill_row
    jr fill_chunk

; void vox_fill(uint8_t c, uint8_t y0, uint8_t y1, uint8_t pen)
_vox_fill:
    push ix
    ld ix,0
    add ix,sp
    ld c,(ix+4)
    ld d,(ix+5)
    ld e,(ix+6)
    ld a,(ix+7)
    call fill_core
    pop ix
    ret

; ---- one slice ----

_vox_slice:
    push ix
    push iy
    ld ix,_vox_s
    ld iy,_vox_ybuf
    ld hl,(_vox_s + VS_X)
    ld (vpx),hl
    ld hl,(_vox_s + VS_Z)
    ld (vpz),hl
    xor a
    ld (vcol),a

col_loop:
    ld a,(iy+0)                     ; this column's top so far
    or a
    jp z,col_next
    ld (ytop),a
    ; the height: heights[(z >> 8 & 127) * 128 + (x >> 8 & 127)]
    ld a,(vpz + 1)
    and 127
    srl a                           ; H part, bit 0 -> carry
    ld h,a
    ld a,(vpx + 1)
    and 127
    jr nc,h_even
    or 128
h_even:
    ld l,a
    ld e,(ix+VS_HEIGHTS)
    ld d,(ix+VS_HEIGHTS+1)
    add hl,de
    ld a,(hl)                       ; h, signed
    ; q = (h * b) >> 8, floor (arithmetic shift as in C)
    ld (hsign),a
    bit 7,a
    jr z,h_pos
    neg
h_pos:
    ld c,a                          ; |h| (0..128)
    ld d,a
    ld e,(ix+VS_B)                  ; |h| * b_lo
    mul d,e
    ld b,d                          ; high byte of the low product
    ld l,e                          ; low byte
    ld d,c
    ld e,(ix+VS_B+1)                ; |h| * b_hi
    mul d,e
    ld a,e                          ; p = |h| * b = (de << 8) + (b:l): q = de + b (p >> 8)
    add a,b
    ld e,a
    jr nc,q_nc
    inc d
q_nc:                               ; de = p >> 8, l = p & 255
    ld a,(hsign)
    bit 7,a
    jr z,q_done
    ; negative h: q = -((p + 255) >> 8) = -(p >> 8) - (l != 0)
    ld a,l
    or a
    jr z,q_neg
    inc de
q_neg:
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
q_done:
    ; y = a - q
    ld l,(ix+VS_A)
    ld h,(ix+VS_A+1)
    or a
    sbc hl,de
    bit 7,h
    jr z,y_pos
    ld hl,0
y_pos:
    ld a,h
    or a
    jp nz,col_next                  ; y >= 256: below everything
    ld a,(ytop)
    cp l
    jp c,col_next
    jp z,col_next                   ; y >= ytop: hidden
    ld a,l
    ld (ynew),a

    ; the pen: the ribbon's band, else the colour map
    ld e,(ix+VS_MID)
    ld d,(ix+VS_MID+1)
    ld a,d
    or e
    jr z,pen_map
    ld a,(vpz + 1)
    and 127
    ld (zrow),a
    ld l,a
    ld h,0
    add hl,hl
    add hl,de                       ; &mid[row]
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(vpx)
    or a
    sbc hl,de                       ; d = x - mid (signed 16)
    ld a,0
    bit 7,h
    jr z,d_pos
    ld a,1
    ex de,hl
    ld hl,0
    or a
    sbc hl,de
    ld a,1
d_pos:
    ld (vleft),a
    ; the stripe for this row
    ld e,(ix+VS_STRIPE)
    ld d,(ix+VS_STRIPE+1)
    push hl
    ld a,(zrow)
    ld l,a
    ld h,0
    add hl,de
    ld a,(hl)
    and 1
    ld (vstripe),a
    pop hl                          ; |d|
    ld b,(ix+VS_NBANDS)
    ld a,b
    or a
    jr z,pen_map
    push ix
    ld de,VS_BANDS
    add ix,de
band_loop:
    ; |d| < half ?
    ld e,(ix+0)
    ld d,(ix+1)
    push hl
    or a
    sbc hl,de
    pop hl
    jr c,band_in
    ld de,5
    add ix,de
    djnz band_loop
    pop ix
    jr pen_map
band_in:
    ld a,(ix+4)                     ; sided
    and 1
    jr z,band_side
    ld a,(vleft)
band_side:
    ld e,a
    ld a,(vstripe)
    xor e
    and 1
    jr z,band_p0
    ld a,(ix+3)
    pop ix
    jr pen_got
band_p0:
    ld a,(ix+2)
    pop ix
    jr pen_got

pen_map:
    ; colours[(cz >> csh) << (7 - csh) | (cx >> csh)]
    ld b,(ix+VS_CSH)
    ld a,(vpz + 1)
    and 127
    ld e,a
    ld a,(vpx + 1)
    and 127
    ld d,a
    ld a,b
    or a
    jr z,map_noshift
map_sh:
    srl e
    srl d
    djnz map_sh
map_noshift:
    ; hl = e << (7 - csh): shift e left by 7 - csh as 16 bits
    ld l,e
    ld h,0
    ld a,7
    sub (ix+VS_CSH)
    jr z,map_noshl
    ld b,a
map_shl:
    add hl,hl
    djnz map_shl
map_noshl:
    ld a,l
    or d
    ld l,a
    ld e,(ix+VS_COLOURS)
    ld d,(ix+VS_COLOURS+1)
    add hl,de
    ld a,(hl)

pen_got:
    ; fill rows ynew .. ytop - 1
    ld (vpen),a
    ld a,(vcol)
    ld c,a
    ld a,(ynew)
    ld d,a
    ld (iy+0),a
    ld a,(ytop)
    ld e,a
    ld a,(vpen)
    call fill_core

col_next:
    ld hl,(vpx)
    ld e,(ix+VS_SX)
    ld d,(ix+VS_SX+1)
    add hl,de
    ld (vpx),hl
    ld hl,(vpz)
    ld e,(ix+VS_SZ)
    ld d,(ix+VS_SZ+1)
    add hl,de
    ld (vpz),hl
    inc iy
    ld a,(vcol)
    inc a
    ld (vcol),a
    cp 128
    jp c,col_loop
    pop iy
    pop ix
    ret

SECTION bss_user
saved53: defb 0
curpage: defb 0
l2base:  defb 0
fpen:    defb 0
fcol:     defb 0
vpx:      defw 0
vpz:      defw 0
vcol:    defb 0
ytop:    defb 0
ynew:    defb 0
hsign:   defb 0
zrow:    defb 0
vleft:   defb 0
vstripe: defb 0
vpen:    defb 0
