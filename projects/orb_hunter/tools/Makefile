# Host tools: the world from the original's levels.c
ORIG ?= $(abspath ../../../../amiga_mcp/examples/orb_hunter)
CC ?= cc

world: gen_world.c
	$(CC) -O2 -Ishim -iquote $(ORIG) -o gen_world gen_world.c $(ORIG)/levels.c
	./gen_world ../gfx/world.bin ../src/items_data.c
	rm -f gen_world

.PHONY: world
