# DevBench: web UI, REST API and MCP over HTTP

```sh
./zx devbench            # http://127.0.0.1:3580/  (the arcade: /arcade)
./zx devbench --open     # ... and open the arcade page in the browser
./zx devbench --port 3590 --host 0.0.0.0     # other port / reachable from the LAN (no auth!)
```

One process serves three things on one port:

- **`/arcade`** — every game with its picture, description and controls:
  *Play here* (headless, the live screen in the page, your keyboard as the
  Spectrum's), *Play in a window*, Rebuild, Open the menu; and the other
  projects (tests, demos) with why they aren't games. The page `--open` lands on.

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
| `GET /arcade` | page: every game (picture, description, controls), *Play here* (live in the page), *Play in a window*; other projects with why; `#play=<project>` starts one |
| `GET /api/arcade` | `games`: every project with an `arcade.json` (`project`, `title`, `machine`, `controls`, `description`, `has_picture`, `built`, `on_image`, `index` on the image, `stale`, `note`, `problem`); `others`: the other projects (`reason`); `built`, `image`, `missing_from_image` |
| `GET /api/arcade/picture/{game}.png` | the menu's picture of a game (another project: its first screenshot) |
| `POST /api/arcade/build` | `./zx arcade --image-only` (games, menu, SD image) |
| `POST /api/arcade/play` `{"game", "window": true, "scratch": false, "direct": false, "resume": false}` | a game on the SD image: a session on the image, the menu picks it and runs it to its title (`started`, `via: "menu"`). A game not on the image (or `direct`): its own program in a session, built first if need be (`via: "direct"`). No `game`: the menu. `scratch`: plays aren't saved to the image; `resume`: a headless session free-runs once the game is up (the page's *Play here*) |

`GET /api/screen.png` ignores a `t=` query parameter (the pages' cache-buster).

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
