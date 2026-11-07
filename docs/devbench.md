# DevBench: web UI, REST API and MCP over HTTP

```sh
./zx devbench            # http://127.0.0.1:3580/
./zx devbench --open     # ... and open the browser
./zx devbench --port 3590 --host 0.0.0.0     # other port / reachable from the LAN (no auth!)
```

One process serves three things on one port:

- **`/`** — a web page: the screen (live refresh), the guest log, run/pause
  controls, key presses and typing, a small debugger (breakpoint/watch on a
  symbol, continue, step, registers, disassembly at PC) and a console for any
  session command. Build, test and boot projects from the header.
- **`/api/...`** — REST over the same session commands as `./zx ctl`.
- **`/mcp`** — the same MCP server as `.venv/bin/zxdev-mcp` (all `emu_*`
  tools), over streamable HTTP, for MCP clients that prefer HTTP:

  ```json
  { "mcpServers": { "zx-http": { "type": "http", "url": "http://127.0.0.1:3580/mcp" } } }
  ```

DevBench does not own the emulators: it talks to session processes through
the registry (`build/sessions/`), so it sees sessions started by `./zx run`,
by MCP `emu_boot`, or by itself, and stopping DevBench leaves them running.

Port 3580 was chosen to stay clear of the other dev servers on this machine
(Amiga devbenches on 3000/3001/3010, 3DO DevBench on 3330, 3DO sessions on
7330). Check with `lsof -nP -iTCP:3580 -sTCP:LISTEN` before choosing another.

## REST

| Method + path | |
|---|---|
| `GET /api/health` | `{"ok": true, "sessions": N}` |
| `GET /api/commands` | every session command with a one-line help (needs a session) |
| `GET /api/projects` | projects, target (`zx`/`zxn`), build outputs, whether a `test.py` exists |
| `POST /api/projects/{name}/build` | `bin/zx-make` → `{"ok", "exit", "output"}` |
| `POST /api/projects/{name}/test` | `./zx test name` → same shape |
| `GET /api/sessions` | registered sessions (`pid`, `control_port`, `zrcp_port`, `machine`, `window`, `mode`, `program`, `selected`) |
| `POST /api/sessions` `{"project", "window", "machine"}` | start a session (headless+paused unless `window`) and select it |
| `DELETE /api/sessions/{pid}` | stop it |
| `POST /api/sessions/{pid}/select` | default session for `/api/cmd` |
| `GET/POST /api/cmd/{command}` | run a command on the selected (else newest) session. Args as JSON body or query string (`?frames=50`; values are parsed as JSON when they can be) |
| `GET/POST /api/sessions/{pid}/cmd/{command}` | ... on a given session |
| `GET /api/screen.png?scale=2&border=false` | current frame as PNG (also `/api/sessions/{pid}/screen.png`) |

Arcade (see [projects/arcade/README.md](../projects/arcade/README.md)):

| Method + path | |
|---|---|
| `GET /arcade` | page: every game with its picture, description, controls, a Play button (window) and Headless |
| `GET /api/arcade` | the games (`project`, `title`, `machine`, `controls`, `description`, `index`) + `built`, `image` |
| `GET /api/arcade/picture/{game}.png` | the menu's picture of a game |
| `POST /api/arcade/build` | `./zx arcade --image-only` (games, menu, SD image) |
| `POST /api/arcade/play` `{"game", "window": true, "scratch": false}` | a session on the SD image; with `game`, the menu picks it and runs it to its title (`started`). `scratch`: plays aren't saved to the image |

Errors: `404` no such session/project, `400` command error (bad
arguments, unknown command, emulator error), `502` session unreachable.

```sh
B=http://127.0.0.1:3580
curl -s -XPOST $B/api/sessions -d '{"project":"hellonext"}'
curl -s -XPOST $B/api/cmd/run_until -d '{"text":"hellonext: ready"}'
curl -s -XPOST $B/api/cmd/press -d '{"keys":"X"}'
curl -s "$B/api/cmd/log?all=true"
curl -s "$B/api/cmd/read_mem?addr=frames&length=2"
curl -s -o screen.png "$B/api/screen.png?scale=2"
```

`tools/zxdev/tests/test_remote.py` (`test_devbench`) checks these routes and
`/mcp` on every `./zx selftest`.
