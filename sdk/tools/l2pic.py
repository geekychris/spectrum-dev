"""Column RLE for sdk/include/l2pic.h: a picture wider than the screen,
stored in 8K pages for the Next to copy into Layer 2 as it scrolls.

    from l2pic import pack
    pages, index = pack(columns, row0, first_page)

columns: a list of 256-value columns (palette indices, top to bottom);
rows row0..255 of each are packed as runs of (count 1..255, colour), and no
column is split across two pages. pages: the 8K pages (bytes) to put at
first_page..; index: 3 bytes a column (its page, its address with the page
in MMU slot 2, 0x4000 + offset), for l2pic_init(). Test: test_tools.py.
"""


def rle_column(col, row0):
    out = bytearray()
    y = row0
    while y < 256:
        c = int(col[y])
        n = 1
        while y + n < 256 and int(col[y + n]) == c and n < 255:
            n += 1
        out += bytes([n, c])
        y += n
    return bytes(out)


def unpack_column(data, row0):
    """the inverse, for tests: the rows row0..255 of a column"""
    out, i = [], 0
    while len(out) < 256 - row0:
        out += [data[i + 1]] * data[i]
        i += 2
    return out


def pack(columns, row0, first_page):
    pages, index, cur = [], bytearray(), bytearray()
    for col in columns:
        r = rle_column(col, row0)
        if len(cur) + len(r) > 8192:
            pages.append(bytes(cur))
            cur = bytearray()
        addr = 0x4000 + len(cur)
        index += bytes([first_page + len(pages), addr & 0xFF, addr >> 8])
        cur += r
    pages.append(bytes(cur))
    return pages, bytes(index)
