NAME   := marble_tilt
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules and the board's small tables: 16K bank 20 at 0x0000 (over the ROM)
$(OBJDIR)/mt_core.c.o $(OBJDIR)/view_data.c.o $(OBJDIR)/marble_data.c.o: \
	EXTRA_CFLAGS += --codesegBANK_20 --constsegBANK_20 --datasegBANK_20
# the game shell and the text: 8K page 44 at 0x6000
$(OBJDIR)/game.c.o $(OBJDIR)/hud.c.o: EXTRA_CFLAGS += --codesegPAGE_44 --constsegPAGE_44 --datasegPAGE_44

# the board's tables and the sound are made on the host (checked in):
#   python3 tools/gen_view.py; python3 tools/gen_sound.py

# view.c keeps the board's vertex buffer in bank 20 at 0x3700-0x3FFF (VT_ADDR)
all: bank20-check
bank20-check: $(OUT)
	@end=$$(awk '/^__BANK_20_tail/ {print $$3}' $(BUILD)/$(NAME).map | tr -d '$$'); \
	 if [ $$((0x$$end & 0xFFFF)) -gt $$((0x3700)) ]; then \
	   echo "error: bank 20 ends at 0x$$end, over view.c's vertex buffer at 0x3700"; rm -f $(OUT); exit 1; fi
.PHONY: bank20-check
