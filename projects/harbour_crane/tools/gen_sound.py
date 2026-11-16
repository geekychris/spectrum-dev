#!/usr/bin/env python3
"""Harbour Crane's sound for the Next's TurboSound.

The original synthesises its effects at start-up (tools/orig/game.c
game_sound_init: tones that slide from f0 to f1 and fade, filtered noise
with an attack and a decay, at 11.8 kHz) and has no music. Here each effect
is the same segments on chip 2, frame by frame (50 a second): a tone's
pitch and volume, the noise's period from its low-pass (more filtering, a
longer period) and its envelope. The AY's volume steps are logarithmic, so
the amplitude a becomes 15 + 2 log2(a / 127).

port: a title tune (the original has none): a shanty-like air in D minor on
chips 0 and 1 (lead, bass and a fiddle-ish drone; a drum on chip 0).

Output (sdk/tools/aystream.py format, sdk/include/aystream.h): sound/bank_N.bin,
src/sound_data.asm, src/sound_data.c/.h.

    python3 tools/gen_sound.py
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PROJ, "..", "..", "sdk", "tools"))
from aystream import Ay, encode, midi_hz, period  # noqa: E402

FIRST_BANK = 26
RATE = 3546895 / 300                     # the original's effects' sample rate
FPS = 50


def vol(a):
    """amplitude 0..127 -> AY volume 0..15"""
    if a <= 0.5:
        return 0
    return max(0, min(15, round(15 + 2 * math.log2(a / 127.0))))


def noise_period(lp):
    """the original's one-pole low-pass on white noise -> the AY noise period"""
    return max(1, min(31, round(lp * 1.6)))


class Fx:
    """segments one after the other, as the original writes them into its buffer"""

    def __init__(self):
        self.segs = []

    def tone(self, n, f0, f1, amp, fade):
        self.segs.append(("t", n, f0, f1, amp, max(1, fade)))
        return self

    def noise(self, n, amp, lp, attack):
        self.segs.append(("n", n, amp, max(1, lp), attack))
        return self

    def stream(self):
        ay = Ay()
        for seg in self.segs:
            n = seg[1]
            frames = max(1, round(n * FPS / RATE))
            for f in range(frames):
                i = int(f * n / frames)
                if seg[0] == "t":
                    _, n, f0, f1, amp, fade = seg
                    hz = f0 + (f1 - f0) * i / n
                    a = amp * (n - i) / fade if i > n - fade else amp
                    ay.tone(0, period(hz), vol(a))
                    ay.off(1)
                else:
                    _, n, amp, lp, attack = seg
                    env = amp * i / attack if i < attack else amp * (n - i) / max(1, n - attack)
                    ay.noise(1, noise_period(lp), vol(env * 0.8))
                    ay.off(0)
                ay.off(2)
                ay.frame()
        for ch in range(3):
            ay.off(ch)
        ay.frame()
        return encode(ay)


def effects():
    """game_sound_init(), in its order (SFX_GRAB .. SFX_GUST)"""
    return [
        ("grab", Fx().noise(300, 80, 2, 2).tone(600, 300, 300, 80, 400)),
        ("release", Fx().tone(600, 500, 350, 80, 400)),
        ("land", Fx().noise(900, 120, 6, 2).tone(700, 140, 110, 90, 600)),
        ("dent", Fx().noise(1200, 127, 2, 2).tone(1200, 220, 160, 110, 1000)),
        ("lost", Fx().noise(3000, 110, 10, 300).tone(3000, 300, 80, 90, 2500)),
        ("bump", Fx().noise(600, 100, 4, 2)),
        ("splash", Fx().noise(3000, 90, 14, 400)),
        ("clear", Fx().tone(1200, 392, 392, 100, 80).tone(1200, 392, 392, 100, 80).tone(2400, 523, 523, 110, 1600)),
        ("over", Fx().tone(6000, 330, 90, 110, 3000)),
        ("win", Fx().tone(1500, 523, 523, 100, 80).tone(1500, 659, 659, 100, 80).tone(3000, 784, 784, 110, 2000)),
        ("gust", Fx().noise(2400, 60, 30, 1200)),
    ]


# ---- the title tune (port): 6/8, a bar = 6 eighths of 7 frames
EIGHTH = 7
D4 = 62
SCALE = {"D": 62, "E": 64, "F": 65, "G": 67, "A": 69, "Bb": 70, "C": 72, "d": 74, "e": 76, "f": 77, "a": 81,
         "c": 72, "C3": 60, "A3": 57, "Bb3": 58, "G3": 55, "F3": 53}
# (note or None, eighths) per bar; chord root per bar (MIDI)
TUNE = [
    ([("A", 1), ("d", 2), ("d", 1), ("c", 1), ("A", 1)], 50),
    ([("G", 2), ("A", 1), ("F", 2), ("D", 1)], 48),
    ([("A", 1), ("d", 2), ("d", 1), ("e", 1), ("f", 1)], 50),
    ([("e", 3), ("e", 2), (None, 1)], 45),
    ([("f", 1), ("e", 1), ("d", 1), ("c", 2), ("A", 1)], 46),
    ([("G", 2), ("Bb", 1), ("A", 2), ("F", 1)], 48),
    ([("E", 1), ("F", 1), ("G", 1), ("A", 2), ("E", 1)], 45),
    ([("D", 3), ("D", 2), (None, 1)], 50),
]


def tune():
    a, b = Ay(), Ay()
    a.loop_here()
    b.loop_here()
    for bar, root in TUNE * 2:
        notes = []
        for n, e in bar:
            notes += [(n, k) for k in range(e * EIGHTH)]
        for f in range(6 * EIGHTH):
            n, age = notes[f]
            if n:
                a.tone(0, period(midi_hz(SCALE[n])), max(6, 13 - age // 4))
            else:
                a.off(0)
            # the bass on the dotted crotchets: root, then the fifth
            beat = f // (3 * EIGHTH)
            bn = root - 12 + (7 if beat else 0)
            a.tone(1, period(midi_hz(bn - 12)), max(0, 12 - (f % (3 * EIGHTH)) // 2))
            # a drum on each dotted crotchet
            fb = f % (3 * EIGHTH)
            if fb < 4:
                a.tone_noise(2, 600 + fb * 300, 8, 13 - fb * 3)
            else:
                a.off(2)
            # chip 1: the drone (root and fifth, quiet) and an echo of the lead
            b.tone(0, period(midi_hz(root)), 6)
            b.tone(1, period(midi_hz(root + 7)), 5)
            if n and age < 2 * EIGHTH:
                b.tone(2, period(midi_hz(SCALE[n] + 12)), max(0, 7 - age // 2))
            else:
                b.off(2)
            a.frame()
            b.frame()
    return encode(a), encode(b)


def main():
    t0, t1 = tune()
    fx = effects()
    blobs = [("title_a", t0), ("title_b", t1)] + [("sfx_" + n, f.stream()) for n, f in fx]
    data = bytearray()
    where = []
    for name, b in blobs:
        where.append((name, FIRST_BANK * 2 + len(data) // 8192, 0x4000 + len(data) % 8192, len(b)))
        data += b
    data += bytes((-len(data)) % 16384)
    nbanks = len(data) // 16384
    os.makedirs(os.path.join(PROJ, "sound"), exist_ok=True)
    asm = ["; generated by tools/gen_sound.py - do not edit: the TurboSound streams", ""]
    for i in range(nbanks):
        open(os.path.join(PROJ, "sound", f"bank_{FIRST_BANK + i}.bin"), "wb").write(data[i * 16384:(i + 1) * 16384])
        asm += [f"SECTION BANK_{FIRST_BANK + i}", f'BINARY "sound/bank_{FIRST_BANK + i}.bin"']
    open(os.path.join(PROJ, "src", "sound_data.asm"), "w").write("\n".join(asm) + "\n")
    names = ["ST_TITLE_A", "ST_TITLE_B"] + ["SFX_" + n.upper() for n, _ in fx]
    h = ["/* generated by tools/gen_sound.py - do not edit */", "#ifndef SOUND_DATA_H", "#define SOUND_DATA_H",
         "#include <stdint.h>", "typedef struct { uint8_t page; uint16_t addr; } Stream;",
         "enum { " + ", ".join(names) + ", ST_COUNT };",
         "extern const Stream streams[ST_COUNT];", "#endif", ""]
    open(os.path.join(PROJ, "src", "sound_data.h"), "w").write("\n".join(h))
    c = ["/* generated by tools/gen_sound.py - do not edit */", '#include "sound_data.h"', "",
         "const Stream streams[ST_COUNT] = {"]
    c += [f"    {{ {p}, 0x{a:04X} }},   /* {n}: {ln} bytes */" for n, p, a, ln in where]
    c += ["};", ""]
    open(os.path.join(PROJ, "src", "sound_data.c"), "w").write("\n".join(c))
    print(f"{len(blobs)} streams, {sum(w[3] for w in where)} bytes, banks {FIRST_BANK}..{FIRST_BANK + nbanks - 1}")


if __name__ == "__main__":
    main()
