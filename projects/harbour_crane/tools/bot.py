"""Harbour Crane's test bot: works the crane with the keys from the game's
probe (src/game.c probe(); on the host, tools/sim.c's), as a player would:
the trolley to the pick spot, the hook down onto the container, grab, up,
along to a slot (or past the stern, to lose one on purpose), down to just
above what is there, let go.

    bot = Bot(); bot.task("load", slot) or bot.task("dump")
    keys = bot.step(decode(probe_bytes))     -> C_* bits; bot.done when the task is over
"""
import struct

PROBE_SIZE = 25
C_LEFT, C_RIGHT, C_UP, C_DOWN, C_FIRE = 1, 2, 4, 8, 16
PICK_X, SHIP_X0, SLOT_W, DECK_Y, BOX_H = -140, 100, 50, -40, 30
TX_MAX = 430


def decode(b):
    v = struct.unpack("<BhhhbbBBBBBBBBihBB", bytes(b))
    keys = ("state", "tx", "hx", "hy", "carrying", "falling", "next", "nbox", "loaded", "lost", "level", "over",
            "won", "autoplay", "score", "tv", "between", "start_level")
    return dict(zip(keys, v))


class Bot:
    def __init__(self):
        self.mode, self.done, self.slot, self.tier = None, True, 0, 0
        self.settle = 0

    def task(self, mode, slot=0, tier=0):
        """mode "load": the next container into slot (on a stack tier high), "dump": past the stern"""
        self.mode, self.slot, self.tier, self.done = mode, slot, tier, False
        self.phase, self.settle = "up", 0
        self.width = None

    def _move(self, p, target):
        """the trolley to target: full speed, then let it coast down in time (acc ~0.08 a tick)"""
        d = target - p["tx"]
        v = p["tv"] / 256.0
        brake = v * v / (2 * 0.045) + 2
        if d > 0 and (v < 0 or d > brake):
            return C_RIGHT
        if d < 0 and (v > 0 or -d > brake):
            return C_LEFT
        return 0

    def _settled(self, p, target, tol=3):
        """at the target, stopped, and the load hanging still for a while"""
        if abs(p["tx"] - target) <= tol and abs(p["tv"]) < 20 and abs(p["hx"] - target) <= 6:
            self.settle += 1
        else:
            self.settle = 0
        return self.settle >= 8

    def step(self, p):
        if self.done or p["state"] != 1 or p["between"] or p["falling"] != -1:
            return 0
        carrying = p["carrying"] != -1
        if self.phase == "up":                       # up to the travel height
            if p["hy"] < (150 if not carrying else 175):
                return C_UP
            self.phase = "go"
        if self.phase == "go":
            if not carrying:
                target = PICK_X
            elif self.mode == "dump":
                target = TX_MAX
            else:
                w = SLOT_W - 2 if self.width is None else self.width
                target = SHIP_X0 + self.slot * SLOT_W + 1 + w // 2
            k = self._move(p, target)
            if k or not self._settled(p, target):
                return k
            self.target = target
            self.phase = "down"
        if self.phase == "down":
            if not carrying:                         # onto the container (its top at 30): the hook stops on it
                if p["hy"] > 36:
                    return C_DOWN
                self.phase = "grab"
            else:
                top = DECK_Y + self.tier * BOX_H if self.mode == "load" else DECK_Y
                if p["hy"] - BOX_H - top > 14:
                    return C_DOWN
                self.phase = "release"
        if self.phase == "grab":
            if carrying:
                self.phase = "up"
                return 0
            self.phase = "grab_wait"
            return C_FIRE
        if self.phase == "grab_wait":                # (fire acts on a press: let it go first)
            self.phase = "grab" if not carrying else "up"
            return 0
        if self.phase == "release":
            self.phase = "released"
            return C_FIRE
        if self.phase == "released":
            if not carrying:
                self.done = True
            else:
                self.phase = "release"
            return 0
        return 0
