#!/bin/sh
# Rebuild Boing Pinball's generated files from the original's sources:
#   tools/orig (the original core and tables) -> build/host/table_0.json
#   -> src/table_0.c (gen_table.py), gfx/ + src/gfx_0.c + src/gfx.h (gen_gfx.py)
# Needs a host C compiler and Python 3 with Pillow.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O1 -w -Itools/orig -o build/host/export_table tools/export_table.c \
   tools/orig/pb_core.c tools/orig/pb_tables.c tools/orig/a3d_fix.c tools/orig/a3d_math.c
build/host/export_table 0 > build/host/table_0.json
python3 tools/gen_table.py build/host/table_0.json 0 > src/table_0.c
python3 tools/gen_gfx.py build/host/table_0.json 0
echo "made src/table_0.c, src/gfx_0.c, src/gfx.h, gfx/"
