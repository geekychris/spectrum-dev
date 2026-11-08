; macros and layouts shared by the physics' assembly (phys*.asm)

; ---- 32-bit values in DEHL against 32-bit variables ----
MACRO LD32 p_a
    ld hl,(p_a)
    ld de,(p_a + 2)
ENDM
MACRO ST32 p_a
    ld (p_a),hl
    ld (p_a + 2),de
ENDM
MACRO LDI32 p_v
    ld hl,+((p_v) & 0xFFFF)
    ld de,+(((p_v) >> 16) & 0xFFFF)
ENDM
MACRO ADD32 p_a
    ld bc,(p_a)
    add hl,bc
    ex de,hl
    ld bc,(p_a + 2)
    adc hl,bc
    ex de,hl
ENDM
MACRO SUB32 p_a
    ld bc,(p_a)
    or a
    sbc hl,bc
    ex de,hl
    ld bc,(p_a + 2)
    sbc hl,bc
    ex de,hl
ENDM
MACRO SRA32 p_n
    ld b,p_n
    call m_sra32
ENDM
MACRO SLA32 p_n
    ld b,p_n
    call m_sla32
ENDM
; fixed shifts
MACRO SRA2
    sra d
    rr e
    rr h
    rr l
    sra d
    rr e
    rr h
    rr l
ENDM
MACRO SRA4
    call m_sra4
ENDM
MACRO SRA8
    ld l,h
    ld h,e
    ld e,d
    ld a,d
    rla
    sbc a,a
    ld d,a
ENDM
MACRO SRA12
    call m_sra12
ENDM
MACRO SRA16
    ex de,hl
    ld a,h
    rla
    sbc a,a
    ld d,a
    ld e,a
ENDM
MACRO SLA4
    call m_sla4
ENDM
MACRO SLA12
    call m_sla12
ENDM
; DEHL *= a 16-bit signed constant
MACRO MULC p_n
    ld bc,p_n
    call m_mul32s16
ENDM
; DEHL *= the 32-bit variable a (any value)
MACRO MUL32 p_a
    ld bc,p_a
    call m_mul32x
ENDM
MACRO DIV32 p_a
    ld bc,p_a
    call m_div32
ENDM
; carry set if DEHL < (p_a), signed; Z if equal
MACRO CMP32 p_a
    ld bc,p_a
    call m_cmp32
ENDM
MACRO COPY32 p_d, p_s
    ld hl,(p_s)
    ld (p_d),hl
    ld hl,(p_s + 2)
    ld (p_d + 2),hl
ENDM
MACRO CLR32 p_d
    ld hl,0
    ld (p_d),hl
    ld (p_d + 2),hl
ENDM
; IX-relative
MACRO LDX32 p_o
    ld l,(ix + p_o)
    ld h,(ix + p_o + 1)
    ld e,(ix + p_o + 2)
    ld d,(ix + p_o + 3)
ENDM
MACRO STX32 p_o
    ld (ix + p_o),l
    ld (ix + p_o + 1),h
    ld (ix + p_o + 2),e
    ld (ix + p_o + 3),d
ENDM

; ---- rs.h's structures as zsdcc lays them out (int 2 bytes, long 4,
;      pointer 2, no padding); phys_c.c checks these with offsetof ----
defc B_PX = 0
defc B_PY = 4
defc B_PZ = 8
defc B_VX = 12
defc B_VY = 16
defc B_VZ = 20
defc B_WX = 24
defc B_WY = 28
defc B_WZ = 32
defc B_R = 36
defc B_INVM = 40
defc B_FRIC = 44
defc B_BOUNCE = 48
defc B_GRAV = 52
defc B_ROTK = 56
defc B_GROUNDED = 60
defc B_FROZEN = 62
defc B_LIVE = 64
defc B_LGX = 66
defc B_LGY = 70
defc B_LGZ = 74
defc B_TENEMY = 78
defc B_TSWEEP = 80
defc B_TTILE = 82
defc B_KILL = 84
defc B_GOAL = 86
defc BALL_SIZE = 88

defc CR_STATE = 0
defc CR_TIMER = 2
defc CR_FALL = 6
defc CRUMBLE_SIZE = 10

defc E_B = 0
defc E_HOMEX = 88
defc E_HOMEY = 92
defc E_HOMEZ = 96
defc E_KIND = 100
defc E_RANGE = 102
defc E_SPEED = 106
defc E_AX = 110
defc E_AZ = 114
defc E_PHASE = 118
defc ENEMY_SIZE = 122

defc W_CRUMBLE = 0
defc W_ENEMY = 320
defc W_NENEMY = 1296
defc W_SWEEP = 1298
defc W_CRUSHH = 1330
defc W_CRUSHCLK = 1362
defc W_SLAMMED = 1394
defc W_CLOCK = 1410
defc WORLD_SIZE = 1414

; OBB: cx cy cz hx hy hz cs sn (long each)
defc O_CX = 0
defc O_CY = 4
defc O_CZ = 8
defc O_HX = 12
defc O_HY = 16
defc O_HZ = 20
defc O_CS = 24
defc O_SN = 28
defc OBB_SIZE = 32
defc ZONE_SIZE = 52             ; box, dir[3], power, kind
defc PILLAR_SIZE = 20           ; x y z r h
defc SWEEP_SIZE = 24            ; x y z len speed yaw
defc CRUSH_SIZE = 36            ; x y z w period phase lift cs sn
defc ESPEC_SIZE = 32            ; kind x y z range speed ax az

; a course triangle in its 8K page (tools/gen_course.py; 178 a page):
; vertices and the bounding sphere's centre are 24-bit
defc T_A = 0                    ; a.x a.y a.z b.x .. c.z
defc T_B = 9
defc T_C = 18
defc T_N = 27                   ; n[3]: int16, Q12
defc T_GROUP = 33
defc T_CLS = 34
defc T_SPH = 35                 ; x y z (24-bit), r (16-bit) at T_SPH + 9
defc T_SPHR = 44
defc TRI_SIZE = 46
defc TRI_WINDOW = 0x4000        ; mapped into MMU slot 2
