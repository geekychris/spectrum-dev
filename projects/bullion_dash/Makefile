NAME   := bullion_dash
TARGET := zx
# memory map (zpragma.inc, src/memmap.h): BSS from 0x5D80, code from ORG up to 0xF000
ORG    := 27648
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk
# the game's own C, smaller
$(patsubst $(SRC_DIR)/%.c,$(OBJDIR)/%.c.o,$(wildcard $(SRC_DIR)/*.c)): EXTRA_CFLAGS += --opt-code-size
