"""Feature suite: one check per harness requirement (input, debugging,
screenshots, restart, replay, logging). Run by ./zx selftest.

Needs projects inputtest, hello48 and hellonext built. Headless only; the
window path is in test_remote.py (ZX_TEST_WINDOW=1).
"""
import json
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from PIL import Image  # noqa: E402

from zxdev.control import ProtocolError, SessionClient  # noqa: E402
from zxdev.emu import Emulator, EmulatorError  # noqa: E402

P = ROOT / "projects"
INPUT = P / "inputtest" / "build" / "inputtest.tap"
H48 = P / "hello48" / "build" / "hello48.tap"
NEXT = P / "hellonext" / "build" / "hellonext.nex"
TMP = Path(tempfile.mkdtemp(prefix="zxfeat-"))
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def kv_pid(e):
    return int(e.raw("zxh-status").split("pid=")[1].split()[0])


def section(t):
    print(f"\n== {t}")


def boot(emu, prog, ready):
    emu.load(prog)
    ok = emu.run_until(ready, 3000)
    emu.run(5)
    emu.log()
    return ok


def logged(emu, frames=10):
    emu.run(frames)
    return emu.log()


# ---------------------------------------------------------------- input

def test_input():
    section("key injection")
    with Emulator() as e:
        check("inputtest boots", boot(e, INPUT, "inputtest: ready"))
        e.press("Q")
        check("single key Q -> row FB bit 0", "row fb=1e" in logged(e))
        e.press("CAPS+SYM")
        t = logged(e)
        check("CAPS SHIFT + SYMBOL SHIFT together", "row fe=1e" in t and "row 7f=1d" in t, t)
        e.press("SYM+P")
        t = logged(e)
        check("SYMBOL SHIFT + P", "row 7f=1d" in t and "row df=1e" in t, t)
        e.press("CAPS+1")
        t = logged(e)
        check("CAPS SHIFT + 1 (EDIT)", "row fe=1e" in t and "row f7=1e" in t, t)

        e.log()
        p0 = e.position()["since_load"]
        e.sequence([{"at": p0 + 20, "hold": "A"}, {"at": p0 + 32, "release": "A"},
                    {"at": p0 + 40, "press": "S", "hold": 4}])
        t = logged(e, 5)
        import re
        fs = [int(x) for x in re.findall(r"f=(\d+)", t)]
        check("timed hold/release/press at given frames", len(fs) == 4 and fs[1] - fs[0] == 12
              and fs[2] - fs[0] == 20 and fs[3] - fs[2] == 4, t)

        for kind, keys, want in (("Kempston", "RIGHT+FIRE", "kempston=11"), ("Kempston", "UP", "kempston=08"),
                                 ("Sinclair 1", "LEFT", "row ef=0f"), ("Sinclair 1", "FIRE", "row ef=1e"),
                                 ("Sinclair 2", "RIGHT", "row f7=1d"), ("Sinclair 2", "FIRE", "row f7=0f"),
                                 ("Cursor", "UP", "row ef=17"), ("Cursor", "LEFT", "row f7=0f")):
            e.joystick(kind)
            e.log()
            e.press(keys)
            t = logged(e)
            check(f"{kind} joystick {keys}", want in t, t)
        e.joystick("Kempston")

    with Emulator() as e:
        boot(e, H48, "hello48: ready")
        e.type("Hi")
        t = logged(e)
        check("typed text (CAPS for capitals)", "key H" in t and "key i" in t, t)

    with Emulator(machine="next") as e:
        boot(e, NEXT, "hellonext: ready")
        for key, b0, b1 in (("EDIT", 0, 0x40), ("BREAK", 0, 0x20), ("DELETE", 0, 0x80),
                            ("CURSOR_LEFT", 0x02, 0), ("CURSOR_UP", 0x08, 0), ("SEMICOLON", 0x80, 0),
                            ("EXTEND", 0, 0x01), ("GRAPH", 0, 0x04)):
            e.press(key)
            t = logged(e)
            check(f"Next extra key {key} (NextReg 0xB0/0xB1)", f"b0={b0:02x} b1={b1:02x}" in t, t)
        e.press("CURSOR_RIGHT")
        check("Next extra key also in the matrix (CAPS+8)", "key " in logged(e))


# ---------------------------------------------------------------- debugging

def test_debugging():
    section("debugging")
    with Emulator() as e:
        e.load(H48)
        main = e.symbols.lookup("main")
        e.break_at("main")
        r = e.continue_(3000)
        check("breakpoint by symbol (z88dk .map)", r["stopped"] and r["pc"] == main, str(r)[:200])
        st = e.status()["last_stop"]
        check("status says where and why it halted (breakpoint)", st["why"] == "breakpoint" and st["pc"] == main
              and st["symbol"] == "_main" and "PC=" in (st["break"] or ""), str(st))
        regs = r["regs"]
        check("registers incl. alternates, IX/IY, I/R, IM, IFF",
              all(k in regs for k in ("AF", "BC", "DE", "HL", "AF'", "BC'", "DE'", "HL'",
                                      "IX", "IY", "I", "R", "SP", "PC", "IM", "IFF")), str(sorted(regs)))
        e.delete()
        r = e.continue_(25)
        check("continue with a frame limit", not r["stopped"] and r["frames"] == 25, str(r))
        check("status: halted on the frame limit", e.status()["last_stop"]["why"] == "frame_limit")

        pa = e.symbols.lookup("zx_print_at")
        e.break_at(f"${pa:04X}")
        e.press("K")
        st = e.status()["last_stop"]
        check("breakpoint by address, hit during input", st["reason"] == "break" and st["pc"] == pa, str(st))
        bt = e.backtrace(6)
        check("backtrace shows the caller (main)", any((b["symbol"] or "").startswith("_main") for b in bt), str(bt))
        e.delete()

        # step + step over a CALL
        e.break_at("main")
        e.load(H48)
        e.continue_(3000)
        e.delete()
        pc0 = e.regs()["PC"]
        s1 = e.stepi(1)
        check("stepi advances one instruction", s1["regs"]["PC"] != pc0)
        check("status: halted after a step", e.status()["last_stop"]["why"] == "step")
        for _ in range(200):
            nxt = e.disasm("pc", 1)[-1]
            if "CALL" in nxt:
                break
            e.stepi(1)
        here = e.regs()["PC"]
        r = e.step_over()
        check("step over a CALL lands after it", "CALL" in nxt and r["regs"]["PC"] == here + 3, nxt + " " + str(r["regs"]["PC"]))

        e.run_until("hello48: ready", 100)
        e.watch("frames", 2, "w")
        r = e.continue_(5)
        check("write watchpoint", r["stopped"] and "Write" in r.get("break", ""), str(r)[:200])
        check("status: halted on a watchpoint", e.status()["last_stop"]["why"] == "watchpoint")
        e.delete()
        e.watch("bx", 1, "r")
        r = e.continue_(5)
        check("read watchpoint", r["stopped"] and "Read" in r.get("break", ""), str(r)[:200])
        e.delete()

        lines = e.disasm("main", 3)
        check("disassembly with labels", lines[0] == "_main:" and len(lines) == 4, str(lines))
        e.write_mem("bx", b"\x07")
        check("memory write/read (mapped)", e.peek("bx") == 7)

    with Emulator(machine="128k") as e:
        e.run(10)
        e.write_mem(0x100, b"bank3!", bank=3)
        check("128K: write/read a bank that is not paged in", e.read_mem(0x100, 6, bank=3) == b"bank3!")
        check("128K: bank 5 == screen at 0x4000", e.read_mem(0, 64, bank=5) == e.read_mem(0x4000, 64))
        check("128K: memory map", "RA5" in e.memory_map()["pages"])

    with Emulator(machine="next") as e:
        boot(e, NEXT, "hellonext: ready")
        mmu = e.memory_map()["mmu"]
        check("Next: MMU pages listed", len(mmu) == 8, str(mmu))
        check("Next: 8K page read == mapped view", e.read_mem(0, 64, page=mmu[4]) == e.read_mem(0x8000, 64))
        e.write_mem(0, b"page40", page=40)
        check("Next: write/read an unmapped 8K page", e.read_mem(0, 6, page=40) == b"page40")


# ---------------------------------------------------------------- screenshots

def test_screens():
    section("screenshots + recording")
    with Emulator() as e:
        boot(e, H48, "hello48: ready")
        full = e.screenshot(TMP / "h48.png")
        check("48K screen with border, native 352x304", full.size == (352, 304))
        ula = e.screenshot(layer="ula")
        paper = full.crop((48, 56, 48 + 256, 56 + 192))
        check("ULA layer (from memory) == displayed paper", ula.size == (256, 192)
              and ula.tobytes() == paper.tobytes())
        g = e.record_video(TMP / "h48.gif", frames=40, every=2)
        im = Image.open(TMP / "h48.gif")
        check("GIF recording", g["frames"] == 20 and getattr(im, "n_frames", 1) >= 2, str(g))
        m = e.record_video(TMP / "h48.mp4", frames=30)
        check("MP4 recording (ffmpeg)", (TMP / "h48.mp4").stat().st_size > 1000, str(m))

    with Emulator(machine="next") as e:
        boot(e, NEXT, "hellonext: ready")
        full = e.screenshot()
        check("Next composited screen 704x608", full.size == (704, 608))
        l2 = e.screenshot(layer="layer2")
        check("Layer 2 native 256x192", l2.size == (256, 192))
        # an opaque Layer 2 pixel away from sprites and text matches the composite
        x, y = 40, 120
        a, b = l2.getpixel((x, y)), full.getpixel((96 + 2 * x, 112 + 2 * y))
        check("Layer 2 render matches the composite", all(abs(p - q) <= 4 for p, q in zip(a, b)), f"{a} vs {b}")
        sp = e.screenshot(layer="sprites")
        check("sprite plane native 320x256 RGBA", sp.size == (320, 256) and sp.mode == "RGBA")
        # sprite 1: 2x scaled ball at sprite coords (232,182) -> centre (248,198)
        c = sp.getpixel((248, 198))
        check("scaled hardware sprite rendered", c[3] == 255 and c[0] > 200 and c[1] < 60, str(c))
        cc = full.getpixel((96 + 2 * (248 - 32), 112 + 2 * (198 - 32)))
        check("sprite visible in the composite", cc[0] > 200 and cc[1] < 60, str(cc))
        ula = e.screenshot(layer="ula")
        check("Next ULA layer has the text", len(ula.crop((8, 8, 240, 16)).getcolors(1 << 16)) >= 2)
        # tilemap row 30 (bottom border): odd tiles solid yellow, even tiles transparent
        ty = 2 * (240 + 24) + 4
        tiles = [full.getpixel((2 * (i * 8 + 16) + 4, ty)) for i in range(4)]
        check("tilemap in the composite (border stripes)", tiles[1][0] > 200 and tiles[1][1] > 200 and tiles[1][2] < 60
              and tiles[0] != tiles[1], str(tiles))
        check("border, Layer 2, sprites, ULA and tilemap all in one frame",
              full.getpixel((4, 4)) == (0, 0, 0) and len(full.getcolors(1 << 16)) > 100)


# ---------------------------------------------------------------- restart

def test_restart():
    section("restart")
    with Emulator() as e:
        boot(e, H48, "hello48: ready")
        fv = e.peek16("frames")
        e.reset(hard=False)
        check("soft reset: CPU at 0, RAM kept", e.regs()["PC"] == 0 and e.peek16("frames") == fv)
        boot(e, H48, "hello48: ready")
        e.reset(hard=True)
        e.run(150)
        t = e.log()
        check("hard reset: program gone, ROM running", e.regs()["PC"] < 0x4000 and "hello48" not in t, t)
        r = e.reload()
        check("reload the program", r["program"].endswith("hello48.tap") and e.run_until("hello48: ready", 3000))
        pid = e.proc.pid
        e.power_cycle()
        check("power cycle: new emulator process", e.proc.pid != pid and e.alive and e.program is None)
        for m, prog, ready in (("128k", H48, "hello48: ready"), ("next", NEXT, "hellonext: ready"),
                               ("48k", H48, "hello48: ready")):
            e.set_machine(m)
            check(f"machine switch to {m}", e.status()["machine"] == m and boot(e, prog, ready))

    # a port collision with another emulator is detected and retried, never driven
    import zxdev.emu as E
    with Emulator() as a:
        orig, ports = E.free_port, [a.port]
        E.free_port = lambda: ports.pop(0) if ports else orig()
        try:
            with Emulator() as b:
                ok = b.port != a.port and kv_pid(b) == b.proc.pid
        finally:
            E.free_port = orig
        check("ZRCP port collision: detected by PID, retried, other emulator untouched",
              ok and a.alive and a.run(3)["frames"] == 3)

    # stopping, rebooting or power-cycling one emulator never touches another
    # (other agents and the user run emulators on the same machine)
    with Emulator() as a:
        a.load(H48)
        a.run_until("hello48: ready", 3000)
        b = Emulator()
        b.reboot()
        b.power_cycle()
        b.close()
        check("close / reboot / power cycle stop only their own emulator",
              a.alive and a.run(3)["frames"] == 3 and kv_pid(a) == a.proc.pid)

    # a session whose emulator is stuck inside a long command
    c = SessionClient.spawn(str(H48))
    try:
        c.call("run_until", text="hello48: ready")
        stuck = SessionClient(c.info["control_port"], timeout=120)
        err = []

        def hang():
            try:
                stuck.call("run", frames=50_000_000)      # hours of emulation
            except (ProtocolError, OSError) as ex:
                err.append(str(ex))
        th = threading.Thread(target=hang, daemon=True)
        th.start()
        time.sleep(1.5)
        t0 = time.time()
        r = c.call("reboot")
        th.join(20)
        check("reboot a wedged session (kills the stuck command)", time.time() - t0 < 20 and err
              and r["program"].endswith("hello48.tap"), f"{time.time() - t0:.1f}s {err}")
        check("rebooted session runs the program again",
              c.call("run_until", text="hello48: ready", max_frames=3000)["found"])
        stuck.close()
    finally:
        c.close(stop=True)


# ---------------------------------------------------------------- replay + snapshots

def test_recorder():
    section("input recorder + per-frame hash replay")
    for m, prog, ready, keys in (("48k", H48, "hello48: ready", ["Q", "CAPS+H", "SYM+P"]),
                                 ("next", NEXT, "hellonext: ready", ["X", "EDIT", "CURSOR_LEFT"])):
        for from_reset in (False, True):
            how = "from reset" if from_reset else "from a snapshot"
            path = TMP / f"rec_{m}_{from_reset}.json"
            with Emulator(machine=m) as e:
                boot(e, prog, ready)
                e.record_input("start", from_reset=from_reset)
                if from_reset:
                    e.run_until(ready, 3000)
                for k in keys:
                    e.press(k)
                    e.run(9)
                e.joystick("Sinclair 1")
                e.hold("RIGHT+FIRE")
                e.run(7)
                e.release()
                e.joystick("Kempston")
                e.run(20)
                info = e.record_input("save", path)
            check(f"{m} {how}: recording has input changes + a hash per frame",
                  info["events"] >= 2 * len(keys) and info["hashes"] >= info["frames"] - 1, str(info))
            seqs = []
            for i in range(2):
                with Emulator(machine=m) as e:
                    e.run(13 * i)        # different history before the replay
                    r = e.replay(path)
                    seqs.append([h for _, h in r["hashes"]])
                    check(f"{m} {how}: replay {i + 1} matches every recorded frame hash",
                          r["ok"] and r["compared"] == info["hashes"], json.dumps({k: r[k] for k in r if k != "hashes"}))
            check(f"{m} {how}: two replays give identical hash sequences", seqs[0] == seqs[1] and len(seqs[0]) > 0)
    bad = json.loads((TMP / "rec_48k_True.json").read_text())
    for ev in bad["events"][1:]:
        ev["f"] += 3                      # same keys, 3 frames later
    with Emulator() as e:
        r = e.replay(bad)
        check("a shifted input log is caught (hash mismatch)", not r["ok"] and r["mismatches"] > 0)
    with Emulator() as a, Emulator() as b:
        b.run(37)
        b.press("Q")
        a.load(H48)
        b.load(H48)
        same = True
        for _ in range(150):
            a.run(1)
            b.run(1)
            same = same and a.raw("zxh-hash now") == b.raw("zxh-hash now") and a.regs() == b.regs()
        check("loads are identical whatever ran before (RAM, registers, FLASH phase)", same)
    zx = str(ROOT / ".venv" / "bin" / "zxdev")
    rp = TMP / "cli.json"
    r1 = subprocess.run([zx, "record", str(H48), "-o", str(rp), "--frames", "300", "--press", "Q@200",
                         "--type", "Hi@230"], capture_output=True, text=True)
    r2 = subprocess.run([zx, "replay", str(rp), "--runs", "2"], capture_output=True, text=True)
    check("./zx record + ./zx replay --runs 2", r1.returncode == 0 and r2.returncode == 0
          and "identical hash sequences" in r2.stdout, r1.stdout + r1.stderr + r2.stdout + r2.stderr)


def test_replay():
    section("replay (checkpoints) + snapshots")
    rec_path = TMP / "rec.json"
    with Emulator() as e:
        boot(e, H48, "hello48: ready")
        e.press("Q")
        e.run(13)
        e.hold("W")
        e.run(7)
        e.release()
        e.run_until("hello48: frame 250", 400)   # an input mid-frame, right after a log line
        e.press("E")
        e.checkpoint("after E")
        e.type("ok")
        e.run(40)
        rec = e.save_replay(rec_path)
        orig_end = rec["end"]["screen"]
    check("input log has frames + T-states", all("f" in x and "t" in x for x in rec["events"])
          and len(rec["events"]) >= 8, str(rec["events"][:2]))
    hashes = []
    for i in range(2):
        with Emulator() as e:
            r = e.replay(rec_path)
            hashes.append([c["got"] for c in r["checkpoints"]])
            check(f"replay {i + 1}: every checkpoint screen hash matches", r["ok"], json.dumps(r)[:300])
    check("two replays give identical screens", hashes[0] == hashes[1] and hashes[0][-1] == orig_end)
    bad = json.loads(rec_path.read_text())
    bad["events"] = [ev for ev in bad["events"] if ev.get("ports", "").startswith("1f1f1f1f1f1f1f1f")
                     or ev is bad["events"][0]][:-1]   # drop most key presses
    with Emulator() as e:
        check("a changed input log is detected", not e.replay(bad)["ok"])
    r = subprocess.run([str(ROOT / ".venv" / "bin" / "zxdev"), "replay", str(rec_path)], capture_output=True, text=True)
    check("./zx replay passes", r.returncode == 0 and "PASS" in r.stdout, r.stdout + r.stderr)

    for m, prog, ready, formats in (("48k", H48, "hello48: ready", ("sna", "z80", "zsf")),
                                    ("128k", H48, "hello48: ready", ("sna", "z80", "zsf")),
                                    ("next", NEXT, "hellonext: ready", ("zsf",))):
        with Emulator(machine=m) as e:
            boot(e, prog, ready)
            for ext in formats:
                p = e.save_state(TMP / f"s_{m}.{ext}")
                regs = e.regs()
                e.run(1)
                h = e.screen_hash()
                e.run(60)
                e.load_state(p)
                same_regs = e.regs()["PC"] == regs["PC"] and e.regs()["SP"] == regs["SP"]
                e.run(1)
                check(f"{m} .{ext} snapshot resumes identically", same_regs and e.screen_hash() == h)
            if m == "next":
                try:
                    e.save_state(TMP / "x.sna")
                    check("Next refuses .sna (ZEsarUX can't write it)", False)
                except EmulatorError:
                    check("Next refuses .sna (ZEsarUX can't write it)", True)


# ---------------------------------------------------------------- logging

def test_logging():
    section("logging + frame counting")
    with Emulator() as e:
        e.load(H48)
        check("zx_log text captured", e.run_until("hello48: ready", 3000))
        pc = e.last_run["pc"]
        log_s = e.symbols.lookup("zx_log_s")
        check("run_until stops inside the logging routine", log_s is not None and log_s <= pc < log_s + 32,
              f"pc ${pc:04X}, zx_log_s ${log_s or 0:04X}")
        p0 = e.position()
        e.run(123)
        p1 = e.position()
        check("frame counting: run(123) = 123 frames", p1["frame"] - p0["frame"] == 123
              and p1["since_load"] - p0["since_load"] == 123)
        check("program frame counter agrees (HALT-paced)", e.run_until("hello48: frame 250", 400))
        f250 = e.last_run["since_load"]
        e.run_until("hello48: frame 500", 400)
        check("250 program frames = 250 emulated frames", e.last_run["since_load"] - f250 == 250)
        e.run(1)
        r = e.run_to(f250 + 400, 1000)
        p = e.position()
        check("run_to an exact frame + T-state", r["reason"] == "at" and p["since_load"] == f250 + 400
              and 1000 <= p["tstates"] < 1100, str(p))


def main():
    for f in (INPUT, H48, NEXT):
        if not f.exists():
            print(f"build first: {f} missing")
            return 2
    for t in (test_input, test_debugging, test_screens, test_restart, test_recorder, test_replay, test_logging):
        t()
    print(f"\n{'FAIL: ' + str(len(failures)) + ' failure(s)' if failures else 'PASS'}")
    for f in failures:
        print("  -", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
