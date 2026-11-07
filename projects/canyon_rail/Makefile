NAME   := canyon_rail
TARGET := zxn
ZX_SDK ?= ../../sdk
# make TRACE=1: the rules' trace instead of the game (src/trace.c, tools/check_trace.sh)
ifeq ($(TRACE),1)
BUILD := build-trace
EXTRA_CFLAGS += -DCR_TRACE
endif
include $(ZX_SDK)/project.mk

.DEFAULT_GOAL := all
all: $(OUT)
	@echo "built $(OUT)"

# memory map (src/main.c; link addresses in zpragma.inc):
# the rules: 16K bank 16 at 0x0000, mapped for a tick (map_rules)
$(OBJDIR)/cr_core.c.o: EXTRA_CFLAGS += --codesegBANK_16 --constsegBANK_16
# the picture: 16K bank 18 at 0x0000, mapped while drawing (map_render)
$(OBJDIR)/render.c.o: EXTRA_CFLAGS += --codesegBANK_18 --constsegBANK_18
