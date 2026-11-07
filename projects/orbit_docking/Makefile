NAME   := orbit_docking
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules: bank 20 at 0x0000; the view: bank 21 at 0x0000
$(OBJDIR)/od_core.c.o: EXTRA_CFLAGS += --codesegBANK_20 --constsegBANK_20 --datasegBANK_20
$(OBJDIR)/view.c.o: EXTRA_CFLAGS += --codesegBANK_21 --constsegBANK_21 --datasegBANK_21
# the text: bank 24 at 0x0000
$(OBJDIR)/hud.c.o $(OBJDIR)/screens.c.o: EXTRA_CFLAGS += --codesegBANK_24 --constsegBANK_24 --datasegBANK_24

# the sprite patterns and the sound are made on the host (checked in):
#   python3 tools/gen_gfx.py; python3 tools/gen_sound.py

# each bank must fit its 16K
all: banks-check
banks-check: $(OUT)
	@for n in BANK_20 BANK_21 BANK_24; do \
	   h=$$(awk "/^__$${n}_head/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   t=$$(awk "/^__$${n}_tail/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   if [ $$((0x$$t - 0x$$h)) -gt 16384 ]; then echo "error: $$n is $$((0x$$t - 0x$$h)) bytes, over 16384"; rm -f $(OUT); exit 1; fi; \
	   echo "  $$n: $$((0x$$t - 0x$$h)) of 16384"; \
	 done
.PHONY: banks-check
