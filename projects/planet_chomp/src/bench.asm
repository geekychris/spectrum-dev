; calibration: 65536 x (dec bc; ld a,b; or c; jr nz) = 26 T-states each
SECTION code_user
PUBLIC _bench_loop, _bench_store
_bench_loop:
    ld bc,0
bl1:
    dec bc
    ld a,b
    or c
    jr nz,bl1
    ret
; 65536 x (ld (hl),a; inc l) unrolled 16 = 11 T a store, to 0x6000 (Layer 2 page mapped by caller)
_bench_store:
    ld bc,4096
    ld hl,0x6000
bs1:
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    dec bc
    ld a,b
    or c
    jr nz,bs1
    ret
