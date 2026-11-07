#!/usr/bin/env python3
"""Warden Keep's sound for the Next's TurboSound (AY register streams for
the SDK's aystream player, sdk/tools/aystream.py).

port: the original has no sound at all ("Real audio" is in its README's
follow-up list). These are written for the port, in the same synthesis
terms as the other amiga3d-era games' effects (tones sweeping between two
pitches and fading, decaying noise; the generator is Marble Tilt's):

Effects (chip 2, tone on A, noise on B): a step, a push (a low scrape),
bumping a wall, a jewel (a chime), the room won (a fanfare), time up, a
key in the initials.

Music (chips 0 and 1): a slow tune in A minor for the keep: chip 0 the
lead, the bass and a hi-hat, chip 1 an arpeggio of the chords and a held
root. It plays behind the title and, quieter, in play.

Output: sound/bank_36.bin, src/sound_data.asm, src/sound_data.c/.h.

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
FX_RATE = 3546895 / 300              # A3_FX_RATE: samples a second
SPF = FX_RATE / 50                   # samples a frame


def ay_vol(a):
    """a linear amplitude 0..1 -> the AY's logarithmic volume (about 3 dB a step)"""
    if a <= 0.004:
        return 0
    return max(0, min(15, int(round(15 + 2 * math.log2(a)))))


# ---- effects: game.c game_sound_init(), in order of the SFX_* enum ----
def tone(ay_frames, n, f0, f1, amp, fade, vol):
    """a3_tone: n samples sweeping f0 -> f1 Hz, the last `fade` fading"""
    frames = max(1, int(round(n / SPF)))
    for k in range(frames):
        s = k * SPF
        f = f0 + (f1 - f0) * s / n
        a = amp / 127 * vol / 64
        if s > n - fade:
            a *= max(0.0, (n - s) / fade)
        ay_frames.append(("t", f, a))


def noise(ay_frames, n, amp, lp, attack, vol):
    """a3_noise: noise of amplitude amp, rising over `attack` samples, then
    decaying; lp smooths it (lower: brighter), taken as the noise pitch"""
    frames = max(1, int(round(n / SPF)))
    for k in range(frames):
        s = k * SPF
        a = amp / 127 * vol / 64 * min(1.0, (s + 1) / max(1, attack)) * (1 - s / n)
        ay_frames.append(("n", 4 + lp * 2, a))


def sfx_list():
    """in the order of sound.h's SFX_*: (name, frames)"""
    out = []
    f = []; noise(f, 300, 60, 2, 2, 30); out.append(("step", f))
    f = []; tone(f, 1400, 140, 90, 100, 900, 50); noise(f, 600, 70, 8, 4, 30); out.append(("push", f))
    f = []; noise(f, 400, 90, 3, 2, 40); out.append(("bump", f))
    f = []; tone(f, 700, 1318, 1318, 100, 100, 54); tone(f, 1400, 1760, 1760, 100, 1000, 54); out.append(("jewel", f))
    f = []
    for note in (440, 554, 659):
        tone(f, 1200, note, note, 100, 80, 60)
    tone(f, 3000, 880, 880, 110, 2000, 60)
    out.append(("win", f))
    f = []; tone(f, 6000, 440, 110, 110, 3000, 60); out.append(("timeout", f))
    f = []; tone(f, 240, 1200, 1400, 90, 120, 50); out.append(("key", f))
    return out


def sfx_stream(frames):
    ay = Ay()
    for kind, x, a in frames:
        if kind == "t":
            ay.tone(0, period(x), ay_vol(a))
        else:
            ay.noise(1, int(x), ay_vol(a))
        ay.frame()
        ay.off(0)
        ay.off(1)
    ay.frame()
    return encode(ay)


# ---- music (port) ----
STEP = 15                            # frames an eighth note (100 beats a minute)
CHORDS = ["Am", "F", "G", "E", "Am", "Dm", "E", "Am"]
TONES = {"Am": (45, 48, 52), "F": (41, 45, 48), "G": (43, 47, 50), "E": (40, 44, 47), "Dm": (38, 41, 45)}
# the lead, eighth notes a bar: a note name, "-" holds it, "." a rest
LEAD = ["A4 - C5 - E5 - D5 C5", "A4 - - - F4 - . .", "G4 - B4 - D5 - C5 B4", "G#4 - - - E4 - . .",
        "A4 C5 E5 A5 G5 - E5 -", "F5 - E5 - D5 - . .", "B4 - D5 - G#4 - B4 -", "A4 - - - - - . ."]
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    return 12 * (int(name[-1]) + 1) + NAMES[name[0]] + (1 if "#" in name else 0)


def env(t, length, peak, sustain):
    """a plucked note: peak at once, down to sustain, quiet at the end"""
    v = peak - (peak - sustain) * min(1.0, t / 6)
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
        arp = [root + 24, third + 24, fifth + 24, third + 24] * 2
        # how long each lead note lasts (holds)
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
                ft = step * STEP + t
                # chip 0 A: the lead
                cur = [n for n in notes if n[0] <= step < n[0] + n[2]]
                if cur and cur[0][1] is not None:
                    start, m, ln = cur[0]
                    a.tone(0, period(midi_hz(m)), env((step - start) * STEP + t, ln * STEP, 13, 10))
                else:
                    a.off(0)
                # chip 0 B: the bass, a quarter on beats 1 and 3, root then fifth
                if step % 4 in (0, 1):
                    m = (root if step < 4 else fifth) - 12 + (12 if root < 40 else 0)
                    a.tone(1, period(midi_hz(m)), env((step % 4) * STEP + t, 2 * STEP, 12, 9))
                else:
                    a.off(1)
                # chip 0 C: a hi-hat on the off-beats
                if step % 2 == 1 and t < 2:
                    a.noise(2, 1, 9 - 3 * t)
                else:
                    a.off(2)
                # chip 1 A: the arpeggio, B: the chord's root held
                b.tone(0, period(midi_hz(arp[step])), env(t, STEP, 9, 6))
                b.tone(1, period(midi_hz(root + 12)), 6 if ft < 4 * STEP * 2 - 2 else 4)
                a.frame()
                b.frame()
    return encode(a), encode(b)


def main():
    blobs = []
    m0, m1 = song()
    blobs += [("music0", m0), ("music1", m1)]
    for name, frames in sfx_list():
        blobs.append((f"sfx_{name}", sfx_stream(frames)))
    data = bytearray()
    where = []
    for name, blob in blobs:
        where.append((name, FIRST_BANK * 2 + len(data) // 8192, 0x4000 + len(data) % 8192, len(blob)))
        data += blob
    data += bytes((-len(data)) % 16384)
    nbanks = len(data) // 16384
    assert FIRST_BANK + nbanks <= 48, "past 1 MB"
    os.makedirs(os.path.join(ROOT, "sound"), exist_ok=True)
    for i in range(nbanks):
        with open(os.path.join(ROOT, "sound", f"bank_{FIRST_BANK + i}.bin"), "wb") as f:
            f.write(data[i * 16384:(i + 1) * 16384])
    with open(os.path.join(ROOT, "src", "sound_data.asm"), "w") as f:
        f.write("; made by tools/gen_sound.py - do not edit: the TurboSound streams\n\n")
        for i in range(nbanks):
            f.write(f"SECTION BANK_{FIRST_BANK + i}\nBINARY \"sound/bank_{FIRST_BANK + i}.bin\"\n")
    with open(os.path.join(ROOT, "src", "sound_data.h"), "w") as f:
        f.write("/* made by tools/gen_sound.py - do not edit */\n#ifndef SOUND_DATA_H\n#define SOUND_DATA_H\n"
                "#include <stdint.h>\n/* a stream: 8K page, address with the page in MMU slot 2 */\n"
                "typedef struct { uint8_t page; uint16_t addr; } Stream;\n"
                "extern const Stream music_streams[2];      /* chips 0 and 1 */\n"
                f"extern const Stream sfx_streams[{len(blobs) - 2}];       /* SFX_* (sound.h) */\n#endif\n")
    with open(os.path.join(ROOT, "src", "sound_data.c"), "w") as f:
        f.write('/* made by tools/gen_sound.py - do not edit */\n#include "sound_data.h"\n\n')
        f.write("const Stream music_streams[2] = {\n")
        for name, page, addr, ln in where[:2]:
            f.write(f"    {{ {page}, 0x{addr:04X} }},   /* {name}: {ln} bytes */\n")
        f.write(f"}};\n\nconst Stream sfx_streams[{len(blobs) - 2}] = {{\n")
        for name, page, addr, ln in where[2:]:
            f.write(f"    {{ {page}, 0x{addr:04X} }},   /* {name}: {ln} bytes */\n")
        f.write("};\n")
    print(f"{len(blobs)} streams, {sum(w[3] for w in where)} bytes, bank {FIRST_BANK}")


if __name__ == "__main__":
    main()
