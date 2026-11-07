NAME   := block_breaker_3d
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/memmap.h and src/main.c; link addresses in zpragma.inc
# the rules: 16K bank 40 at 0x0000 while the logic runs
$(OBJDIR)/bb_core.c.o: EXTRA_CFLAGS += --codesegBANK_40 --constsegBANK_40 --datasegBANK_40
# the renderer and its tables: 16K bank 41 at 0x0000 while drawing
$(OBJDIR)/render.c.o $(OBJDIR)/gfx_data.c.o: EXTRA_CFLAGS += --codesegBANK_41 --constsegBANK_41 --datasegBANK_41
# the HUD: 8K page 84 at 0x6000
$(OBJDIR)/hud.c.o: EXTRA_CFLAGS += --codesegPAGE_84 --constsegPAGE_84 --datasegPAGE_84
# the original's tests (the in-game self-test): 8K page 85 at 0x6000
$(OBJDIR)/test_cases.c.o: EXTRA_CFLAGS += --codesegPAGE_85 --constsegPAGE_85 --datasegPAGE_85

# the pictures, tables, sound and the self-test's source are made on the
# host (checked in): sh tools/make_assets.sh

