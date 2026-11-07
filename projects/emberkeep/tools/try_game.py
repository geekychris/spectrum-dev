"""Play a whole game with the test bot on the host build of the rules.

    python3 tools/try_game.py
"""
import ctypes

from bot import Bot, State, GM_PLAYING, GM_WIN, GM_LOSE

bot = Bot()
lib = bot.lib
lib.sim_start()
last = None
for frame in range(60000):
    gs = ctypes.string_at(lib.sim_gs(), bot.size)
    s = State(gs)
    if s.mode in (GM_WIN, GM_LOSE):
        break
    keys = bot.decide(gs)
    bot.put(gs)
    lib.sim_tick(keys)
    s = State(ctypes.string_at(lib.sim_gs(), bot.size))
    now = (s.level, s.lives, s.runes_left, s.mode)
    if now != last:
        print(f"frame {frame:5d}: level {s.level} lives {s.lives} runes left {s.runes_left} mode {s.mode} "
              f"score {s.score} lantern {s.lantern}")
        last = now
print("end:", {GM_WIN: "WON", GM_LOSE: "LOST"}.get(s.mode, "timeout"), "score", s.score)
