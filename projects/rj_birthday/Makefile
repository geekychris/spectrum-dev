NAME   := rj_birthday
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the rules (game.c): bank 24 at 0x0000; three room updates: page 50 at 0x6000;
# the screens: bank 26 at 0x0000
$(OBJDIR)/game.c.o: EXTRA_CFLAGS += --codesegBANK_24 --constsegBANK_24 --datasegBANK_24
$(OBJDIR)/screens.c.o: EXTRA_CFLAGS += --codesegBANK_26 --constsegBANK_26 --datasegBANK_26
# the songs: page 55 at 0x4000 (sound.c maps it while modplay reads them)
$(OBJDIR)/music_party.c.o $(OBJDIR)/music_bday.c.o: EXTRA_CFLAGS += --constsegPAGE_55 --datasegPAGE_55
$(OBJDIR)/game_rooms.c.o: EXTRA_CFLAGS += --codesegPAGE_50 --constsegPAGE_50 --datasegPAGE_50

# the pictures and the sound are made on the host (checked in):
#   python3 tools/gen_gfx.py; python3 tools/gen_sound.py

all: banks-check
banks-check: $(OUT)
	@for s in BANK_24:16384 PAGE_50:8192 BANK_26:16384 PAGE_55:8192; do \
	   n=$${s%%:*}; max=$${s##*:}; \
	   h=$$(awk "/^__$${n}_head/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   t=$$(awk "/^__$${n}_tail/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   if [ $$((0x$$t - 0x$$h)) -gt $$max ]; then echo "error: $$n is $$((0x$$t - 0x$$h)) bytes, over $$max"; rm -f $(OUT); exit 1; fi; \
	   echo "  $$n: $$((0x$$t - 0x$$h)) of $$max"; \
	 done
.PHONY: banks-check
