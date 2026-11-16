NAME   := terra_strike
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c (and zpragma.inc for the link addresses).
# The original's simulation (~20K of zsdcc's 32-bit arithmetic) runs from bank
# 22 + page 46 (0x0000-0x5FFF), the 3D view from bank 24 (0x0000), in turn.
$(OBJDIR)/world.c.o: EXTRA_CFLAGS += --codesegBANK_22 --constsegBANK_22 --opt-code-size
$(OBJDIR)/world_setup.c.o: EXTRA_CFLAGS += --codesegPAGE_46 --constsegPAGE_46 --opt-code-size
$(OBJDIR)/render.c.o $(OBJDIR)/models_data.c.o: EXTRA_CFLAGS += --codesegBANK_24 --constsegBANK_24 --datasegBANK_24 --bsssegBANK_24 --opt-code-size
# the HUD (tilemap text, bars, radar), the sound and the song (tools/make_music.py) from bank 26 at 0x0000
$(OBJDIR)/hud.c.o $(OBJDIR)/sound.c.o $(OBJDIR)/music_song.c.o: EXTRA_CFLAGS += --codesegBANK_26 --constsegBANK_26 --datasegBANK_26 --bsssegBANK_26 --opt-code-size

# made on the host (checked in): tools/build_data.sh (test.py checks them)
#   src/island.asm, src/island.h   the original's terrain_generate(1987)
#   src/models_data.c              the original's models (models_init)
#   src/music_song.c               the original's MOD (tools/make_music.py)
# src/models.c is the original's, for that tool only
SRCS := $(filter-out $(SRC_DIR)/models.c,$(SRCS))
OBJS := $(filter-out $(OBJDIR)/models.c.o,$(OBJS))
# the SDK's MOD player too: only sound.c (bank 26) calls it, and main memory is full
$(OBJDIR)/sdk/modplay.c.o: EXTRA_CFLAGS += --codesegBANK_26 --constsegBANK_26 --datasegBANK_26 --bsssegBANK_26
