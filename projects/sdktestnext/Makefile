NAME   := sdktestnext
TARGET := zxn
ZX_SDK ?= ../../sdk
include $(ZX_SDK)/project.mk

# a page the program never maps: the harness reads its symbols (src/banked.c)
$(OBJDIR)/banked.c.o: EXTRA_CFLAGS += --codesegPAGE_84 --constsegPAGE_84 --datasegPAGE_84
