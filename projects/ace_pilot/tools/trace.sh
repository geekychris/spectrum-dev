#!/bin/sh
# The port's rules against the original's, tick by tick (tools/trace.c), for a
# few seeds and both player counts: prints "same" or the first difference.
cd "$(dirname "$0")/.." || exit 1
O=${ACE_ORIG:-../../../amiga_mcp/examples/ace_pilot}
SDK=../../sdk
B=build/trace; mkdir -p $B
cc -O1 -w -DORIG -I tools/orig_shim -I $O -I src -o $B/orig tools/trace.c -lm $O/game.c $O/engine3d.c $O/models.c || exit 1
cc -O1 -w -DACE_ORIGINAL_ENEMY_SPEED -I src -I $SDK/include -o $B/port tools/trace.c -lm src/game.c src/colour.c src/trig.c src/models.c $SDK/src/fix88.c $SDK/src/fix88_tab.c || exit 1
fail=0
for bot in 0 1; do for p in 1 2; do for s in 1 2 3; do
  $B/orig $s 30000 $p $bot > $B/o.txt; $B/port $s 30000 $p $bot > $B/p.txt
  what="seed $s players $p bot $bot"
  if cmp -s $B/o.txt $B/p.txt; then
    echo "$what: same ($(wc -l < $B/o.txt | tr -d ' ') ticks, $(grep -c boom $B/o.txt) explosions, $(grep -c ' die' $B/o.txt) deaths, wave $(tail -1 $B/o.txt | sed 's/.* w=\([0-9]*\).*/\1/'))"
  else echo "$what: differ"; diff $B/o.txt $B/p.txt | head -4; fail=1; fi
done; done; done
# the port as shipped (enemy speed fixed, ORIGINAL_BUGS.md) with the autopilot
cc -O1 -w -I src -I $SDK/include -o $B/fixed tools/trace.c src/game.c src/colour.c src/trig.c src/models.c $SDK/src/fix88.c $SDK/src/fix88_tab.c -lm || exit 1
for sp in orig fixed; do
  [ $sp = orig ] && bin=$B/orig || bin=$B/fixed
  $bin 1 30000 1 1 > $B/x.txt
  echo "autopilot, 10 minutes, $sp enemy speed: $(grep -c boom $B/x.txt) explosions, $(grep -c ' die' $B/x.txt) deaths, wave $(tail -1 $B/x.txt | sed 's/.* w=\([0-9]*\).*/\1/'), score $(tail -1 $B/x.txt | sed 's/.* p0=[^ ]* s\([0-9]*\).*/\1/')"
done
exit $fail
