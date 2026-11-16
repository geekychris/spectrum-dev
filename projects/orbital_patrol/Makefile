NAME   := orbital_patrol
TARGET := zxn
ORG    := 18944                # 0x4A00: see src/main.c (bank 5, the ULA off)
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the paged parts' link addresses are in zpragma.inc
# the game's state: 8K page 46, mapped at 0x0000
$(OBJDIR)/state.c.o: EXTRA_CFLAGS += --bsssegPAGE_46
# the music (src/music.c from the MOD, tools/make_music.sh): linked at 0xF200,
# loaded into page 44, copied up at start-up
$(OBJDIR)/music.c.o: EXTRA_CFLAGS += --constsegPAGE_44
# the rest for size (the game's logic is big in zsdcc's code)
$(OBJDIR)/game.c.o $(OBJDIR)/flow.c.o $(OBJDIR)/main.c.o $(OBJDIR)/sound.c.o \
$(OBJDIR)/sdk/modplay.c.o $(OBJDIR)/sdk/sound.c.o $(OBJDIR)/sdk/hiscore.c.o \
$(OBJDIR)/sdk/input.c.o $(OBJDIR)/sdk/zxlog.c.o: EXTRA_CFLAGS += --opt-code-size
