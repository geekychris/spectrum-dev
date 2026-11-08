NAME   := rolling_steel
TARGET := zxn
ORG    := 24576
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; link addresses in zpragma.inc
# the rules (game.c): 16K bank 14 at 0x0000 (over the ROM)
$(OBJDIR)/game.c.o: EXTRA_CFLAGS += --codesegBANK_14 --constsegBANK_14 --datasegBANK_14
# the route functions, the course loader, the sprites: bank 15, also at 0x0000 (banks.c's stubs)
$(OBJDIR)/game_path.c.o $(OBJDIR)/course_c.c.o $(OBJDIR)/sprite_data.c.o: \
	EXTRA_CFLAGS += --codesegBANK_15 --constsegBANK_15 --datasegBANK_15

# the physics: phys.asm's code is SECTION BANK_16 (banks.c's stubs)

# the courses (physics + pictures) are made on the host (checked in):
#   python3 tools/gen_course.py
