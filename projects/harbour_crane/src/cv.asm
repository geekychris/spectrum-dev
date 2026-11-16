; Camera-space points for view.c, in Z80N asm (zsdcc's C took ~6,000
; T-states a point). A point is three int16: X, Y, Z in 1/16 units of the
; fixed camera's space (view.c cv_*).
;
; void cv_mul(int16_t s, const int16_t *v, int16_t *out)
;     out[i] = (s * v[i]) >> 8 for i = 0..2: a Q12 vector times s, in 1/16 units
; void cv_proj(const int16_t *p, int16_t *xy)
;     the screen: r = cv_rcp[Z / 32] (= fov * 4096 / z, z = Z / 16 in steps of 2),
;     xy[0] = VIEW_CX + (X * r) >> 16, xy[1] = VIEW_CY - (Y * r) >> 16

SECTION code_user

PUBLIC _cv_mul, _cv_proj
PUBLIC _cv_rcp, _bank_copy
EXTERN _dma_copy

VIEW_CX equ 128
VIEW_CY equ 99

_cv_mul:
    push ix
    ld ix,0
    add ix,sp                   ; +4 s, +6 v, +8 out
    ld l,(ix+6)
    ld h,(ix+7)
    ld (cm_v),hl
    ld l,(ix+8)
    ld h,(ix+9)
    ld (cm_out),hl
    ld b,3
cm_loop:
    push bc
    ld hl,(cm_v)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (cm_v),hl
    ld l,(ix+4)
    ld h,(ix+5)
    call smul           ; DEHL = s * v[i]
    ld l,h
    ld h,e                      ; HL = bits 8..23 (>> 8, floor)
    ex de,hl
    ld hl,(cm_out)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (cm_out),hl
    pop bc
    djnz cm_loop
    pop ix
    ret

_cv_proj:
    push ix
    ld ix,0
    add ix,sp                   ; +4 p, +6 xy
    ld l,(ix+4)
    ld h,(ix+5)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (cp_x),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (cp_y),de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = Z (1/16 units)
    bit 7,h
    jr nz,cp_near               ; behind the camera: as near as the table goes
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l                        ; HL = Z >> 5 (0..1023)
    ld a,l
    or h
    jr nz,cp_have
cp_near:
    ld hl,1
cp_have:
    add hl,hl
    ld de,_cv_rcp
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = r
    ld (cp_r),de
    ld hl,(cp_x)
    call smul           ; DEHL = X * r
    ld hl,VIEW_CX
    add hl,de                   ; + (X * r) >> 16
    ex de,hl
    ld l,(ix+6)
    ld h,(ix+7)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    push hl
    ld hl,(cp_y)
    ld de,(cp_r)
    call smul           ; DEHL = Y * r
    ld hl,VIEW_CY
    or a
    sbc hl,de                   ; - (Y * r) >> 16
    ex de,hl
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    pop ix
    ret

; DEHL = HL * DE, signed, with the Z80N's MUL (8 x 8 -> 16): the four
; partial products of the unsigned halves, then for each negative operand
; the other one taken off the high word. ~230 T-states. Uses A, BC.
smul:
    ld (sm_a),hl
    ld (sm_b),de
    ld b,h                      ; B = ah
    ld c,l                      ; C = al
    ld a,d                      ; A = bh
    ld d,l
    mul d,e                     ; DE = al * bl
    ld (sm_p0),de
    ld d,b
    ld e,a
    mul d,e                     ; DE = ah * bh
    ld (sm_p3),de
    ld d,c
    ld e,a
    mul d,e                     ; DE = al * bh
    ld hl,(sm_b)
    ld a,l                      ; A = bl
    ld l,e
    ld h,d                      ; HL = al * bh
    ld d,b
    ld e,a
    mul d,e                     ; DE = ah * bl
    add hl,de                   ; HL = the middle (carry: bit 16)
    ld a,0
    adc a,a                     ; A = the middle's carry
    ; DEHL = p3 << 16 + middle << 8 + p0
    ld de,(sm_p0)
    ld c,d                      ; C = p0 high byte
    ld b,e                      ; B = p0 low byte (the result's low byte)
    ld e,l                      ; middle low
    ld d,h                      ; middle high
    ld a,a
    ; low word: L = p0 lo, H = p0 hi + middle lo
    ld l,b
    ld b,a                      ; B = the middle's carry
    ld a,c
    add a,e
    ld h,a
    ; high word: p3 + middle hi + carry(prev) + middle carry << 8
    ld a,(sm_p3)
    adc a,d
    ld e,a
    ld a,(sm_p3 + 1)
    adc a,b
    ld d,a
    ; signs
    ld a,(sm_a + 1)
    bit 7,a
    jr z,sm_b_sign
    ex de,hl
    ld bc,(sm_b)
    or a
    sbc hl,bc
    ex de,hl
sm_b_sign:
    ld a,(sm_b + 1)
    bit 7,a
    ret z
    ex de,hl
    ld bc,(sm_a)
    or a
    sbc hl,bc
    ex de,hl
    ret

; void bank_copy(uint16_t dst, uint16_t src): 3 16K banks (a 256 x 192
; Layer 2 screen) by DMA, an 8K page at a time through MMU slots 2 (the
; source) and 3 (the destination), which get pages 10 and 44 back (view.c
; runs in slots 0 and 1, where the SDK's dma_copy_banks would map)
_bank_copy:
    push ix
    ld ix,0
    add ix,sp                   ; +4 dst, +6 src
    ld a,(ix+4)
    add a,a
    ld (bc_dst),a
    ld a,(ix+6)
    add a,a
    ld (bc_src),a
    ld b,6
bc_loop:
    push bc
    ld a,(bc_src)
    nextreg 0x52,a
    inc a
    ld (bc_src),a
    ld a,(bc_dst)
    nextreg 0x53,a
    inc a
    ld (bc_dst),a
    ld hl,8192
    push hl
    ld hl,0x4000
    push hl
    ld hl,0x6000
    push hl
    call _dma_copy              ; dma_copy(dst 0x6000, src 0x4000, 8192)
    pop hl
    pop hl
    pop hl
    pop bc
    djnz bc_loop
    nextreg 0x52,10
    nextreg 0x53,44
    pop ix
    ret

SECTION bss_user
_cv_rcp:   defs 2048
bc_dst:    defb 0
bc_src:    defb 0
sm_a:      defw 0
sm_b:      defw 0
sm_p0:     defw 0
sm_p3:     defw 0
cm_v:      defw 0
cm_out:    defw 0
cp_x:      defw 0
cp_y:      defw 0
cp_r:      defw 0
