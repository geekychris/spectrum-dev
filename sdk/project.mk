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
# OUTPUT := bin (TARGET=zxn) makes build/<NAME>.bin instead: plain code at ORG
#              to load from BASIC and run with USR (the crt returns to BASIC;
#              projects/arcade). nextbasic.py's plus3dos_code() adds the header.
#
# Overridable: ORG (default 32768), STARTUP (default 31 = no terminal; 0 = 32x24
#              printf terminal, about 2 KB more), OPT, CLIB, EXTRA_CFLAGS,
#              EXTRA_LDFLAGS, EXTRA_OBJS, DEBUG=1 (-O0 and no optimiser),
#              ZX_NO_LOG=1 (compile zx_log calls out)
#
# ASSEMBLER=sjasmplus switches to a pure-assembly project (see above).
#
# Run via bin/zx-make (or ./zx build), which supplies the toolchain.

ifndef NAME
$(error set NAME before including project.mk)
endif

SDK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

TARGET  ?= zx
CLIB    ?= sdcc_ix
STARTUP ?= 31
ORG     ?= 32768
SRC_DIR ?= src
BUILD   ?= build
OBJDIR  := $(BUILD)/obj

ifeq ($(DEBUG),1)
OPT ?= -O0
else
OPT ?= -SO3
endif

ifeq ($(ASSEMBLER),sjasmplus)
# Pure assembly with sjasmplus: src/main.asm writes its own output with
# SAVESNA / SAVETAP / SAVENEX (path build/$(NAME).<ext>, see projects/asm48).
OUT := $(BUILD)/$(NAME).$(if $(filter zxn,$(TARGET)),nex,sna)
ASM_MAIN ?= $(SRC_DIR)/main.asm
.DEFAULT_GOAL := all
all: $(OUT)
	@echo "built $(OUT)"
$(OUT): $(wildcard $(SRC_DIR)/*.asm $(SRC_DIR)/*.inc $(SDK_DIR)/include/*.inc)
	mkdir -p $(BUILD)
	sjasmplus --nologo --fullpath -I$(SDK_DIR)/include --sym=$(BUILD)/$(NAME).sym \
	  --lst=$(BUILD)/$(NAME).lst -DNAME=$(NAME) $(EXTRA_ASFLAGS) $(ASM_MAIN)
clean:
	rm -rf $(BUILD)
.PHONY: all clean
else

ifeq ($(TARGET),zx)
OUT := $(BUILD)/$(NAME).tap
APPFLAGS := -create-app
else ifeq ($(TARGET)$(OUTPUT),zxnbin)
OUT := $(BUILD)/$(NAME).bin
APPFLAGS :=
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
# The SDK goes into a library, so a program links only the modules it uses.
# sdk/src/next/ is only for the Next.
SDK_SRCS := $(wildcard $(SDK_DIR)/src/*.c) $(wildcard $(SDK_DIR)/src/*.asm)
ifeq ($(TARGET),zxn)
SDK_SRCS += $(wildcard $(SDK_DIR)/src/next/*.c) $(wildcard $(SDK_DIR)/src/next/*.asm)
endif
OBJS := $(patsubst $(SRC_DIR)/%,$(OBJDIR)/%.o,$(SRCS)) $(EXTRA_OBJS)
SDK_OBJS := $(patsubst $(SDK_DIR)/src/%,$(OBJDIR)/sdk/%.o,$(SDK_SRCS))
SDK_LIB := $(BUILD)/zxsdk.lib

.DEFAULT_GOAL := all
all: $(OUT)
	@echo "built $(OUT)"

$(OBJDIR) $(OBJDIR)/sdk $(OBJDIR)/sdk/next:
	mkdir -p $@

$(SDK_LIB): $(SDK_OBJS)
	z88dk-z80asm $(if $(filter zxn,$(TARGET)),-mz80n) -x$(BUILD)/zxsdk $(SDK_OBJS)

$(OUT): $(OBJS) $(SDK_LIB) $(wildcard zpragma.inc)
	zcc $(LDFLAGS) $(OBJS) -L$(BUILD) -lzxsdk -o $(BUILD)/$(NAME) $(APPFLAGS)
	@rm -f $(BUILD)/$(NAME)
	@if [ "$(OUTPUT)" = bin ]; then mv $(BUILD)/$(NAME)_CODE.bin $@; fi
	@# frame_init() puts the IM 2 table at 0xF000: code + data must end below it.
	@# A BSS moved below the code (zpragma CRT_ORG_BSS) must end before the code.
	@m=$(BUILD)/$(NAME).map; v() { awk -v s="$$1" '$$1 == s {print $$3}' $$m | tr -d '$$'; }; \
	 for s in __BSS_END_tail __DATA_END_tail; do e=$$(v $$s); \
	   if [ -n "$$e" ] && [ $$((0x$$e)) -gt $$((0xF000)) ]; then \
	     echo "error: $(NAME) ends at 0x$$e ($$s), above 0xF000 (frame_init's IM 2 table)"; rm -f $@; exit 1; fi; done; \
	 b=$$(v __BSS_head); be=$$(v __BSS_END_tail); c=$$(v __CODE_head); \
	 if [ -n "$$b" ] && [ $$((0x$$b)) -lt $$((0x$$c)) ] && [ $$((0x$$be)) -gt $$((0x$$c)) ]; then \
	   echo "error: $(NAME)'s BSS (0x$$b-0x$$be) runs into its code at 0x$$c"; rm -f $@; exit 1; fi
	@# Banked sections (--codesegBANK_n / PAGE_n) must fit their 16K bank / 8K page
	@# from where they are linked; the linker doesn't say when they overflow.
	@# SIZE_WARN_ONLY = 1 in a project's Makefile: a warning instead (a known overflow).
	@m=$(BUILD)/$(NAME).map; v() { awk -v s="$$1" '$$1 == s {print $$3}' $$m | tr -d '$$'; }; \
	 for s in $$(awk '$$1 ~ /^__(BANK|PAGE)_[0-9]+_size$$/ {print $$1}' $$m); do \
	   n=$${s%_size}; sz=$$(v $$s); hd=$$(v $${n}_head); \
	   case $$n in __BANK_*) u=16384;; *) u=8192;; esac; \
	   lim=$$((u - (0x$$hd & (u - 1)))); \
	   if [ $$((0x$$sz)) -gt $$lim ]; then \
	     if [ -n "$(SIZE_WARN_ONLY)" ]; then \
	       echo "warning: $(NAME): $${n#__} is $$((0x$$sz)) bytes, $$((0x$$sz - lim)) more than fits from 0x$$hd (what is past the end is read from whatever is mapped next)"; \
	     else echo "error: $(NAME): $${n#__} is $$((0x$$sz)) bytes, $$((0x$$sz - lim)) more than fits from 0x$$hd"; rm -f $@; exit 1; fi; fi; done

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

$(OBJDIR)/sdk/next/%.c.o: $(SDK_DIR)/src/next/%.c $(HDRS) | $(OBJDIR)/sdk/next
	zcc $(CFLAGS) -c $< -o $@

$(OBJDIR)/sdk/next/%.asm.o: $(SDK_DIR)/src/next/%.asm | $(OBJDIR)/sdk/next
	zcc $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean

endif   # ASSEMBLER
