"""engine/core/grid.c and fx_balls.c on the Mac (P598): clang_lib builds them once, and CBalls stands in for
engine.Balls (engine/binding.c) through ctypes, with the binding's checks, so a test refuses what the board refuses.
Grid is the Mac's own look at grid.c. --- MVP (P598) ---"""

import ctypes
import types

from clang_lib import HAVE_CLANG, STRICT, build, core  # noqa: F401  HAVE_CLANG: the tests' skip

HIT = 1  # events.h's EVENT_HIT
MAX_BALLS, MAX_STEPS, MAX_SUBSTEPS = 320, 64, 8  # binding.c's limits on n, steps and substeps
W, H = 320, 240  # fx_balls.h's box, FX_BALLS_W x FX_BALLS_H
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
