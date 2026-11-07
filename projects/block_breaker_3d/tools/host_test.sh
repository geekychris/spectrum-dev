#!/bin/sh
# The original's tests (tools/orig/test_cases.c, from amiga_games
# block_breaker_3d/tests) on this port's rules (src/bb_core.c), on the host:
# the rules one by one, the autopilot clearing levels 1 and 3, and the
# 20,000-tick autopilot game whose state hash must be the original's
# (tools/orig/vectors.h). Also runs the original's own rules for comparison.
#
#   sh tools/host_test.sh          (from projects/block_breaker_3d)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
sed 's#"../bb.h"#"bb.h"#' tools/orig/test_cases.c > build/host/test_cases.c
cp tools/orig/vectors.h build/host/
cc -O2 -Wall -Wno-unused-function -Isrc -I../../sdk/include -Ibuild/host -o build/host/port_test \
   tools/host_main.c build/host/test_cases.c src/bb_core.c
build/host/port_test
