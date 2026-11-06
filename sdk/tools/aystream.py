"""AY register streams for sdk/include/aystream.h (TurboSound player).

    from aystream import Ay, encode, period

  Ay()                      one chip's registers, frame by frame:
      .tone(ch, period, vol)  .noise(ch, period, vol)  .off(ch)
      .frame()              end of this frame (records the registers)
      .loop_here()          the stream restarts here when it loops
  encode(ay)                -> bytes of the stream (only changed registers)
  period(hz, clock=1750000) AY tone period for a frequency (the Next's AY
                            clock is 1.75 MHz)
  midi_hz(n)                frequency of MIDI note n

Test: python3 sdk/tools/test_tools.py
"""

AY_CLOCK = 1750000


def period(hz, clock=AY_CLOCK):
    return max(1, min(4095, int(round(clock / (16.0 * hz))))) if hz > 0 else 0


def midi_hz(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


class Ay:
    def __init__(self):
        self.r = [0] * 14
        self.r[7] = 0x3F
        self.frames = []
        self.loop = None

    def tone(self, ch, per, vol):
        self.r[ch * 2] = per & 0xFF
        self.r[ch * 2 + 1] = (per >> 8) & 0x0F
        self.r[8 + ch] = max(0, min(15, int(vol)))
        self.r[7] = (self.r[7] | (8 << ch)) & ~(1 << ch)

    def noise(self, ch, per, vol):
        self.r[6] = per & 0x1F
        self.r[8 + ch] = max(0, min(15, int(vol)))
        self.r[7] = (self.r[7] | (1 << ch)) & ~(8 << ch)

    def tone_noise(self, ch, per, nper, vol):
        self.tone(ch, per, vol)
        self.r[6] = nper & 0x1F
        self.r[7] &= ~(8 << ch)

    def off(self, ch):
        self.r[8 + ch] = 0
        self.r[7] |= (9 << ch)

    def frame(self):
        self.frames.append(list(self.r))

    def loop_here(self):
        self.loop = len(self.frames)


def encode(ay):
    """records of changed registers; runs of unchanged frames as waits"""
    out = bytearray([0xFF, 0xFF])
    prev = None
    loop_off = None
    i = 0
    n = len(ay.frames)
    while i < n:
        if ay.loop == i:
            loop_off = len(out)
            prev = None                     # the loop point writes everything
        cur = ay.frames[i]
        changed = [r for r in range(14) if prev is None or cur[r] != prev[r]]
        # unchanged frames after this one (not across the loop point)
        wait = 0
        while (i + 1 + wait < n and wait < 255 and ay.frames[i + 1 + wait] == cur
               and ay.loop != i + 1 + wait):
            wait += 1
        m0 = sum(1 << r for r in changed if r < 8)
        m1 = sum(1 << (r - 8) for r in changed if r >= 8) | (0x40 if wait else 0)
        out += bytes([m0, m1]) + bytes(cur[r] for r in changed)
        if wait:
            out.append(wait)
        prev = cur
        i += 1 + wait
    out += bytes([0, 0x80])
    if loop_off is not None:
        out[0], out[1] = loop_off & 255, loop_off >> 8
    return bytes(out)


def decode(data):
    """the frames a stream plays (for tests): list of 14-register lists"""
    r = [0] * 14
    r[7] = 0x3F
    frames = []
    o = 2
    while True:
        m0, m1 = data[o], data[o + 1]
        o += 2
        if m1 & 0x80:
            return frames
        for k in range(14):
            if (m0 if k < 8 else m1) & (1 << (k if k < 8 else k - 8)):
                r[k] = data[o]
                o += 1
        frames.append(list(r))
        if m1 & 0x40:
            frames += [list(r)] * data[o]
            o += 1
