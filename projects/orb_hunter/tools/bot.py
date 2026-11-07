"""An Orb Hunter player for the tests, through the game's probe
(src/main.c orb_probe) and the keys a player presses (O / P to run, Q to
jump, SPACE to shoot).

Its game: from the start room it runs right (or drops) into the next
cavern, and shoots them down facing them, jumping now and
then; with them gone it goes on right. When it has scored enough it stops
fighting and walks into the creatures until they wear its energy down
(going on right when nothing hurts it where it is).
"""
import struct

PROBE_FMT = "<BBBBhhhhllBBBBBBbBhh"
PROBE_SIZE = struct.calcsize(PROBE_FMT)
FIELDS = ("state", "room_x", "room_y", "alive", "hx", "hy", "health", "max_energy", "score", "items",
          "enemies", "rolled", "facing", "on_ground", "invuln", "fps", "rank", "missiles", "ex", "ey")
PLAYING, DEAD, GAMEOVER, ITEM_GET = 1, 2, 3, 4
S_TITLE, S_GAME, S_ITEM, S_OVER, S_NAME = 0x10, 0x11, 0x12, 0x13, 0x14


def decode(raw):
    return dict(zip(FIELDS, struct.unpack(PROBE_FMT, raw[:PROBE_SIZE])))


class Bot:
    def __init__(self, target_score=100):
        self.target = target_score
        self.t = 0
        self.phase = "fight"
        self.health, self.hurt_t, self.wander = None, 0, 0

    def keys(self, p):
        """the keys to hold this frame"""
        self.t += 1
        if p["state"] == S_ITEM:
            return "SPACE" if self.t % 40 > 30 else ""   # a power-up's screen: fire carries on
        if p["state"] != PLAYING:
            return ""
        if p["score"] >= self.target or self.t > 3000:
            self.phase = "give up"
        if p["health"] != self.health:
            self.health, self.hurt_t = p["health"], self.t
        if self.phase == "give up":
            if self.t - self.hurt_t > 300 and not self.wander:
                self.wander = 150                       # nothing hurts here: on to another room
            if self.wander:
                self.wander -= 1
                if not self.wander:
                    self.hurt_t = self.t
                return "P Q" if self.t % 60 < 10 else "P"
            if p["enemies"]:                            # into the nearest creature's way
                if p["ex"] < p["hx"] - 6:
                    return "O"
                if p["ex"] > p["hx"] + 6:
                    return "P"
                return 
            return "P Q" if self.t % 60 < 10 else "P"   # on to a room with some
        if p["room_x"] == 2 and p["room_y"] == 2:
            return "P"                                  # out of the start room, right
        if p["enemies"]:
            k = "SPACE"                                 # shoot, facing right, stepping forward now and then
            if (self.t // 40) % 3 == 0:
                k += " P"
            if self.t % 70 < 12:
                k += " Q"                               # a jump to dodge and to hit the flyer
            return k
        return "P SPACE"
