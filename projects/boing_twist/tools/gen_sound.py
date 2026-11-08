#!/usr/bin/env python3
"""Boing Twist's sound for the Next's TurboSound (AY register streams for
the SDK's aystream player, sdk/tools/aystream.py).

Effects (chip 2): the original synthesises four at start-up (sound_ahi.c
make()): the ball's thud as it lands (a falling 95 Hz tone and a burst of
noise, 0.45 s), a band's click (1400 Hz and noise, 80 ms), a half's swing
(noise rising in pitch, swelling and fading, 0.35 s) and the solved chord
(C E G coming in one after another, 1.4 s). Here the same pitches, lengths
and envelopes, a value each 50 Hz frame. port: a click for the initials.

Music (chips 0 and 1): port: the original has none; a bouncy tune in
C major for the title and the select screen.

Output: sound/bank_24.bin, src/sound_data.asm, src/sound_data.c/.h.
    python3 tools/gen_sound.py
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "..", "sdk", "tools"))
from aystream import Ay, encode, midi_hz, period  # noqa: E402

BANK = 24                            # SOUND_BANK (src/bt.h)


def ay_vol(a):
    """a linear amplitude 0..1 -> the AY's logarithmic volume (about 3 dB a step)"""
    if a <= 0.004:
        return 0
    return max(0, min(15, int(round(15 + 2 * math.log2(a)))))


def frames(secs):
    return max(1, int(round(secs * 50)))


def sfx_thud():
    ay = Ay()
    n = frames(0.45)
    for k in range(n):
        t = k / 50.0
        ay.tone(0, period(95.0 - 45.0 * t / 0.45), ay_vol(math.exp(-t * 11.0) * 0.95))
        a = math.exp(-t * 90.0) * 0.6
        if a > 0.01:
            ay.noise(1, 20, ay_vol(a))
        else:
            ay.off(1)
        ay.frame()
    return ay


def sfx_click():
    ay = Ay()
    for k in range(frames(0.08)):
        t = k / 50.0
        a = math.exp(-t * 60.0)
        ay.tone(0, period(1400), ay_vol(a * 0.6))
        ay.noise(1, 2, ay_vol(a * 0.4))
        ay.frame()
    return ay


def sfx_flip():
    ay = Ay()
    n = frames(0.35)
    for k in range(n):
        t = k / 50.0
        env = math.sin(math.pi * t / 0.35)
        f = 180.0 + 500.0 * t / 0.35
        ay.noise(1, max(1, int(31 - f / 25)), ay_vol(env * env * 0.7))
        ay.tone(0, period(f), ay_vol(env * env * 0.3))
        ay.frame()
    return ay


def sfx_solved():
    ay = Ay()
    for k in range(frames(1.4)):
        t = k / 50.0
        env = (t / 0.02 if t < 0.02 else 1.0) * math.exp(-t * 2.2)
        ay.tone(0, period(523.25), ay_vol(env * 0.9))
        if t > 0.08:
            ay.tone(1, period(659.25), ay_vol(env * 0.9))
        if t > 0.16:
            ay.tone(2, period(783.99), ay_vol(env * 0.9))
        ay.frame()
    return ay


def sfx_key():
    ay = Ay()
    for k in range(5):
        ay.tone(0, period(1200 + 50 * k), 12 - 2 * k)
        ay.frame()
    return ay


SFX = [("thud", sfx_thud), ("click", sfx_click), ("flip", sfx_flip), ("solved", sfx_solved), ("key", sfx_key)]


def finish(ay):
    for ch in range(3):
        ay.off(ch)
    ay.frame()
    return encode(ay)


# ---- music (port) ----
STEP = 12                            # frames an eighth note (125 beats a minute)
CHORDS = ["C", "Am", "F", "G", "C", "Am", "Dm", "G"]
TONES = {"C": (48, 52, 55), "Am": (45, 48, 52), "F": (41, 45, 48), "G": (43, 47, 50), "Dm": (38, 41, 45)}
LEAD = ["E5 G5 C6 - G5 E5 G5 .", "A5 - E5 - C5 - E5 .", "F5 A5 C6 - A5 F5 A5 .", "G5 - B5 - D6 - B5 .",
        "C6 - G5 - E5 G5 C6 .", "A5 C6 E6 - C6 - A5 .", "D6 - A5 - F5 A5 D6 -", "B5 - G5 - D5 - . ."]
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    return 12 * (int(name[-1]) + 1) + NAMES[name[0]] + (1 if "#" in name else 0)


def env(t, length, peak, sustain):
    v = peak - (peak - sustain) * min(1.0, t / 5)
    if t >= length - 2:
        v -= 3
    return max(0, int(round(v)))


def song():
    a, b = Ay(), Ay()
    a.loop_here()
    b.loop_here()
    for bar in range(8):
        lead = LEAD[bar].split()
        root, third, fifth = TONES[CHORDS[bar]]
        notes = []
        for i, tok in enumerate(lead):
            if tok == "-":
                continue
            ln = 1
            while i + ln < 8 and lead[i + ln] == "-":
                ln += 1
            notes.append((i, None if tok == "." else midi(tok), ln))
        for step in range(8):
            for t in range(STEP):
                cur = [n for n in notes if n[0] <= step < n[0] + n[2]]
                if cur and cur[0][1] is not None:
                    start, m, ln = cur[0]
                    a.tone(0, period(midi_hz(m)), env((step - start) * STEP + t, ln * STEP, 12, 9))
                else:
                    a.off(0)
                # the bass bounces: root and fifth on the beats, an octave jump between
                bass = [root - 12, root, fifth - 12, root][step % 4] + (12 if root < 40 else 0)
                if t < STEP - 2:
                    a.tone(1, period(midi_hz(bass)), env(t, STEP, 12, 8))
                else:
                    a.off(1)
                if step % 2 == 1 and t < 2:          # the off-beat tick
                    a.noise(2, 1, 8 - 3 * t)
                else:
                    a.off(2)
                # chip 1: the chord's third held, its fifth stabbed on the off-beats
                b.tone(0, period(midi_hz(third + 12)), 7 if step or t > 1 else 9)
                if step % 2 == 1 and t < STEP - 3:
                    b.tone(1, period(midi_hz(fifth + 12)), env(t, STEP - 3, 9, 6))
                else:
                    b.off(1)
                a.frame()
                b.frame()
    return encode(a), encode(b)


def main():
    blobs = []
    m0, m1 = song()
    blobs += [("music0", m0), ("music1", m1)]
    for name, fn in SFX:
        blobs.append((f"sfx_{name}", finish(fn())))
    data = bytearray()
    where = []
    for name, blob in blobs:
        where.append((name, BANK * 2 + len(data) // 8192, 0x4000 + len(data) % 8192, len(blob)))
        data += blob
    assert len(data) <= 16384
    data += bytes(16384 - len(data))
    os.makedirs(os.path.join(ROOT, "sound"), exist_ok=True)
    with open(os.path.join(ROOT, "sound", f"bank_{BANK}.bin"), "wb") as f:
        f.write(data)
    with open(os.path.join(ROOT, "src", "sound_data.asm"), "w") as f:
        f.write("; made by tools/gen_sound.py - do not edit: the TurboSound streams\n\n")
        f.write(f"SECTION BANK_{BANK}\nBINARY \"sound/bank_{BANK}.bin\"\n")
    with open(os.path.join(ROOT, "src", "sound_data.h"), "w") as f:
        f.write("/* made by tools/gen_sound.py - do not edit */\n#ifndef SOUND_DATA_H\n#define SOUND_DATA_H\n"
                "#include <stdint.h>\n/* a stream: 8K page, address with the page in MMU slot 2 */\n"
                "typedef struct { uint8_t page; uint16_t addr; } Stream;\n"
                "extern const Stream music_streams[2];      /* chips 0 and 1 */\n"
                f"extern const Stream sfx_streams[{len(SFX)}];       /* SFX_* (sound.h) */\n#endif\n")
    with open(os.path.join(ROOT, "src", "sound_data.c"), "w") as f:
        f.write('/* made by tools/gen_sound.py - do not edit */\n#include "sound_data.h"\n\n')
        f.write("const Stream music_streams[2] = {\n")
        for name, page, addr, ln in where[:2]:
            f.write(f"    {{ {page}, 0x{addr:04X} }},   /* {name}: {ln} bytes */\n")
        f.write(f"}};\n\nconst Stream sfx_streams[{len(SFX)}] = {{\n")
        for name, page, addr, ln in where[2:]:
            f.write(f"    {{ {page}, 0x{addr:04X} }},   /* {name}: {ln} bytes */\n")
        f.write("};\n")
    print(f"{len(blobs)} streams, {sum(w[3] for w in where)} bytes, bank {BANK}")


if __name__ == "__main__":
    main()
