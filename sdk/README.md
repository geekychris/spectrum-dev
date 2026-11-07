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
| `tools/levelpack.py` | ASCII rooms (a `size`, a `legend` of character -> tile id, `room NAME` + rows) -> zxrle-packed C maps, names and a header for `tmap_load`; `parse` / `pack_rooms` as a module (`--help`) |
| `tools/l2fill.py` | `l2fill.h`'s polygons on the host, pixel for pixel: host pictures a test can compare the Next's Layer 2 with |
| `tools/zxrle.py` | `pack` / `unpack` for `zxrle.h` (PackBits-style runs and literals) |
| `tools/runimg.py` | `encode` / `decode` for `l2wrun.h`: column runs of pen offsets (shapes drawn on the host for every view, e.g. Stack Tower's 3D bricks) |
| `tools/fatimg.py` | SD card images: read a FAT12/16 image (with or without an MBR) into a tree, change it (`drop`, `put`), write a FAT16 image with an MBR (8.3 + long names, reproducible); `ls` / `cat` from the command line |
| `tools/nextbasic.py` | text -> tokenised (Next)BASIC (as NextZXOS stores it, dot commands kept as text), +3DOS headers for programs and code (`plus3dos_basic`, `plus3dos_code`) |
| `tools/test_tools.py` | their tests (`./zx selftest` runs them) |

## Game layer

Linked from `build/zxsdk.lib`: a program only carries the modules it calls
(one object per file, so files are split where a game might want only
part). Tests: `projects/sdktest` (48K/128K), `projects/nextruntest` (`l2wrun`, `dma_copy_pages`, `sprtext`), `projects/sdktest48gfx` (the 48K drawing modules and fixmath's sin14 / muls / rot2d, checked pixel by pixel), `projects/sdktestnext`, `projects/nextlibtest`, `projects/splittest` (split screens, two-player input) and `projects/geomtest` (polygons, bands, projection).

| header | module (src/) | what |
|---|---|---|
| `zxgame.h` | `fix88.c`, `fix88_tab.c` | 8.8 fixed point, sin/cos table, atan2, isqrt16 |
| | `rnd.c` | xorshift16 random numbers (deterministic) |
| | `frame.c`, `frame_isr.asm` | IM 2 frame timer, `frame_wait()` returns the frames passed (fixed-timestep loops), `frame_hook` |
| | `frame_exit.c` | `frame_exit()`: back to IM 1 (I = 0x3F) before code returns to BASIC |
| | `input.c`, `input_isr.asm` | keys (QAOP, cursor, Sinclair 1/2) + Kempston -> `IN_*` bits; presses latched in the interrupt (`input_latch`); `input_letter_pressed()` for typing |
| | `input_fast.c` | `input_fast()`: optional table decode of the keys (same bits, ~9,000 T-states less a read, ~300 bytes) |
| | `beep.asm`, `sound.c`, `ay_notes.c` | beeper effects; AY tone/noise, decaying effects, a small note-list music player |
| | `gfx48.c`, `spr_blit.asm` | 48K screen: row table, pre-shifted sprites (xor/or/clear/store), rects, tiles, attributes, big text, numbers |
| | `hiscore.c` | high-score table, the initials editor (`hs_edit_*`, any screen) |
| | `hiscore48.c` | the table and the initials screen on the 48K screen (`hs_draw`, `hs_enter`) |
| | `text_xor.c` | `text_xor`: ROM-font text XORed onto any pixel line (8x8 or 8x16), for HUDs over XOR graphics |
| `line48.h` | `line48.asm`, `line48.c` | 48K lines in asm: eight octant Bresenham loops (~80 T-states a pixel), XOR / OR / CLEAR, `line_draw`, `line_clip` (int16, clipped), `line_poly` (wraps around the screen: x mod 256, y mod 192), `plot_list`; compiled polygons (`poly_compile` / `poly_draw`, ~200 T-states an edge less) and an XOR display list (`poly_sync`: redraw only what moved). Row table at 0xF200-0xF3FF |
| `vec48.h` | `vec48.c` | vector shapes for line48: `vec_rotate`, `vec_scale`, rotation tables (`vec_rot_table`, compiled: `vec_poly_table`) |
| `hit48.h` | `hit48.asm`, `hit48.c` | circle collisions in asm: `hit_first(x, y, r, list, n)` against a list of {x, y, r, id} (16-bit coordinates) |
| `sprclip.h` | `sprclip.asm` | 48K: XOR a pre-shifted sprite at a signed x, clipped at both edges and the bottom (objects that wrap or slide off the left); ~2300 T-states for 16 x 16; `asm_sprc_xor` for assembly callers |
| `modplay.h` | `modplay.c` | plays a MOD converted by `tools/mod2ay.py`: four channels routed to TurboSound chips, tone / noise / kick instruments; leaves free channels to `ay_sfx` |
| `fixmath.h` | `fixmath.asm`, `fixmath.c` | 16-bit fixed point for physics: `mul16` (16x16 -> 32, MUL on the Z80N), `mulq14`, `mulq8`, `isqrt32`, `isqrt32_fast` (the same, ~3x faster: seed table + one Newton step, in `fixmath_isqrt.asm`), `div_q14`; inline C on a host compiler for host tests |
| | `fixmath_sin14.c`, `fixmath_muls.asm` | `fix_sin14` / `fix_cos14` (Q14 table), `muls` (fast multiply by a small factor, shift-and-add) |
| | `rot2d.c`, `rot2d.asm` | `rot2d_set(angle, sx, sy)` + `rot2d(in, out, n)`: rotate int8 point lists with separate x and y scales, in asm, ~1,000 T-states a point: for shapes rotated every frame (vec48's `vec_rotate` is the start-up, uniform-scale one) |
| `bitline.h` | `bitline.asm`, `bitline.c` | lines into any 1-bit bitmap through a row-address table (the screen, or an off-screen buffer such as a ship image), clipped to a box, no wrap-around; `bl_poly` draws each vertex once in XOR; ~45 T-states a pixel inside the box. For screen-only work with wrap-around and compiled polygons use `line48.h` |
| `dots48.h` | `dots48.asm` | XOR lists of points (stars, sparks): XOR the same list again to erase. 60 bytes, no tables (line48's `plot_list` needs its 512-byte row table) |
| `hmap48.h` | `hmap48.c`, `hmap48_fill.asm` | heightmap terrain: surface line, dithered fill below it, cells coloured sky / surface / ground |
| `gauge48.h` | `gauge48.c` | a bar gauge (fuel, energy) on a character row: only the changed pixels redrawn |
| `attrtint.h` | `attrtint.c`, `attrtint_capture.asm` | colour for moving objects: tint the plain (open-sky) cells under them, restore them after; scenery cells keep their colours (a 96-byte bitmap of plain cells) |
| `zxgame.h` | `spr32.asm` | `spr32_shift`: shift a 32-pixel-wide image into place (draw once, blit anywhere without 8 pre-shifted copies) |
| `fix16.h` | `fix16.c`, `fix16.asm`, `fix16_tab.c` | the amiga3d engine's 16.16 fixed point bit for bit (`fmul`, `fdiv` in Z80N asm, `fhypot`, `fsqrt`, `a3_sin`, `a3_angle`): amiga3d games' rules ported unchanged (Marble Tilt's whole-game hash matches its tests); `fix16_model.h` + `tools/test_fix16.c` check the asm's steps on the host; test: `projects/fixfilltest` |
| `vec3.h` | `vec3.asm`, `vec3.c` | Q14 3-vectors with exact 32-bit products: `v3_dot`, `v3_cross`, `v3_scale`, `v3_mat`, `v3_norm` (the usual fixed-point normalise, bit for bit), C twins on the host (Planet Chomp's rules match the original's 32-bit code with them) |
| `font5.h` | `font5.c` | a 5x7 font for drawing text yourself (Layer 2, a dot-matrix display), `font5_width` |
| `nextgfx.h` | `next/nextgfx.c` | Next: 28 MHz, Next registers, hardware sprites (`spr_show_quad` 32x32, patterns from memory pages), palettes (`pal_load`), TurboSound chip select |
| | `next/l2wide.c` | Layer 2 320x256 (`l2w_*`): mode setup, column mapping through any MMU slot, fills, 5x7 text with a dot-matrix option |
| | `next/l2wtext.c` | `l2w_text`: ROM-font text (lower case too) on 320x256, 8 or 6 pixels a character |
| | `next/nextl2.c` | Layer 2 256x192 double buffered: clear (DMA), fill, rect, blit, ROM-font text, `l2_copy_to_back` (a screen kept in other banks, by DMA) |
| | `next/dma_pages.c` | `dma_copy_pages(dst, src, n)`: 8K pages by DMA (e.g. a band of 32-column pages of the 320 mode, a cached part of the screen) |
| | `next/dma.c` | zxnDMA: `dma_copy`, `dma_fill`, whole banks (`dma_copy_banks`, `dma_fill_banks`) at ~2 T-states a byte; `dma_to_port` (memory -> sprite patterns 0x5B, attributes 0x57) |
| | `next/l2text5.c` | `l2_text5`, `l2_text5_center`: the 5x7 font on Layer 2 |
| `l2rle.h` | `next/l2rle.c`, `next/l2rle_blit.asm` | Layer 2 320x256 extras: column-RLE "pieces" (`nextpic.piece`) blitted through a colour table (asm), `l2w_span` (and/or a row: shadows with a half-bright upper palette), `l2w_clear` (DMA), `l2w_plot` |
| `l2wrun.h` | `next/l2wrun.asm` | Layer 2 320 x 256: column-run images (`tools/runimg.py`) drawn as base pen + offset, so one image draws in any colour set laid out alike in the palette; mirrored too (a shape and its mirror image from one picture); ~28 T-states a pixel. For 3D drawn on the host per view (projects/stack_tower) |
| `sprtext.h` | `next/sprtext.c` | big text from hardware sprites: the 5x7 font made into patterns at start-up (with a shadow), one sprite a character, zoomed x2-x8: words over a double-buffered Layer 2 that never need erasing |
| `nextspr.h` | `next/nextspr.c` | sprite groups: cols x rows 16x16 patterns uploaded by DMA, shown as one object (`sprg_show_ex`: hardware zoom, palette offset; `sprg_hide`: a range); `sprg_mask_rle` hides the parts behind a Layer 2 piece (isometric occlusion) |
| `nextcopper.h` | `next/nextcopper.c` | copper lists (`copper_wait`, `copper_move`, `copper_end` (sent by DMA), `copper_stop` / `copper_run`): per-line register writes, e.g. palette switches and gradients. The copper restarts at the vertical blank, line 248 |
| `nexttile.h` | `next/nexttile.c` | the tilemap as a 40x32 text/HUD layer over everything: 1-bit fonts -> 4-bit tiles, glyphs at 1-4x with a drop shadow (`tm_glyphnx`, `tm_glyph2x`), print / fill, a palette offset per cell |
| `palfx.h` | `next/palfx.c` | palettes loaded with an effect (`palfx_load_fx`: c * mul / 8 + add, i.e. darken, half-bright copies, flashes, fades), second palettes, `palfx_active`; `palfx_load_bytes` (raw bytes by DMA, for palettes rewritten every frame) |
| `aystream.h` | `next/aystream.c` | TurboSound register-stream player: music and effects made on the host (`tools/aystream.py`), one stream a chip, banked, looping, attenuation (ducking) |
| `input2.h` | `input2.c` | input for two players: each reads only their own key sets (QAOP, WASD, IJKL, cursor), Sinclair 1/2 and Kempston 1/2 (`in2_scan` once a frame, `in2_update(&pad)`: held + pressed); 48K and Next |
| `nextsplit.h` | `next/nextsplit.c` | split screens with the copper: horizontal panes with their own clip windows (sprites, Layer 2, tilemap) and `split_row` for register moves before a given row; a list can be rebuilt every frame and uploaded in the vertical blank |
| `zxrle.h` | `zxrle.c` | run-length unpacking (`rle_unpack`) of data packed by `tools/zxrle.py`: level maps and other data with long runs |
| `tmap.h` | `tmap.c` | one tile map with a row table: `tmap_load` (a `tools/levelpack.py` stream), `TMAP(c, r)`, `tmap_at` (bounds-checked), `tmap_find`, `tmap_count`, `tmap_replace`; builds on the host too |
| `plat.h` | `plat.c`, `plat_kind.asm` | platformer movement against tmap: `plat_move_x` / `plat_move_y` (move X then Y, test the box's corners, snap to the tile edge, gravity, max fall, `on_ground`), `plat_kind_at` / `plat_solid_at`; tile and body size and a solid table are parameters. It is Miner Meteor's original code, snaps included; builds on the host too |
| `cellspr48.h` | `cellspr48.c`, `cellspr48_spr.asm`, `cellspr48_out.asm`, `cellspr48_ref.asm` | 48K tile room + sprites composed per character cell (tile, then each sprite ORed in from a pre-shifted copy, written once: no flicker; a sprite inks the cells it covers). `cs_update` redraws only the cells that changed (moved sprites, `cs_set`, `cs_mark`); `cs_refresh` rewrites a few lines of animated tiles (belts) without a compose. Tests: projects/sdktestroom |
| `l2poly.h` | `next/l2poly.asm`, `next/l2poly_c.c` | filled convex polygons (any winding, 16-bit coordinates, clipped), spans, rectangles (whole rows by one DMA a page) and *bands* on the 256 x 192 Layer 2 back buffer, in Z80N asm: edges step in 16.8 with slopes from a reciprocal table and MUL; short spans by an unrolled store, long ones by the zxnDMA. A band fills many strips that share their top and bottom rows (a road's cross-section between two depths) in one row loop. ScAIry Fun draws its road with it. Test: `projects/geomtest` |
| `persp.h` | `next/persp.asm`, `next/persp.c` | perspective for pseudo-3D: `persp_scale(z)` = fov x 4096 / z (one division a depth, 16 rounds), then `persp_mul`, `persp_x1/y1`, `persp_xs`, `persp_xy` (a MUL a coordinate, clamped for l2poly); inline C on a host compiler. Test: `projects/geomtest` |
| `l2wpoly.h` | `next/l2wpoly.c` | convex polygons and rectangles on 320x256 column-major planes (Layer 2 or a copy in other pages) as vertical spans (DMA for long ones), clipped, optionally writing a depth key per pixel into a depth plane; plane-to-plane copies and fills; through any two MMU slots (`l2wp_slots`) |
| `nextsprl.h` | `next/nextsprl.c` | the frame's sprites as one list of 16x16 4-bit patterns (128 slots) with palette offsets, sent by DMA, leftovers hidden; `spl_zmask` cuts a pattern where `l2wpoly`'s depth plane is nearer (sprites behind Layer 2 geometry) |
| `l2fill.h` | `next/l2fill.asm` | Layer 2 256x192: convex polygons for scenes of many small ones (Marble Tilt's board): edges stepped into a row table, short spans unrolled, long ones by DMA, any `l2_back` bank; test: `projects/fixfilltest` |
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
