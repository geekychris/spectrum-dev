NAME   := boing_pinball
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules: 16K bank 20 at 0x0000
$(OBJDIR)/pb_rules.c.o: EXTRA_CFLAGS += --codesegBANK_20 --constsegBANK_20
# the table's data and the sine table (the physics' only): 8K page 44 at 0x4000
$(OBJDIR)/table_0.c.o $(OBJDIR)/sin_tab.c.o: EXTRA_CFLAGS += --codesegPAGE_44 --constsegPAGE_44 --datasegPAGE_44
# the front end and its constants: 8K page 46 at 0x6000
$(OBJDIR)/front.c.o $(OBJDIR)/gfx_0.c.o: EXTRA_CFLAGS += --codesegPAGE_46 --constsegPAGE_46
