#!/usr/bin/env python3
"""src/rr_core.c and src/rr.h from the original's (tools/orig), for the Z80:

- int is 16 bits under zsdcc and 32 on the Amiga: the game's fields, the
  events' values and the locals that can pass 32767 become int32_t (the
  state hash reads them as 32-bit values)
- FX(v) constants are written out as integers (zsdcc has no floating point
  in constant expressions), exactly as the original's (fix)(v * 65536.0)
- FMUL / FDIV are fix16.h's fmul / fdiv, the engine's fixed point bit for bit

Everything else is the original, line for line.

    python3 tools/port_core.py
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def fx(m):
    v = float(m.group(1))
    return f"{int(v * 65536.0)}L"


def port(text):
    text = re.sub(r"FX\(([0-9.]+)\)", fx, text)
    text = text.replace("FMUL(", "fmul(").replace("FDIV(", "fdiv(")
    return text


h = open(os.path.join(HERE, "orig", "rr.h")).read()
h = h.replace('#include "a3d_fix.h"', '#include "fix16.h"')
# the struct fields and the event's values: 32-bit as on the Amiga
h = re.sub(r"typedef struct \{ int type, a, b; \} RREvent;", "typedef struct { int32_t type, a, b; } RREvent;", h)
body = h[h.index("typedef struct {\n    fix x, z, v;"):h.index("} RRGame;")]
body32 = re.sub(r"(\n\s+)int ", r"\1int32_t ", body)
body32 = body32.replace("    unsigned prev_in;", "    uint32_t prev_in;")
h = h.replace(body, body32)
h = h.replace("int  rr_place_now(const RRGame *g);", "int32_t rr_place_now(const RRGame *g);")
h = h.replace("int  rr_pop_event(RRGame *g, RREvent *e);", "int rr_pop_event(RRGame *g, RREvent *e);")
h = port(h)
h = h.replace(" * units, one tick 40 ms. No Amiga headers: tested on the host too.\n */",
              " * units, one tick 40 ms. No Amiga headers: tested on the host too.\n *\n"
              " * port: made from the original's rr.h by tools/port_core.py: the game's\n"
              " * ints are int32_t (int is 16 bits on the Z80; the state hash reads them\n"
              " * as 32-bit), FX() constants written out, fix16.h for the fixed point.\n */")

c = open(os.path.join(HERE, "orig", "rr_core.c")).read()
c = c.replace('#include "a3d.h"\n#include "rr.h"', '#include "rr.h"')
c = c.replace("static void event(RRGame *g, int type, int a, int b)", "static void event(RRGame *g, int type, int32_t a, int32_t b)")
c = c.replace("        int bonus;", "        int32_t bonus;")
c = c.replace("int rr_place_now(const RRGame *g)\n{\n    int k, p = 1;", "int32_t rr_place_now(const RRGame *g)\n{\n    int k;\n    int32_t p = 1;")
c = port(c)
c = c.replace(" * AmigaOS 4 race a seed the same way.\n */",
              " * AmigaOS 4 race a seed the same way.\n *\n"
              " * port: made from the original's rr_core.c by tools/port_core.py (int32_t\n"
              " * where the Amiga's int is 32 bits, FX() constants written out, fmul /\n"
              " * fdiv): the Next races a seed as the Amiga does (tools/check_core.sh).\n */")
# the ground and the course (rr_centre, rr_height, rr_on_track and their
# helpers) go to rr_ground.c, in the main memory: the view (banked apart
# from the rules) uses them too
a = c.index("static fix fabs_(fix a)")
b = c.index("int32_t rr_place_now(")
ground = c[a:b]
helpers = ground[:ground.index("int32_t rr_centre(")]       # fabs_ and s16: the rivals use them too
c = c[:a] + helpers + c[b:]
ground = ground.replace("static fix fabs_(fix a) { return a < 0 ? -a : a; }\n", "")   # (not used there)
g = ("/* rr_ground.c - Ridge Runner's ground and course: the original's rr_centre,\n"
     " * rr_height and rr_on_track (rr_core.c), unchanged. port: a file of their\n"
     " * own, in the main memory, because the rules and the view (banked apart)\n"
     " * both use them (tools/port_core.py). */\n#include \"rr.h\"\n\n" + ground)
open(os.path.join(ROOT, "src", "rr.h"), "w").write(h)
open(os.path.join(ROOT, "src", "rr_core.c"), "w").write(c)
open(os.path.join(ROOT, "src", "rr_ground.c"), "w").write(g)
print("src/rr.h, src/rr_core.c, src/rr_ground.c")
