NAME   := lander_3d
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the banked parts' link addresses are in zpragma.inc
# the rules: bank 20 at 0x0000
$(OBJDIR)/ld_core.c.o: EXTRA_CFLAGS += --codesegBANK_20 --constsegBANK_20 --datasegBANK_20
# the autopilot: page 46 at 0x6000 (it calls the rules in bank 20)
$(OBJDIR)/ld_auto.c.o: EXTRA_CFLAGS += --codesegPAGE_46 --constsegPAGE_46 --datasegPAGE_46
# the view: bank 22 at 0x0000; its drawing: page 47 at 0x6000 (its data resident)
$(OBJDIR)/view.c.o: OPT := -SO2
$(OBJDIR)/view.c.o: EXTRA_CFLAGS += --codesegBANK_22 --constsegBANK_22 --datasegBANK_22 --bsssegBANK_22
$(OBJDIR)/vfill.c.o: EXTRA_CFLAGS += --codesegPAGE_47 --constsegPAGE_47

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

# each bank must fit its 16K
all: banks-check
banks-check: $(OUT)
	@for n in BANK_20 BANK_22; do \
	   h=$$(awk "/^__$${n}_head/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   t=$$(awk "/^__$${n}_tail/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   if [ $$((0x$$t - 0x$$h)) -gt 16384 ]; then echo "error: $$n is $$((0x$$t - 0x$$h)) bytes, over 16384"; rm -f $(OUT); exit 1; fi; \
	   echo "  $$n: $$((0x$$t - 0x$$h)) of 16384"; \
	 done
.PHONY: banks-check
