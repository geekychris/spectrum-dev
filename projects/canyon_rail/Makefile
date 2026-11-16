NAME   := canyon_rail
TARGET := zxn
ZX_SDK ?= ../../sdk
# make TRACE=1: the rules' trace instead of the game (src/trace.c, tools/check_trace.sh)
ifeq ($(TRACE),1)
BUILD := build-trace
EXTRA_CFLAGS += -DCR_TRACE
endif
include $(ZX_SDK)/project.mk
ifeq ($(TRACE),1)
# the trace links the rules alone (the picture, sprites and shell left out)
OBJS := $(filter-out $(addprefix $(OBJDIR)/,render.c.o sprites.c.o spr_tab.c.o spr_data.asm.o view.asm.o front.c.o),$(OBJS))
endif

.DEFAULT_GOAL := all
all: $(OUT)
	@echo "built $(OUT)"

# memory map (src/main.c; link addresses in zpragma.inc):
# the rules: 16K bank 16 at 0x0000, mapped for a tick (map_rules)
$(OBJDIR)/cr_core.c.o: EXTRA_CFLAGS += --codesegBANK_16 --constsegBANK_16
# the picture: 16K bank 18 at 0x0000, mapped while drawing (map_render), its variables too
$(OBJDIR)/render.c.o: EXTRA_CFLAGS += --codesegBANK_18 --constsegBANK_18 --datasegBANK_18 --bsssegBANK_18
# the sprites (and their tables): with the picture
$(OBJDIR)/sprites.c.o $(OBJDIR)/spr_tab.c.o: EXTRA_CFLAGS += --codesegBANK_18 --constsegBANK_18 --datasegBANK_18 --bsssegBANK_18
# the shell: 16K bank 20 at 0x0000 (map_front), its own variables too (the shared ones are main.c's)
# with the SDK modules only it uses (the tilemap, the copper, the high scores)
$(OBJDIR)/front.c.o $(OBJDIR)/sdk/next/nexttile.c.o $(OBJDIR)/sdk/next/nexttile_nx.c.o \
$(OBJDIR)/sdk/next/nextcopper.c.o $(OBJDIR)/sdk/hiscore.c.o: \
	EXTRA_CFLAGS += --opt-code-size --codesegBANK_20 --constsegBANK_20 --datasegBANK_20 --bsssegBANK_20
# resident (0x8000-0xEFFF) is tight: the resident C for size
$(OBJDIR)/cr_place.c.o $(OBJDIR)/sdk/a3cam.c.o $(OBJDIR)/sdk/fix16.c.o \
$(OBJDIR)/sdk/sound.c.o $(OBJDIR)/sdk/next/nextl2.c.o \
$(OBJDIR)/sdk/next/nextgfx.c.o $(OBJDIR)/sdk/input.c.o $(OBJDIR)/sdk/zxlog.c.o \
$(OBJDIR)/sdk/next/palfx.c.o: EXTRA_CFLAGS += --opt-code-size
