/* fix16_model.h: the C model of fix16.asm's fmul and fdiv (the same steps),
   included by fix16.c on a host with -DFIX16_Z80_C (sdk/tools/test_fix16.c) */

/* bits 16..47 of the 64-bit product: the unsigned product of the halves,
   then, for each negative operand, the other one's low half taken off the
   high word (two's complement: a = ua - 2^32 when negative) */
fix fmul(fix a, fix b)
{
    uint16_t al = (uint16_t)a, ah = (uint16_t)((uint32_t)a >> 16);
    uint16_t bl = (uint16_t)b, bh = (uint16_t)((uint32_t)b >> 16);
    uint32_t p = mulu16(al, bl) >> 16;
    p += mulu16(al, bh);
    p += mulu16(ah, bl);
    p += (uint32_t)(uint16_t)(ah * bh) << 16;
    if (a < 0) p -= (uint32_t)bl << 16;
    if (b < 0) p -= (uint32_t)al << 16;
    return (fix)p;
}

/* |a| << 16 / |b| in 48 restoring steps, one 32-bit register for the
   dividend's bits going out and the quotient's coming in: 32 steps (fewer
   for |a|'s leading zero bytes) take |a|'s bits, the last 16 zeros */
fix fdiv(fix a, fix b)
{
    uint32_t sr = (uint32_t)(a < 0 ? -a : a), d = (uint32_t)(b < 0 ? -b : b), r = 0;
    uint8_t k = 48, top;
    if (!sr) return 0;
    while (!(sr >> 24)) { sr <<= 8; k -= 8; }
    while (k--) {
        top = (uint8_t)(r >> 31);
        r = (r << 1) | (k >= 16 ? sr >> 31 : 0);
        sr <<= 1;
        if (top || r >= d) { r -= d; sr |= 1; }
    }
    return (a < 0) != (b < 0) ? -(fix)sr : (fix)sr;
}
