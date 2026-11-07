#!/usr/bin/env python3
"""Block Breaker 3D's sound for the Next's TurboSound (three AY chips).

Effects: the original's (game.c game_sound_init), which it synthesises as
8-bit samples at 11.8 kHz: a tone sliding from f0 to f1 Hz, at an amplitude
(0..127) that fades out over its last `fade` samples, or noise. Here each
becomes an AY register stream at 50 Hz: per frame, the tone's frequency at
that point and its amplitude on the AY's logarithmic volume scale. They play
on chip 2, one at a time (a new one replaces the last), as the original's
single effects channel did.

Music: port: the original has none. An original tune in A minor, as AY
streams on chips 0 and 1: on the title the whole arrangement (lead, bass,
drums; arpeggios and a counter-melody), in play a lighter one (bass, drums,
arpeggios) that leaves the effects room.

    python3 tools/gen_sound.py        (from projects/block_breaker_3d)
Output: sound/bank_<n>.bin (the streams, 16K banks), src/sound_data.asm,
src/sound_data.c/.h (where each stream is).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "..", "sdk", "tools"))
from aystream import Ay, encode, midi_hz, period  # noqa: E402

FIRST_BANK = 36
RATE = 3546895 / 300                    # the original's effects' sample rate (A3_FX_RATE)
SPF = RATE / 50                         # samples a frame

# ---- effects (game.c): segments of ("tone", n, f0, f1, amp, fade) or ("noise", n, amp) ----
SFX = [
    ("wall", [("tone", 200, 300, 260, 50, 150)]),
    ("paddle", [("tone", 700, 520, 620, 100, 500)]),
    ("break", [("noise", 700, 90), ("tone", 700, 900, 1500, 80, 600)]),
    ("hit", [("tone", 500, 1200, 900, 90, 400)]),
    ("steel", [("tone", 900, 2000, 1900, 90, 850)]),
    ("lost", [("tone", 5000, 600, 90, 110, 2500)]),
    ("serve", [("tone", 600, 400, 900, 90, 300)]),
    ("capsule", [("tone", 1000, 659, 659, 90, 120), ("tone", 1000, 880, 880, 90, 120), ("tone", 1000, 1318, 1318, 100, 800)]),
    ("clear", [("tone", 1200, 523, 523, 100, 80), ("tone", 1200, 659, 659, 100, 80), ("tone", 1200, 784, 784, 100, 80),
               ("tone", 1200, 1046, 1046, 110, 1000)]),
    ("over", [("tone", 6000, 440, 110, 110, 3000)]),
    ("win", [("tone", 1500, 784, 784, 100, 80), ("tone", 1500, 1046, 1046, 100, 80), ("tone", 3000, 1568, 1568, 110, 2000)]),
    ("key", [("tone", 300, 1200, 1200, 70, 200)]),      # port: a letter taken in the initials
]


def vol(a):
    """amplitude 0..127 -> AY volume (about 3 dB a step)"""
    return 0 if a < 1 else max(0, min(15, int(round(15 + 2 * math.log2(a / 127.0)))))


def sfx_stream(segs):
    """sample the segments at each frame's middle"""
    samples = []                        # (kind, freq, amp) per sample, sparse: by frame
    total = sum(s[1] for s in segs)
    ay = Ay()
    nf = max(1, int(math.ceil(total / SPF)))
    for f in range(nf):
        t = int((f + 0.5) * SPF)
        if t >= total:
            t = total - 1
        o = 0
        for s in segs:
            if t < o + s[1]:
                i = t - o
                if s[0] == "noise":
                    n, amp = s[1], s[2]
                    env = amp * (n - i) / n
                    ay.noise(0, 6, vol(env))
                else:
                    _, n, f0, f1, amp, fade = s
                    fr = f0 + (f1 - f0) * i / n
                    a = amp * (n - i) / fade if i > n - fade else amp
                    ay.tone(0, period(fr), vol(a))
                break
            o += s[1]
        ay.frame()
    ay.off(0)
    ay.frame()
    return encode(ay)


# ---- music ----
FRAMES_16TH = 6                         # 125 beats a minute
A = 57                                  # A3


def chord(root, minor):
    return [root, root + (3 if minor else 4), root + 7]


# bars: (root, minor)
PROG = [(A, 1), (A - 4, 0), (A + 3, 0), (A - 2, 0),      # Am F C G
        (A, 1), (A - 4, 0), (A - 2, 0), (A + 7 - 12, 0), # Am F G E
        (A - 4, 0), (A - 2, 0), (A, 1), (A, 1),          # F G Am Am
        (A + 5 - 12, 1), (A + 7 - 12, 0), (A, 1), (A + 7 - 12, 0)]   # Dm E Am E

# the lead: per bar, (16th, length in 16ths, MIDI note), two bars at a time
MOTIF_A = [(0, 3, 69), (3, 3, 72), (6, 2, 76), (8, 4, 74), (12, 2, 72), (14, 2, 71),
           (16, 3, 72), (19, 3, 69), (22, 2, 65), (24, 6, 67), (30, 2, 69)]
MOTIF_B = [(0, 2, 76), (2, 2, 77), (4, 4, 79), (8, 2, 77), (10, 2, 76), (12, 4, 74),
           (16, 2, 72), (18, 2, 74), (20, 4, 76), (24, 4, 71), (28, 4, 68)]
MOTIF_C = [(0, 4, 65), (4, 2, 69), (6, 2, 72), (8, 4, 77), (12, 4, 76),
           (16, 4, 74), (20, 2, 72), (22, 2, 71), (24, 8, 69)]
MOTIF_D = [(0, 3, 74), (3, 3, 77), (6, 2, 81), (8, 4, 80), (12, 4, 76),
           (16, 6, 69), (22, 2, 71), (24, 4, 72), (28, 4, 68)]
LEAD = MOTIF_A + [(32 + s, l, n) for s, l, n in MOTIF_B] + [(64 + s, l, n) for s, l, n in MOTIF_C] + \
       [(96 + s, l, n) for s, l, n in MOTIF_D]

KICK = "x.......x.x....."
SNARE = "....x.......x..."
HAT = "..x...x...x...xx"
BASS = "x..x..x.x..x.x.."


def song(full):
    """the tune's two chips: lists of Ay frames"""
    steps = len(PROG) * 16
    n = steps * FRAMES_16TH
    a0, a1 = Ay(), Ay()
    lead_on = {}                        # 16th -> (length, note)
    for s, ln, note in LEAD:
        lead_on[s] = (ln, note)
    cur_lead = None
    for st in range(steps):
        bar, i = divmod(st, 16)
        root, minor = PROG[bar]
        ch = chord(root, minor)
        if st in lead_on:
            ln, note = lead_on[st]
            cur_lead = (st, ln, note)
        for f in range(FRAMES_16TH):
            t = st * FRAMES_16TH + f
            # chip 0 A: the lead (title) / chip 0 A: a soft arpeggio (play)
            if full and cur_lead and st < cur_lead[0] + cur_lead[1]:
                age = (st - cur_lead[0]) * FRAMES_16TH + f
                left = (cur_lead[0] + cur_lead[1] - st) * FRAMES_16TH - f
                v = 13 if age < 2 else 12 if age < 12 else 11
                if left <= 2:
                    v -= 3
                vib = 1 + 0.006 * math.sin(age * 0.9) if age > 10 else 1.0
                a0.tone(0, period(midi_hz(cur_lead[2]) * vib), v)
            elif not full:
                k = (st * 2 + (f >= 3)) % 3
                a0.tone(0, period(midi_hz(ch[k] + 12)), 7 if f % 3 == 0 else 6)
            else:
                a0.off(0)
            # chip 0 B: the bass
            if BASS[i] == "x" or (i % 4 == 0):
                age = f
                a0.tone(1, period(midi_hz(root - 24 + (12 if i in (6, 13) else 0))), 13 - min(age, 4))
            else:
                a0.tone(1, period(midi_hz(root - 24)), max(0, 9 - f))
            # chip 0 C: drums (kick a falling tone, snare noise + tone, hat a tick of noise)
            if KICK[i] == "x" and f < 4:
                a0.tone(2, period(120 - 25 * f), 14 - 2 * f)
            elif SNARE[i] == "x" and f < 4:
                a0.tone_noise(2, period(220), 4, 13 - 2 * f)
            elif HAT[i] == "x" and f < 1:
                a0.noise(2, 1, 9)
            else:
                a0.off(2)
            # chip 1 A: arpeggio up the chord, a note each 1/3 of a 16th
            k = (st * 2 + f // 3) % 4
            note = (ch + [ch[0] + 12])[k] + 12
            if full:
                a1.tone(0, period(midi_hz(note)), 9 if f % 3 == 0 else 8)
            else:
                a1.off(0)
            # chip 1 B: a counter-line on the chord's third, half notes (title)
            if full:
                v = 9 if (i % 8) < 1 else 8 if (i % 8) < 6 else 6
                a1.tone(1, period(midi_hz(ch[1] + (0 if bar % 2 else 12))), v)
            else:
                a1.off(1)
            a1.off(2)
            a0.frame()
            a1.frame()
    return encode(a0), encode(a1)


def main():
    blobs = []
    for name, full in (("title", 1), ("play", 0)):
        s0, s1 = song(full)
        # the streams loop from their start
        blobs.append((f"{name}0", loop_from_start(s0)))
        blobs.append((f"{name}1", loop_from_start(s1)))
    for name, segs in SFX:
        blobs.append((f"sfx_{name}", sfx_stream(segs)))
    data = bytearray()
    where = []
    for name, b in blobs:
        off = len(data)
        data += b
        where.append((name, FIRST_BANK * 2 + off // 8192, 0x4000 + off % 8192, len(b)))
    data += bytes((-len(data)) % 16384)
    nbanks = len(data) // 16384
    os.makedirs(os.path.join(ROOT, "sound"), exist_ok=True)
    for i in range(nbanks):
        open(os.path.join(ROOT, "sound", f"bank_{FIRST_BANK + i}.bin"), "wb").write(data[i * 16384:(i + 1) * 16384])
    with open(os.path.join(ROOT, "src", "sound_data.asm"), "w") as f:
        f.write("; generated by tools/gen_sound.py - do not edit: the TurboSound streams\n\n")
        for i in range(nbanks):
            f.write(f"SECTION BANK_{FIRST_BANK + i}\nBINARY \"sound/bank_{FIRST_BANK + i}.bin\"\n")
    nsfx = len(SFX)
    with open(os.path.join(ROOT, "src", "sound_data.h"), "w") as f:
        f.write("/* generated by tools/gen_sound.py - do not edit */\n#ifndef SOUND_DATA_H\n#define SOUND_DATA_H\n"
                "#include <stdint.h>\n/* a stream: 8K page, address with the page in MMU slot 2 */\n"
                "typedef struct { uint8_t page; uint16_t addr; } Stream;\n"
                "extern const Stream music_streams[2][2];   /* title, play x chip */\n"
                f"extern const Stream sfx_streams[{nsfx}];\n"
                "enum { " + ", ".join(f"SFX_{n.upper()}" for n, _ in SFX) + ", SFX_COUNT };\n#endif\n")
    with open(os.path.join(ROOT, "src", "sound_data.c"), "w") as f:
        f.write('/* generated by tools/gen_sound.py - do not edit */\n#include "sound_data.h"\n\n')
        f.write("const Stream music_streams[2][2] = {\n")
        for k in range(2):
            a, b = where[2 * k], where[2 * k + 1]
            f.write(f"    {{ {{ {a[1]}, 0x{a[2]:04X} }}, {{ {b[1]}, 0x{b[2]:04X} }} }},   /* {a[0][:-1]}: {a[3]} + {b[3]} bytes */\n")
        f.write(f"}};\n\nconst Stream sfx_streams[{nsfx}] = {{\n")
        for name, page, addr, ln in where[4:]:
            f.write(f"    {{ {page}, 0x{addr:04X} }},   /* {name}: {ln} bytes */\n")
        f.write("};\n")
    print(f"{len(blobs)} streams, {len(data)} bytes, banks {FIRST_BANK}..{FIRST_BANK + nbanks - 1}")


def loop_from_start(stream):
    """a stream that loops back to its first record"""
    b = bytearray(stream)
    b[0], b[1] = 2, 0
    return bytes(b)


if __name__ == "__main__":
    main()
