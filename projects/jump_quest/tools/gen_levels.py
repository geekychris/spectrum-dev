#!/usr/bin/env python3
"""The original's three levels (tools/levels/level*.h, copied verbatim from
amiga_mcp/examples/jump_quest/src/levels) for the Spectrum: each tile map is
packed with sdk/tools/zxrle.py (level 3's 2,100 bytes are mostly sky), the
entity lists are kept as they are. Writes src/levels_data.c / .h."""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / "sdk" / "tools"))
from zxrle import pack, unpack  # noqa: E402

c = ["/* made by tools/gen_levels.py from the original's levels: do not edit */",
     "#include <stdint.h>", '#include "jq.h"', '#include "levels_data.h"', ""]
h = ["/* made by tools/gen_levels.py: do not edit */", "#ifndef LEVELS_DATA_H", "#define LEVELS_DATA_H",
     '#include "jq.h"']
total = packed = 0
for n in (1, 2, 3):
    src = (HERE / "levels" / f"level{n}.h").read_text()
    s = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    s = re.sub(r"//.*", "", s)
    w = int(re.search(rf"#define LEVEL{n}_W (\d+)", s).group(1))
    body = s[s.index("tiles["):]
    body = body[body.index("= {"):body.index("};")]
    tiles = [int(x) for x in re.findall(r"\b\d+\b", body)]
    assert len(tiles) == w * 14, (n, len(tiles))
    data = pack(bytes(tiles))
    assert unpack(data) == bytes(tiles)
    total += len(tiles)
    packed += len(data)
    c.append(f"/* level {n}: {w} x 14 tiles, {len(tiles)} bytes packed to {len(data)} */")
    c.append(f"const uint8_t level{n}_packed[{len(data)}] = {{")
    for i in range(0, len(data), 16):
        c.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    c.append("};")
    ents = s[s.index("_ents[]"):]
    ents = ents[ents.index("{") + 1:ents.index("};")]
    rows = re.findall(r"\{\s*(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", ents)
    c.append(f"const EntitySpawn level{n}_ents[{len(rows)}] = {{")
    for t, x, y in rows:
        c.append(f"    {{ {t}, {x}, {y} }},")
    c.append("};\n")
    h.append(f"#define LEVEL{n}_W {w}")
    h.append(f"extern const uint8_t level{n}_packed[{len(data)}];")
    h.append(f"extern const EntitySpawn level{n}_ents[{len(rows)}];")
h.append("#endif")
(HERE.parent / "src" / "levels_data.c").write_text("\n".join(c) + "\n")
(HERE.parent / "src" / "levels_data.h").write_text("\n".join(h) + "\n")
print(f"gen_levels: {total} bytes of tiles packed to {packed}")
