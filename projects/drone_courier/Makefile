NAME   := drone_courier
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the original's rules (and the help pages, the high-score entry, the sound): 16K bank 20 at 0x0000 (over the ROM)
# (sound.c moved there when main's bigger SDK pushed the resident part past 0xF000: every snd_ call
# is made with bank 20 mapped - from game.c, screens.c and main.c after it maps bank 20)
$(OBJDIR)/dc_core.c.o $(OBJDIR)/screens.c.o $(OBJDIR)/sound.c.o: EXTRA_CFLAGS += --codesegBANK_20 --constsegBANK_20 --datasegBANK_20
# the 3D view: bank 21 at 0x0000 while drawing (main.c view_draw); its pool from 0x2C00
$(OBJDIR)/view.c.o $(OBJDIR)/gfx_data.c.o: EXTRA_CFLAGS += --codesegBANK_21 --constsegBANK_21 --datasegBANK_21
# its tests: 8K page 45 at 0x6000, mapped while they run (main.c selftest)
$(OBJDIR)/test_cases.c.o: EXTRA_CFLAGS += --codesegPAGE_45 --constsegPAGE_45 --datasegPAGE_45
# the game shell and the text: 8K page 44 at 0x6000 (its variables stay resident)
$(OBJDIR)/game.c.o: EXTRA_CFLAGS += --codesegPAGE_44 --constsegPAGE_44

# the sprite patterns and palette are made on the host (checked in):
#   python3 tools/gen_gfx.py

# view.c keeps its polygon pool in bank 21 from 0x2C00 (VIEW_RAM)
all: bank21-check
bank21-check: $(OUT)
	@end=$$(awk '/^__BANK_21_tail/ {print $$3}' $(BUILD)/$(NAME).map | tr -d '$$'); \
	 if [ $$((0x$$end & 0xFFFF)) -gt $$((0x2C00)) ]; then \
	   echo "error: bank 21 ends at 0x$$end, over view.c's pool at 0x2C00"; rm -f $(OUT); exit 1; fi
.PHONY: bank21-check
