"""Rolling Steel's music and effects for TurboSound (sdk/include/aystream.h),
from the original's own definitions (tools/orig/tools/sounds.py: Music.cs and
Sfx.cs, the 3DO version's renderer): the seven themes note for note (bpm,
swing, scale, every part's steps and envelope), the ten effects (their
pitches, sweeps and lengths).

port: the original renders these as samples (pulse, saw, triangle and FM
voices, 11 kHz). The AY has square tones and one noise per chip, so every
part is a square tone whose volume follows the part's envelope; drums are
the noise (snare, hat) and a falling tone (kick). Music: chip 0 (parts 1-2,
drums), chip 1 (parts 3-4; channel C is left to the rolling rumble, which
sound.c writes itself); effects: chip 2.

Writes gfx/sound_bank_NN.bin, src/sound_banks.asm, src/sound_data.h.
"""
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
sys.path.insert(0, str(HERE / "orig" / "tools"))
from aystream import Ay, encode, period  # noqa: E402

FIRST_PAGE = 204                         # after the courses (gen_course.py: ..203)
FPS = 50.0


def vol_of(amp, ref=0.30):
    """an amplitude (the original's gain x envelope) as an AY volume: 2 steps a halving"""
    if amp <= 0:
        return 0
    v = 15 + 2 * math.log2(amp / ref)
    return max(0, min(15, int(round(v))))


def theme(s):
    import sounds
    step_dur = 60.0 / s["bpm"] / 4.0
    total = sounds.TOTAL
    nframes = int(round(step_dur * total * FPS))
    chips = [Ay(), Ay()]
    voices = [(0, 0), (0, 1), (1, 0), (1, 1)]   # (chip, channel) per part
    notes = []                                   # per part: (start s, freq, part)
    for pi, p in enumerate(s["parts"][:4]):
        lst = []
        for step, deg in enumerate(p["steps"][:total]):
            if deg < 0:
                continue
            off = step_dur * s["swing"] if step & 1 else 0
            lst.append((step * step_dur + off, sounds.degree_freq(s, deg, p["transpose"])))
        notes.append(lst)
    drums = []
    for step in range(total):
        t = step * step_dur
        if s["kick"] and s["kick"][step]:
            drums.append((t, "kick"))
        elif s["snare"] and s["snare"][step]:
            drums.append((t, "snare"))
        elif s["hat"] and s["hat"][step]:
            drums.append((t, "hat"))
    for f in range(nframes):
        t = f / FPS
        for pi, p in enumerate(s["parts"][:4]):
            chip, ch = voices[pi]
            cur = None
            for st, fr in notes[pi]:
                if st <= t:
                    cur = (st, fr)
                else:
                    break
            if cur is None:
                chips[chip].off(ch)
                continue
            dt = t - cur[0]
            env = math.exp(-dt / p["decay"])
            v = vol_of(p["gain"] * env)
            if v:
                chips[chip].tone(ch, period(cur[1]), v)
            else:
                chips[chip].off(ch)
        # drums on chip 0, channel C
        cur = None
        for st, kind in drums:
            if st <= t:
                cur = (st, kind)
            else:
                break
        a = chips[0]
        if cur:
            dt = t - cur[0]
            if cur[1] == "kick" and dt < 0.26:
                fr = 145 + (46 - 145) * min(1.0, dt / 0.07)
                a.tone(2, period(fr), vol_of(0.5 * math.exp(-dt / 0.10), 0.5))
            elif cur[1] == "snare" and dt < 0.18:
                a.tone_noise(2, period(195), 6, vol_of(0.4 * math.exp(-dt / 0.055), 0.4))
            elif cur[1] == "hat" and dt < 0.06:
                a.noise(2, 1, vol_of(0.16 * math.exp(-dt / 0.014), 0.25))
            else:
                a.off(2)
        else:
            a.off(2)
        # chip 1 channel C: the rumble's (noise on, tone off; sound.c sets its volume)
        b = chips[1]
        b.r[7] = (b.r[7] | 8 << 2) & ~(8 << 2) | (1 << 2)
        b.r[7] &= ~(8 << 2)
        b.r[10] = 0
        for c in chips:
            c.frame()
    for c in chips:
        c.loop = 0
    return [encode(c) for c in chips]


def effect(i):
    """Sfx.cs's effect i on one chip (A, B, C; one noise)"""
    a = Ay()

    def sweep(t, f0, f1, dur, power=1.0):
        return f0 + (f1 - f0) * min(1.0, t / dur) ** power

    def envl(t, dur, att, rel):
        if t < att:
            return t / att
        if t > dur - rel:
            return max(0.0, (dur - t) / rel)
        return 1.0
    dur = [0.45, 0.95, 0.7, 0.12, 0.16, 1.25, 0.85, 0.5, 0.6, 0.4][i]
    for f in range(int(math.ceil(dur * FPS)) + 1):
        t = f / FPS
        if i == 0:      # START: a chord
            e = envl(t, dur, 0.01, 0.25)
            for ch, hz in enumerate((330, 440, 660)):
                a.tone(ch, period(hz), vol_of(e / 3, 0.34))
        elif i == 1:    # GOAL: four rising notes
            e = envl(t, dur, 0.01, 0.4)
            a.tone(0, period([523, 659, 784, 1047][min(3, int(t / 0.16))]), vol_of(e, 1.0))
        elif i == 2:    # DEATH: a falling tone
            a.tone(0, period(sweep(t, 420, 60, dur)), vol_of(envl(t, dur, 0.005, 0.35) * 0.9, 0.9))
        elif i == 3:    # CLACK
            a.tone_noise(0, period(180), 4, vol_of(envl(t, dur, 0.002, 0.05), 1.0))
        elif i == 4:    # WARN
            a.tone(0, period(880), vol_of(envl(t, dur, 0.004, 0.08), 1.0))
        elif i == 5:    # FALL: a long whistle down
            e = envl(t, dur, 0.005, 0.5)
            a.tone(0, period(sweep(t, 950, 70, dur, 0.55)), vol_of(e * 0.7, 0.7))
            a.noise(1, 20, vol_of(e * 0.14, 0.7))
        elif i == 6:    # SIZZLE
            e = envl(t, dur, 0.004, 0.4)
            a.tone(0, period(sweep(t, 420, 90, dur)), vol_of(e * 0.35, 0.6))
            a.noise(1, 2, vol_of(e * 0.55, 0.6))
        elif i == 7:    # CHOMP
            e = envl(t, dur, 0.003, 0.2)
            a.tone_noise(0, period(sweep(t, 260, 55, dur, 0.7)), 12, vol_of(e * 0.8, 0.8))
        elif i == 8:    # SHATTER: noise and bell partials
            a.noise(0, 3, vol_of(0.55 * math.exp(-t / 0.045), 0.55))
            for ch, k in ((1, 1), (2, 3)):
                u = t - k * 0.032
                if u >= 0:
                    a.tone(ch, period(1150 + k * 470), vol_of(0.2 * math.exp(-u / 0.085), 0.25))
                else:
                    a.off(ch)
        elif i == 9:    # THUD
            e = envl(t, dur, 0.002, 0.25)
            a.tone_noise(0, period(sweep(t, 150, 38, 0.09)), 24, vol_of(e * 0.8, 0.8))
        a.frame()
    for ch in range(3):
        a.off(ch)
    a.frame()
    return encode(a)


def main():
    import sounds
    blob = bytearray()
    names = []

    def put(name, data):
        # streams may cross pages (the player maps them); keep each in one bank anyway
        page = FIRST_PAGE + len(blob) // 8192
        addr = 0x4000 + len(blob) % 8192
        names.append((name, page, addr))
        blob.extend(data)
    for k, s in enumerate(sounds.songs()):
        a, b = theme(s)
        put(f"theme{k}_0", a)
        put(f"theme{k}_1", b)
    for i in range(10):
        put(f"sfx{i}", effect(i))
    blob += bytes(-len(blob) % 16384)
    last = FIRST_PAGE + len(blob) // 8192 - 1
    assert last <= 223, last
    gfx = HERE.parent / "gfx"
    for f in gfx.glob("sound_bank_*.bin"):
        f.unlink()
    asm = ["; made by tools/gen_sound.py: the music and effects (aystream streams)", ""]
    for k in range(0, len(blob), 16384):
        bank = (FIRST_PAGE * 8192 + k) // 16384
        (gfx / f"sound_bank_{bank}.bin").write_bytes(blob[k:k + 16384])
        asm += [f"SECTION BANK_{bank}", f'BINARY "gfx/sound_bank_{bank}.bin"']
    (HERE.parent / "src" / "sound_banks.asm").write_text("\n".join(asm) + "\n")
    h = ["/* made by tools/gen_sound.py */", "#ifndef SOUND_DATA_H", "#define SOUND_DATA_H", "#include <stdint.h>",
         "typedef struct { uint8_t page; uint16_t addr; } SndRef;",
         "static const SndRef snd_theme[7][2] = {"]
    for k in range(7):
        a = [n for n in names if n[0] == f"theme{k}_0"][0]
        b = [n for n in names if n[0] == f"theme{k}_1"][0]
        h.append(f"    {{ {{ {a[1]}, 0x{a[2]:04X} }}, {{ {b[1]}, 0x{b[2]:04X} }} }},")
    h.append("};")
    h.append("static const SndRef snd_sfx[10] = {")
    for i in range(10):
        n = [x for x in names if x[0] == f"sfx{i}"][0]
        h.append(f"    {{ {n[1]}, 0x{n[2]:04X} }},")
    h += ["};", "#endif", ""]
    (HERE.parent / "src" / "sound_data.h").write_text("\n".join(h))
    print(f"  {len(blob)} bytes, pages {FIRST_PAGE}..{last}")


if __name__ == "__main__":
    main()
