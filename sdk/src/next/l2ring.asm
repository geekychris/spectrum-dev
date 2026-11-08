; l2ring.h: a big picture on Layer 2 (320 x 256, column-major), a column or
; a row at a time, from the run-length picture sdk/tools/l2ring.py makes:
; runs of (length, colour) down each column and along each row, a directory
; of 3-byte pointers (page from the first, offset in the page) to each
; column's and each row's runs. l2ring.c scrolls with these.
;
; Layer 2 is a ring: picture column x is Layer 2 column x mod 320, picture
; row y is Layer 2 row y mod 256. Outside the picture is the void's colour.
;
; Slots: the runs are read through slot 2 (0x4000), Layer 2 is written
; through slot 1 (0x2000); both get back what they held.

SECTION code_user
PUBLIC _lr_open, _lr_col, _lr_row
PUBLIC _lr_w, _lr_h, _lr_ox, _lr_oy, _lr_void, _lr_page, _lr_l2page

; void lr_open(uint8_t page): the picture starting at that 8K page
_lr_open:
    call lr__save
    ld hl,2
    add hl,sp
    ld a,(hl)
    ld (_lr_page),a
    nextreg 0x52,a
    ld hl,(0x4000)
    ld (_lr_w),hl
    ld hl,(0x4002)
    ld (_lr_h),hl
    ld hl,(0x4004)
    ld (_lr_ox),hl
    ld hl,(0x4006)
    ld (_lr_oy),hl
    ld a,(0x4008)
    ld (_lr_void),a
    ld a,(0x4009)               ; colours: the directory follows the palette
    ld l,a
    ld h,0
    add hl,hl
    ld de,10
    add hl,de
    ld (lr__dir),hl
    call lr__home2
    ret

; HL = byte offset in the picture (< 64K) -> mapped in slot 2, HL = address
lr__map:
    ld a,h
    rlca
    rlca
    rlca
    and 7
    ld b,a
    ld a,(_lr_page)
    add a,b
    ld (lr__cur),a
    nextreg 0x52,a
    ld a,h
    and 0x1F
    or 0x40
    ld h,a
    ret

; HL = the runs of directory entry DE (column x, or W + y for a row), mapped
lr__entry:
    ld h,d
    ld l,e
    add hl,hl
    add hl,de                   ; 3 * entry
    ld de,(lr__dir)
    add hl,de
    call lr__map
    ld b,(hl)                   ; page from the first
    call lr__inc
    ld e,(hl)
    call lr__inc
    ld d,(hl)
    ld a,(_lr_page)
    add a,b
    ld (lr__cur),a
    nextreg 0x52,a
    ex de,hl
    ld a,h
    or 0x40
    ld h,a
    ret

; inc HL through the picture's pages (slot 2)
lr__inc:
    inc hl
    ld a,h
    cp 0x60
    ret nz
    ld h,0x40
    ld a,(lr__cur)
    inc a
    ld (lr__cur),a
    nextreg 0x52,a
    ret

; ---------------------------------------------------------------------
; void lr__col(uint16_t c, int16_t x, int16_t y0): Layer 2 column c (0..319)
; = picture column x, rows y0 .. y0 + 255
_lr_col:
    call lr__save
    push ix
    ld ix,4
    add ix,sp
    ; Layer 2 page and column address in slot 1
    ld l,(ix + 0)
    ld h,(ix + 1)               ; c
    ld a,l
    and 31
    or 0x20
    ld (lc_hi),a
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,(_lr_l2page)
    add a,h                     ; + c >> 5
    nextreg 0x51,a
    ld a,(ix + 4)               ; y0 & 255: the first row's place
    ld (lc_lo),a
    ; void column? x < 0, x >= w, y0 >= h, y0 + 256 <= 0
    ld l,(ix + 2)
    ld h,(ix + 3)
    bit 7,h
    jp nz,lc_void
    ld de,(_lr_w)
    or a
    sbc hl,de
    jp nc,lc_void
    ld l,(ix + 4)
    ld h,(ix + 5)
    ld (lc_y),hl
    bit 7,h
    jr nz,lc_neg
    ld de,(_lr_h)
    or a
    sbc hl,de
    jp nc,lc_void
    jr lc_go
lc_neg:
    ld de,256
    add hl,de
    bit 7,h
    jp nz,lc_void
    ld a,h
    or l
    jp z,lc_void
lc_go:
    ; rows to write: 256; DE = destination
    ld a,(lc_hi)
    ld d,a
    ld a,(lc_lo)
    ld e,a
    ld bc,256
    ld (lc_left),bc
    ; rows above the picture: void
    ld hl,(lc_y)
    bit 7,h
    jr z,lc_top
    ; n = -y0 voids
    xor a
    sub l
    ld b,a                      ; -y0 (1..255)
    ld a,(_lr_void)
lc_v1:
    ld (de),a
    inc e
    djnz lc_v1
    ld hl,(lc_y)                ; left = 256 + y0
    ld bc,256
    add hl,bc
    ld (lc_left),hl
    ld hl,0
    ld (lc_y),hl
lc_top:
    ; the column's runs; skip to row y
    push de
    ld e,(ix + 2)
    ld d,(ix + 3)
    call lr__entry               ; HL = runs
    pop de
    ld bc,(lc_y)                ; rows still to skip
lc_skip:
    ld a,b
    or c
    jr z,lc_out0
    ld a,(hl)                   ; run length
    push hl
    ld h,0
    ld l,a
    or a
    sbc hl,bc                   ; len - skip
    jr z,lc_sk_whole
    jr nc,lc_sk_part
lc_sk_whole:
    ; the run ends at or before y: skip it
    pop hl
    push af
    ld a,(hl)
    ld (lc_t),a
    pop af
    ld a,c                      ; skip -= len
    push hl
    ld hl,lc_t
    sub (hl)
    ld c,a
    ld a,b
    sbc a,0
    ld b,a
    pop hl
    call lr__inc
    call lr__inc
    jr lc_skip
lc_sk_part:
    ; y falls in this run: len - skip rows of it remain
    ld a,l                      ; remaining (1..255)
    pop hl
    push af                     ; (lr__inc changes A)
    call lr__inc
    ld c,(hl)                   ; colour
    call lr__inc
    pop af
    ld b,a
    jr lc_emit
lc_out0:
    ; at a run start
    ld b,(hl)
    call lr__inc
    ld c,(hl)
    call lr__inc
lc_emit:
    ; B rows of colour C, but no more than left
    push hl
    ld hl,(lc_left)
    ld a,h
    or a
    jr nz,lc_e1                 ; left >= 256: all of the run
    ld a,l
    cp b
    jr nc,lc_e1
    ld b,a                      ; the last part of the run
lc_e1:
    ld a,l                      ; left -= b
    sub b
    ld l,a
    ld a,h
    sbc a,0
    ld h,a
    ld (lc_left),hl
    ld a,c
lc_e2:
    ld (de),a
    inc e
    djnz lc_e2
    pop hl
    ld bc,(lc_left)
    ld a,b
    or c
    jr z,lc_done
    ; the picture's last row? then void
    ; (rows written so far + y0 >= h)
    push hl
    ld hl,256
    or a
    sbc hl,bc                   ; written
    ld c,(ix + 4)
    ld b,(ix + 5)
    add hl,bc                   ; y of the next row
    ld bc,(_lr_h)
    or a
    sbc hl,bc
    pop hl
    jr c,lc_out0
    ; void to the end
    ld bc,(lc_left)
    ld b,c
    ld a,(_lr_void)
lc_v2:
    ld (de),a
    inc e
    djnz lc_v2
lc_done:
    call lr__home
    pop ix
    ret
lc_void:
    ld a,(lc_hi)
    ld h,a
    ld l,0
    ld a,(_lr_void)
    ld b,0
lc_v3:
    ld (hl),a
    inc l
    djnz lc_v3
    jr lc_done

; ---------------------------------------------------------------------
; void lr__row(uint16_t c0, int16_t x0, int16_t y): Layer 2 row y & 255 of
; columns c0 .. c0 + 319 (mod 320) = picture row y, columns x0 .. x0 + 319
_lr_row:
    call lr__save
    push ix
    ld ix,4
    add ix,sp
    ld a,(ix + 4)
    ld (lr_lo),a
    ; the first column's page and address
    ld l,(ix + 0)
    ld h,(ix + 1)
    ld (lr_c),hl
    call lr_page                ; D = address high byte, E = row
    ld bc,320
    ld (lr_left),bc
    ; void row? y < 0, y >= h
    ld l,(ix + 4)
    ld h,(ix + 5)
    bit 7,h
    jp nz,lr_allvoid
    ld bc,(_lr_h)
    or a
    sbc hl,bc
    jp nc,lr_allvoid
    ; columns left of the picture (x0 < 0): void
    ld l,(ix + 2)
    ld h,(ix + 3)
    ld (lr_x),hl
    bit 7,h
    jr z,lr_in
    ld bc,320
    add hl,bc
    bit 7,h
    jp nz,lr_allvoid
    ld a,h
    or l
    jp z,lr_allvoid
    ; -x0 voids (1..319)
    ld hl,(lr_x)
    xor a
    sub l
    ld c,a
    ld a,0
    sbc a,h
    ld b,a                      ; BC = -x0
    push bc
    call lr_voids
    pop bc
    ld hl,(lr_left)
    or a
    sbc hl,bc
    ld (lr_left),hl
    ld hl,0
    ld (lr_x),hl
lr_in:
    ; x0 >= w: all void
    ld hl,(lr_x)
    ld bc,(_lr_w)
    or a
    sbc hl,bc
    jp nc,lr_rest
    ; the row's runs: entry W + y
    push de
    ld hl,(_lr_w)
    ld e,(ix + 4)
    ld d,(ix + 5)
    add hl,de
    ex de,hl
    call lr__entry
    pop de
    ; skip to column x (x < w)
    ld bc,(lr_x)
lr_skip:
    ld a,b
    or c
    jr z,lr_at
    ld a,(hl)
    push hl
    ld h,0
    ld l,a
    or a
    sbc hl,bc
    jr z,lr_skw
    jr nc,lr_skp
lr_skw:
    pop hl
    ld a,(hl)
    ld (lr_t),a
    ld a,c
    push hl
    ld hl,lr_t
    sub (hl)
    ld c,a
    ld a,b
    sbc a,0
    ld b,a
    pop hl
    call lr__inc
    call lr__inc
    jr lr_skip
lr_skp:
    ld a,l
    pop hl
    push af
    call lr__inc
    ld c,(hl)
    call lr__inc
    pop af
    ld b,a
    jr lr_emit
lr_at:
    ld b,(hl)
    call lr__inc
    ld c,(hl)
    call lr__inc
lr_emit:
    ; B columns of colour C (no more than left); x advances
    push hl
    ld hl,(lr_left)
    ld a,h
    or a
    jr nz,lr_e1
    ld a,l
    cp b
    jr nc,lr_e1
    ld b,a
lr_e1:
    ld a,l
    sub b
    ld l,a
    ld a,h
    sbc a,0
    ld h,a
    ld (lr_left),hl
    ld hl,(lr_x)
    ld a,l
    add a,b
    ld l,a
    ld a,h
    adc a,0
    ld h,a
    ld (lr_x),hl
lr_e2:
    ld a,c
    ld (de),a
    inc d                       ; the next column
    ld a,d
    cp 0x40
    call z,lr_nextpage
    djnz lr_e2
    pop hl
    ld bc,(lr_left)
    ld a,b
    or c
    jr z,lr_done
    ; past the picture's right edge?
    push hl
    ld hl,(lr_x)
    ld bc,(_lr_w)
    or a
    sbc hl,bc
    pop hl
    jr c,lr_at
lr_rest:
    ld bc,(lr_left)
    call lr_voids
lr_done:
    call lr__home
    pop ix
    ret
lr_allvoid:
    ld bc,320
    call lr_voids
    jr lr_done

; BC voids along the row (BC >= 1)
lr_voids:
    ld a,b
    or c
    ret z
lrv_l:
    ld a,(_lr_void)
    ld (de),a
    inc d
    ld a,d
    cp 0x40
    call z,lr_nextpage
    dec bc
    ld a,b
    or c
    jr nz,lrv_l
    ret

; the next 32 columns' page (wrapping at column 320)
lr_nextpage:
    push hl
    ld hl,(lr_c)
    ld a,l
    and 0xE0
    ld l,a
    ld a,32
    add a,l
    ld l,a
    ld a,0
    adc a,h
    ld h,a                      ; the next multiple of 32
    push de
    ld de,320
    or a
    sbc hl,de
    add hl,de
    pop de
    jr c,lrn_1
    ld hl,0
lrn_1:
    ld (lr_c),hl
    call lr_page
    pop hl
    ret

; map Layer 2 column (lr_c) into slot 1: D = its address high byte, E = the row
lr_page:
    ld hl,(lr_c)
    ld a,l
    and 31
    or 0x20
    ld d,a
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,(_lr_l2page)           ; (B is the caller's count, C its colour)
    add a,h
    nextreg 0x51,a
    ld a,(lr_lo)
    ld e,a
    ret

; MMU slots 1 and 2 as the caller had them, and back
lr__save:
    ld bc,0x243B
    ld a,0x51
    out (c),a
    inc b
    in a,(c)
    ld (lr__m1),a
    dec b
    ld a,0x52
    out (c),a
    inc b
    in a,(c)
    ld (lr__m2),a
    ret
lr__home:
    ld a,(lr__m1)
    nextreg 0x51,a
lr__home2:
    ld a,(lr__m2)
    nextreg 0x52,a
    ret

SECTION data_user
_lr_l2page: defb 18             ; Layer 2's first 8K page (NextReg 0x12 = 9)

SECTION bss_user
lr__m1:   defs 1
lr__m2:   defs 1
_lr_w:    defs 2
_lr_h:    defs 2
_lr_ox:   defs 2
_lr_oy:   defs 2
_lr_void: defs 1
_lr_page: defs 1
lr__dir:   defs 2
lr__cur:   defs 1
lc_hi:    defs 1
lc_lo:    defs 1
lc_y:     defs 2
lc_left:  defs 2
lc_t:     defs 1
lr_lo:    defs 1
lr_c:     defs 2
lr_x:     defs 2
lr_left:  defs 2
lr_t:     defs 1
