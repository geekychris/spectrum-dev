NAME   := jump_quest
TARGET := zx
# memory map (zpragma.inc): BSS 0x5D80.., code from ORG up to 0xF000
ORG    := 30800                # 0x7850, right after the BSS; the first ~1.6K is newlib (printf)
ZX_SDK ?= ../../sdk
# cellspr48: 14 sprites (5 enemies, the player, 8 particles), for the SDK's C and asm too
EXTRA_CFLAGS := -DCS_MAX_SPR=14
include $(ZX_SDK)/project.mk
# the game's own C, smaller
$(patsubst $(SRC_DIR)/%.c,$(OBJDIR)/%.c.o,$(wildcard $(SRC_DIR)/*.c)): EXTRA_CFLAGS += --opt-code-size
