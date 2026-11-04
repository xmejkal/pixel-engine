"""engine/core/grid.c and fx_balls.c on the Mac (P598): clang_lib builds them once, and CBalls stands in for
engine.Balls (engine/binding.c) through ctypes, with the binding's checks, so a test refuses what the board refuses.
Grid is the Mac's own look at grid.c. --- MVP (P598) ---"""

import ctypes
import types

from clang_lib import HAVE_CLANG, STRICT, build, core  # noqa: F401  HAVE_CLANG: the tests' skip

HIT = 1  # events.h's EVENT_HIT
MAX_BALLS, MAX_STEPS, MAX_SUBSTEPS = 320, 64, 8  # binding.c's limits on n, steps and substeps
W, H = 320, 240  # fx_balls.h's box, FX_BALLS_W x FX_BALLS_H
R_MIN, R_MAX, COLOURS = 2, 5, 16  # fx_balls.h's radii and palette size
PANEL_W, PANEL_H = 240, 320  # the panel fx_balls_draw fills

_lib = _grid_lib = None


def grid_lib():
    """grid.c alone, built on the first call."""
    global _grid_lib
    if _grid_lib is None:
        _grid_lib = build(core("grid"), STRICT)
        _grid_lib.grid_bytes.restype = ctypes.c_size_t
        _grid_lib.grid_cell.restype = ctypes.c_int32
        _grid_lib.grid_cell.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32,
                                        ctypes.POINTER(ctypes.POINTER(ctypes.c_int32))]
    return _grid_lib


def lib():
    """The balls' library, grid.c and fx_balls.c, built on the first call."""
    global _lib
    if _lib is None:
        _lib = build(core("grid", "fx_balls"), STRICT)
        _lib.fx_balls_bytes.restype = ctypes.c_size_t
        _lib.fx_balls_bytes.argtypes = [ctypes.c_int32]
        _lib.fx_balls_spawn.restype = ctypes.c_int32
        _lib.fx_balls_read.restype = ctypes.c_int32
        _lib.fx_balls_drain.restype = ctypes.c_int32
    return _lib


class Grid:
    """grid.c: cols x rows cells of cell_px, room for cap items. build(xs, ys) sorts the items in, cell(cx, cy) lists
    one cell's items."""

    def __init__(self, cols, rows, cell_px, cap):
        self.lib = grid_lib()
        self.block = ctypes.create_string_buffer(self.lib.grid_bytes(cols, rows, cap))
        self.lib.grid_init(self.block, cols, rows, cell_px, cap)

    def build(self, xs, ys):
        n = len(xs)
        self.lib.grid_build(self.block, (ctypes.c_float * n)(*xs), (ctypes.c_float * n)(*ys), n)

    def cell(self, cx, cy):
        items = ctypes.POINTER(ctypes.c_int32)()
        m = self.lib.grid_cell(self.block, cx, cy, ctypes.byref(items))
        return [items[k] for k in range(m)]


class CBalls:
    """engine.Balls on fx_balls.c: the same methods, the same errors. xy() is the Mac's own look at the float x and y."""

    def __init__(self, n):
        if not 1 <= n <= MAX_BALLS:
            raise ValueError("n is 1 to %d" % MAX_BALLS)
        self.lib, self.n = lib(), n
        self.block = ctypes.create_string_buffer(self.lib.fx_balls_bytes(n))
        self.lib.fx_balls_init(self.block, n)

    @property
    def count(self):
        return ctypes.c_int32.from_buffer(self.block, 4).value  # fx_balls_t: int32 cap, n, then events.h's n, dropped

    @property
    def dropped(self):
        return ctypes.c_int32.from_buffer(self.block, 12).value

    def spawn(self, x, y, vx, vy, r, colour):
        if not R_MIN <= r <= R_MAX or not 0 <= colour < COLOURS:
            raise ValueError("r is %d to %d, colour 0 to %d" % (R_MIN, R_MAX, COLOURS - 1))
        return self.lib.fx_balls_spawn(self.block, *(ctypes.c_float(v) for v in (x, y, vx, vy)), r, colour)

    def step(self, steps, substeps, dt, gravity, damping, hit_min):
        if not 0 <= steps <= MAX_STEPS or not 1 <= substeps <= MAX_SUBSTEPS:
            raise ValueError("steps is 0 to %d, substeps 1 to %d" % (MAX_STEPS, MAX_SUBSTEPS))
        self.lib.fx_balls_step(self.block, steps, substeps, *(ctypes.c_float(v) for v in (dt, gravity, damping, hit_min)))

    def read(self, out):
        if out.typecode != "h" or len(out) < 3 * self.n:
            raise ValueError("an array('h') of 3n ints is needed")
        return self.lib.fx_balls_read(self.block, (ctypes.c_int16 * len(out)).from_buffer(out))

    def drain(self, out):
        if out.typecode != "i":
            raise ValueError("an array('i') is needed")
        return self.lib.fx_balls_drain(self.block, (ctypes.c_int32 * len(out)).from_buffer(out), len(out) // 3)

    def draw(self, tft, palette, bg):
        if palette.typecode != "H" or len(palette) < COLOURS:
            raise ValueError("an array('H') of %d colours is needed" % COLOURS)
        self.lib.fx_balls_draw(self.block, (ctypes.c_uint16 * (len(tft.frame) // 2)).from_buffer(tft.frame),
                               (ctypes.c_uint16 * len(palette)).from_buffer(palette), ctypes.c_uint16(bg))

    def clear(self):
        self.lib.fx_balls_clear(self.block)

    def xy(self):
        """Each ball's x and y as the float32 the kernel holds: the floats follow the struct and the grid block, x
        first, then y."""
        head, n = self.lib.fx_balls_bytes(0) + 4 * self.n, self.count  # the grid's items grow by 4 bytes a ball
        return (list((ctypes.c_float * n).from_buffer(self.block, head)),
                list((ctypes.c_float * n).from_buffer(self.block, head + 4 * self.n)))


def engine():
    """A stand-in for the engine module's balls part: Balls and HIT."""
    return types.SimpleNamespace(Balls=CBalls, HIT=HIT)
