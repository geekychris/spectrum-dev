; texview.asm (see texview.h): a picture drawn through a table made on the
; host (sdk/tools/texview.py): for every pixel of a shape, the texel it shows
; and its shade (Boing Twist's ball: a sphere, turned by adding to u).
;
; A view is a stream of rows in 8K pages: dy, dx0 (signed, from the view's
; centre), n, then n records of (u, v, shade). 0x80: the stream goes on at
; the next page; 0x7F: the end. A pixel is  texture[v][u + off] | shade:
; the texture (up to 256 x 96, rows of 256) is mapped at 0x0000-0x5FFF (MMU
; slots 0-2) for reading, the stream at 0x6000-0x7FFF (slot 3), and the
; pixel is written through Layer 2's write paging (port 0x123B) into the 16K
; bank in NextReg 0x13: the back buffer, or any bank. Reads and writes share
; 0x0000-0x3FFF: reads come from the MMU, writes go to the bank. A row is a
; jump into an unrolled loop: 68 T-states a pixel (~80 in ZEsarUX at 28 MHz).
; Rows from tv_rows down are left out, and columns off either side.
;
; void tv_draw(void)            the view in the tv_* variables; MMU slots 0-3
;                               are put back as they were
; void tv_or_span(uint8_t y, uint8_t x, uint8_t n, uint8_t bits)
;                               OR bits into n pixels of row y of bank tv_bank
; void tv_copy(const uint8_t *src, uint8_t *dst, uint8_t n, uint8_t turned)
;                               n bytes, forwards or (turned) backwards from src
; void tv_fill(uint8_t *dst, uint8_t v, uint16_t n)
;                               a fill that never reads what it writes

SECTION code_user

PUBLIC _tv_draw
PUBLIC _tv_or_span
PUBLIC _tv_copy
PUBLIC _tv_fill
PUBLIC _tv_page, _tv_cx, _tv_cy, _tv_off, _tv_bank, _tv_tex, _tv_pixels, _tv_rows

MAXN equ 160                    ; the longest row (the ball is 135 pixels across)
CSIZE equ 12                    ; bytes of one unrolled pixel

SECTION data_user
_tv_page:   defb 0              ; the stream's first page
_tv_cx:     defw 0              ; the screen point the rows are relative to
_tv_cy:     defw 0
_tv_off:    defb 0              ; added to u (the ball's turn)
_tv_bank:   defb 0              ; the 16K bank written (third 0 = rows 0..63)
_tv_tex:    defb 0              ; the texture's first 8K page (3 pages)
_tv_pixels: defw 0              ; pixels drawn by the last tv_draw
_tv_rows:   defb 192            ; rows 0 .. tv_rows - 1 are drawn
patched:    defb 0              ; the offset in the unrolled loop now
cur_page:   defb 0
cur_third:  defb 0
skip_after: defw 0
save_mmu:   defs 4
save_sp:    defw 0

SECTION code_user

; read NextReg A -> A
nreg_read:
    push bc
    ld bc,0x243B
    out (c),a
    inc b                       ; 0x253B
    in a,(c)
    pop bc
    ret

_tv_draw:
    push ix
    push iy
    ld a,0x50
    call nreg_read
    ld (save_mmu),a
    ld a,0x51
    call nreg_read
    ld (save_mmu+1),a
    ld a,0x52
    call nreg_read
    ld (save_mmu+2),a
    ld a,0x53
    call nreg_read
    ld (save_mmu+3),a

    ; the offset into every unrolled copy, when it changed
    ld a,(_tv_off)
    ld hl,patched
    cp (hl)
    jr z,offset_ok
    ld (hl),a
    ld hl,unroll + 2            ; the immediate of the first add a,n
    ld de,CSIZE
    ld b,MAXN
patch_loop:
    ld (hl),a
    add hl,de
    djnz patch_loop
offset_ok:

    ; the texture at 0x0000-0x5FFF
    ld a,(_tv_tex)
    nextreg 0x50,a
    inc a
    nextreg 0x51,a
    inc a
    nextreg 0x52,a
    ld a,(_tv_bank)
    nextreg 0x13,a
    ld a,(_tv_page)
    ld (cur_page),a
    nextreg 0x53,a
    ld a,0xFF
    ld (cur_third),a
    ld hl,0
    ld (_tv_pixels),hl
    ld hl,0x6000

row_loop:
    ld a,(hl)
    cp 0x80
    jr nz,not_next
    ld a,(cur_page)
    inc a
    ld (cur_page),a
    nextreg 0x53,a
    ld hl,0x6000
    jr row_loop
not_next:
    cp 0x7F
    jp z,done
    inc hl
    ; y = cy + dy
    ld e,a
    add a,a
    sbc a,a
    ld d,a
    push hl
    ld hl,(_tv_cy)
    add hl,de
    ex de,hl                    ; DE = y
    pop hl
    ld a,(hl)                   ; dx0
    inc hl
    ld c,a
    add a,a
    sbc a,a
    ld b,a                      ; BC = dx0
    ld a,(hl)                   ; n
    inc hl
    ; off screen above or below: skip the row
    push af
    ld a,d
    or a
    jp nz,skip_row_pop          ; y < 0 or y > 255
    ld a,(_tv_rows)
    ld d,a
    ld a,e
    cp d
    jp nc,skip_row_pop
    pop af
    ld (row_y),a                ; (n kept in A; row_y gets it for a moment)
    ld a,e
    ld (row_yv),a
    ; x = cx + dx0
    push hl
    ld hl,(_tv_cx)
    add hl,bc
    ld b,h
    ld c,l                      ; BC = x
    pop hl
    ld a,(row_y)                ; A = n
    ld e,a
    ld d,0                      ; DE = n
    ld (skip_after),de          ; nothing clipped on the right yet: 0 below
    push de
    ld de,0
    ld (skip_after),de
    pop de
    ; x < 0: skip -x records
    bit 7,b
    jr z,x_not_neg
    ; k = -x
    push hl
    ld hl,0
    or a
    sbc hl,bc                   ; HL = k
    ld a,h
    or a
    jr nz,clip_all              ; k > 255
    ld a,l
    cp e
    jr nc,clip_all              ; k >= n: nothing left
    ld a,e
    sub l
    ld e,a                      ; n -= k
    ld a,l                      ; skip 3k bytes of the stream
    ld b,0
    ld c,a
    pop hl
    add hl,bc
    add hl,bc
    add hl,bc
    ld bc,0                     ; x = 0
    jr x_ok
clip_all:
    pop hl
    jp skip_n_e
x_not_neg:
    ld a,b
    or a
    jp nz,skip_n_e              ; x > 255
x_ok:
    ; x + n > 256: n = 256 - x, the rest skipped after
    ld a,c
    add a,e
    jr nc,right_ok
    jr z,right_ok               ; x + n == 256 exactly
    ld d,a                      ; the overflow (x + n - 256) records
    ld a,e
    sub d
    ld e,a
    push hl
    ld h,0
    ld l,d
    ld (skip_after),hl
    pop hl
right_ok:
    ; the write third for row y
    ld a,(row_yv)
    and 0xC0
    ld d,a
    ld a,(cur_third)
    cp d
    jr z,third_ok
    ld a,d
    ld (cur_third),a
    or 0x0B                     ; write paging, Layer 2 visible, the bank in NextReg 0x13
    push bc
    ld bc,0x123B
    out (c),a
    pop bc
third_ok:
    ; count it
    push hl
    ld hl,(_tv_pixels)
    ld d,0
    add hl,de
    ld (_tv_pixels),hl
    ; the entry into the unrolled loop: unroll_end - n * CSIZE
    ld d,e
    ld e,CSIZE
    mul d,e
    ld hl,unroll_end
    or a
    sbc hl,de
    push hl
    pop iy
    pop hl
    ld a,(row_yv)
    and 63
    ld d,a
    ld e,c                      ; DE = the row's first pixel in the write window
    jp (iy)

skip_row_pop:
    pop af
    ld e,a
skip_n_e:                       ; skip E records
    ld d,0
    add hl,de
    add hl,de
    add hl,de
    jp row_loop

row_done:
    ld de,(skip_after)
    ld a,d
    or e
    jp z,row_loop
    add hl,de
    add hl,de
    add hl,de
    jp row_loop

done:
    ld a,0x02                   ; Layer 2 visible, no paging
    ld bc,0x123B
    out (c),a
    ld a,(save_mmu)
    nextreg 0x50,a
    ld a,(save_mmu+1)
    nextreg 0x51,a
    ld a,(save_mmu+2)
    nextreg 0x52,a
    ld a,(save_mmu+3)
    nextreg 0x53,a
    pop iy
    pop ix
    ret

SECTION data_user
row_y:  defb 0
row_yv: defb 0

SECTION code_user
unroll:
    REPT MAXN
    ld a,(hl)                   ; u
    add a,0                     ; + the turn (patched)
    ld c,a
    inc hl
    ld b,(hl)                   ; v: BC = the texel
    inc hl
    ld a,(bc)
    or (hl)                     ; the shade
    inc hl
    ld (de),a
    inc e
    ENDR
unroll_end:
    jp row_done


; void tv_or_span(uint8_t y, uint8_t x, uint8_t n, uint8_t bits)
_tv_or_span:
    ld hl,2
    add hl,sp
    ld a,(hl)                   ; y
    inc hl
    ld e,(hl)                   ; x
    inc hl
    ld b,(hl)                   ; n
    inc hl
    ld c,(hl)                   ; bits
    ld d,a
    and 0xC0
    or 0x0F                     ; read and write paging, visible, NextReg 0x13's bank
    push bc
    ld bc,0x123B
    out (c),a
    pop bc
    ld a,(_tv_bank)
    nextreg 0x13,a
    ld a,d
    and 63
    ld h,a
    ld l,e
    ld a,b
    or a
    jr z,or_done
or_loop:
    ld a,(hl)
    or c
    ld (hl),a
    inc l
    djnz or_loop
or_done:
    ld a,0x02
    ld bc,0x123B
    out (c),a
    ret


; void tv_copy(const uint8_t *src, uint8_t *dst, uint8_t n, uint8_t turned)
_tv_copy:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = src
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = dst
    inc hl
    ld a,(hl)                   ; n
    inc hl
    or a
    ret z
    push bc                     ; dst
    ld c,a
    ld a,(hl)                   ; turned
    ld b,0
    ex de,hl                    ; HL = src
    pop de                      ; DE = dst
    or a
    jr nz,copy_back
    ldir
    ret
copy_back:
    ld b,c
back_loop:
    ld a,(hl)
    dec hl
    ld (de),a
    inc de
    djnz back_loop
    ret


; void tv_fill(uint8_t *dst, uint8_t v, uint16_t n)
; a fill that never reads what it writes (memset's LDIR does: through Layer
; 2's write window it would copy whatever is mapped for reading)
_tv_fill:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)                   ; v
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = n
    ex de,hl
    ld e,a
fill_loop:
    ld a,b
    or c
    ret z
    ld (hl),e
    inc hl
    dec bc
    jr fill_loop


