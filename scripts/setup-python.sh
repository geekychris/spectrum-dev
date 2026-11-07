#!/usr/bin/env bash
# Create .venv with the zxdev package (emulator host, CLI, MCP server, DevBench)
# and what sdk/tools and the games' asset tools need (Pillow, numpy).
source "$(dirname "$0")/lib.sh"

step "python environment (.venv)"
VENV="$ZX_ROOT/.venv"
if have uv; then
  [ -x "$VENV/bin/python" ] || uv venv -q --python ">=3.10" "$VENV"
  uv pip install -q --python "$VENV/bin/python" -e "$ZX_ROOT/tools/zxdev"
else
  py=""
  for c in python3.13 python3.12 python3.11 python3.10 python3; do
    if have "$c" && "$c" -c 'import sys; sys.exit(sys.version_info < (3,10))' 2>/dev/null; then py="$c"; break; fi
  done
  [ -n "$py" ] || die "need Python >= 3.10 (or install uv: https://docs.astral.sh/uv/)"
  [ -x "$VENV/bin/python" ] || "$py" -m venv "$VENV"
  "$VENV/bin/python" -m pip install -q --upgrade pip
  "$VENV/bin/python" -m pip install -q -e "$ZX_ROOT/tools/zxdev"
fi
"$VENV/bin/python" -c "import zxdev.emu, PIL, numpy, mcp, starlette, uvicorn" 2>/dev/null >/dev/null \
  || die "python deps failed to import"
ok "zxdev CLI at .venv/bin/zxdev, MCP server at .venv/bin/zxdev-mcp"
