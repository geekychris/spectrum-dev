NAME   := harbour_crane
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# the backdrop and the sound are made on the host (checked in):
#   python3 tools/gen_gfx.py && python3 tools/gen_sound.py

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules (the original's hc_core.c): 16K bank 20 at 0x0000, over the ROM (its strings stay in main memory: it fills the bank)
$(OBJDIR)/hc_core.c.o: EXTRA_CFLAGS += --codesegBANK_20 --datasegBANK_20
# the picture and its lists: 16K bank 21 at 0x0000 while it draws
$(OBJDIR)/view.c.o: EXTRA_CFLAGS += --codesegBANK_21 --constsegBANK_21 --datasegBANK_21 --bsssegBANK_21
# the game shell: 8K page 44 at 0x6000
$(OBJDIR)/game.c.o: EXTRA_CFLAGS += --codesegPAGE_44 --constsegPAGE_44 --datasegPAGE_44
