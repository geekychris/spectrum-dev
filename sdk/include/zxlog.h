/*
 * zxlog.h - debug log for programs running in the spectrum_dev harness.
 *
 * Text goes to ZEsarUX's ZXI debug port (OUT 0xCF3B,1 selects register 1,
 * then every OUT 0xDF3B,c is one character). The patched emulator captures
 * it: ./zx log, MCP emu_log / emu_run_until, Emulator.run_until() in tests.
 * On real hardware (or another emulator) the ports do nothing, so the calls
 * can stay in release builds; build with -DZX_NO_LOG to compile them out.
 *
 * Convention: one line per event, "<program>: <what> key=value...\n".
 * A program without a test.py passes `./zx test` by logging "ZX:DONE" (or
 * fails with "ZX:FAIL <why>").
 */
#ifndef ZXLOG_H
#define ZXLOG_H

#ifdef ZX_NO_LOG
#define zx_log(...)   ((void)0)
#define zx_log_s(s)   ((void)0)
#define zx_log_c(c)   ((void)0)
#else
/* printf-style (z88dk printf: %d/%u/%x are 16-bit, use %ld/%lu for long).
   Output is truncated to 127 characters. Pulls in vsnprintf (~1.5 KB). */
extern void zx_log(const char *fmt, ...);
/* A plain string: tiny and fast (no formatting code linked). */
extern void zx_log_s(const char *s) __z88dk_fastcall;
/* One character. */
extern void zx_log_c(unsigned char c) __z88dk_fastcall;
#endif

#endif
