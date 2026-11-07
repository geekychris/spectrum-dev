#!/usr/bin/env bash
# Build ZEsarUX natively with the zxh harness (deterministic frame stepping,
# captured debug log, zxh-* remote commands). See emulator/patches/README.md.
# Output: third_party/zesarux/src/zesarux (built in third_party/zesarux-build)
#
# Needs: a C compiler + make, SDL2 (macOS: brew install sdl2; Debian/Ubuntu:
# sudo apt install libsdl2-dev). Without SDL2 it still builds, headless only.
source "$(dirname "$0")/lib.sh"

step "ZEsarUX @ ${ZESARUX_COMMIT:0:10} (13.0) + zxh harness"
have git  || die "git is required"
have make || die "make is required (macOS: xcode-select --install; Linux: build-essential)"
have cc   || die "a C compiler is required (macOS: xcode-select --install; Linux: build-essential)"
have sdl2-config || warn "sdl2-config not found: building without a window (macOS: brew install sdl2; Debian: apt install libsdl2-dev)"

# The live tree (third_party/zesarux: the binary, its ROMs and data) is shared
# by every worktree and by running emulators, so it is never cleaned or built
# in place: the build happens in a private clone and the binary is swapped in
# with a rename (atomic: running emulators keep their old file, new ones get
# the new one). Overwriting a running executable in place can crash it on macOS.
git_checkout_pinned "$ZESARUX_REPO" "$ZESARUX_DIR" "$ZESARUX_COMMIT"   # first install only
BUILD_DIR="$THIRD_PARTY/zesarux-build"
[ -d "$BUILD_DIR/.git" ] || git clone -q "$ZESARUX_DIR" "$BUILD_DIR"
git -C "$BUILD_DIR" fetch -q "$ZESARUX_DIR" "$ZESARUX_COMMIT" 2>/dev/null || true
git -C "$BUILD_DIR" -c advice.detachedHead=false checkout -q -f "$ZESARUX_COMMIT"

# Always start from a pristine build tree so patches apply cleanly and repeatably.
git -C "$BUILD_DIR" reset -q --hard
git -C "$BUILD_DIR" clean -q -fdx
cp "$ZX_ROOT"/emulator/ext/zxh.[ch] "$BUILD_DIR/src/zrcp/"
for p in "$ZX_ROOT"/emulator/patches/*.patch; do
  git -C "$BUILD_DIR" apply --whitespace=nowarn "$p" || die "patch failed: $(basename "$p")"
  ok "applied $(basename "$p")"
done

# Only the drivers we use: SDL2 window + audio (or null), coreaudio on macOS.
cfg=(--enable-sdl2 --disable-cocoa --disable-xwindows --disable-caca --disable-aa
     --disable-cursesw --disable-curses --disable-fbdev --disable-pulse --disable-dsp
     --disable-sndfile)
[ "$(host_os)" = darwin ] || cfg+=(--disable-coreaudio)
log="$BUILD_DIR/build.log"
( cd "$BUILD_DIR/src" && CFLAGS="-O2" ./configure "${cfg[@]}" ) >"$log" 2>&1 \
  || { tail -30 "$log"; die "configure failed (full log: $log)"; }
grep -q "Enabled Video output drivers:.*sdl" "$log" || warn "SDL2 not detected: window mode unavailable"
make -C "$BUILD_DIR/src" -j"$(ncpu)" >>"$log" 2>&1 \
  || { tail -30 "$log"; die "build failed (full log: $log)"; }
[ -x "$BUILD_DIR/src/zesarux" ] || die "no binary at $BUILD_DIR/src/zesarux"

# install: copy next to the live binary, then rename over it (same filesystem)
tmp="$ZESARUX_BIN.new.$$"
cp "$BUILD_DIR/src/zesarux" "$tmp" && chmod +x "$tmp" && mv -f "$tmp" "$ZESARUX_BIN" \
  || { rm -f "$tmp"; die "could not install $ZESARUX_BIN"; }
cp "$log" "$ZESARUX_DIR/build.log.new.$$" && mv -f "$ZESARUX_DIR/build.log.new.$$" "$ZESARUX_DIR/build.log"
ok "built and swapped in $ZESARUX_BIN (running emulators keep the old one)"
