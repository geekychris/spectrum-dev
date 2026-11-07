; Shadow screen + 8-pixel masked sprites for the 48K (see shadow48.h).
; The shadow sits at screen | 0x2000 (0x6000-0x7AFF: bitmap and attributes),
; same layout as the screen.

SECTION code_user

PUBLIC _spr8_draw, _spr8_restore, _shadow_save, _shadow_show
PUBLIC _spr8_attr_fill, _spr8_attr_restore, _spr8_frame, _spr8_draw_all
PUBLIC _spr8_plane
EXTERN _zx_row

; HL = the screen (or shadow) byte of (C = x, B = y); A = x & 7. Uses DE.
scr_addr:
    ld l,b
    ld h,0
    add hl,hl
    ld de,_zx_row
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld a,c
    rrca
    rrca
    rrca
    and 31
    or l                        ; zx_row low bytes are multiples of 32
    ld l,a
    ld a,(_spr8_plane)
    or h
    ld h,a
    ld a,c
    and 7
    ret

; the next pixel line down from HL (screen or shadow layout)
MACRO NEXT_LINE
    inc h
    ld a,h
    and 7
    jr nz,ASMPC+12              ; -> past the macro
    ld a,l
    add a,32
    ld l,a
    jr c,ASMPC+6
    ld a,h
    sub 8
    ld h,a
ENDM

; void spr8_draw(uint16_t xy, const uint8_t *img)
_spr8_draw:
    ld hl,2
    add hl,sp
    ld c,(hl)                   ; x
    inc hl
    ld b,(hl)                   ; y
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = image
; C = x, B = y, DE = image { h, h x (mask, data) }. Each byte is rotated
; right s = x & 7 times (an unrolled run of rrca entered by a patched jr),
; then split by the patched masks into its left and right screen bytes.
draw_regs:
    push ix
    push de
    call scr_addr
    pop de
    ld b,a                      ; s
    neg
    add a,7                     ; skip 7 - s of the 7 rrca
    ld (mask_jr + 1),a
    ld (data_jr + 1),a
    ld (box_jr + 1),a
    ld (boxm_jr + 1),a
    ld a,0xFF                   ; left part: 0xFF >> s
    inc b
left_mask:
    dec b
    jr z,left_done
    srl a
    jr left_mask
left_done:
    ld (lm1 + 1),a
    ld (lm2 + 1),a
    ld (lm3 + 1),a
    cpl
    ld (rm1 + 1),a
    ld (rm2 + 1),a
    ld (rm3 + 1),a
    ld a,(de)                   ; height
    inc de
    bit 7,a
    jr nz,box_sprite
    or a
    jr z,draw_done
    ld ixl,a
draw_line:
    ld a,(de)                   ; mask
    inc de
mask_jr:
    jr mask_run
mask_run:
    rrca
    rrca
    rrca
    rrca
    rrca
    rrca
    rrca
    ld c,a
lm1:
    and 0xFF
    cpl
    and (hl)
    ld (hl),a
    inc l
    ld a,c
rm1:
    and 0
    cpl
    and (hl)
    ld (hl),a
    ld a,(de)                   ; data
    inc de
data_jr:
    jr data_run
data_run:
    rrca
    rrca
    rrca
    rrca
    rrca
    rrca
    rrca
    ld c,a
rm2:
    and 0
    or (hl)
    ld (hl),a
    dec l
    ld a,c
lm2:
    and 0xFF
    or (hl)
    ld (hl),a
    NEXT_LINE
    dec ixl
    jr nz,draw_line
draw_done:
    pop ix
    ret

; a box sprite (height | 0x80, then the data bytes): the mask is 0xFC on
; every line, so its two shifted bytes are patched in once
box_sprite:
    and 0x7F
    jr z,draw_done
    ld ixl,a
    ld a,0xFC
boxm_jr:
    jr boxm_run
boxm_run:
    rrca
    rrca
    rrca
    rrca
    rrca
    rrca
    rrca
    ld c,a
    ld a,(lm1 + 1)
    and c
    cpl
    ld (bm1 + 1),a              ; ~(0xFC >> s)
    ld a,(rm1 + 1)
    and c
    cpl
    ld (bm2 + 1),a              ; ~(0xFC << (8 - s))
box_line:
    ld a,(de)                   ; data
    inc de
box_jr:
    jr box_run
box_run:
    rrca
    rrca
    rrca
    rrca
    rrca
    rrca
    rrca
    ld c,a
    ld a,(hl)
bm1:
    and 0
    ld b,a
    ld a,c
lm3:
    and 0xFF
    or b
    ld (hl),a
    inc l
    ld a,(hl)
bm2:
    and 0
    ld b,a
    ld a,c
rm3:
    and 0
    or b
    ld (hl),a
    dec l
    NEXT_LINE
    dec ixl
    jr nz,box_line
    pop ix
    ret

; void spr8_restore(uint16_t xy, uint16_t h): two bytes a line, shadow -> screen
_spr8_restore:
    ld hl,2
    add hl,sp
    ld c,(hl)                   ; x
    inc hl
    ld b,(hl)                   ; y
    inc hl
    ld a,(hl)                   ; h
; C = x, B = y, A = lines
restore_regs:
    or a
    ret z
    push af
    call scr_addr
    res 5,h                     ; HL = screen, whatever spr8_plane is
    pop bc                      ; B = lines
restore_line:
    set 5,h
    ld a,(hl)
    inc l
    ld c,(hl)
    res 5,h
    ld (hl),c
    dec l
    ld (hl),a
    NEXT_LINE
    djnz restore_line
    ret

; spr8_obj: on, x, y, w, img (2), attr
; void spr8_frame(const spr8_obj *drawn, const spr8_obj *now, uint16_t n):
; erase each drawn object (its colours only if its cells or colour change),
; draw the new ones (spr8_draw_all)
_spr8_frame:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = drawn
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = now
    inc hl
    ld a,(hl)                   ; n
    or a
    ret z
    cp 17
    ret nc
    push ix
    ld (frame_drawn),de
    ld (frame_now),bc
    ld (frame_n),a
    push de
    pop ix                      ; IX = drawn[i]
    ld (frame_left),a
frame_erase:
    ld a,(ix+0)
    or a
    jp z,frame_erase_next
    ld c,(ix+1)                 ; x
    ld b,(ix+2)                 ; y
    ld l,(ix+4)
    ld h,(ix+5)
    ld a,(hl)                   ; h
    and 0x7F
    call restore_regs
    ; the colours: kept if now[i] is on, with the same colour and cells
    ld de,(frame_drawn)
    ld hl,(frame_now)
    push ix
    pop bc
    or a
    sbc hl,de
    add hl,bc                   ; HL = now[i]
    ld a,(hl)
    or a
    jp z,frame_attr
    inc hl
    ld a,(ix+1)                 ; same first column: (x ^ x') < 8
    xor (hl)
    and 0xF8
    jp nz,frame_attr
    ld a,(ix+1)                 ; same last column: x + w - 1
    add a,(ix+3)
    dec a
    ld c,a
    ld a,(hl)
    inc hl
    inc hl
    add a,(hl)
    dec a
    xor c
    and 0xF8
    jp nz,frame_attr
    dec hl
    ld a,(ix+2)                 ; same first row
    xor (hl)
    and 0xF8
    jp nz,frame_attr
    ld c,(hl)                   ; same last row: y + h - 1 (h = image[0])
    inc hl
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld a,(hl)
    and 0x7F
    add a,c
    dec a
    ld c,a                      ; y' + h' - 1
    ld l,(ix+4)
    ld h,(ix+5)
    ld a,(hl)
    and 0x7F
    add a,(ix+2)
    dec a                       ; y + h - 1
    xor c
    and 0xF8
    jp nz,frame_attr
    ld de,(frame_drawn)         ; same colour
    ld hl,(frame_now)
    push ix
    pop bc
    or a
    sbc hl,de
    add hl,bc
    ld bc,6
    add hl,bc
    ld a,(hl)
    cp (ix+6)
    jr z,frame_erase_next       ; same cells, same colour: leave them
frame_attr:
    call ix_box
    call attr_restore_regs
frame_erase_next:
    ld de,7
    add ix,de
    ld hl,frame_left
    dec (hl)
    jp nz,frame_erase
    pop ix
    ; draw the new ones
    ld a,(frame_n)
    ld l,a
    ld h,0
    push hl
    ld hl,(frame_now)
    push hl
    call _spr8_draw_all
    pop hl
    pop hl
    ret

; C = x, B = y, E = w, D = h of the object at IX
ix_box:
    ld c,(ix+1)
    ld b,(ix+2)
    ld e,(ix+3)
    ld l,(ix+4)
    ld h,(ix+5)
    ld a,(hl)
    and 0x7F
    ld d,a
    ret

; void spr8_draw_all(const spr8_obj *o, uint16_t n): draws the objects that
; are on, top first (ascending y: each is finished before the beam gets
; there when this runs at the start of the frame), and colours their cells
_spr8_draw_all:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    or a
    ret z
    cp 17
    ret nc                      ; at most 16
    push ix
    ld (all_left),a
    ld (all_base),de
    xor a
    ld (all_k),a
    ld c,a                      ; C = object index
collect:
    ld a,(de)                   ; on?
    or a
    jr z,collect_next
    push de
    inc de
    inc de
    ld a,(de)                   ; y
    ld (all_y),a
    ld hl,all_ys                ; HL = ys + k (insertion sort from the end)
    ld a,(all_k)
    ld b,a
    add a,l
    ld l,a
    jr nc,insert
    inc h
insert:
    ld a,b
    or a
    jr z,insert_put
    dec hl                      ; ys[j - 1]
    ld a,(all_y)
    cp (hl)
    jr nc,insert_here           ; y >= ys[j - 1]: goes at j
    ld a,(hl)
    inc hl
    ld (hl),a                   ; ys[j] = ys[j - 1]
    ld de,16
    add hl,de                   ; idx[j]
    dec hl
    ld a,(hl)
    inc hl
    ld (hl),a                   ; idx[j] = idx[j - 1]
    ld de,-17
    add hl,de                   ; ys[j - 1]
    dec b
    jr insert
insert_here:
    inc hl
insert_put:
    ld a,(all_y)
    ld (hl),a
    ld de,16
    add hl,de
    ld (hl),c                   ; idx[j] = this object
    ld hl,all_k
    inc (hl)
    pop de
collect_next:
    ld hl,7
    add hl,de
    ex de,hl
    inc c
    ld hl,all_left
    dec (hl)
    jr nz,collect
    ld a,(all_k)
    or a
    jr z,all_drawn
    ld b,a
    ld hl,all_ys + 16           ; the sorted indices
draw_next:
    push bc
    push hl
    ld a,(hl)                   ; index -> object: base + 7 * index
    ld l,a
    ld h,0
    ld d,h
    ld e,l
    add hl,hl
    add hl,hl
    add hl,hl
    sbc hl,de
    ld de,(all_base)
    add hl,de
    push hl
    pop ix
    ld c,(ix+1)                 ; x
    ld b,(ix+2)                 ; y
    ld e,(ix+4)
    ld d,(ix+5)                 ; image
    push bc
    push ix
    call draw_regs
    pop ix
    pop bc
    ld e,(ix+3)                 ; w
    ld l,(ix+4)
    ld h,(ix+5)
    ld a,(hl)                   ; h
    and 0x7F
    ld d,a
    ld a,(ix+6)                 ; attr
    call attr_fill_regs
    pop hl
    inc hl
    pop bc
    djnz draw_next
all_drawn:
    pop ix
    ret

; void shadow_save(void) / shadow_show(void)
_shadow_save:
    ld hl,0x4000
    ld de,0x6000
    jr copy_screen
_shadow_show:
    ld hl,0x6000
    ld de,0x4000
copy_screen:
    ld bc,6912
    ldir
    ret

; the attribute cells under a box: C = x, B = y, E = w, D = h (pixels)
; -> HL = first cell, E = columns, D = rows
cell_box:
    ld a,c
    and 7
    add a,e
    dec a
    rrca
    rrca
    rrca
    and 31
    inc a
    ld e,a
    ld a,b
    and 7
    add a,d
    dec a
    rrca
    rrca
    rrca
    and 31
    inc a
    ld d,a
    ld a,b
    and 0xF8
    ld l,a
    ld h,0x16
    add hl,hl
    add hl,hl                   ; 0x5800 + (y & 0xF8) * 4
    ld a,c
    rrca
    rrca
    rrca
    and 31
    or l
    ld l,a
    ret

; fetch xy, wh from the stack (after one return address)
box_args:
    ld hl,4
    add hl,sp
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ret

; void spr8_attr_fill(uint16_t xy, uint16_t wh, uint16_t attr): colour the cells
_spr8_attr_fill:
    call box_args
    ld a,(hl)
; C = x, B = y, E = w, D = h, A = attr
attr_fill_regs:
    ld (f_attr),a
    call cell_box
fill_row:
    ld b,e
    ld a,(f_attr)
    push hl
fill_col:
    ld (hl),a
    inc l
    djnz fill_col
    pop hl
    ld bc,32
    add hl,bc
    dec d
    jr nz,fill_row
    ret

; void spr8_attr_restore(uint16_t xy, uint16_t wh): the cells' colours from the shadow
_spr8_attr_restore:
    call box_args
; C = x, B = y, E = w, D = h
attr_restore_regs:
    call cell_box
attr_row:
    ld b,e
    push hl
attr_col:
    set 5,h
    ld a,(hl)
    res 5,h
    ld (hl),a
    inc l
    djnz attr_col
    pop hl
    ld bc,32
    add hl,bc
    dec d
    jr nz,attr_row
    ret

SECTION data_user
_spr8_plane: defb 0

SECTION bss_user
f_attr:  defb 0
frame_drawn: defw 0
frame_now:   defw 0
frame_n:     defb 0
frame_left:  defb 0
all_left: defb 0
all_k:    defb 0
all_y:    defb 0
all_base: defw 0
all_ys:   defs 32               ; sorted y, then the objects' indices
