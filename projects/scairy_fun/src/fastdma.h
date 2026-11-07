/* fastdma.h - dma_copy / dma_to_port with a quicker set-up (fastdma.c) */
#ifndef FASTDMA_H
#define FASTDMA_H
#include <stdint.h>
void fdma_copy(void *dst, const void *src, uint16_t len);
void fdma_to_port(uint16_t port, const void *src, uint16_t len);
#endif
