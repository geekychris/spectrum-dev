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
