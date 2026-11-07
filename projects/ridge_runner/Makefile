NAME   := ridge_runner
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules: 16K bank 39 at 0x0000 while they run
$(OBJDIR)/rr_core.c.o: EXTRA_CFLAGS += --codesegBANK_39 --constsegBANK_39 --datasegBANK_39
# the view: 16K bank 40 at 0x0000 while it draws
$(OBJDIR)/view.c.o: EXTRA_CFLAGS += --codesegBANK_40 --constsegBANK_40 --datasegBANK_40
