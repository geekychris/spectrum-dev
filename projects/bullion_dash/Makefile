NAME   := bullion_dash
TARGET := zx
# memory map (zpragma.inc, src/memmap.h): BSS from 0x5D00, code from ORG up to 0xF000
# (27408: the repaired levels pack to 394 bytes more; tile_row moved above 0xF000 for it)
ORG    := 27408
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk
# the game's own C, smaller
$(patsubst $(SRC_DIR)/%.c,$(OBJDIR)/%.c.o,$(wildcard $(SRC_DIR)/*.c)): EXTRA_CFLAGS += --opt-code-size
