#!/usr/bin/env python3
"""RJ's Birthday Bash's sound for the Next's TurboSound.

Music: the original's two MODs (tools/orig/party.mod on the title and in
play, birthday.mod after) through sdk/tools/mod2ay.py for the SDK's modplay:
the four MOD channels on chips 0 and 1 (A and B), an instrument for each
sample. port: all four channels are music (the original played two and kept
two of Paula's channels for effects); the effects have chip 2 to themselves.

Effects: the original's sound.c makes each effect as an 8-bit sample
(sound_init); this makes the same samples, plays them at the original's
Paula period and turns each 50th of a second into the AY: a tone at the
sample's pitch (autocorrelation) with its loudness, or noise where it has
no clear pitch. The arcade cabinet's four voice clips (tools/orig/
snd_arcade1-4.raw, 10026 Hz) go the same way. port: so they keep the
clips' rhythm, pitch and loudness, not the words (the AY can't speak); the
bubble still says them, and the music still pauses for the clip's length.

Output: sound/bank_27.bin (the streams, sdk aystream, on chip 2: page 54),
src/sound_data.asm, src/sound_data.h, src/music_party.c, src/music_bday.c
(page 55, mapped at 0x4000 while modplay reads it: see the Makefile).

    python3 tools/gen_sound.py
"""
import math
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(PROJ))
ORIG = os.path.join(HERE, "orig")
sys.path.insert(0, os.path.join(ROOT, "sdk", "tools"))
from aystream import Ay, encode  # noqa: E402

BANK = 27
PAL = 3546895
AY_CLOCK = 1750000

# ---- the original's sound_init (sound.c), sample by sample ----
rng = [77777]


def snd_rand():
    rng[0] = (rng[0] * 1103515245 + 12345) & 0xFFFFFFFF
    return (rng[0] >> 16) & 0x7FFF


def cdiv(a, b):
    """C's / (towards zero)"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def cmod(a, b):
    return a - cdiv(a, b) * b


def sbyte(v):
    return ((int(v) + 128) & 0xFF) - 128


def bell():
    n, out = 512, []
    for i in range(n):
        env = 127 - cdiv(i * 127, n)
        freq = 12 if i < n // 2 else 9
        val = 80 if ((i * freq) & 0xFF) < 128 else -80
        out.append(sbyte(cdiv(val * env, 127)))
    return out


def slide():
    n, out = 128, []
    for i in range(n):
        env = 127 - cdiv(i * 127, n)
        freq = 3 + cdiv(i * 20, n)
        noise = cmod(snd_rand(), 40) - 20
        out.append(sbyte(cdiv((40 if ((i * freq) & 0xFF) > 128 else -40) * env, 127) + cdiv(noise * env, 500)))
    return out


def chomp():
    n, out = 64, []
    for i in range(n):
        env = i * 12 if i < 10 else 127 - cdiv((i - 10) * 127, n - 10)
        out.append(sbyte(cdiv((60 if ((i * 25) & 0xFF) > 128 else -60) * env, 127)))
    return out


def ding():
    n, out = 256, []
    for i in range(n):
        env = 127 - cdiv(i * 127, n)
        val = (50 if ((i * 15) & 0xFF) < 128 else -50) + (30 if ((i * 30) & 0xFF) < 128 else -30)
        out.append(sbyte(cdiv(val * env, 127)))
    return out


def buzz():
    n, out = 256, []
    for i in range(n):
        env = 127 - cdiv(i * 127, n)
        val = (cmod(snd_rand(), 60) - 30) + (40 if ((i * 5) & 0xFF) > 128 else -40)
        out.append(sbyte(cdiv(val * env, 127)))
    return out


def cheer():
    n, out = 1024, []
    for i in range(n):
        env = cdiv(i * 127, n // 4) if i < n // 4 else 127 - cdiv((i - n // 4) * 127, n * 3 // 4)
        noise = cmod(snd_rand(), 200) - 100
        out.append(sbyte(cdiv(noise * env, 127)))
    return out


def pick():
    n, out = 64, []
    for i in range(n):
        env = 127 - cdiv(i * 127, n)
        freq = 20 - cdiv(i * 15, n)
        out.append(sbyte(cdiv((64 if ((i * freq) & 0xFF) > 128 else -64) * env, 127)))
    return out


def crash():
    n, out = 512, []
    for i in range(n):
        env = 127 - cdiv(i * 127, n)
        out.append(sbyte(cdiv((cmod(snd_rand(), 256) - 128) * env, 127)))
    return out


def party():
    n, out = 2048, []
    for i in range(n):
        if i < n // 3:
            env = cdiv(i * 127, n // 3)
        elif i < n * 2 // 3:
            env = 127
        else:
            env = 127 - cdiv((i - n * 2 // 3) * 127, n // 3)
        freq = 5 + cdiv(i * 15, n)
        val = (60 if ((i * freq) & 0xFF) < 128 else -60) + cmod(snd_rand(), 20) - 10
        out.append(sbyte(cdiv(val * env, 127)))
    return out


def arcade_a():
    n, out = 4096, []
    for t in range(n):
        if t < 200:
            env = cdiv(t * 127, 200)
        elif t < n - 500:
            env = 127 - cdiv(t * 20, n)
        else:
            env = cdiv((n - t) * 80, 500)
        wobble = 3 + cdiv(t * 3, n)
        val = 60 if ((t * wobble) & 0xFF) < 128 else -60
        val += 25 if ((t * wobble * 3) & 0xFF) < 128 else -25
        ffreq = 12 + cdiv(t * 20, n)
        if n // 3 < t < n * 2 // 3:
            ffreq = 32 - cdiv(t * 15, n)
        val += 20 if ((t * ffreq) & 0xFF) < 128 else -20
        val += cmod(snd_rand(), 16) - 8
        val = max(-127, min(127, cdiv(val * env, 127)))
        out.append(sbyte(val))
    return out


# (name, the sample, Paula period, volume 0..64, priority) in sound_init's order (its snd_rand calls)
EFFECTS = []
for name, fn, per, vol, pri in (("doorbell", bell, 280, 55, 30), ("slide", slide, 200, 40, 20),
                                ("chomp", chomp, 250, 50, 25), ("ding", ding, 200, 55, 40),
                                ("buzzer", buzz, 400, 50, 35), ("cheer", cheer, 350, 55, 50),
                                ("pickup", pick, 180, 50, 25), ("crash", crash, 280, 60, 45),
                                ("party", party, 300, 60, 55), ("arcade_a", arcade_a, 500, 64, 60)):
    EFFECTS.append((name, fn(), per, vol, pri))
RNG_AFTER_INIT = rng[0]              # arcade_voice_play_random goes on from here


def ay_vol(a):
    """a linear amplitude 0..1 -> the AY's volume (about 3 dB a step)"""
    if a <= 0.004:
        return 0
    return max(0, min(15, int(round(15 + 2 * math.log2(a)))))


def to_ay(samples, rate, vol64, ay, tone_ch=0, noise_ch=1):
    """one AY frame a 50th of a second: the sample's pitch and loudness"""
    x = np.array(samples, dtype=float) / 128.0
    spf = rate / 50.0
    nfr = max(1, int(math.ceil(len(x) / spf)))
    lo, hi = max(2, int(rate / 4000)), int(rate / 70)
    for k in range(nfr):
        seg = x[int(k * spf):int((k + 1) * spf)]
        if len(seg) < 8:
            seg = x[max(0, len(x) - 64):]
        seg = seg - seg.mean()
        rms = math.sqrt(float((seg * seg).mean())) if len(seg) else 0.0
        a = min(1.0, rms * 1.6) * vol64 / 64
        v = ay_vol(a)
        best, lag = 0.0, 0
        e0 = float((seg * seg).sum()) or 1e-9
        for L in range(lo, min(hi, len(seg) - 4)):
            c = float((seg[:-L] * seg[L:]).sum()) / e0
            if c > best:
                best, lag = c, L
        if v == 0:
            ay.off(tone_ch)
            ay.off(noise_ch)
        elif best > 0.45 and lag:
            hz = rate / lag
            ay.tone(tone_ch, max(1, min(4095, int(round(AY_CLOCK / (16 * hz))))), v)
            ay.off(noise_ch)
        else:
            zc = int(np.count_nonzero(np.diff(np.sign(seg)))) / max(1, len(seg))
            hz = max(200.0, zc * rate / 2)
            npd = max(1, min(31, int(round(AY_CLOCK / (16 * hz)))))
            ay.off(tone_ch)
            ay.noise(noise_ch, npd, v)
        ay.frame()
    ay.off(tone_ch)
    ay.off(noise_ch)
    ay.frame()


def voices():
    out = []
    for i in range(1, 5):
        d = open(os.path.join(ORIG, f"snd_arcade{i}.raw"), "rb").read()
        out.append(([sbyte(b) for b in d], len(d)))
    return out


def music():
    mod2ay = os.path.join(ROOT, "sdk", "tools", "mod2ay.py")
    specs = {
        "party": ("party.mod", "party_song", ["1=tone:12", "2=tone:13", "3=kick:14:1:2:220:250",
                                              "4=noise:12:1:1:10", "5=noise:8:1:3:1", "6=noise:11:1:2:6",
                                              "7=tone:10:2:1", "8=tone:11"]),
        "bday": ("birthday.mod", "bday_song", ["1=tone:12", "2=tone:12", "3=tone:9:2:1", "4=tone:12",
                                               "5=tone:9:1:2", "6=tone:10"]),
    }
    for key, (mod, name, insts) in specs.items():
        args = [sys.executable, mod2ay, os.path.join(ORIG, mod), "--name", name, "--route", "0=0A,1=0B,2=1A,3=1B"]
        for s in insts:
            args += ["--inst", s]
        c = subprocess.run(args, check=True, capture_output=True, text=True).stdout
        c = c.replace("generated by sdk/tools/mod2ay.py", "generated by tools/gen_sound.py (sdk/tools/mod2ay.py)")
        open(os.path.join(PROJ, "src", f"music_{key}.c"), "w").write(c)


def main():
    page = bytearray()
    entries = []
    for name, smp, per, vol, pri in EFFECTS:
        ay = Ay()
        to_ay(smp, PAL / per, vol, ay)
        entries.append((name.upper(), len(page), pri, 0))
        page += encode(ay)
    for i, (smp, size) in enumerate(voices()):
        ay = Ay()
        to_ay(smp, PAL / 354, 64, ay)
        entries.append((f"VOICE{i}", len(page), 127, size))
        page += encode(ay)
    assert len(page) <= 8192, len(page)
    os.makedirs(os.path.join(PROJ, "sound"), exist_ok=True)
    open(os.path.join(PROJ, "sound", f"bank_{BANK}.bin"), "wb").write(bytes(page))
    open(os.path.join(PROJ, "src", "sound_data.asm"), "w").write(
        "; made by tools/gen_sound.py: the effects and voices as AY streams (sdk aystream)\n"
        f"SECTION PAGE_{BANK * 2}\nBINARY \"sound/bank_{BANK}.bin\"\n")
    h = ["/* made by tools/gen_sound.py - do not edit */", "#ifndef SOUND_DATA_H", "#define SOUND_DATA_H",
         "#include <stdint.h>", f"#define SND_PAGE {BANK * 2}      /* the streams, addresses with it in slot 2 */",
         f"#define SND_RNG_AFTER_INIT {RNG_AFTER_INIT}UL   /* the original's snd_rng after sound_init */", ""]
    h.append("enum { " + ", ".join(f"SND_{n}" for n, *_ in entries) + ", SND_COUNT };")
    h.append("static const uint16_t snd_addr[SND_COUNT] = { " + ", ".join(hex(0x4000 + o) for _, o, _, _ in entries) + " };")
    h.append("static const uint8_t snd_pri[SND_COUNT] = { " + ", ".join(str(p) for _, _, p, _ in entries) + " };")
    h.append("static const uint16_t voice_size[4] = { " + ", ".join(str(s) for *_, s in entries[-4:]) + " };   /* the clips' bytes */")
    h += ["#endif", ""]
    open(os.path.join(PROJ, "src", "sound_data.h"), "w").write("\n".join(h))
    music()
    print(f"streams {len(page)} bytes in page {BANK * 2}; rng after init {RNG_AFTER_INIT}")


if __name__ == "__main__":
    main()
