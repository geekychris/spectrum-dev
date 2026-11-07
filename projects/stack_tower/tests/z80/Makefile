# The rules (../../src/st_core.c) on the Z80, against the browser's own games
# (src/vectors.h, the original's tests/vectors.h): test.py runs it first.
#   bin/zx-make -C projects/stack_tower/tests/z80
NAME   := stack_rules
TARGET := zxn
ZX_SDK ?= ../../../../sdk
include $(ZX_SDK)/project.mk
EXTRA_CFLAGS := -I../../src
