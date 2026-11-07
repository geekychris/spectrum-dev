NAME   := scairy_fun
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the simulation's objects and traffic: 16K bank 16 at 0x0000
$(OBJDIR)/world_traffic.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_16 --constsegBANK_16
# the autopilot and the car: 16K bank 17 at 0x4000
$(OBJDIR)/world_ai.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_17 --constsegBANK_17
$(OBJDIR)/world_flow.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_16 --constsegBANK_16
$(OBJDIR)/route.c.o $(OBJDIR)/world_scene.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_17 --constsegBANK_17
# the renderer: 16K bank 18 at 0x0000
# its variables, the sprites' code and tables: 8K page 38 at 0x4000
$(OBJDIR)/render.c.o: EXTRA_CFLAGS += --codesegBANK_18 --constsegBANK_18 --datasegPAGE_38 --bsssegPAGE_38
$(OBJDIR)/sprites.c.o $(OBJDIR)/spr_tab.c.o: EXTRA_CFLAGS += --codesegPAGE_38 --constsegPAGE_38 --datasegPAGE_38 --bsssegPAGE_38
# the sprites' patterns: banks 40.. (tools/gen_sprites.py, src/spr_data.asm)
# the front end (game.c): 16K bank 20 at 0x0000, its variables too
# and the SDK modules only it uses (nexttile, hiscore)
$(OBJDIR)/game.c.o $(OBJDIR)/sdk/next/nexttile.c.o $(OBJDIR)/sdk/hiscore.c.o: \
	EXTRA_CFLAGS += --opt-code-size --codesegBANK_20 --constsegBANK_20 --datasegBANK_20 --bsssegBANK_20
# the music (music.c from scairy.mod by sdk/tools/mod2ay.py): linked at 0xF200,
# loaded into page 39, copied up at start-up (see main.c)
$(OBJDIR)/music.c.o: EXTRA_CFLAGS += --constsegPAGE_39
# resident (0x8000-0xEFFF) is tight: the resident C for size
$(OBJDIR)/main.c.o $(OBJDIR)/world.c.o $(OBJDIR)/track.c.o $(OBJDIR)/sc_math.c.o \
$(OBJDIR)/sdk/next/nextl2.c.o $(OBJDIR)/sdk/next/nextgfx.c.o $(OBJDIR)/sdk/sound.c.o \
$(OBJDIR)/sdk/modplay.c.o $(OBJDIR)/sdk/input.c.o $(OBJDIR)/sdk/next/dma.c.o \
$(OBJDIR)/sdk/zxlog.c.o $(OBJDIR)/sdk/next/persp.c.o: EXTRA_CFLAGS += --opt-code-size
