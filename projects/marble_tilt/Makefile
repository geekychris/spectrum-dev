NAME   := marble_tilt
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules: 16K bank 20 at 0x0000 (over the ROM)
$(OBJDIR)/mt_core.c.o: EXTRA_CFLAGS += --codesegBANK_20 --constsegBANK_20 --datasegBANK_20
# the game shell and the text: 8K page 44 at 0x6000
$(OBJDIR)/game.c.o $(OBJDIR)/hud.c.o: EXTRA_CFLAGS += --codesegPAGE_44 --constsegPAGE_44 --datasegPAGE_44

# the board's tables and the sound are made on the host (checked in):
#   python3 tools/gen_view.py; python3 tools/gen_sound.py
