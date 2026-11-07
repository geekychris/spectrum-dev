; input_sample(): call from frame_hook (every 50 Hz interrupt). Samples the
; 8 keyboard half-rows and the Kempston and collects press *edges* (keys
; that went down since the previous sample), so a tap is never lost and two
; quick taps are two presses, however slow the game loop is (see input.c).
; input_edge_rows: 1 = this key went down since the last input_read
; input_edge_kemp: likewise for the Kempston bits (1 = pressed)

SECTION code_user

PUBLIC _input_sample
PUBLIC _input_edge_rows, _input_edge_kemp, _input_last_rows, _input_last_kemp
EXTERN _input_kempston

_input_sample:
    ld hl,last_rows
    ld de,_input_edge_rows
    ld bc,0xFEFE
sample_row:
    in a,(c)
    and 0x1F                    ; 0 = pressed
    push bc
    ld c,a                      ; C = now
    ld a,(hl)                   ; last
    ld (hl),c
    ld b,a
    ld a,c
    cpl                         ; 1 = pressed now
    and b                       ; ... and released last time = a new press
    ex de,hl
    or (hl)
    ld (hl),a
    ex de,hl
    inc hl
    inc de
    pop bc
    rlc b
    jr c,sample_row             ; 8 rows: B goes FE FD FB F7 EF DF BF 7F
    ld a,(_input_kempston)
    or a
    ret z
    ld bc,0x001F
    in a,(c)
    and 0x1F                    ; 1 = pressed
    ld hl,last_kemp
    ld b,(hl)
    ld (hl),a
    ld c,a
    ld a,b
    cpl
    and c                       ; pressed now, not last time
    ld hl,_input_edge_kemp
    or (hl)
    ld (hl),a
    ret

SECTION data_user
_input_last_rows:
last_rows:        defb 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F
_input_last_kemp:
last_kemp:        defb 0
_input_edge_rows: defb 0, 0, 0, 0, 0, 0, 0, 0
_input_edge_kemp: defb 0
