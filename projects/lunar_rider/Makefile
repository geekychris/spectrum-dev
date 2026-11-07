NAME   := lunar_rider
TARGET := zxn
ZX_SDK ?= ../../sdk
ORG    := 24576                # 0x6000: bank 5's upper page (see src/main.c)
include $(ZX_SDK)/project.mk

# the sprite images, tiles and palettes are made on the host (checked in):
#   python3 tools/gen_gfx.py

# memory map: see src/main.c. The original's game logic (game.c, ~10K of
# zsdcc's 32-bit arithmetic) runs from 16K bank 22, mapped over the ROM.
$(OBJDIR)/game.c.o: EXTRA_CFLAGS += --codesegBANK_22 --constsegBANK_22
