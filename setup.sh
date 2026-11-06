#!/usr/bin/env bash
# One-shot setup for the ZX Spectrum / Next dev environment. Safe to re-run.
#
#   ./setup.sh            everything, then build + test all projects
#   ./setup.sh --quick    skip the final build/test pass
#
# Requires: git, make, a C compiler, SDL2 (for the window; headless works
# without), Docker (any engine), Python >= 3.10 (or uv).
# macOS (Apple Silicon / Intel) and Linux (x86_64 / arm64). Windows: WSL2.
source "$(dirname "$0")/scripts/lib.sh"

QUICK=0
for a in "$@"; do
  case "$a" in
    --quick) QUICK=1 ;;
    -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option $a" ;;
  esac
done

step "prerequisites ($(host_os)/$(host_arch))"
missing=()
for t in git make cc; do have "$t" || missing+=("$t"); done
if [ ${#missing[@]} -gt 0 ]; then
  if [ "$(host_os)" = darwin ]; then
    die "missing: ${missing[*]} – run: xcode-select --install"
  else
    die "missing: ${missing[*]} – e.g. sudo apt install git build-essential"
  fi
fi
have docker || die "Docker is required for the z88dk/sjasmplus toolchain. Install Docker Desktop, Rancher Desktop, OrbStack or colima (macOS) / docker-ce (Linux)."
if ! have sdl2-config; then
  if [ "$(host_os)" = darwin ]; then warn "SDL2 missing: no emulator window (brew install sdl2), headless still works"
  else warn "SDL2 missing: no emulator window (sudo apt install libsdl2-dev), headless still works"; fi
fi
ok "git, make, cc, docker"

"$ZX_ROOT/scripts/build-toolchain-image.sh"
"$ZX_ROOT/scripts/build-emulator.sh"
"$ZX_ROOT/scripts/setup-python.sh"

if [ "$QUICK" = 0 ]; then
  step "build + test all projects"
  "$ZX_ROOT/zx" selftest
fi

cat <<EOF

${_c_g}ZX Spectrum / Next dev environment ready.${_c_0}

  ./zx run hello48         48K hello in a window (keys type into the Spectrum)
  ./zx run hellonext       Spectrum Next hello (.nex, Layer 2)
  ./zx new mygame [--next] start a project from the template
  ./zx test mygame         headless tests
  ./zx devbench --open     web UI + REST + MCP over HTTP (:3580)
  ./zx doctor              diagnose problems

Claude Code: open this directory with \`claude\`; the "zx" MCP server in .mcp.json
gives Claude build/boot/screenshot/input/memory/debugger tools (see CLAUDE.md).
EOF
