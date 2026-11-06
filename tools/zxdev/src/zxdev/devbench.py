"""DevBench: web UI + REST API + MCP over HTTP, on one port (default :3580).

    ./zx devbench [--port 3580] [--open]

    /                                        web UI (screen, log, input, debugger, console)
    /api/health
    /api/commands                            every session command with help
    /api/projects                            projects and their build output
    /api/projects/{name}/build   POST        build (z88dk in Docker)
    /api/projects/{name}/test    POST        ./zx test <name>
    /api/sessions                GET|POST    list | start {"project", "window", "machine"}
    /api/sessions/{pid}          DELETE      stop a session
    /api/sessions/{pid}/select   POST        make it the default for /api/cmd
    /api/cmd/{command}           GET|POST    run a session command (JSON body or ?k=v)
    /api/sessions/{pid}/cmd/{command}        ... on a specific session
    /api/screen.png                          current frame (?scale=2&border=0)
    /mcp                                     the MCP server (streamable HTTP)
"""
from __future__ import annotations

import base64
import json
import os
import subprocess
import threading
from pathlib import Path

from starlette.concurrency import run_in_threadpool
from starlette.requests import Request
from starlette.responses import FileResponse, JSONResponse, Response
from starlette.routing import Route

from .control import ProtocolError, SessionClient, list_sessions
from .emu import repo_root
from .session import resolve_program

ROOT = repo_root()
WEB = Path(__file__).parent / "web"


class Bench:
    def __init__(self):
        self.lock = threading.Lock()
        self.clients: dict[int, SessionClient] = {}
        self.locks: dict[int, threading.Lock] = {}
        self.selected: int | None = None

    def pid_for(self, pid: int | None) -> int:
        ss = list_sessions()
        pids = [s["pid"] for s in ss]
        if pid is None:
            pid = self.selected if self.selected in pids else (pids[-1] if pids else None)
        if pid is None:
            raise LookupError("no running session: start one (POST /api/sessions) or ./zx run <project>")
        if pid not in pids:
            raise LookupError(f"no session {pid}")
        return pid

    def client(self, pid: int) -> tuple[SessionClient, threading.Lock]:
        with self.lock:
            c = self.clients.get(pid)
            if c is None or not c.alive:
                info = next(s for s in list_sessions() if s["pid"] == pid)
                c = SessionClient(info["control_port"])
                c.info = info
                self.clients[pid] = c
                self.locks[pid] = threading.Lock()
            return c, self.locks[pid]

    def call(self, pid: int | None, cmd: str, args: dict):
        pid = self.pid_for(pid)
        c, lk = self.client(pid)
        with lk:
            try:
                return c.call(cmd, **args)
            except (OSError, ProtocolError) as ex:
                if isinstance(ex, OSError):
                    self.clients.pop(pid, None)
                raise


bench = Bench()


def ok(data, status: int = 200):
    return JSONResponse(data, status_code=status)


def err(msg: str, status: int):
    return JSONResponse({"error": msg}, status_code=status)


def _coerce(v: str):
    try:
        return json.loads(v)
    except json.JSONDecodeError:
        return v


async def args_of(request: Request) -> dict:
    args = {k: _coerce(v) for k, v in request.query_params.items()}
    if request.method == "POST":
        body = await request.body()
        if body.strip():
            data = json.loads(body)
            if not isinstance(data, dict):
                raise ValueError("body must be a JSON object")
            args.update(data)
    return args


async def call(pid, cmd: str, args: dict):
    try:
        return ok(await run_in_threadpool(bench.call, pid, cmd, args))
    except LookupError as ex:
        return err(str(ex), 404)
    except ProtocolError as ex:
        return err(str(ex), 400)
    except OSError as ex:
        return err(f"session connection failed: {ex}", 502)


async def index(request: Request):
    return FileResponse(WEB / "index.html")


async def health(request: Request):
    return ok({"ok": True, "sessions": len(list_sessions()), "root": str(ROOT)})


async def commands(request: Request):
    return await call(None, "help", {})


async def projects(request: Request):
    out = []
    for d in sorted((ROOT / "projects").iterdir()):
        mk = d / "Makefile"
        if mk.exists():
            built = sorted(str(p.relative_to(ROOT)) for ext in ("*.tap", "*.nex") for p in (d / "build").glob(ext))
            out.append({"name": d.name, "target": "zxn" if "TARGET := zxn" in mk.read_text() else "zx",
                        "built": built, "test": (d / "test.py").exists()})
    return ok(out)


def _run(cmd, timeout=1800):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return {"ok": r.returncode == 0, "exit": r.returncode, "output": (r.stdout + r.stderr)[-20000:]}


async def build(request: Request):
    name = request.path_params["name"]
    if not (ROOT / "projects" / name / "Makefile").exists():
        return err(f"no project {name}", 404)
    return ok(await run_in_threadpool(_run, [str(ROOT / "bin" / "zx-make"), "-C", str(ROOT / "projects" / name)]))


async def test(request: Request):
    name = request.path_params["name"]
    if not (ROOT / "projects" / name / "Makefile").exists():
        return err(f"no project {name}", 404)
    return ok(await run_in_threadpool(_run, [str(ROOT / "zx"), "test", name]))


async def sessions(request: Request):
    if request.method == "GET":
        ss = list_sessions()
        for s in ss:
            s["selected"] = s["pid"] == (bench.selected or (ss[-1]["pid"] if ss else None))
        return ok(ss)
    a = await args_of(request)
    try:
        prog = resolve_program(a.get("project")) if a.get("project") else None
    except FileNotFoundError as ex:
        return err(str(ex), 404)

    def spawn():
        c = SessionClient.spawn(prog, window=bool(a.get("window")), machine=a.get("machine"))
        with bench.lock:
            bench.clients[c.info["pid"]] = c
            bench.locks[c.info["pid"]] = threading.Lock()
            bench.selected = c.info["pid"]
        return c.info
    try:
        return ok(await run_in_threadpool(spawn))
    except ProtocolError as ex:
        return err(str(ex), 500)


async def session_one(request: Request):
    pid = request.path_params["pid"]
    res = await call(pid, "quit", {})
    with bench.lock:
        c = bench.clients.pop(pid, None)
    if c is not None and c.proc is not None:
        await run_in_threadpool(c.close, True)
    return res


async def select(request: Request):
    pid = request.path_params["pid"]
    try:
        bench.pid_for(pid)
    except LookupError as ex:
        return err(str(ex), 404)
    bench.selected = pid
    return ok({"selected": pid})


async def run_cmd(request: Request):
    try:
        a = await args_of(request)
    except (ValueError, json.JSONDecodeError) as ex:
        return err(str(ex), 400)
    return await call(request.path_params.get("pid"), request.path_params["command"], a)


async def screen_png(request: Request):
    a = {k: _coerce(v) for k, v in request.query_params.items()}
    try:
        res = await run_in_threadpool(bench.call, request.path_params.get("pid"), "screenshot", a)
    except LookupError as ex:
        return err(str(ex), 404)
    except (ProtocolError, OSError) as ex:
        return err(str(ex), 400)
    return Response(base64.b64decode(res["png_b64"]), media_type="image/png",
                    headers={"Cache-Control": "no-store"})


ROUTES = [
    Route("/", index),
    Route("/api/health", health),
    Route("/api/commands", commands),
    Route("/api/projects", projects),
    Route("/api/projects/{name}/build", build, methods=["POST"]),
    Route("/api/projects/{name}/test", test, methods=["POST"]),
    Route("/api/sessions", sessions, methods=["GET", "POST"]),
    Route("/api/sessions/{pid:int}", session_one, methods=["DELETE"]),
    Route("/api/sessions/{pid:int}/select", select, methods=["POST"]),
    Route("/api/sessions/{pid:int}/screen.png", screen_png),
    Route("/api/sessions/{pid:int}/cmd/{command}", run_cmd, methods=["GET", "POST"]),
    Route("/api/screen.png", screen_png),
    Route("/api/cmd/{command}", run_cmd, methods=["GET", "POST"]),
]


def make_app():
    """Our REST routes on the MCP server's Starlette app (keeps its lifespan for /mcp)."""
    from .mcp_server import server as mcp
    app = mcp.streamable_http_app(streamable_http_path="/mcp")
    app.router.routes[0:0] = ROUTES
    return app


def main(port: int = 3580, host: str = "127.0.0.1", open_browser: bool = False):
    import uvicorn
    url = f"http://{host}:{port}/"
    print(f"ZX DevBench on {url}  (REST /api, MCP /mcp)", flush=True)
    if open_browser:
        threading.Timer(1.0, lambda: os.system(f"open {url} >/dev/null 2>&1 || xdg-open {url} >/dev/null 2>&1")).start()
    uvicorn.run(make_app(), host=host, port=port, log_level="warning")
