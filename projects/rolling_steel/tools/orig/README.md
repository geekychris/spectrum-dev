The original's files, unchanged, from amiga_mcp/examples/rolling_steel
(the 3DO version's C, MIT, see LICENSE.original): game.c, game.h, phys.c,
course.c, rs.h, render.c, and data/course1-6.bin (built by the 3DO
version's tools/courses.py from the Unity game's course files).

tools/hostsim.c builds game.c, phys.c and course.c on the host (32-bit
long) as the reference for the Next's rules; tools/gen_course.py draws the
courses and packs their physics from data/.
