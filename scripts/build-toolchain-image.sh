#!/usr/bin/env bash
# Build the toolchain image: z88dk (official image, pinned) + sjasmplus.
# Native for the host architecture (z88dk publishes amd64 and arm64).
source "$(dirname "$0")/lib.sh"

step "toolchain image $TOOLCHAIN_IMAGE (z88dk + sjasmplus $SJASMPLUS_REF)"
have docker || die "Docker is required (Docker Desktop, Rancher Desktop, OrbStack, colima, or docker-ce)"
docker info >/dev/null 2>&1 || die "Docker is installed but not running – start it and retry"
if docker image inspect "$TOOLCHAIN_IMAGE" >/dev/null 2>&1 && [ "${1:-}" != "--force" ]; then
  ok "$TOOLCHAIN_IMAGE present (--force to rebuild)"
  exit 0
fi
mkdir -p "$ZX_ROOT/build"
log="$ZX_ROOT/build/toolchain-image.log"
docker build -t "$TOOLCHAIN_IMAGE" \
  --build-arg Z88DK_IMAGE="$Z88DK_IMAGE" --build-arg SJASMPLUS_REF="$SJASMPLUS_REF" \
  "$ZX_ROOT/docker" >"$log" 2>&1 || { tail -30 "$log"; die "docker build failed (full log: $log)"; }
docker run --rm "$TOOLCHAIN_IMAGE" sh -c 'zcc 2>&1 | head -1; sjasmplus --version | head -1' | sed 's/^/    /'
ok "built $TOOLCHAIN_IMAGE"
