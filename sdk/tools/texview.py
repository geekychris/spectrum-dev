"""Views for sdk/include/texview.h: a picture drawn through a table.

    from texview import Stream, from_pixels, model

  Stream(cx, cy)            a view: rows relative to the screen point (cx, cy)
      .row(dy, dx0, recs)   a run of pixels: row cy + dy from column cx + dx0,
                            recs = [(u, v, shade), ...]; rows go in 8K pages
                            (a row never crosses one: 0x80 = the next page)
      .end()                0x7F, the end; .pages = the 8K pages (bytearrays)
  from_pixels(xs, ys, recs, cx, cy)
                            a Stream from pixels in any order, a run a row (a
                            convex shape); rows top to bottom
  model(pages, tex, off, cx, cy, rows=192, out=None)
                            what tv_draw writes: {(x, y): texel | shade}, tex
                            a list of rows of 256 texels (out: a dict to add to)

A pixel is tex[v][(u + off) & 255] | shade. Test: projects/texviewtest.
"""
PAGE = 8192


class Stream:
    def __init__(self, cx, cy):
        self.cx, self.cy = cx, cy
        self.pages = [bytearray()]
        self.pixels = 0
        self.rows = 0

    def row(self, dy, dx0, recs):
        assert -127 <= dy <= 126 and -128 <= dx0 <= 127 and 0 < len(recs) <= 160
        blob = bytes([dy & 255, dx0 & 255, len(recs)]) + bytes(b for r in recs for b in r)
        if len(self.pages[-1]) + len(blob) + 1 > PAGE:
            self.pages[-1].append(0x80)
            self.pages.append(bytearray())
        self.pages[-1] += blob
        self.pixels += len(recs)
        self.rows += 1

    def end(self):
        self.pages[-1].append(0x7F)
        return self


def from_pixels(xs, ys, recs, cx, cy):
    order = sorted(range(len(xs)), key=lambda i: (ys[i], xs[i]))
    st = Stream(cx, cy)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and ys[order[j + 1]] == ys[order[i]]:
            j += 1
        x0 = xs[order[i]]
        assert xs[order[j]] - x0 == j - i, "a row with a hole in it"
        st.row(int(ys[order[i]] - cy), int(x0 - cx), [recs[k] for k in order[i:j + 1]])
        i = j + 1
    return st.end()


def model(pages, tex, off, cx, cy, rows=192, out=None):
    out = {} if out is None else out
    p, pos = 0, 0
    while True:
        b = pages[p][pos]
        if b == 0x80:
            p, pos = p + 1, 0
            continue
        if b == 0x7F:
            return out
        dy = b - 256 if b > 127 else b
        dx = pages[p][pos + 1]
        dx = dx - 256 if dx > 127 else dx
        n = pages[p][pos + 2]
        pos += 3
        y = cy + dy
        for k in range(n):
            u, v, s = pages[p][pos:pos + 3]
            pos += 3
            x = cx + dx + k
            if 0 <= y < rows and 0 <= x < 256:
                out[(x, y)] = tex[v][(u + off) & 255] | s
