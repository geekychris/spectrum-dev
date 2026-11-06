# Shared build rules for Spectrum projects in this repo (z88dk, newlib).
#
# A project Makefile is just:
#
#   NAME   := mygame
#   TARGET := zx          # zx = 48K/128K Spectrum (.tap), zxn = Spectrum Next (.nex)
#   include ../../sdk/project.mk
#
# Layout:
#
#   src/*.c src/*.asm  -> compiled (zsdcc, -clib=sdcc_ix) and linked
#   zpragma.inc        -> optional CRT pragmas (stack, heap, printf converters...)
#
# sdk/src/*.c *.asm (zx_log; see sdk/include/zxlog.h) is linked into every
# project.
#
# Outputs:  build/<NAME>.tap   (TARGET=zx: BASIC loader + code, autoloads)
#           build/<NAME>.nex   (TARGET=zxn: loads directly on the Next)
#           build/<NAME>.map   symbols (./zx and the MCP tools read it)
#
# Overridable: ORG (default 32768), STARTUP (default 0 = 32x24 text terminal
#              on stdout; 31 = no terminal, smaller), OPT, CLIB, EXTRA_CFLAGS,
#              EXTRA_LDFLAGS, EXTRA_OBJS, DEBUG=1 (-O0 and no optimiser),
#              ZX_NO_LOG=1 (compile zx_log calls out)
#
# Run via bin/zx-make (or ./zx build), which supplies the toolchain.

ifndef NAME
$(error set NAME before including project.mk)
endif

SDK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

TARGET  ?= zx
CLIB    ?= sdcc_ix
STARTUP ?= 0
ORG     ?= 32768
SRC_DIR ?= src
BUILD   ?= build
OBJDIR  := $(BUILD)/obj

ifeq ($(DEBUG),1)
OPT ?= -O0
else
OPT ?= -SO3
endif

ifeq ($(TARGET),zx)
OUT := $(BUILD)/$(NAME).tap
APPFLAGS := -create-app
else ifeq ($(TARGET),zxn)
OUT := $(BUILD)/$(NAME).nex
APPFLAGS := -subtype=nex -create-app
else
$(error TARGET must be zx or zxn, not $(TARGET))
endif

DEFS := $(if $(filter 1,$(ZX_NO_LOG)),-DZX_NO_LOG)
CFLAGS  = +$(TARGET) -vn -clib=$(CLIB) $(OPT) -I$(SRC_DIR) -I$(SDK_DIR)/include $(DEFS) $(EXTRA_CFLAGS)
PRAGMAS = $(if $(wildcard zpragma.inc),-pragma-include:zpragma.inc)
LDFLAGS = +$(TARGET) -vn -clib=$(CLIB) -startup=$(STARTUP) -zorg=$(ORG) $(PRAGMAS) -m $(EXTRA_LDFLAGS)

SRCS := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*.asm)
SDK_SRCS := $(wildcard $(SDK_DIR)/src/*.c) $(wildcard $(SDK_DIR)/src/*.asm)
OBJS := $(patsubst $(SRC_DIR)/%,$(OBJDIR)/%.o,$(SRCS)) \
        $(patsubst $(SDK_DIR)/src/%,$(OBJDIR)/sdk/%.o,$(SDK_SRCS)) $(EXTRA_OBJS)

.DEFAULT_GOAL := all
all: $(OUT)
	@echo "built $(OUT)"

$(OBJDIR) $(OBJDIR)/sdk:
	mkdir -p $@

$(OUT): $(OBJS) $(wildcard zpragma.inc)
	zcc $(LDFLAGS) $(OBJS) -o $(BUILD)/$(NAME) $(APPFLAGS)
	@rm -f $(BUILD)/$(NAME)

# every object depends on all headers: simple and safe for small projects
HDRS := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(SDK_DIR)/include/*.h)

$(OBJDIR)/%.c.o: $(SRC_DIR)/%.c $(HDRS) | $(OBJDIR)
	zcc $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.asm.o: $(SRC_DIR)/%.asm | $(OBJDIR)
	zcc $(CFLAGS) -c $< -o $@

$(OBJDIR)/sdk/%.c.o: $(SDK_DIR)/src/%.c $(HDRS) | $(OBJDIR)/sdk
	zcc $(CFLAGS) -c $< -o $@

$(OBJDIR)/sdk/%.asm.o: $(SDK_DIR)/src/%.asm | $(OBJDIR)/sdk
	zcc $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
