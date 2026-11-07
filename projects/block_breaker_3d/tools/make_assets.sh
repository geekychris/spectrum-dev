#!/bin/sh
# Remake Block Breaker 3D's data on the host: the pictures and tables
# (gen_gfx.py), the sound (gen_sound.py), the original's tests for the
# in-game self-test (src/test_cases.c, from tools/orig/test_cases.c), then
# the original's tests on this port's rules on the host (host_test.sh).
# Needs python3 with numpy and Pillow, and a C compiler.
#
#   sh tools/make_assets.sh        (from projects/block_breaker_3d)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
python3 tools/gen_gfx.py
python3 tools/gen_sound.py
python3 tools/port_tests.py
sh tools/host_test.sh
