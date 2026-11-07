#!/bin/sh
# The port's rules against the original's on the host: the same scripted
# input, the state after every tick must be the same (tools/sim.c).
#   sh tools/compare.sh [TICKS] [SEED]
set -e
cd "$(dirname "$0")/.."
SDK=../../sdk
mkdir -p build
cc -O1 -w -DORIG -Itools/orig -Itools/stub tools/sim.c tools/orig/game.c tools/orig/maze.c \
   -include tools/orig32.h -o build/sim_orig
cc -O1 -w -DPORT -Isrc -I$SDK/include tools/sim.c src/game.c src/maze.c src/maze_tab.c src/logic_tab.c src/state.c $SDK/src/vec3.c -o build/sim_port
build/sim_orig ${1:-200000} ${2:-1} > build/trace_orig.txt
build/sim_port ${1:-200000} ${2:-1} > build/trace_port.txt
if cmp -s build/trace_orig.txt build/trace_port.txt; then
    echo "compare: the same $(wc -l < build/trace_port.txt | tr -d ' ') ticks ($(grep -c ' d1$' build/trace_port.txt) in the demo)"
else
    echo "compare: DIFFERENT"; diff build/trace_orig.txt build/trace_port.txt | head -6; exit 1
fi
