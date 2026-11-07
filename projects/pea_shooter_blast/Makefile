NAME   := pea_shooter_blast
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; link addresses in zpragma.inc
# the original's rules (game.c, 15K): 16K bank 40 at 0x0000; zsdcc's
# --opt-code-size makes it fit (test.py checks it computes what the original
# does: tools/host_trace.sh)
$(OBJDIR)/game.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_40 --constsegBANK_40 --datasegBANK_40
# the levels and their maps (6K): page 82 at 0x4000
$(OBJDIR)/levels.c.o: EXTRA_CFLAGS += --codesegPAGE_82 --constsegPAGE_82 --datasegPAGE_82 --bsssegPAGE_82
# the HUD, its font, the palettes, the test probe: page 84 at 0x6000
UI_SEG := --codesegPAGE_84 --constsegPAGE_84 --datasegPAGE_84 --bsssegPAGE_84
$(OBJDIR)/hud.c.o $(OBJDIR)/font.c.o $(OBJDIR)/gfx_data.c.o $(OBJDIR)/probe.c.o: EXTRA_CFLAGS += $(UI_SEG)
# the scripted game's input and state hash: page 86 at 0x6000 during that run
$(OBJDIR)/state_hash.c.o: EXTRA_CFLAGS += --codesegPAGE_86 --constsegPAGE_86 --datasegPAGE_86 --bsssegPAGE_86
# the banked parts must fit their 8K pages / 16K bank
all: pages-check
pages-check: $(OUT)
	@for p in PAGE_82:8192 PAGE_84:8192 PAGE_86:8192 BANK_40:16384; do s=$${p%:*}; max=$${p#*:}; \
	  head=$$(awk "/^__$${s}_head/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	  tail=$$(awk "/^__$${s}_tail/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	  if [ $$(( 0x$$tail - 0x$$head )) -gt $$max ]; then \
	    echo "error: $$s holds $$(( 0x$$tail - 0x$$head )) bytes, over $$max"; rm -f $(OUT); exit 1; fi; done
.PHONY: pages-check

# main memory is full (28K below the IM 2 table): its files for size too
$(OBJDIR)/view.c.o $(OBJDIR)/main.c.o $(OBJDIR)/sound.c.o: EXTRA_CFLAGS += --opt-code-size

# the pictures, the sound and the music are made on the host (checked in):
#   sh tools/make_assets.sh
