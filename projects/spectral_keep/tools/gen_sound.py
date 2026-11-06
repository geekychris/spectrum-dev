#!/usr/bin/env python3
"""Spectral Keep's music and effects for the Next's TurboSound.

The original renders its music offline from a seeded composer (the 3DO
version's tools/assets.py, after the Unity game's Music.cs, with .NET's
System.Random so the melodies match) and its effects from Beeper.cs's
table. This is that composer and that table, unchanged (copied below),
played on three AY chips instead of being rendered to samples:

  chip 0   A lead, B bass, C drums (kick: a falling tone, snare: tone +
           noise, hat: a click of noise)
  chip 1   A arpeggio, B and C the pad's root and fifth
  chip 2   effects (one at a time; a new one replaces the last)

Each voice's volume follows the original's envelope (attack, decay,
release) sampled at 50 Hz on the AY's logarithmic volume steps.
port: the pad's third, the lead's echo and vibrato and the filters are not
played (six channels for seven voices).

Output (tools/aystream format, sdk/include/aystream.h): 16K banks in
sound/, src/sound_data.asm, src/sound_data.c/.h with where each stream is.

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
FPS = 50.0

# ---- .NET System.Random (the seeded, Knuth subtractive generator) ----

class NetRandom:
    MBIG = 2147483647

    def __init__(self, seed):
        sa = [0] * 56
        sub = 2147483647 if seed == -2147483648 else abs(seed)
        mj = 161803398 - sub
        sa[55] = mj
        mk = 1
        for i in range(1, 55):
            ii = (21 * i) % 55
            sa[ii] = mk
            mk = mj - mk
            if mk < 0:
                mk += self.MBIG
            mj = sa[ii]
        for _ in range(4):
            for i in range(1, 56):
                sa[i] -= sa[1 + (i + 30) % 55]
                if sa[i] < 0:
                    sa[i] += self.MBIG
        self.sa, self.inext, self.inextp = sa, 0, 21

    def _sample(self):
        a = self.inext + 1
        if a >= 56:
            a = 1
        b = self.inextp + 1
        if b >= 56:
            b = 1
        r = self.sa[a] - self.sa[b]
        if r == self.MBIG:
            r -= 1
        if r < 0:
            r += self.MBIG
        self.sa[a] = r
        self.inext, self.inextp = a, b
        return r * (1.0 / self.MBIG)

    def next_double(self):
        return self._sample()

    def next(self, lo, hi=None):
        if hi is None:
            lo, hi = 0, lo
        return int(self._sample() * (hi - lo)) + lo


# ---- songs (Music.cs) ----

MINOR = [0, 2, 3, 5, 7, 8, 10]
DORIAN = [0, 2, 3, 5, 7, 9, 10]
HARMONIC = [0, 2, 3, 5, 7, 8, 11]


def song(name, bpm, root, seed, scale, bars, kick, snare, hat, bass, arp=None, pad=1.0,
         arp_level=1.0, lead=1.0, drums=1.0, bass_level=1.0, lead_octave=1):
    return dict(name=name, bpm=bpm, root=root, seed=seed, scale=scale, bars=bars, kick=kick,
                snare=snare, hat=hat, bass=bass, arp=arp or [0, 1, 2, 3, 2, 1, 0, 1], pad=pad,
                arp_level=arp_level, lead=lead, drums=drums, bass_level=bass_level,
                lead_octave=lead_octave)


SONGS = {
    "title": song("SPECTRAL KEEP", 108, 57, 11, MINOR, [0, 5, 2, 6, 0, 5, 2, 6, 3, 5, 0, 6, 3, 5, 6, 6],
                  "x.....x.x.......", "....x.......x...", "..x...x...x...x.", "x..x..x.x..x..x.", pad=1.1),
    "gatehouse": song("THE GATEHOUSE", 124, 50, 23, DORIAN, [0, 3, 0, 6, 0, 3, 6, 3, 2, 3, 0, 6, 2, 3, 4, 4],
                      "x...x...x...x...", "....x.......x...", ".x.x.x.x.x.x.x.x", "x.xx..x.x.xx..x.",
                      arp=[0, 2, 1, 3, 0, 2, 1, 2], pad=0.7),
    "crypt": song("THE CRYPT", 92, 52, 37, HARMONIC, [0, 5, 3, 4, 0, 5, 1, 4, 0, 3, 5, 4, 5, 3, 1, 4],
                  "x.......x..x....", "........x.......", "x...x...x...x...", "x.......x.x.....",
                  arp=[0, 1, 2, 1], pad=1.5, arp_level=0.6, drums=0.7, lead_octave=1),
    "tower": song("THE TOWER", 132, 48, 41, MINOR, [0, 5, 2, 6, 0, 5, 2, 6, 5, 6, 0, 0, 5, 6, 4, 4],
                  "x..x..x.x..x..x.", "....x.......x..x", "xxxxxxxxxxxxxxxx", "xx.xx.xxxx.xx.x.",
                  arp=[0, 1, 2, 3, 1, 2, 3, 2], pad=0.8, drums=1.1),
}


def note(s, degree):
    return s["root"] + 12 * (degree // 7) + s["scale"][degree % 7]


RHYTHMS = ["x..x..x.x...x...", "x.x.x...x..x....", "x...x.x.x.x.x...",
           "x..x..x...x.x...", "x.....x.x.x.x...", "x..x.x..x...x..."]


def motif(rng):
    notes = []
    deg = rng.next(0, 5)
    for bar in range(2):
        r = RHYTHMS[rng.next(len(RHYTHMS))]
        on = [i for i in range(16) if r[i] == "x"]
        for k in range(len(on)):
            nxt = on[k + 1] if k + 1 < len(on) else 16
            ln = min(nxt - on[k], 6)
            deg = max(-1, min(8, deg + rng.next(-2, 3)))
            notes.append((bar * 16 + on[k], ln * 0.92, deg))
    return notes


def nearest_chord_tone(want, chord_deg):
    best, bd = want, 99
    for o in range(-14, 22):
        d = chord_deg + o
        if (d - chord_deg) % 7 not in (0, 2, 4):
            continue
        if abs(d - want) < bd:
            bd, best = abs(d - want), d
    return best


def compose(s):
    ev = []
    rng = NetRandom(s["seed"])
    bars = len(s["bars"])
    for b in range(bars):
        deg = s["bars"][b]
        chord = [deg, deg + 2, deg + 4, deg + 7]
        for i in range(16):
            step = b * 16 + i
            if s["kick"][i] == "x":
                ev.append(("kick", step, 2, 0))
            if s["snare"][i] == "x":
                ev.append(("snare", step, 2, 0))
            if s["hat"][i] == "x":
                ev.append(("hat", step, 1, 0))
            if s["bass"][i] == "x":
                ln = 1
                while i + ln < 16 and s["bass"][i + ln] != "x" and ln < 4:
                    ln += 1
                octave = (i // 4) % 2 == 1 and rng.next_double() < 0.3
                ev.append(("bass", step, ln * 0.9, note(s, deg) - 12 + (12 if octave else 0)))
            if i % 2 == 0 or s["bpm"] < 110:
                a = s["arp"][(i // (1 if s["bpm"] < 110 else 2)) % len(s["arp"])]
                ev.append(("arp", step, 0.9, note(s, chord[a]) + 12))
        for c in (deg, deg + 2, deg + 4):
            ev.append(("pad", b * 16, 16, note(s, c) + 12))
    ma, mb = motif(rng), motif(rng)
    for section in range(bars // 4):
        m = mb if section == 2 else ma
        lift = 2 if section == 2 else 0
        for half in range(2):
            vary = half == 1 and section != 2
            for pos, ln, contour in m:
                step = section * 64 + half * 32 + pos
                deg = s["bars"][step // 16]
                want = contour + lift + ((pos % 3) - 1 if vary and pos >= 24 else 0) + s["lead_octave"] * 7
                if pos % 4 == 0:
                    want = nearest_chord_tone(want, deg)
                ev.append(("lead", step, ln, note(s, want) + 12))
    return ev


def sfx_list():
    lerp = lambda a, b, t: a + (b - a) * min(1.0, max(0.0, t))
    relic = [523, 659, 784, 1047, 784, 1047, 1319]
    done = [392, 523, 659, 784, 659, 784, 1047, 0, 1047, 1047, 1047]
    start = [523, 0, 523, 659, 784]
    return [
        ("jump", 0.12, lambda t: lerp(300, 900, t / 0.12), 0),
        ("land", 0.03, lambda t: 120, 0),
        ("pickup", 0.18, lambda t: 1200 if t < 0.06 else 1600 if t < 0.12 else 2000, 0),
        ("relic", 0.5, lambda t: relic[min(6, int(t / 0.07))], 0),
        ("key", 0.25, lambda t: 1800 if t < 0.08 else 900 if t < 0.16 else 1800, 0),
        ("gate", 0.4, lambda t: lerp(90, 220, t / 0.4) * (1.0 if (t % 0.05) < 0.025 else 1.5), 0),
        ("death", 0.9, lambda t: lerp(1400, 60, math.sqrt(t / 0.9)), 0.35),
        ("door", 0.06, lambda t: lerp(200, 500, t / 0.06), 0),
        ("talk", 0.2, lambda t: 700 + 300 * (1 if math.sin(t * 60) >= 0 else -1), 0),
        ("bark", 0.16, lambda t: lerp(500, 180, t / 0.16), 0.4),
        ("done", 1.6, lambda t: done[min(10, int(t / 0.14))], 0),
        ("start", 0.7, lambda t: start[min(4, int(t / 0.14))], 0),
        ("blip", 0.03, lambda t: 1500, 0),
    ]


# ---- the Next's part ----

def vol(a, top=0.9):
    """amplitude (the original's mix levels) -> AY volume 0..15, 3 dB a step"""
    if a <= 0:
        return 0
    return max(0, min(15, int(round(15 + 2.0 * math.log2(a / top)))))


class Voice:
    """the note playing on one channel: start frame, length (s), what"""

    def __init__(self):
        self.ev = None


def song_streams(s):
    """two chips of music for song s, one frame at a time"""
    step_frames = FPS * 60.0 / s["bpm"] / 4.0
    nframes = int(round(len(s["bars"]) * 16 * step_frames))
    by_frame = {}
    for voice, st, steps, midi in compose(s):
        f = int(round(st * step_frames))
        by_frame.setdefault(f, []).append((voice, steps * step_frames / FPS, midi))
    chips = [Ay(), Ay()]
    for ay in chips:
        ay.loop_here()                             # music loops from the top
    playing = {}                                   # channel key -> (start frame, dur s, voice, midi)
    route = {"lead": (0, 0), "bass": (0, 1), "drum": (0, 2), "arp": (1, 0), "pad0": (1, 1), "pad1": (1, 2)}
    drum_rank = {"kick": 3, "snare": 2, "hat": 1}
    for f in range(nframes):
        pads = []
        for voice, dur, midi in by_frame.get(f, []):
            if voice in ("kick", "snare", "hat"):
                cur = playing.get("drum")
                if cur and cur[0] == f and drum_rank[cur[2]] >= drum_rank[voice]:
                    continue
                playing["drum"] = (f, dur, voice, 0)
            elif voice == "pad":
                pads.append((dur, midi))
            else:
                playing[voice] = (f, dur, voice, midi)
        if pads:
            pads.sort(key=lambda p: p[1])
            playing["pad0"] = (f, pads[0][0], "pad", pads[0][1])
            playing["pad1"] = (f, pads[-1][0], "pad", pads[-1][1])
        for key, (chip, ch) in route.items():
            ay = chips[chip]
            if key not in playing:
                ay.off(ch)
                continue
            start, dur, voice, midi = playing[key]
            t = (f - start) / FPS
            if voice == "kick":
                a = 0.9 * s["drums"] * math.exp(-t * 7.0)
                if t > 0.35:
                    a = 0
                ay.tone(ch, period(45.0 + 110.0 * math.exp(-t * 28.0)), vol(a))
            elif voice == "snare":
                a = 0.38 * s["drums"] * math.exp(-t * 16.0) if t < 0.25 else 0
                ay.tone_noise(ch, period(185.0), 6, vol(a))
            elif voice == "hat":
                a = 0.11 * s["drums"] * math.exp(-t * 55.0) if t < 0.07 else 0
                ay.noise(ch, 1, vol(a * 1.5))
            elif voice == "bass":
                env = min(1.0, (t + 0.01) / 0.005) * (1.0 if t < dur else math.exp(-(t - dur) * 40.0))
                ay.tone(ch, period(midi_hz(midi)), vol(0.32 * s["bass_level"] * env * (0.6 + 0.4 * math.exp(-t * 9.0))))
            elif voice == "arp":
                ay.tone(ch, period(midi_hz(midi)), vol(0.075 * s["arp_level"] * 2.0 * math.exp(-t * 14.0)))
            elif voice == "pad":
                env = min(1.0, (t + 0.02) / 0.45) * (1.0 if t < dur else math.exp(-(t - dur) * 6.0))
                ay.tone(ch, period(midi_hz(midi)), vol(0.05 * s["pad"] * 2.0 * env))
            elif voice == "lead":
                env = (1.0 - t / dur * 0.25) if t < dur else math.exp(-(t - dur) * 30.0) * 0.75
                ay.tone(ch, period(midi_hz(midi)), vol(0.16 * s["lead"] * 1.6 * env))
        for ay in chips:
            ay.frame()
    return [encode(ay) for ay in chips]


def sfx_stream(dur, freq, noise_mix):
    ay = Ay()
    n = max(1, int(math.ceil(dur * FPS)))
    for i in range(n + 1):
        t = i / FPS
        if i == n:
            ay.off(0)
        else:
            f = freq(t)
            env = min(1.0, (t + 0.01) / 0.004) * math.exp(-t * 3.5 / max(0.05, dur)) * min(1.0, max(0.0, (dur - t) / 0.02 + 0.5))
            v = vol(0.6 * env, 0.6)
            if f <= 0:
                ay.off(0)
            elif noise_mix > 0:
                ay.tone_noise(0, period(f), 4, v)
            else:
                ay.tone(0, period(f), v)
        ay.frame()
    return encode(ay)


def main():
    blobs = []                       # (name, bytes)
    for key in ("title", "gatehouse", "crypt", "tower"):
        for c, data in enumerate(song_streams(SONGS[key])):
            blobs.append((f"{key}{c}", data))
    for name, dur, freq, noise in sfx_list():
        blobs.append((f"sfx_{name}", sfx_stream(dur, freq, noise)))
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
        f.write("; generated by tools/gen_sound.py - do not edit: the TurboSound streams\n\n")
        for i in range(nbanks):
            f.write(f"SECTION BANK_{FIRST_BANK + i}\nBINARY \"sound/bank_{FIRST_BANK + i}.bin\"\n")
    with open(os.path.join(ROOT, "src", "sound_data.h"), "w") as f:
        f.write("/* generated by tools/gen_sound.py - do not edit */\n#ifndef SOUND_DATA_H\n#define SOUND_DATA_H\n"
                "#include <stdint.h>\n/* a stream: 8K page, address with the page in MMU slot 2 */\n"
                "typedef struct { uint8_t page; uint16_t addr; } Stream;\n"
                "extern const Stream music_streams[4][2];   /* title, gatehouse, crypt, tower x chip */\n"
                "extern const Stream sfx_streams[13];\n#endif\n")
    with open(os.path.join(ROOT, "src", "sound_data.c"), "w") as f:
        f.write('/* generated by tools/gen_sound.py - do not edit */\n#include "sound_data.h"\n\n')
        f.write("const Stream music_streams[4][2] = {\n")
        for k in range(4):
            a, b = where[2 * k], where[2 * k + 1]
            f.write(f"    {{ {{ {a[1]}, 0x{a[2]:04X} }}, {{ {b[1]}, 0x{b[2]:04X} }} }},   /* {a[0][:-1]}: {a[3]} + {b[3]} bytes */\n")
        f.write("};\n\nconst Stream sfx_streams[13] = {\n")
        for name, page, addr, ln in where[8:]:
            f.write(f"    {{ {page}, 0x{addr:04X} }},   /* {name}: {ln} bytes */\n")
        f.write("};\n")
    print(f"{len(blobs)} streams, {len(data)} bytes, banks {FIRST_BANK}..{FIRST_BANK + nbanks - 1}")
    for name, page, addr, ln in where:
        print(f"  {name:16s} {ln:6d}")


if __name__ == "__main__":
    main()
