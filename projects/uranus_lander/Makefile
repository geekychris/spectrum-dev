NAME   := uranus_lander
TARGET := zx
# memory map (zpragma.inc): BSS 0x5D80.., code from ORG up to 0xF000
ORG    := 29184
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk
# the game's own C, smaller (it only just fits a 48K)
$(patsubst $(SRC_DIR)/%.c,$(OBJDIR)/%.c.o,$(wildcard $(SRC_DIR)/*.c)): EXTRA_CFLAGS += --opt-code-size
