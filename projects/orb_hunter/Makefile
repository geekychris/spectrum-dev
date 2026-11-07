NAME   := orb_hunter
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c. The rules (game.c) run from bank 30 at 0x0000
# (its link address is in zpragma.inc), built for size to fit its 16K.
$(OBJDIR)/game.c.o: EXTRA_CFLAGS += --codesegBANK_30 --opt-code-size

# made on the host (checked in):
#   make -C tools world; python3 tools/gen_gfx.py; python3 tools/make_music.py
# the rules against the original's: make -C tests
