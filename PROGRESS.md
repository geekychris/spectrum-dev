# Progress

Phase 1: infrastructure (emulator harness, toolchain, host tool + MCP, SDK, docs).
Porting games is phase 2.

## 2026-11-06

- [x] Evaluated ZEsarUX 13.0 ZRCP: has registers, memory, breakpoints,
      membreakpoints, cpu-step, snapshots, smartload (.tap/.nex/.sna/.z80),
      set-machine, keyboard matrix + joystick injection (set-ui-io-ports),
      BMP screenshots of the rendered frame (save-screen). Missing for agents:
      deterministic "run N frames" (its `run` counts opcodes, is paced by the
      host clock and polls the socket every opcode), a capturable guest log,
      run-until-log.
- [x] Patch (emulator/patches, emulator/ext/zxh.c): frame counter, no-wait
      deterministic mode, debug-log ring fed by ZXI port register 1
      (OUT 0xCF3B,1 / OUT 0xDF3B,char), `zxh-run/zxh-log/zxh-status/...`
      ZRCP commands. ~44x real time on 48K, ~9x on Next (headless).
- [x] Native macOS arm64 build with Homebrew SDL2 (configure patch).
- [x] Toolchain image zx-dev-toolchain:1 = z88dk/z88dk:sha-1ead955 + sjasmplus 1.24.0.
- [ ] Python host (zxdev): ZRCP client, Emulator, sessions, CLI, MCP, devbench
- [ ] SDK: project.mk, zx_log, template, hello48 + hellonext with tests
- [ ] Docs, selftest, screenshots
