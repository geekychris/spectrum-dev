; gridbfs.h: the breadth-first search loop behind gb_step (gridbfs.c has the
; same loop in C for host builds). Takes cells from the queue, labels the
; neighbours that can step into each one with its distance + 1 and queues
; those that can be entered; stops when the queue is empty or the wanted
; cell has a distance. Cells are (flags, distance) byte pairs; the queue is a
; ring of their offsets from gb_cells in one 256-byte page (so a pointer
; advances by its low byte alone). ~300 T-states a cell.

SECTION code_user

PUBLIC _gb_run, _gb_clear
EXTERN _gb_cells, _gb_stride2, _gb_qhead, _gb_qtail, _gb_want, _gb_searched, _gb_overflow

_gb_run:
    push ix
    ld ix,(_gb_qtail)           ; IX = queue tail
gr_loop:
    ld hl,(_gb_want)
    inc hl
    ld a,(hl)
    inc a
    jr nz,gr_done               ; the wanted cell has its distance
    ld hl,(_gb_qhead)
    push ix
    pop de
    or a
    sbc hl,de
    jr z,gr_done                ; queue empty
    add hl,de
    ld e,(hl)
    inc l
    ld d,(hl)
    inc l
    ld (_gb_qhead),hl
    ld hl,(_gb_searched)
    inc hl
    ld (_gb_searched),hl
    ld hl,(_gb_cells)
    add hl,de                   ; HL = cell m
    inc hl
    ld a,(hl)
    inc a                       ; distance of its neighbours
    cp 253
    jr z,gr_stop                ; too far (253 = "no path"): give up
    ld c,a
    dec hl
    ex de,hl                    ; DE = m
    ld hl,2                     ; the cell to the right leaves left into m
    add hl,de
    ld a,(hl)
    and 0x01
    call nz,label
    ld h,d                      ; the cell to the left leaves right
    ld l,e
    dec hl
    dec hl
    ld a,(hl)
    and 0x02
    call nz,label
    ld hl,(_gb_stride2)         ; the cell below leaves up
    add hl,de
    ld a,(hl)
    and 0x04
    call nz,label
    push de                     ; the cell above leaves down
    ld hl,(_gb_stride2)
    ex de,hl
    or a
    sbc hl,de
    pop de
    ld a,(hl)
    and 0x08
    call nz,label
    jr gr_loop
gr_stop:
    push ix
    pop hl
    ld (_gb_qhead),hl
gr_done:
    ld (_gb_qtail),ix
    pop ix
    ret

; HL = a neighbour's flags byte, C = distance: label it if it has none, and
; queue it if it can be entered (GB_IN). Keeps DE and C.
label:
    inc hl
    ld a,(hl)
    inc a
    ret nz                      ; already has a distance (or "no path")
    ld (hl),c
    dec hl
    bit 4,(hl)
    ret z
    push de
    ld de,(_gb_cells)
    or a
    sbc hl,de
    pop de
    ld (ix+0),l
    ld (ix+1),h
    inc ixl
    inc ixl
    ld a,(_gb_qhead)
    cp ixl
    ret nz
    ld a,1                      ; the tail caught up with the head
    ld (_gb_overflow),a
    ret

; void gb_clear(uint8_t *dist, uint16_t n, uint8_t carry): every n-th other
; byte from dist becomes 255 (unknown); with carry, 253 and 255 ("no path",
; not reached) become 253 instead. Arguments on the stack (sdcc_ix).
_gb_clear:
    pop af                      ; return address
    pop hl                      ; dist
    pop de                      ; n
    dec sp
    pop bc                      ; B = carry
    push bc
    inc sp
    push de
    push hl
    push af
    ld a,b
    or a
    jr nz,gc_carry
gc_plain:
    ld (hl),255
    inc hl
    inc hl
    dec de
    ld a,d
    or e
    jr nz,gc_plain
    ret
gc_carry:
    ld a,(hl)
    cp 253
    ld a,255
    jr c,gc_unknown             ; it had a distance
    ld a,253
gc_unknown:
    ld (hl),a
    inc hl
    inc hl
    dec de
    ld a,d
    or e
    jr nz,gc_carry
    ret
