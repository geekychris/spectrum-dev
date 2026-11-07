NAME   := canyon_rail
TARGET := zxn
ZX_SDK ?= ../../sdk
# make TRACE=1: the rules' trace instead of the game (src/trace.c, tools/check_trace.sh)
ifeq ($(TRACE),1)
BUILD := build-trace
EXTRA_CFLAGS += -DCR_TRACE
endif
include $(ZX_SDK)/project.mk

.DEFAULT_GOAL := all
all: $(OUT)
	@echo "built $(OUT)"
