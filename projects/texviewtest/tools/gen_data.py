#!/usr/bin/env python3
"""texviewtest's view: a disc 20 pixels round, each pixel's texel from its
place (u = 5x, v = 30 + y), every other column shaded (bit 5): into bank 40
(gfx/bank_40.bin, src/view_data.asm). test.py makes the same disc."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PROJ, "..", "..", "sdk", "tools"))
from texview import from_pixels  # noqa: E402

BANK = 40


def disc():
    xs, ys, recs = [], [], []
    for y in range(-20, 21):
        for x in range(-20, 21):
            if x * x + y * y <= 400:
                xs.append(x)
                ys.append(y)
                recs.append(((5 * x) & 255, 30 + y, (x & 1) << 5))
    return from_pixels(xs, ys, recs, 0, 0)


if __name__ == "__main__":
    st = disc()
    data = b"".join(bytes(p) + bytes(8192 - len(p)) for p in st.pages)
    data += bytes((-len(data)) % 16384)
    assert len(data) == 16384
    open(os.path.join(PROJ, "gfx", f"bank_{BANK}.bin"), "wb").write(data)
    open(os.path.join(PROJ, "src", "view_data.asm"), "w").write(
        f"; made by tools/gen_data.py\nSECTION BANK_{BANK}\nBINARY \"gfx/bank_{BANK}.bin\"\n")
    print(f"{st.pixels} pixels, {len(st.pages)} page(s)")
