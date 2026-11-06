#!/usr/bin/env bash
# One-line installer:
#
#   curl -fsSL https://raw.githubusercontent.com/geekychris/spectrum-dev/main/scripts/install.sh | bash
#
# Checks prerequisites, clones (or updates) the repo, runs ./setup.sh.
# Knobs (environment):
#   ZX_DEV_REPO   git URL      (default https://github.com/geekychris/spectrum-dev.git)
#   ZX_DEV_SRC    install dir  (default ~/spectrum-dev)
#   ZX_DEV_REF    branch/tag   (default main)
#   ZX_DEV_QUICK=1             skip the test suite
#   ZX_DEV_START=0             don't open hello48 in a window at the end
set -euo pipefail
REPO="${ZX_DEV_REPO:-https://github.com/geekychris/spectrum-dev.git}"
SRC="${ZX_DEV_SRC:-$HOME/spectrum-dev}"
REF="${ZX_DEV_REF:-main}"

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31m  ✗ %s\033[0m\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

say "checking prerequisites"
for t in git make cc docker; do have "$t" || die "missing $t (macOS: xcode-select --install + a Docker engine; Linux: build-essential git docker.io)"; done
docker info >/dev/null 2>&1 || die "Docker is installed but not running"
have python3 || have uv || die "need Python >= 3.10 or uv"
have sdl2-config || printf '  ! SDL2 not found: the emulator window needs it (brew install sdl2 / apt install libsdl2-dev)\n'

if [ -d "$SRC/.git" ]; then
  say "updating $SRC"
  git -C "$SRC" fetch -q origin "$REF" && git -C "$SRC" checkout -q "$REF" && git -C "$SRC" pull -q --ff-only
else
  say "cloning into $SRC"
  git clone -q --branch "$REF" "$REPO" "$SRC"
fi
cd "$SRC"
if [ "${ZX_DEV_QUICK:-0}" = 1 ]; then ./setup.sh --quick; else ./setup.sh; fi
if [ "${ZX_DEV_START:-1}" = 1 ] && have sdl2-config; then
  say "opening hello48 (close the window to finish)"
  ./zx run hello48 || true
fi
