# Host test of the rules: the port against the original (see host_diff.c)
ORIG ?= $(abspath ../../../../amiga_mcp/examples/orb_hunter)
CC ?= cc

run: host_diff
	./host_diff

host_diff: host_diff.c port_side.c orig_side.c side.h snap.h ../src/game.c ../src/game.h ../src/items_data.c
	$(CC) -O2 -w -c -o orig_side.o orig_side.c -I../tools/shim -iquote $(ORIG) \
	    -DORIG_GAME='"$(ORIG)/game.c"' -DORIG_LEVELS='"$(ORIG)/levels.c"'
	$(CC) -O2 -Wall -o $@ host_diff.c port_side.c ../src/game.c ../src/particles.c ../src/items_data.c orig_side.o
	rm -f orig_side.o

.PHONY: run
