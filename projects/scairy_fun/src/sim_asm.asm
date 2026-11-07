; The simulation's hottest loops in Z80N assembly (port: zsdcc's code for
; them was 150-300 T-states per object slot). Each does exactly what the C
; it replaces did; the C is in the comment above it. Resident (main RAM).
;
; Obj (sc.h) as zsdcc lays it out:
;   0 type  1 flags  2 colour  3 hz  5 lane  7 z (4)  11 d  13 v  15 vd
;   17 vt  19 timer  21 cool   = 23 bytes; MAX_OBJS = 40
; Seg: 0 cx  2 y  4 dir  5 side[2]  7 flags  8 closed  9 ghost   = 10 bytes

SECTION code_user

PUBLIC _seg_of, _lead_scan, _lane_scan, _obj_find, _obj_top, _closed_down

; obj_top: slots from obj_top up are all OBJ_NONE (world_spawn raises it,
; objects_update lowers it again each step), so the scans stop there

OBJ_SIZE  equ 23
MAX_OBJS  equ 40

; int seg_of(int32_t z)              return z >> 11
_seg_of:
    ld hl,3
    add hl,sp
    ld a,(hl)                   ; bits 8..15
    inc hl
    ld e,(hl)                   ; 16..23
    inc hl
    ld d,(hl)                   ; 24..31
    ld l,a
    ld h,e
    ld e,d                      ; E:H:L = z >> 8 (low 24 bits of it)
    sra e
    rr h
    rr l
    sra e
    rr h
    rr l
    sra e
    rr h
    rr l
    ret

; const Obj *lead_scan(const Obj *objs, const Obj *o, int32_t *best)
; vehicle_follow's search for what o is following:
;   for (k = 0; k < MAX_OBJS; k++) { p = &objs[k];
;     if (p == o || p->type == OBJ_NONE || p->v < 0 || p->type == OBJ_CYCLIST ||
;         p->type == OBJ_DEBRIS) continue;
;     if (iabs16(p->d - o->d) > 640) continue;
;     dz = p->z - o->z;
;     if (dz > 0 && dz < *best) { *best = dz; found = p; } }
;   return found (NULL: none)
_lead_scan:
    push ix
    ld hl,4
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = objs
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = o
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (ls_best_ptr),hl
    ld a,(hl)
    ld (ls_best),a
    inc hl
    ld a,(hl)
    ld (ls_best+1),a
    inc hl
    ld a,(hl)
    ld (ls_best+2),a
    inc hl
    ld a,(hl)
    ld (ls_best+3),a
    push de                     ; objs, for IX below
    push bc
    pop ix                      ; o's fields
    ld l,(ix+11)
    ld h,(ix+12)
    ld de,-640
    add hl,de
    ld (ls_od),hl               ; o->d - 640
    ld a,(ix+7)
    ld (ls_oz),a
    ld a,(ix+8)
    ld (ls_oz+1),a
    ld a,(ix+9)
    ld (ls_oz+2),a
    ld a,(ix+10)
    ld (ls_oz+3),a
    ld hl,0
    ld (ls_found),hl
    pop ix                      ; IX = p = objs
    ld a,(_obj_top)
    or a
    jp z,ls_done
    ld (ls_count),a
; The tests are in another order than the C's (they are all "skip if"), and
; p == o needs no test of its own: its dz is 0.
ls_loop:
    ld a,(ix+0)
    or a
    jr z,ls_next
    ld l,(ix+11)
    ld h,(ix+12)
    ld de,(ls_od)
    or a
    sbc hl,de                   ; HL = p->d - o->d + 640 (|d| < 7000: no overflow)
    ld de,1281
    or a
    sbc hl,de
    jr nc,ls_next               ; unsigned > 1280: |p->d - o->d| > 640
    cp 4                        ; OBJ_CYCLIST
    jr z,ls_next
    cp 7                        ; OBJ_DEBRIS
    jr z,ls_next
    bit 7,(ix+14)               ; v < 0
    jr nz,ls_next
    ld hl,ls_oz
    ld a,(ix+7)
    sub (hl)
    ld e,a
    inc hl
    ld a,(ix+8)
    sbc a,(hl)
    ld d,a
    inc hl
    ld a,(ix+9)
    sbc a,(hl)
    ld c,a
    inc hl
    ld a,(ix+10)
    sbc a,(hl)
    ld b,a                      ; BCDE = dz
    bit 7,b
    jr nz,ls_next               ; dz < 0
    or c
    or d
    or e
    jr z,ls_next                ; dz == 0
    ld hl,ls_best
    ld a,e
    sub (hl)
    inc hl
    ld a,d
    sbc a,(hl)
    inc hl
    ld a,c
    sbc a,(hl)
    inc hl
    ld a,b
    sbc a,(hl)
    jp p,ls_next                ; dz - best >= 0 (both are positive)
    ld (ls_best),de
    ld (ls_best+2),bc
    ld (ls_found),ix
ls_next:
    ld de,OBJ_SIZE
    add ix,de
    ld hl,ls_count
    dec (hl)
    jp nz,ls_loop
ls_done:
    ld hl,(ls_best_ptr)
    ld de,(ls_best)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld de,(ls_best+2)
    ld (hl),e
    inc hl
    ld (hl),d
    ld hl,(ls_found)
    pop ix
    ret

; int lane_scan(const Obj *objs, int lane, int32_t z, int32_t lo, int32_t hi, int skip)
; lane_clear's loop (lo = dz0 * SEGF, hi = dz1 * SEGF):
;   for (i = 0; i < MAX_OBJS; i++) { o = &objs[i];
;     if (i == skip || o->type == OBJ_NONE || o->lane != lane) continue;
;     dz = o->z - z;
;     if (dz > lo && dz < hi) return 0; }
;   return 1
_lane_scan:
    push ix
    ld hl,4
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push de                     ; objs
    ld de,ls_lane
    ld bc,2 + 4 + 4 + 4 + 2     ; lane, z, lo, hi, skip
    ldir
    pop ix
    ld c,0                      ; i
    ld a,(_obj_top)
    or a
    jr z,lc_clear
lc_loop:
    ld a,(ix+0)
    or a
    jr z,lc_next
    ld a,(ls_skip+1)            ; skip is 16-bit; i < 40
    or a
    jr nz,lc_noskip
    ld a,(ls_skip)
    cp c
    jr z,lc_next
lc_noskip:
    ld hl,(ls_lane)
    ld a,(ix+5)
    cp l
    jr nz,lc_next
    ld a,(ix+6)
    cp h
    jr nz,lc_next
    ld hl,ls_z
    ld a,(ix+7)
    sub (hl)
    ld e,a
    inc hl
    ld a,(ix+8)
    sbc a,(hl)
    ld d,a
    inc hl
    ld a,(ix+9)
    sbc a,(hl)
    ld b,a
    inc hl
    ld a,(ix+10)
    sbc a,(hl)
    ld (lc_dz3),a               ; dz = A:B:D:E (A = top byte)
    ; dz > lo  <=>  lo - dz < 0 (signed, 32-bit; no overflow: |values| < 2^30)
    ld hl,ls_lo
    ld a,(hl)
    sub e
    inc hl
    ld a,(hl)
    sbc a,d
    inc hl
    ld a,(hl)
    sbc a,b
    inc hl
    ld a,(lc_dz3)
    ld l,a
    ld a,(ls_lo+3)
    sbc a,l
    jp p,lc_next                ; lo - dz >= 0: dz <= lo
    ; dz < hi  <=>  dz - hi < 0
    ld hl,ls_hi
    ld a,e
    sub (hl)
    inc hl
    ld a,d
    sbc a,(hl)
    inc hl
    ld a,b
    sbc a,(hl)
    inc hl
    ld a,(lc_dz3)
    sbc a,(hl)
    jp p,lc_next
    ld hl,0                     ; something in the way
    pop ix
    ret
lc_next:
    ld de,OBJ_SIZE
    add ix,de
    inc c
    ld a,(_obj_top)
    cp c
    jp nz,lc_loop
lc_clear:
    ld hl,1
    pop ix
    ret

; int obj_find(const Obj *objs, int start, uint16_t mask)
; the first index i >= start whose type t has bit t set in mask, else MAX_OBJS:
;   for (i = start; i < MAX_OBJS; i++) if (mask & (1 << objs[i].type)) return i;
;   return MAX_OBJS;
; (types are 0..10; the loops that use it went through all 40 slots)
_obj_find:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = objs
    inc hl
    ld c,(hl)                   ; C = start (0..40)
    inc hl
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (of_mask),hl
    ld a,(_obj_top)
    ld b,a
    ld a,c
    cp b
    jr nc,of_none
    push de
    ld d,c
    ld e,OBJ_SIZE
    mul d,e                     ; DE = start * 23
    pop hl
    add hl,de                   ; HL = &objs[start]
    ld a,(of_mask)
    cp 0xFE
    jr nz,of_loop               ; mask 0xFFFE (any type): just the non-empty slots
    ld a,(of_mask+1)
    inc a
    jr nz,of_loop
    ld a,(_obj_top)
    ld b,a
    ld de,OBJ_SIZE
of_any:
    ld a,(hl)
    or a
    jr nz,of_found
    add hl,de
    inc c
    ld a,c
    cp b
    jr c,of_any
    jr of_none
of_loop:
    ld a,(hl)                   ; type
    ld de,of_bits
    add de,a
    ld a,(de)                   ; 1 << (type & 7)
    bit 3,(hl)
    jr nz,of_high
    ld b,a
    ld a,(of_mask)
    and b
    jr nz,of_found
of_next:
    ld de,OBJ_SIZE
    add hl,de
    inc c
    ld a,(_obj_top)
    cp c
    jr z,of_none
    jr nc,of_loop
of_none:
    ld hl,MAX_OBJS
    ret
of_high:
    ld b,a
    ld a,(of_mask+1)
    and b
    jr z,of_next
of_found:
    ld l,c
    ld h,0
    ret

SECTION rodata_user
of_bits: defb 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128

SECTION bss_user
_obj_top:    defs 1
of_mask:     defs 2
ls_best_ptr: defs 2
ls_best:     defs 4
ls_od:       defs 2
ls_oz:       defs 4
ls_found:    defs 2
ls_count:    defs 1
ls_lane:     defs 2
ls_z:        defs 4
ls_lo:       defs 4
ls_hi:       defs 4
ls_skip:     defs 2
lc_dz3:      defs 1


; int closed_down(const Seg *seg, int hi, int lo)
;   for (k = hi; k >= lo; k--) if (seg[k].closed & 1) return k;
;   return -1;
; (objects_update's wait for a coned zone: hi - lo < 255, 38 at most there)
_closed_down:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = seg
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = hi
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = lo
    push hl
    ld a,c                      ; count = hi - lo + 1
    sub l
    ld l,a
    ld a,b
    sbc a,h
    jp m,cd_none_pop            ; hi < lo
    inc l
    jr z,cd_none_pop            ; (no more than 255)
    push hl                     ; L = count
    ld h,b
    ld l,c
    add hl,hl                   ; 2 hi
    ld b,h
    ld c,l
    add hl,hl
    add hl,hl
    add hl,bc                   ; 10 hi
    add hl,de
    ld de,8
    add hl,de                   ; &seg[hi].closed
    ld de,-10
    pop bc
    ld b,c                      ; B = count
cd_loop:
    bit 0,(hl)
    jr nz,cd_found
    add hl,de
    djnz cd_loop
cd_none_pop:
    pop hl
    ld hl,-1
    ret
cd_found:                       ; k = lo - 1 + B
    pop hl
    dec hl
    ld c,b
    ld b,0
    add hl,bc
    ret
