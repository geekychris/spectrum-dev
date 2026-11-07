# Host test of the rules: the port against the original (see host_diff.c)
ORIG ?= $(abspath ../../../../amiga_mcp/examples/stack_tower)
CC ?= cc

run: host_diff
	./host_diff

host_diff: host_diff.c orig_core.c orig_st.h orig_names.h ../src/st_core.c ../src/st.h
	$(CC) -O2 -Wall -c -o orig_core.o orig_core.c -DORIG='"$(ORIG)/st_core.c"'
	$(CC) -O2 -o $@ host_diff.c ../src/st_core.c orig_core.o
	rm -f orig_core.o

.PHONY: run
