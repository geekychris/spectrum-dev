NAME   := rolling_steel
TARGET := zxn
ORG    := 24576
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; link addresses in zpragma.inc
# the rules (game.c) and the course loader: 16K bank 14 at 0x0000 (over the ROM)
$(OBJDIR)/game.c.o $(OBJDIR)/course_c.c.o: EXTRA_CFLAGS += --codesegBANK_14 --constsegBANK_14 --datasegBANK_14
# the route functions: bank 15, also at 0x0000 (banks.c's stubs)
$(OBJDIR)/game_path.c.o: \
	EXTRA_CFLAGS += --codesegBANK_15 --constsegBANK_15 --datasegBANK_15 --bsssegBANK_15
# the HUD: bank 17, also at 0x0000
$(OBJDIR)/hud.c.o: EXTRA_CFLAGS += --codesegBANK_17 --constsegBANK_17 --datasegBANK_17 --bsssegBANK_17

# the physics: phys.asm's code is SECTION BANK_16 (banks.c's stubs)

# the courses (physics + pictures) are made on the host (checked in):
#   python3 tools/gen_course.py
