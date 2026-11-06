/*
 * zxh - spectrum_dev harness extension for ZEsarUX.
 *
 * Copied to src/zrcp/zxh.{c,h} by scripts/build-emulator.sh and compiled as
 * part of zrcp/remote.c (remote.c #includes zxh.c at its end, so no Makefile
 * changes are needed). The hooks below are called from a handful of places
 * in the emulator; see emulator/patches/README.md.
 *
 * GPL-3.0-or-later, like ZEsarUX.
 */
#ifndef ZXH_H
#define ZXH_H

/* 1 while the harness runs frames as fast as possible (zxh-run, zxh-turbo):
   no wall-clock pacing, every frame rendered, 20 ms housekeeping tied to
   emulated frames instead of the host clock. */
extern int zxh_nowait;

/* Completed emulated frames since start (never reset). */
extern unsigned long zxh_frames;

/* ula.c: a byte written to ZXI register 1 (HARDWARE_DEBUG_ASCII). */
extern void zxh_debug_char(unsigned char c);

/* core_spectrum.c: end of every emulated frame (before the pacing check). */
extern void zxh_end_frame(void);

/* timer.c: filters get_timer_check_interrupt(); in nowait mode the "20 ms
   passed" tick comes from emulated frames, not the host clock. */
extern int zxh_timer_tick(int host_tick);

/* screen.c: nonzero = render this frame (no frameskip). */
extern int zxh_force_render(void);

/* core_spectrum.c, screen.c: nonzero on the main thread. Host keyboard,
   joystick and window refresh are skipped while ZRCP (zxh-run, cpu-step...)
   drives the core from its network thread: SDL/AppKit allow them only on the
   main thread (macOS aborts otherwise). The rendered frame (rainbow buffer,
   used by screenshots) is unaffected. */
extern int zxh_host_io_allowed(void);

/* tbblue.c: value of NextReg 0xB0 / 0xB1 (extended keys), set by zxh-nextkeys. */
extern int zxh_next_extkeys(unsigned char reg);

/* core_spectrum.c: start of every core loop iteration (instruction boundary);
   records input-state changes while zxh-rec is on. */
extern void zxh_input_poll(void);

/* remote.c: dispatch for commands starting with "zxh-". */
extern void zxh_remote_command(int misocket, char *command, char *params);

#endif
