#!/usr/bin/env bash
# Build ZEsarUX natively with the zxh harness (deterministic frame stepping,
# captured debug log, zxh-* remote commands). See emulator/patches/README.md.
# Output: third_party/zesarux/src/zesarux
#
# Needs: a C compiler + make, SDL2 (macOS: brew install sdl2; Debian/Ubuntu:
# sudo apt install libsdl2-dev). Without SDL2 it still builds, headless only.
source "$(dirname "$0")/lib.sh"

step "ZEsarUX @ ${ZESARUX_COMMIT:0:10} (13.0) + zxh harness"
have git  || die "git is required"
have make || die "make is required (macOS: xcode-select --install; Linux: build-essential)"
have cc   || die "a C compiler is required (macOS: xcode-select --install; Linux: build-essential)"
have sdl2-config || warn "sdl2-config not found: building without a window (macOS: brew install sdl2; Debian: apt install libsdl2-dev)"

git_checkout_pinned "$ZESARUX_REPO" "$ZESARUX_DIR" "$ZESARUX_COMMIT"

# Always start from a pristine tree so patches apply cleanly and repeatably.
git -C "$ZESARUX_DIR" reset -q --hard
git -C "$ZESARUX_DIR" clean -q -fdx
cp "$ZX_ROOT"/emulator/ext/zxh.[ch] "$ZESARUX_DIR/src/zrcp/"
for p in "$ZX_ROOT"/emulator/patches/*.patch; do
  git -C "$ZESARUX_DIR" apply --whitespace=nowarn "$p" || die "patch failed: $(basename "$p")"
  ok "applied $(basename "$p")"
done

# Only the drivers we use: SDL2 window + audio (or null), coreaudio on macOS.
cfg=(--enable-sdl2 --disable-cocoa --disable-xwindows --disable-caca --disable-aa
     --disable-cursesw --disable-curses --disable-fbdev --disable-pulse --disable-dsp
     --disable-sndfile)
[ "$(host_os)" = darwin ] || cfg+=(--disable-coreaudio)
log="$ZESARUX_DIR/build.log"
( cd "$ZESARUX_DIR/src" && CFLAGS="-O2" ./configure "${cfg[@]}" ) >"$log" 2>&1 \
  || { tail -30 "$log"; die "configure failed (full log: $log)"; }
grep -q "Enabled Video output drivers:.*sdl" "$log" || warn "SDL2 not detected: window mode unavailable"
make -C "$ZESARUX_DIR/src" -j"$(ncpu)" >>"$log" 2>&1 \
  || { tail -30 "$log"; die "build failed (full log: $log)"; }
[ -x "$ZESARUX_BIN" ] || die "no binary at $ZESARUX_BIN"
ok "built $ZESARUX_BIN"
