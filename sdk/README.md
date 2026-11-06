# SDK

| | |
|---|---|
| `project.mk` | shared make rules; a project `Makefile` sets `NAME`, `TARGET` (`zx` = 48K/128K `.tap`, `zxn` = Next `.nex`) and includes it. `ASSEMBLER := sjasmplus` for pure assembly. Options are listed at the top of the file. |
| `include/zxlog.h`, `src/zxlog.c`, `src/zxlog_s.asm` | `zx_log(fmt, ...)`, `zx_log_s(s)`, `zx_log_c(c)`: debug log to the harness (ZXI port), a no-op elsewhere |
| `include/zxlog.inc` | `ZXLOG "text\n"` macro for sjasmplus |
| `include/zxtext.h`, `src/zxtext.c` | `zx_print_at(col, row, s)`, `zx_attr_at(col, row, len, attr)`: text with the ROM font, no terminal driver needed |
| `template/` | what `./zx new <name> [--next]` copies: main loop, keyboard + Kempston input, logging, `test.py` |


## Tools

| | |
|---|---|
| `tools/mod2ay.py` | ProTracker MOD -> a `modplay` song: channel routing, instruments, pitch from each sample's waveform (`--help`) |

## Game layer

Linked from `build/zxsdk.lib`: a program only carries the modules it calls
(one object per file, so files are split where a game might want only
part). Tests: `projects/sdktest` (48K/128K) and `projects/sdktestnext`.

| header | module (src/) | what |
|---|---|---|
| `zxgame.h` | `fix88.c`, `fix88_tab.c` | 8.8 fixed point, sin/cos table, atan2, isqrt16 |
| | `rnd.c` | xorshift16 random numbers (deterministic) |
| | `frame.c`, `frame_isr.asm` | IM 2 frame timer, `frame_wait()` returns the frames passed (fixed-timestep loops), `frame_hook` |
| | `input.c`, `input_isr.asm` | keys (QAOP, cursor, Sinclair 1/2) + Kempston -> `IN_*` bits; presses latched in the interrupt (`input_latch`); `input_letter_pressed()` for typing |
| | `beep.asm`, `sound.c`, `ay_notes.c` | beeper effects; AY tone/noise, decaying effects, a small note-list music player |
| | `gfx48.c`, `spr_blit.asm` | 48K screen: row table, pre-shifted sprites (xor/or/clear/store), rects, tiles, attributes, big text, numbers |
| | `hiscore.c` | high-score table, the initials editor (`hs_edit_*`, any screen) |
| | `hiscore48.c` | the table and the initials screen on the 48K screen (`hs_draw`, `hs_enter`) |
| `modplay.h` | `modplay.c` | plays a MOD converted by `tools/mod2ay.py`: four channels routed to TurboSound chips, tone / noise / kick instruments; leaves free channels to `ay_sfx` |
| `fixmath.h` | `fixmath.asm`, `fixmath.c` | 16-bit fixed point for physics: `mul16` (16x16 -> 32, MUL on the Z80N), `mulq14`, `mulq8`, `isqrt32`, `div_q14`; inline C on a host compiler for host tests |
| `font5.h` | `font5.c` | a 5x7 font for drawing text yourself (Layer 2, a dot-matrix display), `font5_width` |
| `nextgfx.h` | `next/nextgfx.c` | Next: 28 MHz, Next registers, hardware sprites (`spr_show_quad` 32x32, patterns from memory pages), palettes (`pal_load`), TurboSound chip select |
| | `next/l2wide.c` | Layer 2 320x256 (`l2w_*`): mode setup, column mapping through any MMU slot, fills, 5x7 text with a dot-matrix option |
| | `next/nextl2.c` | Layer 2 256x192 double buffered: clear, fill, rect, blit, ROM-font text |
| | `next/l2text5.c` | `l2_text5`, `l2_text5_center`: the 5x7 font on Layer 2 |

`zxlog` and `zxtext` are linked into every z88dk project.

## Toolchain

`bin/zx-make` runs `make` in the `zx-dev-toolchain:1` image (official
`z88dk/z88dk` image, pinned, plus sjasmplus 1.24.0; native arm64/amd64). The
repo is mounted at `/work`, so `ZX_SDK` is `/work/sdk` inside the container.
Projects outside the repo work too (mounted at `/project`; keep
`ZX_SDK ?= ...` in their Makefile).

C builds use z88dk's newlib with zsdcc: `zcc +zx|+zxn -clib=sdcc_ix -SO3`,
one object per source file, linked with `-startup=31` (no terminal; set
`STARTUP=0` for a printf terminal) at `ORG=32768`, with a map file
(`-m`). Why `sdcc_ix`: it keeps IY free, which the ROM's IM 1 interrupt
handler needs, so `intrinsic_ei()` + `intrinsic_halt()` gives a 50 Hz loop.

Useful z88dk headers: `<arch/zx.h>` (screen addresses, `zx_cls`,
`zx_border`, colours), `<arch/zxn.h>` on the Next (includes `arch/zx.h`; Next
registers `ZXN_WRITE_REG/ZXN_READ_REG`, MMU), `<input.h>` (`in_inkey`,
`in_key_pressed(IN_KEY_SCANCODE_x)`, `in_stick_kempston`), `<intrinsic.h>`
(`intrinsic_ei/di/halt`), `<z80.h>` (`z80_outp/inp`). Docs:
<https://github.com/z88dk/z88dk/wiki>. Don't include both `arch/zx.h` and
`arch/zxn.h` (conflicting declarations).
