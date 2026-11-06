"""End-to-end tests for sessions, the control port, the debugger, the MCP
server (stdio) and DevBench (REST + MCP over HTTP). Run by ./zx selftest.

Needs projects/hello48 and projects/hellonext built.
"""
import asyncio
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.control import ProtocolError, SessionClient, list_sessions  # noqa: E402

TAP = ROOT / "projects" / "hello48" / "build" / "hello48.tap"
NEX = ROOT / "projects" / "hellonext" / "build" / "hellonext.nex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def section(t):
    print(f"\n== {t}")


def test_session():
    section("session process + control port")
    c = SessionClient.spawn(str(TAP))
    try:
        pid = c.info["pid"]
        check("session registered", any(s["pid"] == pid for s in list_sessions()))
        st = c.call("status")
        check("status: 48k, paused, symbols", st["machine"] == "48k" and st["paused"] and st["symbols"] > 100, str(st))
        r = c.call("run_until", text="hello48: ready", max_frames=2000)
        check("run_until", r["found"], str(r))
        f0 = c.call("status")["frame"]
        c.call("run", frames=7)
        check("run advances exactly N frames", c.call("status")["frame"] == f0 + 7)
        c.call("log")
        c.call("press", keys="K")
        r = c.call("run_until", text="hello48: key k", max_frames=20)
        check("press + log", r["found"])
        c.call("hold", keys="L")
        r = c.call("run_until", text="hello48: key l", max_frames=20)
        c.call("release")
        check("hold / release", r["found"])
        r = c.call("screenshot", scale=2, border=False)
        check("screenshot (cropped, x2)", (r["width"], r["height"]) == (512, 384), str((r["width"], r["height"])))
        check("joystick type", c.call("joystick", kind="Sinclair 1")["joystick"] == "Sinclair 1"
              and c.call("joystick", kind="Kempston")["joystick"] == "Kempston")

        section("debugger")
        r = c.call("symbols", pattern="^_main$")
        check("symbols", r and r[0]["name"] == "_main", str(r))
        main = r[0]["addr"]
        r = c.call("disasm", addr="main", count=2)
        check("disasm by symbol", r["listing"].startswith("_main:"), r["listing"])
        r = c.call("read_mem", addr="frames", length=2)
        check("read_mem by symbol", len(r["hex"]) == 4)
        c.call("write_mem", addr="bx", hex="05")
        check("write_mem", c.call("read_mem", addr="bx", length=1)["hex"] == "05")
        c.call("watch", addr="frames", length=2, kind="w")
        r = c.call("continue", max_frames=5)
        check("watchpoint stops on the write", r["stopped"] and "_main" in (r.get("symbol") or ""), str(r)[:300])
        c.call("delete")
        r = c.call("continue", max_frames=5)
        check("delete all -> runs freely", not r["stopped"] and r["frames"] == 5)
        r = c.call("stepi", count=3)
        check("stepi", "PC" in r["regs"] and r["next"])
        bt = c.call("backtrace", items=4)
        check("backtrace", len(bt) == 4)
        r = c.call("break", condition="PC=56")    # the ROM IM1 handler, every frame
        r = c.call("continue", max_frames=3)
        check("conditional breakpoint", r["stopped"] and r["pc"] == 56, str(r)[:200])
        c.call("delete")
        regs = c.call("set_reg", reg="HL", value=0x1234)
        check("set_reg", regs["HL"] == 0x1234)

        section("snapshots + machines")
        p = c.call("save_state", path="selftest")["path"]
        v = c.call("read_mem", addr="frames", length=2)["hex"]
        c.call("run", frames=20)
        c.call("load_state", path="selftest")
        check("save_state / load_state round trip", c.call("read_mem", addr="frames", length=2)["hex"] == v, p)
        r = c.call("load", path=str(NEX))
        check("load .nex switches to the Next", r["machine"] == "next", str(r))
        check("Next program runs", c.call("run_until", text="hellonext: ready", max_frames=2000)["found"])
        check("raw ZRCP", "Next" in c.call("zrcp", line="get-current-machine")["output"])
        try:
            c.call("nope")
            check("unknown command is an error", False)
        except ProtocolError:
            check("unknown command is an error", True)
    finally:
        c.close(stop=True)
    time.sleep(0.5)
    check("session unregistered on quit", not any(s["pid"] == pid for s in list_sessions()))


def test_cli_ctl():
    section("./zx ctl against a served session")
    c = SessionClient.spawn(str(TAP))
    try:
        port = str(c.info["control_port"])
        zx = [str(ROOT / ".venv" / "bin" / "zxdev"), "ctl", "--port", port]
        r = subprocess.run(zx + ["run_until", "text=hello48: ready"], capture_output=True, text=True)
        check("ctl run_until", r.returncode == 0 and '"found": true' in r.stdout, r.stdout + r.stderr)
        r = subprocess.run(zx + ["disasm", "addr=main", "count=2"], capture_output=True, text=True)
        check("ctl prints listings plainly", r.stdout.startswith("_main:"), r.stdout)
    finally:
        c.close(stop=True)


def test_mcp():
    section("MCP server (stdio)")
    from mcp.client.session import ClientSession
    from mcp.client.stdio import StdioServerParameters, stdio_client

    async def run():
        p = StdioServerParameters(command=str(ROOT / ".venv" / "bin" / "zxdev-mcp"), args=[])
        async with stdio_client(p) as (r, w):
            async with ClientSession(r, w) as s:
                await s.initialize()
                names = {t.name for t in (await s.list_tools()).tools}
                check("MCP exposes build/run/debug tools",
                      {"build", "emu_boot", "emu_run_until", "emu_look", "emu_press", "emu_read_mem",
                       "emu_break", "emu_watch", "emu_continue", "emu_regs", "emu_cmd"} <= names, str(sorted(names)))

                async def call(n, **a):
                    return await s.call_tool(n, a)

                await call("emu_boot", project="hello48")
                res = await call("emu_run_until", text="hello48: ready", max_frames=2000)
                check("MCP emu_run_until", '"found": true' in res.content[0].text, res.content[0].text[:200])
                res = await call("emu_look", scale=1)
                check("MCP emu_look returns an image", [c.type for c in res.content] == ["image"])
                await call("emu_press", keys="M")
                res = await call("emu_run_until", text="hello48: key m", max_frames=20)
                check("MCP emu_press", '"found": true' in res.content[0].text)
                res = await call("emu_break", addr="zx_print_at")
                res = await call("emu_press", keys="N")      # the key handler prints -> breaks
                check("MCP breakpoint hit during press", '"reason": "break"' in res.content[0].text
                      and "zx_print_at" in res.content[0].text, res.content[0].text[:300])
                res = await call("emu_continue", max_frames=20)
                check("MCP continue runs on (one print per key)", '"stopped": false' in res.content[0].text)
                await call("emu_hold", keys="B")
                res = await call("emu_continue", max_frames=20)
                await call("emu_release")
                check("MCP continue stops at the next hit", '"stopped": true' in res.content[0].text
                      and "zx_print_at" in res.content[0].text, res.content[0].text[:300])
                await call("emu_delete")
                res = await call("emu_read_mem", addr="frames", length=2)
                check("MCP emu_read_mem", len(res.content[0].text.split()) >= 3)
                await call("emu_close")

    asyncio.run(asyncio.wait_for(run(), 300))


def test_devbench():
    section("DevBench REST + MCP over HTTP")
    sk = socket.socket()
    sk.bind(("127.0.0.1", 0))
    port = sk.getsockname()[1]
    sk.close()
    base = f"http://127.0.0.1:{port}"
    srv = subprocess.Popen([str(ROOT / ".venv" / "bin" / "zxdev"), "devbench", "--port", str(port)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def req(method, path, body=None, raw=False):
        r = urllib.request.Request(base + path, method=method,
                                   data=json.dumps(body).encode() if body is not None else None,
                                   headers={"content-type": "application/json"} if body is not None else {})
        try:
            with urllib.request.urlopen(r, timeout=120) as resp:
                data = resp.read()
                return resp.status, (data if raw else json.loads(data))
        except urllib.error.HTTPError as ex:
            return ex.code, json.loads(ex.read() or b"{}")

    pid = None
    try:
        for _ in range(100):
            try:
                if req("GET", "/api/health")[1]["ok"]:
                    break
            except OSError:
                time.sleep(0.2)
        check("devbench serves /api/health", req("GET", "/api/health")[0] == 200)
        st, ui = req("GET", "/", raw=True)
        check("devbench serves the web UI", st == 200 and b"ZX DevBench" in ui)
        st, ps = req("GET", "/api/projects")
        check("projects listed", st == 200 and {"hello48", "hellonext"} <= {p["name"] for p in ps})
        st, r = req("POST", "/api/sessions", {"project": "hellonext"})
        pid = r.get("pid")
        check("POST /api/sessions starts a session", st == 200 and pid, str(r))
        st, r = req("POST", f"/api/sessions/{pid}/cmd/run_until", {"text": "hellonext: ready"})
        check("REST run_until", st == 200 and r["found"], str(r))
        st, png = req("GET", f"/api/sessions/{pid}/screen.png", raw=True)
        check("screen.png is a PNG", st == 200 and png[:4] == b"\x89PNG")
        st, r = req("GET", "/api/cmd/regs")
        check("GET on the selected session", st == 200 and "PC" in r)
        check("unknown command -> 400", req("GET", "/api/cmd/nope")[0] == 400)
        check("bad session -> 404", req("GET", "/api/sessions/1/cmd/status")[0] == 404)
        init = {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}}}
        r = urllib.request.Request(base + "/mcp", data=json.dumps(init).encode(), method="POST", headers={
            "content-type": "application/json", "accept": "application/json, text/event-stream"})
        with urllib.request.urlopen(r, timeout=20) as resp:
            check("MCP over HTTP at /mcp", b'"serverInfo"' in resp.read())
    finally:
        if pid:
            req("DELETE", f"/api/sessions/{pid}")
        srv.terminate()
        srv.wait(10)


def main():
    for f in (TAP, NEX):
        if not f.exists():
            print(f"build first: {f} missing (./zx build hello48 / hellonext)")
            return 2
    test_session()
    test_cli_ctl()
    test_mcp()
    test_devbench()
    print(f"\n{'FAIL' if failures else 'PASS'}: {len(failures)} failure(s)" if failures else "\nPASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
