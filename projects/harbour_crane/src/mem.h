/* The memory map (see main.c): at 0x0000 (over the ROM) the rules (bank
   20) or the picture (bank 21, while it draws); the game shell in page 44
   at 0x6000 (l2poly borrows slot 3 while it fills and puts page 44 back).
   The
   SDK's whole-bank DMA copies (l2_copy_to_back, dma_copy_banks) go through
   slots 0-1 and put the ROM back, so the rules are mapped in again after. */
#ifndef MEM_H
#define MEM_H
#include <arch/zxn.h>
#define RULES() do { ZXN_WRITE_MMU0(40); ZXN_WRITE_MMU1(41); } while (0)
#define SHELL() ZXN_WRITE_MMU3(44)
#define VIEW()  do { ZXN_WRITE_MMU0(42); ZXN_WRITE_MMU1(43); } while (0)
#endif
