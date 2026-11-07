# SDK

| | |
|---|---|
| `project.mk` | shared make rules; a project `Makefile` sets `NAME`, `TARGET` (`zx` = 48K/128K `.tap`, `zxn` = Next `.nex`) and includes it. `ASSEMBLER := sjasmplus` for pure assembly; `OUTPUT := bin` (Next) for plain code to load from BASIC and run with USR (projects/arcade). Options are listed at the top of the file. |
| `include/zxlog.h`, `src/zxlog.c`, `src/zxlog_s.asm` | `zx_log(fmt, ...)`, `zx_log_s(s)`, `zx_log_c(c)`: debug log to the harness (ZXI port), a no-op elsewhere |
| `include/zxlog.inc` | `ZXLOG "text\n"` macro for sjasmplus |
| `include/zxtext.h`, `src/zxtext.c` | `zx_print_at(col, row, s)`, `zx_attr_at(col, row, len, attr)`: text with the ROM font, no terminal driver needed |
| `include/sdfile.h`, `src/next/sdfile.c` | Next, from NextZXOS: files on the SD card through esxDOS: `sd_load`, `sd_save`, `sd_load_l2w` (a column-major picture straight into Layer 2 320 x 256). Tested by projects/arcade |
| `include/listui.h`, `src/next/listui.c` | Next: a scrolling list on Layer 2 320 x 256 (ROM font, selection bar, right-aligned tags, scroll marks); projects/arcade |
| `template/` | what `./zx new <name> [--next]` copies: main loop, keyboard + Kempston input, logging, `test.py` |


## Tools

| | |
|---|---|
| `tools/mod2ay.py` | ProTracker MOD -> a `modplay` song: channel routing, instruments, pitch from each sample's waveform (`--help`) |
| `tools/nextpic.py` | pictures -> Layer 2 banks (320 or 256 mode) + the `BANK_n` asm + a palette, sprite sheets -> patterns (16x16 or 32x32 quads); also a Python module (`rgb9`, `l2_bytes`, `write_banks`, `quad_patterns`; for pictures made in Python: `quantize`, `nearest`, `piece` / `rle_columns` (column RLE), `group_patterns`; boing_pinball's and spectral_keep's gen_gfx.py use it) |
| `tools/aystream.py` | AY register streams for `aystream.h`: an `Ay` you set tones/noise on frame by frame, `encode` keeps only the changes and runs of quiet frames |
| `tools/zxrle.py` | `pack` / `unpack` for `zxrle.h` (PackBits-style runs and literals) |
| `tools/fatimg.py` | SD card images: read a FAT12/16 image (with or without an MBR) into a tree, change it (`drop`, `put`), write a FAT16 image with an MBR (8.3 + long names, reproducible); `ls` / `cat` from the command line |
| `tools/nextbasic.py` | text -> tokenised (Next)BASIC (as NextZXOS stores it, dot commands kept as text), +3DOS headers for programs and code (`plus3dos_basic`, `plus3dos_code`) |
| `tools/test_tools.py` | their tests (`./zx selftest` runs them) |

## Game layer

Linked from `build/zxsdk.lib`: a program only carries the modules it calls
(one object per file, so files are split where a game might want only
part). Tests: `projects/sdktest` (48K/128K), `projects/sdktestnext` and `projects/nextlibtest`.

| header | module (src/) | what |
|---|---|---|
| `zxgame.h` | `fix88.c`, `fix88_tab.c` | 8.8 fixed point, sin/cos table, atan2, isqrt16 |
| | `rnd.c` | xorshift16 random numbers (deterministic) |
| | `frame.c`, `frame_isr.asm` | IM 2 frame timer, `frame_wait()` returns the frames passed (fixed-timestep loops), `frame_hook` |
| | `frame_exit.c` | `frame_exit()`: back to IM 1 (I = 0x3F) before code returns to BASIC |
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
| | `next/l2wtext.c` | `l2w_text`: ROM-font text (lower case too) on 320x256, 8 or 6 pixels a character |
| | `next/nextl2.c` | Layer 2 256x192 double buffered: clear (DMA), fill, rect, blit, ROM-font text, `l2_copy_to_back` (a screen kept in other banks, by DMA) |
| | `next/dma.c` | zxnDMA: `dma_copy`, `dma_fill`, whole banks (`dma_copy_banks`, `dma_fill_banks`) at ~2 T-states a byte; `dma_to_port` (memory -> sprite patterns 0x5B, attributes 0x57) |
| | `next/l2text5.c` | `l2_text5`, `l2_text5_center`: the 5x7 font on Layer 2 |
| `l2rle.h` | `next/l2rle.c`, `next/l2rle_blit.asm` | Layer 2 320x256 extras: column-RLE "pieces" (`nextpic.piece`) blitted through a colour table (asm), `l2w_span` (and/or a row: shadows with a half-bright upper palette), `l2w_clear` (DMA), `l2w_plot` |
| `nextspr.h` | `next/nextspr.c` | sprite groups: cols x rows 16x16 patterns uploaded by DMA, shown as one object; `sprg_mask_rle` hides the parts behind a Layer 2 piece (isometric occlusion) |
| `nextcopper.h` | `next/nextcopper.c` | copper lists (`copper_wait`, `copper_move`, `copper_end`, `copper_stop` / `copper_run`): per-line register writes, e.g. palette switches and gradients. The copper restarts at the vertical blank, line 248 |
| `nexttile.h` | `next/nexttile.c` | the tilemap as a 40x32 text/HUD layer over everything: 1-bit fonts -> 4-bit tiles, 2x glyphs with a drop shadow, print / fill, a palette offset per cell |
| `palfx.h` | `next/palfx.c` | palettes loaded with an effect (`palfx_load_fx`: c * mul / 8 + add, i.e. darken, half-bright copies, flashes, fades), second palettes, `palfx_active` |
| `aystream.h` | `next/aystream.c` | TurboSound register-stream player: music and effects made on the host (`tools/aystream.py`), one stream a chip, banked, looping, attenuation (ducking) |
| `zxrle.h` | `zxrle.c` | run-length unpacking (`rle_unpack`) of data packed by `tools/zxrle.py`: level maps and other data with long runs |
| `isosort.h` | `isosort.c` | isometric draw order for boxes (separating axis + depth key, then a depth-first sort); `iso_behind` for "is this scenery in front of the sprite" |

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
