"""core/world.c on the Mac (P590): clang_lib builds it once, and CBodies stands in for engine.Bodies
(binding.c) through ctypes, with the binding's checks, so a test refuses what the board refuses. The build takes
no fused multiply-add (clang_lib.NO_FMA), which the twin's Python maths never does. --- MVP (P590) ---"""

import ctypes
import types

from clang_lib import HAVE_CLANG, NO_FMA, STRICT, build, core  # noqa: F401  HAVE_CLANG: the tests' skip

HIT, SLEEP = 1, 2  # events.h's EVENT_HIT and EVENT_SLEEP
MAX_BODIES, MAX_STEPS = 256, 64  # binding.c's limits on n and on steps per call

_lib = None


def lib():
    """The built library, built on the first call."""
    global _lib
    if _lib is None:
        _lib = build(core("world"), STRICT + NO_FMA)
        _lib.world_bytes.restype = ctypes.c_size_t
        _lib.world_bytes.argtypes = [ctypes.c_int32]
        _lib.world_spawn.restype = ctypes.c_int32
        _lib.world_read.restype = ctypes.c_int32
        _lib.world_drain.restype = ctypes.c_int32
    return _lib


class CBodies:
    """engine.Bodies on world.c: the same methods, the same errors. ys() is the Mac's own look at the float y."""

    def __init__(self, n):
        if not 1 <= n <= MAX_BODIES:
            raise ValueError("n is 1 to %d" % MAX_BODIES)
        self.lib, self.n = lib(), n
        self.world = ctypes.create_string_buffer(self.lib.world_bytes(n))
        self.lib.world_init(self.world, n)

    @property
    def dropped(self):
        return ctypes.c_int32.from_buffer(self.world, 8).value  # world_t: int32 cap, then events.h's n, dropped

    def spawn(self, i, x, y, vx, vy, rest_y):
        if not 0 <= i < self.n:
            raise IndexError("a body index is 0 to n - 1")
        self.lib.world_spawn(self.world, i, *(ctypes.c_float(v) for v in (x, y, vx, vy, rest_y)))

    def step(self, steps, dt, gravity, restitution, friction):
        if not 0 <= steps <= MAX_STEPS:
            raise ValueError("steps is 0 to %d" % MAX_STEPS)
        self.lib.world_step(self.world, steps, *(ctypes.c_float(v) for v in (dt, gravity, restitution, friction)))

    def read(self, out):
        if out.typecode != "h" or len(out) < 3 * self.n:
            raise ValueError("an array('h') of 3n ints is needed")
        return self.lib.world_read(self.world, (ctypes.c_int16 * len(out)).from_buffer(out))

    def drain(self, out):
        if out.typecode != "i":
            raise ValueError("an array('i') is needed")
        return self.lib.world_drain(self.world, (ctypes.c_int32 * len(out)).from_buffer(out), len(out) // 3)

    def ys(self):
        """Each body's y as the float32 the world holds: x is first after the struct, then y."""
        head = self.lib.world_bytes(0)  # sizeof(world_t)
        return list((ctypes.c_float * self.n).from_buffer(self.world, head + 4 * self.n))


def engine():
    """A stand-in for the engine module's world part: Bodies, HIT and SLEEP."""
    return types.SimpleNamespace(Bodies=CBodies, HIT=HIT, SLEEP=SLEEP)
