NAME   := orbit_docking
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules: bank 20 at 0x0000; the view: bank 21 at 0x0000
$(OBJDIR)/od_core.c.o: EXTRA_CFLAGS += --codesegBANK_20 --constsegBANK_20 --datasegBANK_20
$(OBJDIR)/view.c.o: EXTRA_CFLAGS += --codesegBANK_21 --constsegBANK_21 --datasegBANK_21
