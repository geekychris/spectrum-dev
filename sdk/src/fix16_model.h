/* fix16_model.h: the C model of fix16.asm's fmul and fdiv (the same steps),
   included by fix16.c on a host with -DFIX16_Z80_C (sdk/tools/test_fix16.c) */

/* bits 16..47 of the 64-bit product (floor), from the magnitudes:
   P = |a| * |b| from one, two or four 16 x 16 products by their sizes (none
   when one is 0 or exactly 65536), R = P >> 16 mod 2^32, and for a negative
   product -(R + (P mod 65536 != 0)) */
fix fmul(fix a, fix b)
{
    uint32_t ua = (uint32_t)(a < 0 ? -a : a), ub = (uint32_t)(b < 0 ? -b : b), r, frac = 0;
    uint16_t al = (uint16_t)ua, ah = (uint16_t)(ua >> 16), bl = (uint16_t)ub, bh = (uint16_t)(ub >> 16);
    uint32_t q;
    if (!ua || !ub) return 0;
    if (ub == 65536UL) r = ua;
    else if (ua == 65536UL) r = ub;
    else if (!ah && !bh) { q = mulu16(al, bl); r = q >> 16; frac = q & 0xFFFF; }
    else if (!ah) { q = mulu16(al, bl); r = mulu16(al, bh) + (q >> 16); frac = q & 0xFFFF; }
    else if (!bh) { q = mulu16(al, bl); r = mulu16(ah, bl) + (q >> 16); frac = q & 0xFFFF; }
    else {
        q = mulu16(al, bl);
        r = mulu16(al, bh) + mulu16(ah, bl) + ((uint32_t)(uint16_t)mulu16(ah, bh) << 16) + (q >> 16);
        frac = q & 0xFFFF;
    }
    if ((a < 0) != (b < 0)) r = 0u - (r + (frac != 0));
    return (fix)r;
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
