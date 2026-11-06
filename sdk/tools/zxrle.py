"""Run-length packing for sdk/include/zxrle.h (rle_unpack).

    from zxrle import pack, unpack
    data = pack(b"....")            # bytes -> stream (ends with 0x80)

Control byte c: c < 0x80: c + 1 literals; c > 0x80: the next byte c - 0x7E
times (runs of 3..129); 0x80: the end.
"""


def pack(data):
    data = bytes(data)
    out = bytearray()
    lit = bytearray()
    i = 0

    def flush():
        while lit:
            chunk = lit[:128]
            out.append(len(chunk) - 1)
            out.extend(chunk)
            del lit[:128]

    while i < len(data):
        n = 1
        while i + n < len(data) and data[i + n] == data[i] and n < 129:
            n += 1
        if n >= 3:
            flush()
            out += bytes([0x7E + n, data[i]])
            i += n
        else:
            lit.append(data[i])
            i += 1
    flush()
    out.append(0x80)
    return bytes(out)


def unpack(stream):
    out = bytearray()
    i = 0
    while stream[i] != 0x80:
        c = stream[i]
        if c < 0x80:
            out += stream[i + 1:i + 2 + c]
            i += 2 + c
        else:
            out += bytes([stream[i + 1]]) * (c - 0x7E)
            i += 2
    return bytes(out)
