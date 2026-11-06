NAME   := scairy_fun
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the simulation's objects and traffic: 16K bank 16 at 0x0000
$(OBJDIR)/world_traffic.c.o: EXTRA_CFLAGS += --codesegBANK_16 --constsegBANK_16
# the autopilot and the car: 16K bank 17 at 0x4000
$(OBJDIR)/world_ai.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_17 --constsegBANK_17
$(OBJDIR)/world_scene.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_16 --constsegBANK_16
$(OBJDIR)/route.c.o: EXTRA_CFLAGS += --opt-code-size --codesegBANK_17 --constsegBANK_17
