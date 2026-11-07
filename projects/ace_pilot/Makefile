NAME   := ace_pilot
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# memory map: see src/main.c; the link addresses are in zpragma.inc
# the renderer: bank 16 at 0x0000 while drawing
$(OBJDIR)/render.c.o: EXTRA_CFLAGS += --codesegBANK_16 --constsegBANK_16 --datasegBANK_16

# every banked part must fit its slots (16K banks)
all: banks-check
banks-check: $(OUT)
	@for s in BANK_16:16384; do \
	   n=$${s%%:*}; max=$${s##*:}; \
	   h=$$(awk "/^__$${n}_head/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   t=$$(awk "/^__$${n}_tail/ {print \$$3}" $(BUILD)/$(NAME).map | tr -d '$$'); \
	   if [ $$((0x$$t - 0x$$h)) -gt $$max ]; then echo "error: $$n is $$((0x$$t - 0x$$h)) bytes, over $$max"; rm -f $(OUT); exit 1; fi; \
	   echo "  $$n: $$((0x$$t - 0x$$h)) of $$max"; \
	 done
.PHONY: banks-check
