; Uranus Lander: the star field in assembly (in C it was 40% of the frame).
;
; stars_update_asm: port: the original's stars_update() (game.c) for n
;   frames at once: accum + n * speed, a pixel left for every 4 (the same as
;   n single updates, since speed < 4), wrapping at 0 to 319.
; stars_scan: view.c's drawing: each star whose screen x changed is XORed
;   off where it was and on where it is now if that cell is open sky
;   (tint_map). st_force: look at every star; st_alloff: take them all off;
;   st_half = 1 / 2: only the even / odd ones this time (0: all).
;   Per star, star_rec holds: shown x, on, the screen row's address, the
;   row's offset in tint_map (0xFF: never visible, in a HUD row), spare.

SECTION code_user

PUBLIC _stars_update_asm, _stars_scan
PUBLIC _st_force, _st_alloff, _st_half
EXTERN _star_rec, _wx2sx, _tint_map

STAR_SIZE equ 10                ; x, y, brightness, speed, accum (WORDs)
REC_SIZE  equ 6
NSTARS    equ 60

; void stars_update_asm(Star *stars, uint8_t n)
_stars_update_asm:
    ld hl,4
    add hl,sp
    ld a,(hl)                   ; n
    ld (step_tab + 1),a         ; speed * n for speed 1, 2, 3
    ld c,a
    add a,c
    ld (step_tab + 2),a
    add a,c
    ld (step_tab + 3),a
    dec hl
    ld d,(hl)
    dec hl
    ld e,(hl)
    ex de,hl                    ; HL = stars
    ld a,NSTARS
    ld (upd_count),a
upd_loop:
    push hl                     ; &x
    ld de,6
    add hl,de
    ld e,(hl)                   ; speed (1..3)
    ld d,0
    inc hl
    inc hl                      ; &accum
    push hl
    ld hl,step_tab
    add hl,de
    ld a,(hl)                   ; speed * n
    pop hl
    add a,(hl)                  ; + accum (0..3)
    ld c,a
    and 3
    ld (hl),a                   ; accum
    ld a,c
    rrca
    rrca
    and 3                       ; pixels to move
    pop hl                      ; &x
    jr z,upd_next
    ld c,a
    ld e,(hl)
    inc hl
    ld d,(hl)
    ex de,hl                    ; HL = x, DE = &x + 1
    ld b,0
    or a
    sbc hl,bc                   ; x -= moves
    jr nc,upd_store
    ld bc,320                   ; x < 0: x += 320
    add hl,bc
upd_store:
    ex de,hl
    ld (hl),d
    dec hl
    ld (hl),e
upd_next:
    ld de,STAR_SIZE
    add hl,de
    ld a,(upd_count)
    dec a
    ld (upd_count),a
    jr nz,upd_loop
    ret

; void stars_scan(const Star *stars)
_stars_scan:
    pop bc
    pop hl
    push hl
    push bc
    push ix
    push iy
    push hl
    pop ix                      ; IX = the star
    ld iy,_star_rec             ; IY = its record
    xor a
    ld (idx),a
scan_loop:
    ld a,(_st_half)
    or a
    jr z,scan_all
    ld b,a
    ld a,(idx)
    and 1
    inc a                       ; 1 = even index, 2 = odd
    cp b
    jp nz,scan_next
scan_all:
    ld l,(ix+0)
    ld h,(ix+1)
    ld de,_wx2sx
    add hl,de
    ld c,(hl)                   ; C = sx
    ld a,(_st_force)
    or a
    jr nz,scan_eval
    ld a,(iy+0)
    cp c
    jp z,scan_next              ; not moved on the screen
scan_eval:
    ld a,(iy+1)
    or a
    jr z,scan_new
    ld a,(iy+0)
    call plot                   ; off where it was
scan_new:
    ld (iy+0),c
    ld (iy+1),0
    ld a,(_st_alloff)
    or a
    jr nz,scan_next
    ld a,(iy+4)
    cp 0xFF
    jr z,scan_next
    ld l,a                      ; tint_map + row * 4 + sx / 64
    ld a,c
    rlca
    rlca
    and 3
    add a,l
    ld e,a
    ld d,0
    ld hl,_tint_map
    add hl,de
    ld b,(hl)
    ld a,c
    rrca
    rrca
    rrca
    and 7                       ; the cell's bit
    ld e,a
    ld hl,bits
    add hl,de
    ld a,(hl)
    and b
    jr z,scan_next              ; not open sky
    ld (iy+1),1
    ld a,c
    call plot                   ; on where it is
scan_next:
    ld de,STAR_SIZE
    add ix,de
    ld de,REC_SIZE
    add iy,de
    ld hl,idx
    inc (hl)
    ld a,(hl)
    cp NSTARS
    jp c,scan_loop
    pop iy
    pop ix
    ret

; XOR pixel x = A on the record's row
plot:
    ld b,a
    rrca
    rrca
    rrca
    and 31
    add a,(iy+2)
    ld l,a
    ld h,(iy+3)                 ; screen rows are 32-byte aligned: no carry
    ld a,b
    and 7
    ld e,a
    ld d,0
    push hl
    ld hl,bits
    add hl,de
    ld a,(hl)
    pop hl
    xor (hl)
    ld (hl),a
    ret

bits:
    defb 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

SECTION data_user

step_tab:    defb 0, 0, 0, 0

SECTION bss_user

upd_count:   defb 0
idx:         defb 0
_st_force:   defb 0
_st_alloff:  defb 0
_st_half:    defb 0
