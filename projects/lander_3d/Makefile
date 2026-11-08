NAME   := lander_3d
TARGET := zxn
ZX_SDK ?= ../../sdk
# the song made at build time (below)
EXTRA_OBJS := build/obj/music_song.c.o
EXTRA_CFLAGS := -Ibuild/gen
# PROF=1: the frame's sections timed (src/prof.h, tools/frame_prof.py)
ifdef PROF
EXTRA_CFLAGS += -DLD_PROF
endif
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the banked parts' link addresses are in zpragma.inc
# the rules: bank 20 at 0x0000
$(OBJDIR)/ld_core.c.o $(OBJDIR)/ld_land.c.o $(OBJDIR)/game_rules.c.o: EXTRA_CFLAGS += --codesegBANK_20 --constsegBANK_20 --datasegBANK_20
# (the touchdown, a level's start and the shell's part aren't hot: smaller
# code, the bank is full)
$(OBJDIR)/ld_land.c.o $(OBJDIR)/game_rules.c.o: EXTRA_CFLAGS += --opt-code-size
# the autopilot and fix16's arc tangents and 3-D lengths: page 46 at 0x6000
# (resident memory is full; only the rules and the shell with them call them)
$(OBJDIR)/ld_auto.c.o $(OBJDIR)/sdk/fix16_atan.c.o $(OBJDIR)/sdk/fix16_hyp3.c.o: EXTRA_CFLAGS += --codesegPAGE_46 --constsegPAGE_46 --datasegPAGE_46
# the view: bank 22 at 0x0000; its drawing: page 47 at 0x6000 (its data resident)
$(OBJDIR)/view.c.o: OPT := -SO2
$(OBJDIR)/view.c.o: EXTRA_CFLAGS += --codesegBANK_22 --constsegBANK_22 --datasegBANK_22 --bsssegBANK_22
$(OBJDIR)/vfill.c.o: EXTRA_CFLAGS += --codesegPAGE_47 --constsegPAGE_47
# the text (and a level's start): bank 24 at 0x0000
$(OBJDIR)/hud.c.o $(OBJDIR)/screens.c.o $(OBJDIR)/game_out.c.o $(OBJDIR)/ld_level.c.o: EXTRA_CFLAGS += --codesegBANK_24 --constsegBANK_24 --datasegBANK_24 --bsssegBANK_24
# (a level's start, once a level: there because bank 20 is full)
$(OBJDIR)/ld_level.c.o: EXTRA_CFLAGS += --opt-code-size

# the levels: made by the original's own code (tools/orig) on the host, at
# build time (gcc in the toolchain image); src/level_data.asm puts the banks
# into the .nex
LEVELS := $(BUILD)/levels
$(LEVELS)/stamp: tools/gen_levels.c src/lv_rec.h src/flight_hash.h $(wildcard tools/orig/*.[ch] tools/orig/amiga3d/*.[ch])
	mkdir -p $(LEVELS)
	cc -O2 -Itools/orig -Itools/orig/amiga3d tools/gen_levels.c tools/orig/ld_core.c \
	   tools/orig/amiga3d/a3d_math.c tools/orig/amiga3d/a3d_fix.c -o $(BUILD)/gen_levels
	$(BUILD)/gen_levels $(LEVELS) > $(LEVELS)/levels.txt
	touch $@
$(OBJDIR)/level_data.asm.o: $(LEVELS)/stamp

# the sprite patterns and the music (the original's font and MOD), also made
# at build time: build/gen (gfx_data.h, music_song.c, the banks' data)
GEN := $(BUILD)/gen
$(GEN)/stamp: tools/gen_gfx.py tools/make_music.py tools/orig/lander_3d.mod tools/orig/amiga3d/a3d_font16.h
	mkdir -p $(GEN)
	python3 tools/gen_gfx.py $(GEN)
	python3 tools/make_music.py $(GEN)
	touch $@
$(OBJDIR)/music_song.c.o: $(GEN)/stamp | $(OBJDIR)
	zcc $(CFLAGS) -c $(GEN)/music_song.c -o $@
$(OBJS): $(GEN)/stamp

# each bank must fit its 16K
all: banks-check
banks-check: $(OUT)
	@for n in BANK_20 BANK_22 BANK_24; do \
	   h=$$(awk "/^__$${n}_head/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   t=$$(awk "/^__$${n}_tail/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   if [ $$((0x$$t - 0x$$h)) -gt 16384 ]; then echo "error: $$n is $$((0x$$t - 0x$$h)) bytes, over 16384"; rm -f $(OUT); exit 1; fi; \
	   echo "  $$n: $$((0x$$t - 0x$$h)) of 16384"; \
	 done
.PHONY: banks-check
