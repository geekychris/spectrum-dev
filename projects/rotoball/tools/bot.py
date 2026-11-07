"""Rotoball's test bot: plays P1 from the game's probe (src/game.c
probe_update; read from the Next by test.py, or from the host build of the
same code by tools/botsim.py).

It first lets the CPU score (stands still), then wins the ball back and
carries it into the CPU's goal, swerving round the CPU on the way.
"""
import struct

ONE = 65536
PROBE_SIZE = 37
C_FWD, C_BACK, C_LEFT, C_RIGHT = 1, 2, 4, 8
PITCH_W = 40 * ONE


def decode(b):
    v = struct.unpack("<6ib2hiBBBB", bytes(b))
    return {"p1": (v[0], v[1]), "p2": (v[2], v[3]), "ball": (v[4], v[5]), "carrier": v[6],
            "score": (v[7], v[8]), "left": v[9], "state": v[10], "players": v[11],
            "cooldown": v[12], "events": v[13]}


class Bot:
    WAIT_X = -25 * ONE
    WAIT_DZ = 3 * ONE          # a glancing tackle leaves the CPU running the other way

    def __init__(self, concede=True):
        self.phase = "concede" if concede else "score"

    def step(self, p):
        """controls (C_* bits) for P1"""
        if p["state"] != 1:
            return 0
        if self.phase == "concede":
            if p["score"][1] >= 1:
                self.phase = "score"
                return 0
            # out of the CPU's way, to the sideline on P1's left (+Z)
            return C_LEFT if p["p1"][1] < 30 * ONE else 0
        (x, z), (ox, oz), (bx, bz) = p["p1"], p["p2"], p["ball"]
        c = 0
        if p["carrier"] == 0:
            c |= C_FWD
            # swerve: keep the CPU off our line while it is ahead of us
            if ox > x and abs(oz - z) < 12 * ONE and ox - x < 60 * ONE:
                # away from it, to the side with room (P1's right is -Z)
                go_minus = oz > z if abs(z) < 30 * ONE else z > 0
                c |= C_RIGHT if go_minus else C_LEFT
            return c
        # a free ball: let the CPU have it (a race to it ends in a tackle at
        # once) and wait for it a quarter of the way up the pitch, in its path:
        # it runs into us and we take the ball
        tz = (oz + self.WAIT_DZ) if p["carrier"] == 1 else bz + self.WAIT_DZ
        if tz < z - ONE:
            c |= C_RIGHT
        elif tz > z + ONE:
            c |= C_LEFT
        if x < self.WAIT_X - ONE:
            c |= C_FWD
        elif x > self.WAIT_X + ONE:
            c |= C_BACK
        return c
