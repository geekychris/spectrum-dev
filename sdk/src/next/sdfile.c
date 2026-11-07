/* Files on the SD card through esxDOS (see sdfile.h). */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include <arch/zxn/esxdos.h>
#include "nextgfx.h"
#include "sdfile.h"

int16_t sd_load(const char *path, void *dst, uint16_t max)
{
    uint16_t n;
    uint8_t h = esx_f_open(path, ESX_MODE_READ | ESX_MODE_OPEN_EXIST);
    if (h == 0xFF) return -1;
    n = esx_f_read(h, dst, max);
    esx_f_close(h);
    return (int16_t)n;
}

int16_t sd_save(const char *path, const void *src, uint16_t len)
{
    uint16_t n;
    uint8_t h = esx_f_open(path, ESX_MODE_WRITE | ESX_MODE_OPEN_CREAT_TRUNC);
    if (h == 0xFF) return -1;
    n = esx_f_write(h, (void *)src, len);
    esx_f_close(h);
    return (int16_t)n;
}

/* a column at a time, read straight into the Layer 2 page mapped by
   l2w_column (esxDOS writes through the current memory map) */
uint8_t sd_load_l2w(const char *path, uint16_t x, uint8_t y, uint16_t w, uint8_t h)
{
    uint8_t f = esx_f_open(path, ESX_MODE_READ | ESX_MODE_OPEN_EXIST), ok = 1;
    if (f == 0xFF) return 0;
    for (; w; w--, x++)
        if (esx_f_read(f, l2w_column(x) + y, h) != h) { ok = 0; break; }
    l2w_done();
    esx_f_close(f);
    return ok;
}
