# SDK

| | |
|---|---|
| `project.mk` | shared make rules; a project `Makefile` sets `NAME`, `TARGET` (`zx` = 48K/128K `.tap`, `zxn` = Next `.nex`) and includes it. `ASSEMBLER := sjasmplus` for pure assembly. Options are listed at the top of the file. |
| `include/zxlog.h`, `src/zxlog.c`, `src/zxlog_s.asm` | `zx_log(fmt, ...)`, `zx_log_s(s)`, `zx_log_c(c)`: debug log to the harness (ZXI port), a no-op elsewhere |
| `include/zxlog.inc` | `ZXLOG "text\n"` macro for sjasmplus |
| `include/zxtext.h`, `src/zxtext.c` | `zx_print_at(col, row, s)`, `zx_attr_at(col, row, len, attr)`: text with the ROM font, no terminal driver needed |
| `template/` | what `./zx new <name> [--next]` copies: main loop, keyboard + Kempston input, logging, `test.py` |

Everything in `src/` is linked into every z88dk project.

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
