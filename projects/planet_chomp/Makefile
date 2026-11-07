NAME   := planet_chomp
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules: bank 16 at 0x0000, their grid tables in bank 17 at 0x4000
$(OBJDIR)/game.c.o $(OBJDIR)/maze.c.o: EXTRA_CFLAGS += --codesegBANK_16 --constsegBANK_16 --datasegBANK_16
$(OBJDIR)/logic_tab.c.o: EXTRA_CFLAGS += --constsegBANK_17
# the renderer: bank 18 at 0x0000, its tables in pages 39 and 40 at 0x6000
$(OBJDIR)/render.c.o $(OBJDIR)/rtab.c.o: EXTRA_CFLAGS += --codesegBANK_18 --constsegBANK_18 --datasegBANK_18 -DXF_ASM
$(OBJDIR)/rtab_a.c.o: EXTRA_CFLAGS += --constsegPAGE_39
$(OBJDIR)/rtab_b.c.o: EXTRA_CFLAGS += --constsegPAGE_40
# the HUD: bank 21 at 0x0000
$(OBJDIR)/hud.c.o: EXTRA_CFLAGS += --codesegBANK_21 --constsegBANK_21 --datasegBANK_21
# the sprites' positions and the radar: page 41 at 0x6000 after drawing
$(OBJDIR)/view.c.o: EXTRA_CFLAGS += --codesegPAGE_41 --constsegPAGE_41 --datasegPAGE_41

# the tables and pictures are made on the host (checked in):
#   sh tools/make_assets.sh

# every banked part must fit its slot(s): 16K banks, 8K pages (bank 18 keeps
# its last 1.5K for draw.asm's jump table and row buffers)
all: banks-check
banks-check: $(OUT)
	@for s in BANK_16:16384 BANK_17:16384 BANK_18:14848 BANK_21:16384 PAGE_38:8192 PAGE_39:8192 PAGE_40:8192 PAGE_41:8192; do \
	   n=$${s%%:*}; max=$${s##*:}; \
	   h=$$(awk "/^__$${n}_head/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   t=$$(awk "/^__$${n}_tail/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   if [ $$((0x$$t - 0x$$h)) -gt $$max ]; then echo "error: $$n is $$((0x$$t - 0x$$h)) bytes, over $$max"; rm -f $(OUT); exit 1; fi; \
	   echo "  $$n: $$((0x$$t - 0x$$h)) of $$max"; \
	 done
.PHONY: banks-check
