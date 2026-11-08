NAME   := boing_twist
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/bt.h; the link addresses are in zpragma.inc
# the logic bank (25, at 0x0000): the text, the tables, the rules' variables
$(OBJDIR)/screens.c.o $(OBJDIR)/hud.c.o $(OBJDIR)/gfx_data.c.o $(OBJDIR)/state.c.o: \
	EXTRA_CFLAGS += --codesegBANK_25 --constsegBANK_25 --datasegBANK_25 --bsssegBANK_25
# the rules (the original's bt_core.c): code in page 52 at 0x4000, variables in bank 25
$(OBJDIR)/bt_core.c.o: EXTRA_CFLAGS += --codesegPAGE_52 --constsegPAGE_52 --datasegPAGE_52 --bsssegBANK_25

# the views, pictures, rooms and sound are made on the host (checked in):
#   python3 tools/gen_data.py; python3 tools/gen_sound.py
# the rules against the original's tests: sh tools/check_core.sh
