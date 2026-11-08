; vfill.asm - the ground strips (vfill.h) in Z80N assembly: vf_ground()
; draws every strip near to far into the back buffer (mapped at 0x0000-
; 0x5FFF by vf_draw: pixel (x, row) at row * 256 + x), keeping each
; column's highest row drawn so far in vf_ybuf.
;
; A strip is drawn as runs: one per side edge of its cells (the wedge
; between the edge and the near line takes the near side's cell), then
; one per segment of its far line (its cells, down to what the edges and
; the nearer strips left). A run steps a line across its columns (8.8
; rows, 1 add a column); in a column whose new top T is above vf_ybuf,
; rows T.. down to vf_ybuf are filled (a jump into an unrolled block) and
; vf_ybuf becomes T.
;
; Rows here are offset by 32 (vf_y, vf_ybuf: row + 32, 8.8 in vf_y), so a
; line that runs up to 31 rows above the view still steps in 16 bits; a
; row above the view fills from row 0.
;
; Page 47 at 0x6000 (MMU slot 3), with vfill.c. IX and IY are kept.

EXTERN _vf_v, _vf_col, _vf_nlines, _vf_snap_at, _dma_copy
EXTERN l_mulu_32_16x16
PUBLIC _vf_ground, _vf_ybuf, _vf_snap, _vf_tmp, _vf_full, _vf_seg, _vf_poly_page

NVX     equ 14
NLINE   equ 14
VROWS   equ 92
OFF     equ 32                  ; the row offset
NONE    equ 0xFF
NSNAP   equ 4

SECTION bss_align_256
ALIGN 256
_vf_ybuf:   defs 256            ; each column's highest row drawn + 32
_vf_snap:   defs 256 * NSNAP    ; vf_ybuf as it was before vf_snap_at[q]
_vf_tmp:    defs 256            ; a polygon's lower edge (rows + 32)
_vf_full:   defs 256            ; VROWS + 32 everywhere (no snapshot: nothing in front)

; ---------------------------------------------------------------- the block
; n rows down from (H, L): enter at fill_blk + 184 - 2n with C = the pen
; (resident, 256-byte aligned: the entry's low byte is all that changes)
SECTION data_align_256
ALIGN 256
fill_blk:
    REPT VROWS
    ld (hl),c
    inc h
    ENDR
    jp run_next

SECTION PAGE_47

; ---------------------------------------------------------------- a run
; IX = the row + 32 at the first column (8.8), DE = the slope (8.8 a
; column), L = the first column, B = columns (0: 256), C = the pen
run_loop:
    ld a,ixh                    ; T
run_page:
    ld h,_vf_ybuf / 256         ; (the columns' tops: vf_ybuf, or vf_tmp for a polygon)
    cp (hl)
    jr nc,run_next              ; at or below what is drawn: hidden
    cp OFF
    jr nc,run_vis
    ld a,OFF                    ; above the view: from row 0
run_vis:
    ld iyl,a
    add a,a                     ; 2T - 2 ybuf = -2n
    sub (hl)
    sub (hl)
    add a,184
    ld (run_jp + 1),a
    ld a,iyl
    ld (hl),a                   ; vf_ybuf = T
    sub OFF
    ld h,a                      ; row T - 32
run_jp:
    jp fill_blk                 ; (low byte set above)
run_next:
    add ix,de
    inc l
    djnz run_loop
    ret

; ---------------------------------------------------------------- a segment
; from (sg_xa, sg_ya) to (sg_xb, sg_yb) (contiguous), sg_xa < sg_xb (x:
; pixels, -256..511; y: rows + 32, 8.8), at least one end on the screen's
; columns, in pen sg_pen
seg:
    ld hl,(sg_xb)
    ld de,(sg_xa)
    or a
    sbc hl,de
    ld (sg_dx),hl               ; dx = xb - xa (1..767)
    ; x0 = max(xa, 0), k = columns skipped at the left
    ld hl,0
    bit 7,d
    jr z,sg_xapos
    sbc hl,de                   ; (carry clear) k = -xa
    ld (sg_k),hl
    ld l,0                      ; x0 = 0
    jr sg_setx0
sg_xapos:
    ld (sg_k),hl                ; 0
    ld l,e                      ; x0 = xa (< 256)
sg_setx0:
    ld a,l
    ld (sg_x0),a
    ; count = min(xb, 256) - x0 (256: 0)
    ld hl,(sg_xb)
    ld a,h
    or a
    ld a,l
    jr z,sg_x1
    xor a                       ; xb >= 256: x1 = 256
sg_x1:
    ld hl,sg_x0
    sub (hl)
    ld (sg_n),a
    ; dy = yb - ya, 17 bits (carry: negative)
    ld hl,(sg_yb)
    ld de,(sg_ya)
    or a
    sbc hl,de
    ld a,0
    jr nc,sg_dypos
    ex de,hl                    ; |dy| = 0 - HL
    xor a
    ld h,a
    ld l,a
    sbc hl,de
    ld a,1
sg_dypos:
    ld (sg_neg),a
    ; |slope| = |dy| * R[dx] >> 16 (dx = 1: |dy|)
    ld de,(sg_dx)
    ld a,d
    or a
    jr nz,sg_rtab
    ld a,e
    dec a
    jr z,sg_slope
sg_rtab:
    ex de,hl
    add hl,hl
    ld bc,recip
    add hl,bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    ex de,hl
    ld e,c
    ld d,b
    call l_mulu_32_16x16        ; DEHL = |dy| * R
    ex de,hl
sg_slope:
    bit 7,h
    jr z,sg_sat
    ld hl,32767
sg_sat:
    ld a,(sg_neg)
    or a
    jr z,sg_spos
    ex de,hl
    xor a
    ld l,a
    ld h,a
    sbc hl,de
sg_spos:
    ld (sg_slope_v),hl
    ; the row at the first column: ya + slope * k (mod 2^16)
    ld de,(sg_k)
    ld a,d
    or e
    jr z,sg_k0
    call l_mulu_32_16x16        ; HL = the low word of slope * k
    ld de,(sg_ya)
    add hl,de
    jr sg_acc
sg_k0:
    ld hl,(sg_ya)
sg_acc:
    push hl
    pop ix
    ld de,(sg_slope_v)
    ld a,(sg_n)
    ld b,a
    ld a,(sg_x0)
    ld l,a
    ld a,(sg_pen)
    ld c,a
    ld a,(sg_mode)
    or a
    jp z,run_loop               ; returns from there
; write mode: vf_tmp[x] = min(T + 1, snapshot[x]): a polygon's lower edge
wrun_loop:
    ld a,ixh
wrun_snap:
    ld h,_vf_snap / 256         ; (the polygon's snapshot page)
    cp (hl)
    ld a,(hl)
    jr nc,wrun_put              ; T >= the snapshot: it
    ld a,ixh
    inc a
wrun_put:
    ld h,_vf_tmp / 256
    ld (hl),a
    add ix,de
    inc l
    djnz wrun_loop
    ret

; ---------------------------------------------------------------- the strips
; void vf_ground(void): vf_v[line][k] = { int16 x, uint16 y }, x = VF_BEHIND
; (0x8000) for a point behind the camera
VREC    equ 4
LSTEP   equ NVX * VREC          ; a line's records

_vf_ground:
    push ix
    push iy
    ld a,_vf_ybuf / 256
    ld (run_page + 1),a
    xor a
    ld (sg_mode),a
    ld hl,_vf_ybuf
    ld (hl),VROWS + OFF
    ld de,_vf_ybuf + 1
    ld bc,255
    ldir
    xor a
    ld (gs_s),a
    ld hl,_vf_v
    ld (gs_near),hl
    ld hl,_vf_col
    ld (gs_col),hl
gs_strip:
    ld a,(_vf_nlines)
    dec a
    ld b,a
    ld a,(gs_s)
    cp b
    jp nc,gs_end
    call snaps
    ; ---- side edges leaning right (near x > far x), k = 1.., their left cell
    ld hl,(gs_near)
    ld de,VREC
    add hl,de                   ; edge 1
    ld (gs_p),hl
    ld hl,(gs_col)
    ld (gs_c),hl                ; cell k - 1
    ld a,NVX - 1
gs_er:
    ld (gs_n),a
    ld hl,(gs_c)
    ld a,(hl)
    cp NONE
    jr z,gs_er_next
    ld (sg_pen),a
    ld hl,(gs_p)
    call edge_load              ; carry: skip; Z: leans right
    jr c,gs_er_next
    jr nz,gs_er_next
    ld hl,(e_far)               ; a = far, b = near
    ld de,sg_xa
    ldi
    ldi
    ldi
    ldi
    ld hl,(gs_p)
    ldi
    ldi
    ldi
    ldi
    call seg_vis
gs_er_next:
    ld hl,(gs_p)
    ld de,VREC
    add hl,de
    ld (gs_p),hl
    ld hl,(gs_c)
    inc hl
    ld (gs_c),hl
    ld a,(gs_n)
    dec a
    jr nz,gs_er
    ; ---- leaning left, k = NVX - 2 .. 0, their right cell (k)
    ld hl,(gs_near)
    ld de,(NVX - 2) * VREC
    add hl,de
    ld (gs_p),hl
    ld hl,(gs_col)
    ld de,NVX - 2
    add hl,de
    ld (gs_c),hl
    ld a,NVX - 1
gs_el:
    ld (gs_n),a
    ld hl,(gs_c)
    ld a,(hl)
    cp NONE
    jr z,gs_el_next
    ld (sg_pen),a
    ld hl,(gs_p)
    call edge_load
    jr c,gs_el_next
    jr z,gs_el_next
    ld hl,(gs_p)                ; a = near, b = far
    ld de,sg_xa
    ldi
    ldi
    ldi
    ldi
    ld hl,(e_far)
    ldi
    ldi
    ldi
    ldi
    call seg_vis
gs_el_next:
    ld hl,(gs_p)
    ld de,-VREC
    add hl,de
    ld (gs_p),hl
    ld hl,(gs_c)
    dec hl
    ld (gs_c),hl
    ld a,(gs_n)
    dec a
    jr nz,gs_el
    ; ---- the far line's segments, k = 0 .. NVX - 2, cell k
    ld hl,(gs_near)
    ld de,LSTEP
    add hl,de
    ld (gs_p),hl
    ld hl,(gs_col)
    ld (gs_c),hl
    ld a,NVX - 1
gs_fa:
    ld (gs_n),a
    ld hl,(gs_c)
    ld a,(hl)
    cp NONE
    jr z,gs_fa_next
    ld (sg_pen),a
    ld hl,(gs_p)                ; points k, k + 1 -> sg_xa sg_ya sg_xb sg_yb
    ld de,sg_xa
    ld bc,8
    ldir
    ld hl,(sg_xa)
    ld a,h
    cp 0x80
    jr nz,gs_fa_a
    ld a,l
    or a
    jr z,gs_fa_next             ; behind the camera
gs_fa_a:
    ld hl,(sg_xb)
    ld a,h
    cp 0x80
    jr nz,gs_fa_b
    ld a,l
    or a
    jr z,gs_fa_next
gs_fa_b:
    ld de,(sg_xa)               ; xb > xa, else facing away sideways
    or a
    sbc hl,de
    jr z,gs_fa_next
    jp m,gs_fa_next
    call seg_vis
gs_fa_next:
    ld hl,(gs_p)
    ld de,VREC
    add hl,de
    ld (gs_p),hl
    ld hl,(gs_c)
    inc hl
    ld (gs_c),hl
    ld a,(gs_n)
    dec a
    jr nz,gs_fa
    ; ---- the next strip
    ld hl,(gs_near)
    ld de,LSTEP
    add hl,de
    ld (gs_near),hl
    ld hl,(gs_col)
    ld de,NVX - 1
    add hl,de
    ld (gs_col),hl
    ld hl,gs_s
    inc (hl)
    jp gs_strip
gs_end:
    ld a,255                    ; the snapshots still to take
    call snaps
    pop iy
    pop ix
    ret

;  void vf_seg(const VfVtx *a, const VfVtx *b, uint8_t pen, uint8_t mode)
; a polygon's edge from a to b (a.x < b.x): mode 0 fills down from it to
; vf_tmp (its upper edges), 1 writes vf_tmp (its lower ones) clipped by
; the snapshot page set by vf_poly_page()
_vf_seg:
    push ix
    push iy
    ld hl,6
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; a
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; b
    inc hl
    ld a,(hl)
    ld (sg_pen),a
    inc hl
    ld a,(hl)
    ld (sg_mode),a
    push bc                     ; (LDI counts BC down)
    ex de,hl
    ld de,sg_xa
    ldi
    ldi
    ldi
    ldi
    pop hl
    ldi
    ldi
    ldi
    ldi
    call seg_vis
    pop iy
    pop ix
    ret

; void vf_poly_page(uint8_t snap_page): the polygons' tops and snapshot
_vf_poly_page:
    ld hl,2
    add hl,sp
    ld a,(hl)
    ld (wrun_snap + 1),a
    ld a,_vf_tmp / 256
    ld (run_page + 1),a
    ret

; seg if any of sg_xa..sg_xb is on the screen's columns (xb > 0, xa < 256)
seg_vis:
    ld hl,(sg_xb)
    bit 7,h
    ret nz
    ld a,h
    or l
    ret z
    ld hl,(sg_xa)
    bit 7,h
    jp nz,seg
    ld a,h
    or a
    ret nz
    jp seg

; HL = edge k's near record (its far one LSTEP on): carry if either is
; behind the camera or the edge is upright; else e_far = the far record
; and Z if it leans right (near x > far x)
edge_load:
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; near x
    dec hl
    ld a,d
    cp 0x80
    jr nz,el_n
    ld a,e
    or a
    scf
    ret z
el_n:
    ld bc,LSTEP
    add hl,bc
    ld (e_far),hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; far x
    cp 0
    jr nz,el_f
    ld a,h
    cp 0x80
    scf
    ret z
el_f:
    ex de,hl                    ; HL = near x, DE = far x
    or a
    sbc hl,de
    scf
    ret z                       ; upright
    ld a,h
    and 0x80                    ; Z: near > far (carry clear)
    ret

; A = the strip about to be drawn (255: the end): vf_ybuf to vf_snap[q]
; for every q with vf_snap_at[q] == A (at the end: those not taken)
snaps:
    ld (sn_s),a
    ld b,0
sn_loop:
    ld hl,_vf_snap_at
    ld e,b
    ld d,0
    add hl,de
    ld a,(sn_s)
    cp 255
    jr z,sn_end_test
    cp (hl)
    jr nz,sn_next
    jr sn_take
sn_end_test:
    ld a,(_vf_nlines)
    dec a                       ; strips drawn
    ld c,a
    ld a,(hl)
    cp c
    jr c,sn_next                ; taken during the strips
sn_take:
    push bc
    ld hl,256                   ; dma_copy(vf_snap + q * 256, vf_ybuf, 256)
    push hl
    ld hl,_vf_ybuf
    push hl
    ld h,b
    ld l,0
    ld de,_vf_snap
    add hl,de
    push hl
    call _dma_copy
    pop af
    pop af
    pop af
    pop bc
sn_next:
    inc b
    ld a,b
    cp NSNAP
    jr c,sn_loop
    ret

; R[dx] = 65536 / dx rounded (dx = 2..767; [1] unused)
recip:
    defw 0, 65535, 32768, 21845, 16384, 13107, 10923, 9362, 8192, 7282, 6554, 5958
    defw 5461, 5041, 4681, 4369, 4096, 3855, 3641, 3449, 3277, 3121, 2979, 2849
    defw 2731, 2621, 2521, 2427, 2341, 2260, 2185, 2114, 2048, 1986, 1928, 1872
    defw 1820, 1771, 1725, 1680, 1638, 1598, 1560, 1524, 1489, 1456, 1425, 1394
    defw 1365, 1337, 1311, 1285, 1260, 1237, 1214, 1192, 1170, 1150, 1130, 1111
    defw 1092, 1074, 1057, 1040, 1024, 1008, 993, 978, 964, 950, 936, 923
    defw 910, 898, 886, 874, 862, 851, 840, 830, 819, 809, 799, 790
    defw 780, 771, 762, 753, 745, 736, 728, 720, 712, 705, 697, 690
    defw 683, 676, 669, 662, 655, 649, 643, 636, 630, 624, 618, 612
    defw 607, 601, 596, 590, 585, 580, 575, 570, 565, 560, 555, 551
    defw 546, 542, 537, 533, 529, 524, 520, 516, 512, 508, 504, 500
    defw 496, 493, 489, 485, 482, 478, 475, 471, 468, 465, 462, 458
    defw 455, 452, 449, 446, 443, 440, 437, 434, 431, 428, 426, 423
    defw 420, 417, 415, 412, 410, 407, 405, 402, 400, 397, 395, 392
    defw 390, 388, 386, 383, 381, 379, 377, 374, 372, 370, 368, 366
    defw 364, 362, 360, 358, 356, 354, 352, 350, 349, 347, 345, 343
    defw 341, 340, 338, 336, 334, 333, 331, 329, 328, 326, 324, 323
    defw 321, 320, 318, 317, 315, 314, 312, 311, 309, 308, 306, 305
    defw 303, 302, 301, 299, 298, 297, 295, 294, 293, 291, 290, 289
    defw 287, 286, 285, 284, 282, 281, 280, 279, 278, 277, 275, 274
    defw 273, 272, 271, 270, 269, 267, 266, 265, 264, 263, 262, 261
    defw 260, 259, 258, 257, 256, 255, 254, 253, 252, 251, 250, 249
    defw 248, 247, 246, 245, 245, 244, 243, 242, 241, 240, 239, 238
    defw 237, 237, 236, 235, 234, 233, 232, 232, 231, 230, 229, 228
    defw 228, 227, 226, 225, 224, 224, 223, 222, 221, 221, 220, 219
    defw 218, 218, 217, 216, 216, 215, 214, 213, 213, 212, 211, 211
    defw 210, 209, 209, 208, 207, 207, 206, 205, 205, 204, 204, 203
    defw 202, 202, 201, 200, 200, 199, 199, 198, 197, 197, 196, 196
    defw 195, 194, 194, 193, 193, 192, 192, 191, 191, 190, 189, 189
    defw 188, 188, 187, 187, 186, 186, 185, 185, 184, 184, 183, 183
    defw 182, 182, 181, 181, 180, 180, 179, 179, 178, 178, 177, 177
    defw 176, 176, 175, 175, 174, 174, 173, 173, 172, 172, 172, 171
    defw 171, 170, 170, 169, 169, 168, 168, 168, 167, 167, 166, 166
    defw 165, 165, 165, 164, 164, 163, 163, 163, 162, 162, 161, 161
    defw 161, 160, 160, 159, 159, 159, 158, 158, 158, 157, 157, 156
    defw 156, 156, 155, 155, 155, 154, 154, 153, 153, 153, 152, 152
    defw 152, 151, 151, 151, 150, 150, 150, 149, 149, 149, 148, 148
    defw 148, 147, 147, 147, 146, 146, 146, 145, 145, 145, 144, 144
    defw 144, 143, 143, 143, 142, 142, 142, 142, 141, 141, 141, 140
    defw 140, 140, 139, 139, 139, 139, 138, 138, 138, 137, 137, 137
    defw 137, 136, 136, 136, 135, 135, 135, 135, 134, 134, 134, 133
    defw 133, 133, 133, 132, 132, 132, 132, 131, 131, 131, 131, 130
    defw 130, 130, 130, 129, 129, 129, 129, 128, 128, 128, 128, 127
    defw 127, 127, 127, 126, 126, 126, 126, 125, 125, 125, 125, 124
    defw 124, 124, 124, 123, 123, 123, 123, 122, 122, 122, 122, 122
    defw 121, 121, 121, 121, 120, 120, 120, 120, 120, 119, 119, 119
    defw 119, 119, 118, 118, 118, 118, 117, 117, 117, 117, 117, 116
    defw 116, 116, 116, 116, 115, 115, 115, 115, 115, 114, 114, 114
    defw 114, 114, 113, 113, 113, 113, 113, 112, 112, 112, 112, 112
    defw 111, 111, 111, 111, 111, 111, 110, 110, 110, 110, 110, 109
    defw 109, 109, 109, 109, 109, 108, 108, 108, 108, 108, 107, 107
    defw 107, 107, 107, 107, 106, 106, 106, 106, 106, 106, 105, 105
    defw 105, 105, 105, 105, 104, 104, 104, 104, 104, 104, 103, 103
    defw 103, 103, 103, 103, 102, 102, 102, 102, 102, 102, 101, 101
    defw 101, 101, 101, 101, 101, 100, 100, 100, 100, 100, 100, 99
    defw 99, 99, 99, 99, 99, 99, 98, 98, 98, 98, 98, 98
    defw 98, 97, 97, 97, 97, 97, 97, 97, 96, 96, 96, 96
    defw 96, 96, 96, 95, 95, 95, 95, 95, 95, 95, 94, 94
    defw 94, 94, 94, 94, 94, 93, 93, 93, 93, 93, 93, 93
    defw 93, 92, 92, 92, 92, 92, 92, 92, 92, 91, 91, 91
    defw 91, 91, 91, 91, 91, 90, 90, 90, 90, 90, 90, 90
    defw 90, 89, 89, 89, 89, 89, 89, 89, 89, 88, 88, 88
    defw 88, 88, 88, 88, 88, 87, 87, 87, 87, 87, 87, 87
    defw 87, 87, 86, 86, 86, 86, 86, 86, 86, 86, 86, 85

SECTION bss_user
gs_s:   defb 0
gs_n:   defb 0
sn_s:   defb 0
gs_near: defw 0
gs_col: defw 0
gs_p:   defw 0
gs_c:   defw 0
e_far:  defw 0
sg_xa:  defw 0                  ; (these four together)
sg_ya:  defw 0
sg_xb:  defw 0
sg_yb:  defw 0
sg_x0:  defb 0
sg_n:   defb 0
sg_k:   defw 0
sg_dx:  defw 0
sg_neg: defb 0
sg_pen: defb 0
sg_mode: defb 0
sg_slope_v: defw 0
