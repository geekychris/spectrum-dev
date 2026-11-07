#!/usr/bin/env python3
"""Rotoball's music and effects for the Next's TurboSound.

The original (amiga_mcp/examples/rotoball/sound.c) has two sampled
effects and no music:

  CLANG  goals: six inharmonic partials (about 101, 224, 438, 732, 1165
         and 1855 Hz) with a noise attack, 0.36 s, squared decay
  PING   pickups and tackles: about 424 Hz plus its third harmonic,
         65 ms, squared decay

Here they are AY effects on chip 2 (three partials at once). port: added
for the Spectrum version: a title tune (chips 0 and 1: lead, bass, drums;
arpeggio, pad), a full-time jingle, the kick-off whistle and a menu blip.

Output (sdk/tools/aystream.py format): sound/bank_N.bin, src/sound_data.asm,
src/sound_data.c/.h (where each stream is).

    python3 tools/gen_sound.py
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PROJ, "..", "..", "sdk", "tools"))
from aystream import Ay, encode, midi_hz, period  # noqa: E402

FIRST_BANK = 30
FPS = 50

# ---- the title tune: A minor, 125 bpm (a beat = 24 frames, an 8th = 12)
EIGHTH = 12
CHORDS = [(57, "m"), (53, ""), (48, ""), (55, "")] * 2 + [(50, "m"), (53, ""), (52, "m"), (52, "")]
LEAD = [  # a bar of 8 eighths a chord (MIDI, 0 = rest, -1 = hold)
    [69, -1, 72, 74, 76, -1, 74, 72], [72, -1, 69, -1, 65, -1, 69, 72],
    [67, -1, 72, -1, 76, 74, 72, 67], [71, -1, 74, -1, 79, -1, 74, 71],
    [69, 72, 76, 81, 79, -1, 76, 74], [77, -1, 76, 74, 72, -1, 69, -1],
    [72, 76, 79, -1, 76, -1, 72, 76], [74, -1, 71, -1, 67, -1, 0, 0],
    [74, -1, 77, -1, 81, -1, 77, 74], [72, -1, 77, -1, 81, 79, 77, 76],
    [76, -1, 79, -1, 83, -1, 79, 76], [80, -1, 76, -1, 71, -1, 68, -1],
]


def chord_tones(root, kind):
    third = 3 if kind == "m" else 4
    return [root, root + third, root + 7]


def vol_env(age, top=13, decay=0.035, floor=8):
    return max(floor, top - age * decay * 12)


def title_streams():
    a, b = Ay(), Ay()
    a.loop_here()
    b.loop_here()
    total = len(CHORDS) * 8 * EIGHTH
    lead_note, lead_age = 0, 0
    for f in range(total):
        bar, pos = divmod(f, 8 * EIGHTH)
        eighth, sub = divmod(pos, EIGHTH)
        root, kind = CHORDS[bar]
        tones = chord_tones(root, kind)
        # lead
        n = LEAD[bar][eighth]
        if sub == 0 and n != -1:
            lead_note, lead_age = n, 0
        if lead_note:
            vib = 1.0 + 0.004 * math.sin(lead_age * 0.6) if lead_age > 10 else 1.0
            a.tone(0, period(midi_hz(lead_note) * vib), max(0, 13 - lead_age // 6))
        else:
            a.off(0)
        lead_age += 1
        # bass: root in octaves, eighths
        bn = root - 24 + (12 if eighth % 2 else 0)
        a.tone(1, period(midi_hz(bn)), max(0, 13 - sub))
        # drums: kick on beats 1 and 3, snare on 2 and 4, hats on the off-beats
        beat_sub = pos % (2 * EIGHTH)
        beat = pos // (2 * EIGHTH)
        if beat in (0, 2) and beat_sub < 6:
            a.tone(2, 400 + beat_sub * 250, 15 - beat_sub * 2)
        elif beat in (1, 3) and beat_sub < 6:
            a.tone_noise(2, 300, 6, 13 - beat_sub * 2)
        elif sub < 2 and eighth % 2:
            a.noise(2, 1, 8 - sub * 3)
        else:
            a.off(2)
        # chip 1: arpeggio (16ths) and the pad's root
        arp = tones[(pos // 6) % 3] + 12
        b.tone(0, period(midi_hz(arp)), 9 - (pos % 6))
        b.tone(1, period(midi_hz(tones[0])), 7)
        b.tone(2, period(midi_hz(tones[1])), 6)
        a.frame()
        b.frame()
    return encode(a), encode(b)


def jingle_streams():
    """full time: a rising arpeggio and a held chord"""
    a, b = Ay(), Ay()
    notes = [57, 60, 64, 69, 72, 76]
    for f in range(110):
        k = min(f // 6, len(notes) - 1)
        a.tone(0, period(midi_hz(notes[k])), 13 if f < 90 else 13 - (f - 90) // 2)
        a.tone(1, period(midi_hz(45)), 12 if f < 90 else 12 - (f - 90) // 2)
        a.off(2)
        if f >= 36:
            b.tone(0, period(midi_hz(69)), 10 if f < 90 else 10 - (f - 90) // 2)
            b.tone(1, period(midi_hz(72)), 9 if f < 90 else 9 - (f - 90) // 2)
            b.tone(2, period(midi_hz(76)), 9 if f < 90 else 9 - (f - 90) // 2)
        else:
            for ch in range(3):
                b.off(ch)
        a.frame()
        b.frame()
    for ay in (a, b):
        for ch in range(3):
            ay.off(ch)
        ay.frame()
    return encode(a), encode(b)


def squared(t, length):
    e = max(0.0, 1.0 - t / length)
    return e * e


def clang():
    """the original's CLANG: partials 224, 438, 732 Hz (and 101 Hz under the
    noise attack), 0.36 s = 18 frames, squared decay"""
    ay = Ay()
    for f in range(18):
        e = squared(f, 18)
        v = 15 * math.sqrt(e) if e > 0 else 0       # the AY's volume steps are logarithmic
        if f == 0:
            ay.tone_noise(0, period(224), 4, 15)
        else:
            ay.tone(0, period(224), v)
        ay.tone(1, period(438), v * 0.85)
        ay.tone(2, period(732 if f % 2 else 101), v * 0.8)
        ay.frame()
    for ch in range(3):
        ay.off(ch)
    ay.frame()
    return encode(ay)


def ping():
    """the original's PING: 424 Hz + 1272 Hz, 65 ms (4 frames)"""
    ay = Ay()
    for f in range(4):
        e = squared(f, 4)
        v = 15 * math.sqrt(e)
        ay.tone(0, period(424), v)
        ay.tone(1, period(1272), v * 0.6)
        ay.off(2)
        ay.frame()
    for ch in range(3):
        ay.off(ch)
    ay.frame()
    return encode(ay)


def whistle():
    """the kick-off: a referee's two-tone whistle with breath noise"""
    ay = Ay()
    for f in range(30):
        hz = 2100 if (f // 3) % 2 else 2300
        v = 12 if f < 24 else 12 - (f - 24) * 2
        ay.tone_noise(0, period(hz), 2, v)
        ay.tone(1, period(hz * 1.01), v - 3)
        ay.off(2)
        ay.frame()
    for ch in range(3):
        ay.off(ch)
    ay.frame()
    return encode(ay)


def blip():
    ay = Ay()
    for f, hz in enumerate((1200, 1600, 1600)):
        ay.tone(0, period(hz), 11 - f * 3)
        ay.frame()
    ay.off(0)
    ay.frame()
    return encode(ay)


def main():
    t0, t1 = title_streams()
    j0, j1 = jingle_streams()
    blobs = [("title_a", t0), ("title_b", t1), ("jingle_a", j0), ("jingle_b", j1),
             ("sfx_clang", clang()), ("sfx_ping", ping()), ("sfx_whistle", whistle()), ("sfx_blip", blip())]
    data = bytearray()
    where = []
    for name, b in blobs:
        # (a stream may cross into the next page: the player maps each byte's page)
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
    h = ["/* generated by tools/gen_sound.py - do not edit */", "#ifndef SOUND_DATA_H", "#define SOUND_DATA_H",
         "#include <stdint.h>", "typedef struct { uint8_t page; uint16_t addr; } Stream;",
         "enum { ST_TITLE_A, ST_TITLE_B, ST_JINGLE_A, ST_JINGLE_B, ST_CLANG, ST_PING, ST_WHISTLE, ST_BLIP };",
         f"extern const Stream streams[{len(blobs)}];", "#endif", ""]
    open(os.path.join(PROJ, "src", "sound_data.h"), "w").write("\n".join(h))
    c = ["/* generated by tools/gen_sound.py - do not edit */", '#include "sound_data.h"', "",
         f"const Stream streams[{len(blobs)}] = {{"]
    c += [f"    {{ {p}, 0x{a:04X} }},   /* {n}: {ln} bytes */" for n, p, a, ln in where]
    c += ["};", ""]
    open(os.path.join(PROJ, "src", "sound_data.c"), "w").write("\n".join(c))
    print(f"{len(blobs)} streams, {sum(w[3] for w in where)} bytes, banks {FIRST_BANK}..{FIRST_BANK + nbanks - 1}")


if __name__ == "__main__":
    main()
