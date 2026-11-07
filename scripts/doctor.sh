#!/usr/bin/env bash
# Check every piece of the environment; print a fix for anything broken.
source "$(dirname "$0")/lib.sh"
set +e
bad=0
fail() { warn "$1"; printf '      fix: %s\n' "$2" >&2; bad=$((bad+1)); }

step "spectrum_dev doctor ($(host_os)/$(host_arch))"
for t in git make cc; do have "$t" && ok "$t" || fail "$t missing" "xcode-select --install (macOS) / apt install build-essential git"; done

if have docker && docker info >/dev/null 2>&1; then
  ok "docker running"
  if docker image inspect "$TOOLCHAIN_IMAGE" >/dev/null 2>&1; then
    v="$(docker run --rm "$TOOLCHAIN_IMAGE" sh -c 'zcc 2>&1 | head -1 | sed "s/.*- v/v/"; sjasmplus --version 2>&1 | head -1 | sed "s/.*v\([0-9.]*\).*/sjasmplus \1/"' | tr '\n' ' ')"
    ok "toolchain $TOOLCHAIN_IMAGE: $v"
  else
    fail "toolchain image $TOOLCHAIN_IMAGE missing" "scripts/build-toolchain-image.sh"
  fi
else
  fail "docker not installed or not running" "start Docker Desktop / Rancher / OrbStack / colima"
fi

have sdl2-config && ok "SDL2 $(sdl2-config --version)" \
  || fail "SDL2 missing (window mode unavailable; headless works)" "brew install sdl2 / sudo apt install libsdl2-dev, then scripts/build-emulator.sh"

if [ -x "$ZESARUX_BIN" ]; then
  have_rev="$(git -C "$ZESARUX_DIR" rev-parse HEAD 2>/dev/null)"
  [ "$have_rev" = "$ZESARUX_COMMIT" ] && ok "ZEsarUX built at the pinned commit" \
    || fail "ZEsarUX checkout is $have_rev, expected $ZESARUX_COMMIT" "scripts/build-emulator.sh"
  grep -q "zxh_remote_command" "$ZESARUX_DIR/src/zrcp/remote.c" 2>/dev/null && ok "zxh harness patch applied" \
    || fail "zxh patch not applied" "scripts/build-emulator.sh"
  grep -q "Enabled Video output drivers:.*sdl" "$ZESARUX_DIR/build.log" 2>/dev/null && ok "emulator has the SDL window driver" \
    || warn "emulator built without SDL (headless only)"
else
  fail "emulator not built" "scripts/build-emulator.sh"
fi

if [ -x "$ZX_ROOT/.venv/bin/python" ] && "$ZX_ROOT/.venv/bin/python" -c "import zxdev.emu, zxdev.mcp_server, zxdev.devbench" 2>/dev/null; then
  ok "python env (.venv) with zxdev"
else
  fail "python env missing or broken" "scripts/setup-python.sh"
fi
if [ -x "$ZX_ROOT/.venv/bin/python" ] && "$ZX_ROOT/.venv/bin/python" -c "import numpy, PIL" 2>/dev/null; then
  ok "python env has numpy + Pillow (sdk/tools, asset tools)"
else
  fail "numpy or Pillow missing from .venv (sdk/tools need them)" "scripts/setup-python.sh"
fi

if [ -x "$ZESARUX_BIN" ] && [ -x "$ZX_ROOT/.venv/bin/python" ]; then
  if "$ZX_ROOT/.venv/bin/python" - <<'PY' 2>/dev/null
from zxdev.emu import Emulator
with Emulator() as e:
    r = e.run(50)
    assert r["frames"] == 50, r
    assert "zxh-status" in e.raw("zxh-help")
PY
  then ok "headless emulator boots and runs 50 frames"
  else fail "headless emulator smoke test failed" "scripts/build-emulator.sh; then .venv/bin/zxdev log hello48"
  fi
fi

stale="$(ls "$ZX_ROOT"/build/sessions/*.json 2>/dev/null | wc -l | tr -d ' ')"
[ "$stale" = 0 ] || ok "$stale session(s) registered (./zx sessions)"

[ "$bad" -eq 0 ] && ok "all good" || die "$bad problem(s)"
