NAME   := spectral_keep
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules (room.c, game.c): 16K bank 39 at 0x0000 while the logic runs
$(OBJDIR)/room.c.o $(OBJDIR)/game.c.o: EXTRA_CFLAGS += --codesegBANK_39 --constsegBANK_39 --datasegBANK_39
# the scene: 16K bank 40 at 0x0000 while drawing
$(OBJDIR)/scene.c.o: EXTRA_CFLAGS += --codesegBANK_40 --constsegBANK_40 --datasegBANK_40
# the HUD: 8K page 84 at 0x6000 while it draws
$(OBJDIR)/hud.c.o: EXTRA_CFLAGS += --codesegPAGE_84 --constsegPAGE_84 --datasegPAGE_84
# the physics and the test probe: 8K page 82 at 0x6000 while the logic runs
$(OBJDIR)/world.c.o $(OBJDIR)/probe.c.o: EXTRA_CFLAGS += --codesegPAGE_82 --constsegPAGE_82 --datasegPAGE_82

# the pictures, the sound and the level data are made on the host: tools/make_assets.sh

# bank 40 keeps scene.c's tables above its code (SCENE_MEM in scene.c)
all: banks-check
banks-check: $(OUT)
	@end=$$(awk '/^__BANK_40_tail/ {print $$3}' $(BUILD)/$(NAME).map | tr -d '$$'); \
	 if [ $$((0x$$end & 0xFFFF)) -gt $$((0x2C00)) ]; then \
	   echo "error: bank 40's code ends at 0x$$end, over scene.c's tables at 0x2C00"; rm -f $(OUT); exit 1; fi
.PHONY: banks-check
