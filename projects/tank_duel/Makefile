NAME   := tank_duel
TARGET := zxn
ZX_SDK ?= ../../sdk
# make TRACE=1: the original's 9000-tick autopilot fight and its state hash
# instead of the game (test.py checks it against the original's)
ifeq ($(TRACE),1)
BUILD := build-trace
EXTRA_CFLAGS += -DTD_TRACE
endif
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules (the original's td_core.c): 16K bank 16 at 0x0000
$(OBJDIR)/td_core.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_16 --constsegBANK_16
# their fixed point: 16K bank 17 at 0x4000
$(OBJDIR)/td_fix.c.o: EXTRA_CFLAGS += --codesegBANK_17 --constsegBANK_17
# the enemies' minds and the autopilot, with the fixed point
$(OBJDIR)/td_ai.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_17 --constsegBANK_17
# the renderer and the models: 16K bank 18 at 0x0000
$(OBJDIR)/render.c.o $(OBJDIR)/models.c.o: EXTRA_CFLAGS += --codesegBANK_18 --constsegBANK_18
