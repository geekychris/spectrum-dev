; vproj.asm - the ground's grid points onto the screen (view.c ground(),
; vfill.h vf_v) and its cells' pens (vf_col), in Z80N assembly.
;
; For each grid line r (world z) and point k (world x) of the window: the
; height from the level's pages (3 bytes + hbase, to engine units), the
; camera's turn (yaw 0, pitch): y' = (dy cos + dz sin) >> 12,
; z' = (dz cos - dy sin) >> 12, then the perspective with a reciprocal
; table: s = 655360 / z' (one look-up and a shift), x = 128 + dx s >> 12,
; y = ((830 - y' s >> 9) + 512) * 16, as view.c's C had it (x clamped to
; -256..511, y to 256..65280; z' < 12: behind the camera).
; Cells: the original's slope shading from the four corners' heights.
;
; Page 47 at 0x6000 with vfill (it maps the level's pages into slot 2).
; IX, IY kept.

EXTERN _vf_v, _vf_col, l_mulu_32_16x16
PUBLIC _vp_project, vp_smul_us, vp_sh12, vp_recip_s, vp_scr, vp_dx, vp_yp
PUBLIC _vp_dx0, _vp_dz0, _vp_cell, _vp_camy, _vp_cp, _vp_sp
PUBLIC _vp_i0, _vp_j0, _vp_n, _vp_page, _vp_rpp, _vp_rowb, _vp_hb12, _vp_hbf, _vp_cdiv, _vp_pen0

NVX     equ 14
NLINE   equ 14
NONE    equ 0xFF
BEHIND  equ 0x8000

SECTION bss_user
_vp_dx0:  defw 0                ; point 0's world x - the camera's (units)
_vp_dz0:  defw 0                ; line 0's world z - the camera's
_vp_cell: defw 0                ; a cell (units)
_vp_camy: defw 0                ; the camera's height (units, clamped to 16 bits)
_vp_cp:   defw 0                ; cos, sin of the pitch, Q12 (both >= 0)
_vp_sp:   defw 0
_vp_i0:   defw 0                ; point 0's grid column, line 0's grid row (may be < 0)
_vp_j0:   defw 0
_vp_n:    defb 0                ; grid points a side
_vp_page: defb 0                ; the heights' first page, rows a page, bytes a row
_vp_rpp:  defb 0
_vp_rowb: defw 0
_vp_hb12: defw 0                ; hbase >> 12, hbase & 0xFFF
_vp_hbf:  defw 0
_vp_cdiv: defw 0                ; the slope's step (cell / 3 + 1)
_vp_pen0: defb 0                ; the lightest ground pen
vp_r:     defb 0
vp_k:     defb 0
vp_j:     defw 0
vp_dz:    defw 0
vp_dx:    defw 0
vp_dzc:   defw 0, 0             ; dz cos, dz sin (32 bits)
vp_dzs:   defw 0, 0
vp_t32:   defw 0, 0
vp_yp:    defw 0                ; y'
vp_s:     defw 0
vp_out:   defw 0                ; the record being written
vp_col:   defw 0
vp_neg:   defb 0
vp_hcur:  defs NVX * 2          ; this line's heights (units)
vp_hprev: defs NVX * 2          ; the last line's
ht_i:     defw 0
ce_a:     defw 0
ce_b:     defw 0
ce_c:     defw 0
ce_d:     defw 0
ce_pp:    defw 0
ce_pc:    defw 0

SECTION PAGE_47

; DEHL = HL * DE, HL signed, DE unsigned (< 32768 or so): |HL| * DE, negated
vp_smul_us:
smul_us:
    bit 7,h
    jp z,l_mulu_32_16x16
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a                      ; |HL|
    call l_mulu_32_16x16
neg32:                          ; DEHL = -DEHL
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

; DEHL >> 12 to a clamped int16 in HL (-32768..32767): bits 12..27, valid
; when bits 27..31 agree (the Z80N's barrel shift)
vp_sh12:
sh12:
    ld a,d
    and 0xF8
    jr z,sh12_ok
    cp 0xF8
    jr z,sh12_ok
    bit 7,d
    ld hl,32767
    ret z
    ld hl,-32768
    ret
sh12_ok:
    ld a,h
    ld b,4
    bsla de,b                   ; DE << 4: bits 20..27 in D, 16..19 in E's top
    swapnib
    and 0x0F                    ; bits 12..15
    or e
    ld l,a
    ld h,d
    ret

; void vp_project(void)
_vp_project:
    push ix
    push iy
    xor a
    ld (vp_r),a
    ld hl,(_vp_dz0)
    ld (vp_dz),hl
    ld hl,(_vp_j0)
    ld (vp_j),hl
    ld hl,_vf_v
    ld (vp_out),hl
    ld hl,_vf_col
    ld (vp_col),hl
vp_line:
    ; dz cos, dz sin
    ld hl,(vp_dz)
    ld de,(_vp_cp)
    call smul_us
    ld (vp_dzc),hl
    ld (vp_dzc + 2),de
    ld hl,(vp_dz)
    ld de,(_vp_sp)
    call smul_us
    ld (vp_dzs),hl
    ld (vp_dzs + 2),de
    ; this line's heights (vp_hcur), off the map: 0x8000
    call heights
    ; the points
    ld hl,(_vp_dx0)
    ld (vp_dx),hl
    xor a
    ld (vp_k),a
    ld iy,vp_hcur
vp_point:
    ld l,(iy+0)
    ld h,(iy+1)
    ld a,h
    cp 0x80
    jr nz,vp_on
    ld a,l
    or a
    jp z,vp_behind
vp_on:
    ; dy = h - camy
    ld de,(_vp_camy)
    or a
    sbc hl,de
    push hl
    ; z' = (dz cos - dy sin) >> 12
    ld de,(_vp_sp)
    call smul_us                ; DEHL = dy sin
    ld bc,(vp_dzc)
    ld a,c
    sub l
    ld l,a
    ld a,b
    sbc a,h
    ld h,a
    ld bc,(vp_dzc + 2)
    ld a,c
    sbc a,e
    ld e,a
    ld a,b
    sbc a,d
    ld d,a
    call sh12                   ; HL = z'
    pop bc                      ; dy
    bit 7,h
    jp nz,vp_behind
    ld a,h
    or a
    jr nz,vp_zok
    ld a,l
    cp 12
    jp c,vp_behind
    cp 21
    jr nc,vp_zok
    ld l,21                     ; nearer than 21: as 21 (s < 32768)
vp_zok:
    push hl                     ; z'
    ; y' = (dy cos + dz sin) >> 12
    ld h,b
    ld l,c
    ld de,(_vp_cp)
    call smul_us
    ld bc,(vp_dzs)
    add hl,bc
    ex de,hl
    ld bc,(vp_dzs + 2)
    adc hl,bc
    ex de,hl
    call sh12
    ld (vp_yp),hl
    pop hl                      ; z'
    ld ix,(vp_out)
    call vp_scr
    jr vp_next
vp_behind:
    ld ix,(vp_out)
    ld (ix+0),BEHIND % 256
    ld (ix+1),BEHIND / 256
vp_next:
    ld hl,(vp_out)
    ld de,4
    add hl,de
    ld (vp_out),hl
    ld hl,(vp_dx)
    ld de,(_vp_cell)
    add hl,de
    ld (vp_dx),hl
    inc iy
    inc iy
    ld a,(vp_k)
    inc a
    ld (vp_k),a
    cp NVX
    jp c,vp_point
    ; the cells between this line and the last
    ld a,(vp_r)
    or a
    call nz,cells
    ; this line's heights are the next one's last
    ld hl,vp_hcur
    ld de,vp_hprev
    ld bc,NVX * 2
    ldir
    ld hl,(vp_dz)
    ld de,(_vp_cell)
    add hl,de
    ld (vp_dz),hl
    ld hl,(vp_j)
    inc hl
    ld (vp_j),hl
    ld a,(vp_r)
    inc a
    ld (vp_r),a
    cp NLINE
    jp c,vp_line
    ld a,10                     ; slot 2: the tilemap's page again
    nextreg 0x52,a
    pop iy
    pop ix
    ret

; HL = z' (21..32767), vp_dx = x', vp_yp = y': the screen point into the
; record at IX (x: -256..511, y: (row + 32) * 256, 256..65280)
vp_scr:
    call recip_s                ; HL = s = 655360 / z'
    ld (vp_s),hl
    ; x = 128 + (dx s) >> 12
    ex de,hl
    ld hl,(vp_dx)
    call smul_us
    call sh12                   ; HL = dx s >> 12
    ; x = HL + 128, clamped to -256..511 (HL < -384: -256, > 383: 511)
    bit 7,h
    jr z,vp_xpos
    ld de,384
    add hl,de                   ; HL + 384 < 0: below -256
    jr nc,vp_xlo
    ld de,-256
    add hl,de                   ; HL + 128
    jr vp_xok
vp_xlo:
    ld hl,-256
    jr vp_xok
vp_xpos:
    ld de,-384
    add hl,de                   ; carry: HL >= 384
    jr c,vp_xhi
    ld de,512
    add hl,de                   ; HL + 128
    jr vp_xok
vp_xhi:
    ld hl,511
vp_xok:
    ld (ix+0),l
    ld (ix+1),h
    ; y = 21472 - (y' s) >> 5, clamped to 256..65280
    ld hl,(vp_yp)
    ld de,(vp_s)
    call smul_us                ; DEHL = y' s
    ld a,e                      ; >> 5, arithmetic (barrel shifts)
    ld b,5
    bsra de,b
    ex de,hl
    bsrl de,b
    and 0x1F                    ; the high word's low 5 bits to the low word's top
    add a,a
    add a,a
    add a,a
    or d
    ld d,a
    ex de,hl
    ; 21472 - DEHL (32-bit), clamp
    ld a,0xE0                   ; 21472 = 0x53E0
    sub l
    ld l,a
    ld a,0x53
    sbc a,h
    ld h,a
    ld a,0
    sbc a,e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    bit 7,d
    jr nz,vp_ylo                ; negative
    ld a,d
    or e
    jr nz,vp_yhi                ; >= 65536
    ld a,h
    or a
    jr z,vp_ylo                 ; < 256
    cp 0xFF
    jr nz,vp_yok
vp_yhi:
    ld hl,65280
    jr vp_yok
vp_ylo:
    ld hl,256
vp_yok:
    ld (ix+2),l
    ld (ix+3),h
    ret

; HL = z' (21..32767): HL = 655360 / z' from ST[] (zn = z' normalised to
; 2048..4095, k shifts: z' = zn >> k): s = ST >> (6 - k) or << (k - 6)
vp_recip_s:
recip_s:
    ld b,6                      ; 6 - k
    ld a,h
    cp 0x10
    jr c,rs_small
rs_big:                         ; z' >= 4096: shift right
    srl h
    rr l
    inc b
    ld a,h
    cp 0x10
    jr nc,rs_big
    jr rs_look
rs_small:
    cp 0x08
    jr nc,rs_look
    add hl,hl
    dec b
    ld a,h
    jr rs_small
rs_look:                        ; HL = zn (0x800..0xFFF), B = 6 - k (-1..9)
    ld a,l                      ; index = (zn - 2048) >> 3
    srl h
    rra
    srl h
    rra
    srl h
    rra                         ; A = (zn >> 3) & 255
    add a,a
    ld l,a
    ld a,0
    adc a,0
    ld h,a
    ld de,stab
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; ST
    ld a,b
    bit 7,a
    jr nz,rs_left
    or a
    ret z
rs_r:
    srl h
    rr l
    djnz rs_r
    ret
rs_left:
    add hl,hl                   ; (k = 7: one left)
    ret

; this line's heights into vp_hcur (units; off the map: 0x8000)
heights:
    ld hl,(vp_j)
    bit 7,h
    jp nz,ht_offline
    ld a,h
    or a
    jp nz,ht_offline
    ld a,(_vp_n)
    ld b,a
    ld a,l
    cp b
    jp nc,ht_offline
    ; page = page0 + j / rpp, row = j - p * rpp
    ld c,0                      ; p
    ld a,(_vp_rpp)
    ld b,a
    ld a,l
ht_div:
    cp b
    jr c,ht_dived
    sub b
    inc c
    jr ht_div
ht_dived:                       ; A = the row in its page, C = p
    push af
    ld a,(_vp_page)
    add a,c
    nextreg 0x52,a
    pop af
    ld e,a
    ld d,0
    ld hl,(_vp_rowb)
    call l_mulu_32_16x16        ; HL = row * rowb
    ld de,0x4000
    add hl,de                   ; the row
    push hl
    pop ix
    ; point 0's column: i0 (may be < 0): ix += i0 * 3
    ld iy,vp_hcur
    ld hl,(_vp_i0)
    ld (ht_i),hl
    ld b,NVX
ht_pt:
    push bc
    ld hl,(ht_i)
    bit 7,h
    jr nz,ht_off
    ld a,h
    or a
    jr nz,ht_off
    ld a,(_vp_n)
    ld b,a
    ld a,l
    cp b
    jr nc,ht_off
    ; IX + 3 i: the 3 bytes v; h = hb12 + ((v + hbf) >> 12)
    ld e,l
    ld d,0
    push ix
    pop hl
    add hl,de
    add hl,de
    add hl,de
    ld a,(_vp_hbf)
    add a,(hl)                  ; v0 + hbf low
    inc hl
    ld a,(_vp_hbf + 1)
    adc a,(hl)
    ld e,a                      ; bits 8..15
    inc hl
    ld a,(hl)
    adc a,0
    ld d,a                      ; bits 16..23: D:E >> 4 = (v + hbf) >> 12
    srl d
    rr e
    srl d
    rr e
    srl d
    rr e
    srl d
    rr e
    ld hl,(_vp_hb12)
    add hl,de
    jr ht_store
ht_off:
    ld hl,BEHIND
ht_store:
    ld (iy+0),l
    ld (iy+1),h
    inc iy
    inc iy
    ld hl,(ht_i)
    inc hl
    ld (ht_i),hl
    pop bc
    djnz ht_pt
    ret
ht_offline:
    ld hl,vp_hcur
    ld b,NVX
ht_ol:
    ld (hl),BEHIND % 256
    inc hl
    ld (hl),BEHIND / 256
    inc hl
    djnz ht_ol
    ret

; the cells between the last line (vp_hprev) and this one (vp_hcur):
; render.c ground(): slope = ((ha + hd) - (hb + hc)) + ((ha + hb) - (hc + hd)) / 2,
; pen = 2 - slope / cdiv, clamped 0..5 (a, b the last line's i, i + 1; d, c
; this line's). A corner off the map (0x8000): no cell.
cells:
    ld hl,(vp_hprev)
    ld (ce_a),hl
    ld hl,(vp_hcur)
    ld (ce_d),hl
    ld hl,vp_hprev + 2
    ld (ce_pp),hl
    ld hl,vp_hcur + 2
    ld (ce_pc),hl
    ld b,NVX - 1
ce_loop:
    push bc
    ld hl,(ce_pp)               ; hb, hc
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (ce_pp),hl
    ld (ce_b),de
    ld hl,(ce_pc)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (ce_pc),hl
    ld (ce_c),de
    ld a,(ce_a + 1)
    cp 0x80
    jp z,ce_none
    ld a,(ce_b + 1)
    cp 0x80
    jp z,ce_none
    ld a,(ce_c + 1)
    cp 0x80
    jp z,ce_none
    ld a,(ce_d + 1)
    cp 0x80
    jp z,ce_none
    ld hl,(ce_a)
    ld bc,(ce_b)
    add hl,bc                   ; a + b
    ld de,(ce_c)
    ex de,hl
    ld bc,(ce_d)
    add hl,bc                   ; c + d
    ex de,hl
    or a
    sbc hl,de                   ; s2 = (a + b) - (c + d)
    bit 7,h
    jr z,ce_s2p
    inc hl
ce_s2p:
    sra h
    rr l                        ; s2 / 2, toward 0
    push hl
    ld hl,(ce_a)
    ld bc,(ce_d)
    add hl,bc                   ; a + d
    ld de,(ce_b)
    ex de,hl
    ld bc,(ce_c)
    add hl,bc                   ; b + c
    ex de,hl
    or a
    sbc hl,de                   ; s1
    pop de
    add hl,de                   ; slope
    ld de,(_vp_cdiv)
    bit 7,h
    jr nz,ce_negs
    ld c,2                      ; slope >= 0: pen 2, 1, 0 for q = 0, 1, >= 2
    or a
    sbc hl,de
    jr c,ce_pen
    dec c
    sbc hl,de
    jr c,ce_pen
    dec c
    jr ce_pen
ce_negs:                        ; slope < 0: pen = 2 + trunc(|slope| / cdiv), at most 5
    ex de,hl
    push hl
    ld hl,0
    or a
    sbc hl,de
    pop de
    ld c,2
    or a
    sbc hl,de
    jr c,ce_pen
    inc c
    sbc hl,de
    jr c,ce_pen
    inc c
    sbc hl,de
    jr c,ce_pen
    inc c
ce_pen:
    ld a,(_vp_pen0)
    add a,c
    jr ce_put
ce_none:
    ld a,NONE
ce_put:
    ld hl,(vp_col)
    ld (hl),a
    inc hl
    ld (vp_col),hl
    ld hl,(ce_b)                ; the next cell's a, d
    ld (ce_a),hl
    ld hl,(ce_c)
    ld (ce_d),hl
    pop bc
    dec b
    jp nz,ce_loop
    ret

; ST[i] = 655360 * 64 / (2048 + 8 i + 4)
stab:
    defw 20440, 20361, 20282, 20204, 20126, 20049, 19973, 19897, 19822, 19747, 19673, 19600
    defw 19527, 19454, 19382, 19311, 19240, 19170, 19100, 19030, 18962, 18893, 18825, 18758
    defw 18691, 18625, 18559, 18493, 18428, 18364, 18300, 18236, 18173, 18110, 18048, 17986
    defw 17924, 17863, 17803, 17742, 17683, 17623, 17564, 17505, 17447, 17389, 17332, 17275
    defw 17218, 17162, 17106, 17050, 16995, 16940, 16885, 16831, 16777, 16724, 16671, 16618
    defw 16565, 16513, 16461, 16410, 16358, 16308, 16257, 16207, 16157, 16107, 16058, 16009
    defw 15960, 15912, 15863, 15816, 15768, 15721, 15674, 15627, 15581, 15534, 15489, 15443
    defw 15398, 15353, 15308, 15263, 15219, 15175, 15131, 15087, 15044, 15001, 14958, 14916
    defw 14873, 14831, 14790, 14748, 14707, 14665, 14624, 14584, 14543, 14503, 14463, 14423
    defw 14384, 14344, 14305, 14266, 14228, 14189, 14151, 14113, 14075, 14037, 14000, 13962
    defw 13925, 13888, 13852, 13815, 13779, 13743, 13707, 13671, 13636, 13600, 13565, 13530
    defw 13495, 13461, 13426, 13392, 13358, 13324, 13290, 13256, 13223, 13190, 13157, 13124
    defw 13091, 13058, 13026, 12994, 12961, 12929, 12898, 12866, 12834, 12803, 12772, 12741
    defw 12710, 12679, 12649, 12618, 12588, 12558, 12528, 12498, 12468, 12439, 12409, 12380
    defw 12351, 12322, 12293, 12264, 12235, 12207, 12179, 12150, 12122, 12094, 12066, 12039
    defw 12011, 11984, 11956, 11929, 11902, 11875, 11848, 11822, 11795, 11769, 11742, 11716
    defw 11690, 11664, 11638, 11612, 11586, 11561, 11535, 11510, 11485, 11460, 11435, 11410
    defw 11385, 11361, 11336, 11311, 11287, 11263, 11239, 11215, 11191, 11167, 11143, 11120
    defw 11096, 11073, 11049, 11026, 11003, 10980, 10957, 10934, 10911, 10889, 10866, 10844
    defw 10821, 10799, 10777, 10755, 10733, 10711, 10689, 10667, 10645, 10624, 10602, 10581
    defw 10560, 10538, 10517, 10496, 10475, 10454, 10434, 10413, 10392, 10372, 10351, 10331
    defw 10310, 10290, 10270, 10250
