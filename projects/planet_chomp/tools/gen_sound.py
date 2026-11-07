#!/usr/bin/env python3
"""Planet Chomp's sound for the Next's TurboSound (sdk/include/aystream.h).

The original synthesises its effects at start-up (tools/orig/sfx_paula.c,
after Sfx.cs): a pitch function per effect, a waveform, a short attack and
release. Here each effect is that pitch function sampled at 50 Hz as AY
tone periods, its envelope as AY volumes:

  chip 2   the crumbs' plinks (the original's channel 0, two alternating)
  chip 0   the other effects (key, eat, death, start, clear, extra life)
  chip 1   the ambience loops: the hum while playing, the bubbling while
           the spooks are frightened (the original's channel 2)

port: the original has no music. The title plays a tune made from its start
jingle (the same notes, G major), on chips 0 and 1: melody, a counter
line, bass and a soft drum (noise).

Output: 16K banks in sound/, src/sound_data.asm, src/sound_data.c/.h.

    python3 tools/gen_sound.py
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "..", "sdk", "tools"))
from aystream import Ay, encode, midi_hz, period  # noqa: E402

FIRST_BANK = 36
RATE = 11050                                    # the original's sample rate

# ---- the original's effects (sfx_paula.c: freq() and the build() calls) ----

START = [392, 523, 659, 587, 523, 440, 523, 0, 587, 698, 880, 784, 698, 659, 784, 0]
CLEAR = [523, 659, 784, 1047, 784, 1047, 1319, 1568, 1319, 1568, 2093, 2093]
EXTRA = [1319, 1568, 2637, 2093, 2349, 3136]
KEY = [523, 659, 784, 1047, 1319]
LOSE = [523, 466, 392]
BUBBLE = [392, 494, 587, 784, 988, 784, 587, 494]


def freq(name, i):
    """Hz at sample i (the original's freq())"""
    if name == "crumb_a":
        return 880 + 110 * i // (RATE // 20)
    if name == "crumb_b":
        return 1047 + 130 * i // (RATE // 20)
    if name == "key":
        return KEY[min(4, i // (RATE // 10))]
    if name == "eat":
        return 1047 if i < RATE // 12 else 1568
    if name == "death":
        n = i // (RATE // 5)
        if n < 3:
            return LOSE[n]
        return 330 - 220 * (i - RATE * 3 // 5) // (RATE * 9 // 10)
    if name == "start":
        return START[min(15, i // (RATE * 12 // 100))]
    if name == "clear":
        return CLEAR[min(11, i // (RATE * 9 // 100))]
    if name == "extra":
        return EXTRA[min(5, i // (RATE * 8 // 100))]
    if name == "hum":
        return 165 if (i // (RATE * 3 // 10)) & 1 else 147
    if name == "fright":
        return BUBBLE[(i // (RATE // 8)) & 7]
    raise KeyError(name)


# name, ms, wave, amp (the original's build() calls), looped, Paula volume of its channel
SFX = [
    ("crumb_a", 50, "tri", 60, False, 24), ("crumb_b", 50, "tri", 60, False, 24),
    ("key", 600, "tri", 80, False, 40), ("eat", 350, "square", 45, False, 40),
    ("death", 1500, "tri", 90, False, 40), ("start", 1900, "square", 35, False, 40),
    ("clear", 1100, "tri", 70, False, 40), ("extra", 500, "tri", 60, False, 40),
    ("hum", 1200, "tri", 100, True, 10), ("fright", 1000, "tri", 100, True, 12),
]


def ay_vol(a):
    """amplitude 0..1 -> AY volume (about 3 dB a step: half the amplitude is 2 steps)"""
    if a <= 0.004:
        return 0
    return max(0, min(15, int(round(15 + 2 * math.log2(a)))))


def sfx_stream(name, ms, wave, amp, looped, chan_vol):
    ay = Ay()
    n = RATE * ms // 1000
    att, rel = RATE // 200, RATE * 3 // 100
    soft = {"square": 1.0, "tri": 0.7, "sine": 0.75}[wave]   # a triangle is quieter than a square
    frames = max(1, int(round(ms / 20)))
    if looped:
        ay.loop_here()
    for k in range(frames):
        i = min(n - 1, k * RATE // 50)
        f = freq(name, i)
        env = 1.0
        if not looped:
            if i < att:
                env = i / att if att else 1
            elif n - i < rel:
                env = (n - i) / rel
        a = (amp / 100) * soft * env * (chan_vol / 40)
        if f > 0 and a > 0:
            ay.tone(0, period(f), ay_vol(a))
        else:
            ay.off(0)
        ay.frame()
    if not looped:
        ay.off(0)
        ay.frame()
    return encode(ay)


# ---- the title tune (port) ----

def title_streams():
    """32 beats of 12 frames (125 bpm, 8ths at 6): bars 1-4 the start jingle's
    notes, bars 5-8 an answer; chip 0: melody (A), bass (B), drum (C:
    noise); chip 1: a counter line (A) an octave down, arpeggio (B)"""
    hz = {"G3": 196, "A3": 220, "B3": 247, "C4": 262, "D4": 294, "E4": 330, "F4": 349, "G4": 392, "A4": 440,
          "B4": 494, "C5": 523, "D5": 587, "E5": 659, "F5": 698, "G5": 784, "A5": 880, "B5": 988, "C6": 1047,
          "D3": 147, "E3": 165, "F3": 175, "C3": 131, "G2": 98, "A2": 110}
    # melody in 8ths ('-' holds, '.' rests): the start jingle, then its answer
    melody = ("G4 C5 E5 D5 C5 A4 C5 .  D5 F5 A5 G5 F5 E5 G5 .  "
              "E5 - D5 C5 D5 - G4 .  A4 C5 B4 A4 G4 - - .  "
              "G4 C5 E5 D5 C5 A4 C5 .  D5 F5 A5 G5 F5 E5 G5 .  "
              "A5 G5 F5 E5 D5 E5 F5 D5 C5 - E5 - C5 - - .").split()
    bass = ("C3 C4 C3 C4 F3 F4 F3 F4 D3 D4 D3 D4 G2 G3 G2 G3 "
            "C3 C4 C3 C4 G2 G3 G2 G3 A2 A3 A2 A3 G2 G3 G2 G3 "
            "C3 C4 C3 C4 F3 F4 F3 F4 D3 D4 D3 D4 G2 G3 G2 G3 "
            "F3 F4 G2 G3 A2 A3 G2 G3 C3 C4 G2 G3 C3 - - .").split()
    chords = [("C4", "E4", "G4"), ("F4", "A4", "C5"), ("D4", "F4", "A4"), ("G3", "B3", "D4"),
              ("C4", "E4", "G4"), ("G3", "B3", "D4"), ("A3", "C4", "E4"), ("G3", "B3", "D4")] * 2
    step = 6

    def resolve(tokens):
        """(note or None, frames since its attack at the eighth's start) per eighth"""
        out, prev, age = [], None, 0
        for tok in tokens:
            if tok == "-":
                age += step
            elif tok == ".":
                prev, age = None, 0
            else:
                prev, age = tok, 0
            out.append((prev, age))
        return out
    mel, bas = resolve(melody), resolve(bass)
    a0, a1 = Ay(), Ay()
    a0.loop_here()
    a1.loop_here()
    for k in range(len(mel)):
        for f in range(step):
            t = k * step + f
            m, age = mel[k]
            if m:
                a0.tone(0, period(hz[m]), max(9, 13 - (age + f) // 3))
                a1.tone(0, period(hz[m] / 2), max(5, 8 - (age + f) // 3))
            else:
                a0.off(0)
                a1.off(0)
            bnote, bage = bas[k]
            if bnote:
                a0.tone(1, period(hz[bnote]), max(8, 12 - (bage + f)))
            else:
                a0.off(1)
            # the drum: a click of noise on the beats, louder on 1 and 3
            if k % 2 == 0 and f < 2:
                a0.noise(2, 6 if (k // 2) % 2 == 0 else 3, 10 if f == 0 else 6)
            else:
                a0.off(2)
            # an arpeggio over the bar's chord, a note every two frames
            ch = chords[k // 8]
            a1.tone(1, period(hz[ch[(t // 2) % 3]] * 2), 6)
            a0.frame()
            a1.frame()
    return encode(a0), encode(a1)


def main():
    blobs = []
    t0, t1 = title_streams()
    blobs += [("title0", t0), ("title1", t1)]
    for s in SFX:
        blobs.append(("sfx_" + s[0], sfx_stream(*s)))
    data = bytearray()
    where = []
    for name, b in blobs:
        off = len(data)
        data += b
        where.append((name, FIRST_BANK * 2 + off // 8192, 0x4000 + off % 8192, len(b)))
    data += bytes((-len(data)) % 16384)
    nbanks = len(data) // 16384
    assert FIRST_BANK + nbanks <= 48, "past 1 MB"
    os.makedirs(os.path.join(ROOT, "sound"), exist_ok=True)
    for i in range(nbanks):
        open(os.path.join(ROOT, "sound", f"bank_{FIRST_BANK + i}.bin"), "wb").write(data[i * 16384:(i + 1) * 16384])
    with open(os.path.join(ROOT, "src", "sound_data.asm"), "w") as f:
        f.write("; made by tools/gen_sound.py - do not edit: the TurboSound streams\n\n")
        for i in range(nbanks):
            f.write(f"SECTION BANK_{FIRST_BANK + i}\nBINARY \"sound/bank_{FIRST_BANK + i}.bin\"\n")
    with open(os.path.join(ROOT, "src", "sound_data.h"), "w") as f:
        f.write("/* made by tools/gen_sound.py - do not edit */\n#ifndef SOUND_DATA_H\n#define SOUND_DATA_H\n"
                "#include <stdint.h>\n/* a stream: 8K page, its address with the page in MMU slot 2 */\n"
                "typedef struct { uint8_t page; uint16_t addr; } Stream;\n"
                "extern const Stream title_streams[2];       /* chips 0, 1 */\n"
                f"extern const Stream sfx_streams[{len(SFX)}];          /* game.h's SFX_* */\n#endif\n")
    with open(os.path.join(ROOT, "src", "sound_data.c"), "w") as f:
        f.write('/* made by tools/gen_sound.py - do not edit */\n#include "sound_data.h"\n\n')
        f.write("const Stream title_streams[2] = {\n")
        for name, page, addr, ln in where[:2]:
            f.write(f"    {{ {page}, 0x{addr:04X} }},   /* {name}: {ln} bytes */\n")
        f.write(f"}};\n\nconst Stream sfx_streams[{len(SFX)}] = {{\n")
        for name, page, addr, ln in where[2:]:
            f.write(f"    {{ {page}, 0x{addr:04X} }},   /* {name}: {ln} bytes */\n")
        f.write("};\n")
    print(f"gen_sound: {len(blobs)} streams, {sum(len(b) for _, b in blobs)} bytes, bank {FIRST_BANK}..{FIRST_BANK + nbanks - 1}")


if __name__ == "__main__":
    main()
