# Shared helpers for spectrum_dev scripts. Source this; don't run it.
# shellcheck shell=bash

set -euo pipefail

ZX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export ZX_ROOT

# ---- pinned upstream versions (bump deliberately, then re-run setup) ----
ZESARUX_REPO="https://github.com/chernandezba/zesarux.git"
ZESARUX_COMMIT="0bb4277d714128311d78bc0625fb12ae8308899a"   # tag ZEsarUX-13.0

# Toolchain image: z88dk (pinned image tag) + sjasmplus (pinned git tag).
# Bump TOOLCHAIN_IMAGE's number whenever docker/Dockerfile changes.
Z88DK_IMAGE="z88dk/z88dk:sha-1ead955"
SJASMPLUS_REF="v1.24.0"
TOOLCHAIN_IMAGE="zx-dev-toolchain:1"

THIRD_PARTY="$ZX_ROOT/third_party"
ZESARUX_DIR="$THIRD_PARTY/zesarux"
# ZEsarUX looks for its ROMs and data files next to the binary, so it runs
# from its source tree.
ZESARUX_BIN="$ZESARUX_DIR/src/zesarux"

if [ -t 1 ]; then
  _c_b=$'\033[1m'; _c_g=$'\033[32m'; _c_y=$'\033[33m'; _c_r=$'\033[31m'; _c_0=$'\033[0m'
else
  _c_b=""; _c_g=""; _c_y=""; _c_r=""; _c_0=""
fi

step() { printf '%s==> %s%s\n' "$_c_b" "$*" "$_c_0"; }
ok()   { printf '%s  ✓ %s%s\n' "$_c_g" "$*" "$_c_0"; }
warn() { printf '%s  ! %s%s\n' "$_c_y" "$*" "$_c_0" >&2; }
die()  { printf '%s  ✗ %s%s\n' "$_c_r" "$*" "$_c_0" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

host_os()   { uname -s | tr '[:upper:]' '[:lower:]'; }   # darwin | linux
host_arch() { uname -m; }                                  # arm64 | x86_64 | aarch64

ncpu() { getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4; }

# Clone `repo` into `dir` and check out exactly `commit` (shallow).
git_checkout_pinned() {
  local repo="$1" dir="$2" commit="$3"
  if [ ! -d "$dir/.git" ]; then
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$repo"
  fi
  if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" != "$commit" ]; then
    git -C "$dir" fetch -q --depth 1 origin "$commit"
    git -C "$dir" checkout -q -f FETCH_HEAD
  fi
}
