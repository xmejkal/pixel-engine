"""fx_fire_cool (P803) on the Mac: clang builds core/fx_fire.c with the driver's blend565.h, and ctypes calls it. A
cool lowers only the cells of its rectangle, at least to 0, clips the rectangle to the field, kills the embers inside
it and changes nothing else. Skipped without clang or without the driver submodule. Run from the repository root:
python3 -B -m unittest discover tests. --- MVP (P803) ---"""

import ctypes
import os
import random
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from clang_lib import HAVE_CLANG, S3LCD_SRC, STRICT, build, core  # noqa: E402

HAVE_DRIVER = os.path.exists(os.path.join(S3LCD_SRC, "blend565.h"))
W, H, SEED = 24, 18, 803
GUARD = W + 1  # bytes after the field, which no cool may write: a row past the bottom and a cell past its end
MAP_W, MAP_H, MAX_SPOTS, MAX_EMBERS, MAX_RISES = 256, 32, 16, 24, 8  # fx_fire.h's FX_FIRE_ sizes


class Field(ctypes.Structure):
    """fx_fire_t, field for field (pixel-games' tests/test_fire_core.py has the same)."""

    _fields_ = [("w", ctypes.c_int32), ("h", ctypes.c_int32), ("heat", ctypes.POINTER(ctypes.c_uint8)),
                ("cooling", ctypes.POINTER(ctypes.c_uint8)), ("rng", ctypes.c_uint32)]
    _fields_ += [(name, ctypes.c_int32) for name in ("map_row", "fast_map_row", "decay", "cool", "sway", "spots",
                                                      "spread", "fast", "embers", "fade")]
    _fields_ += [("spot_x", ctypes.c_int32 * MAX_SPOTS)]
    _fields_ += [(name, ctypes.c_int32 * MAX_EMBERS) for name in ("ember_x", "ember_y", "ember_vx", "ember_vy",
                                                                   "ember_heat")]
    _fields_ += [(name, ctypes.c_int32) for name in ("base", "filter", "born")]
    _fields_ += [("born_rise", ctypes.c_int32 * MAX_RISES)]


class Heat:
    """A field of W x H with random heat, seeded, GUARD bytes of random after it, and its cooling map."""

    def __init__(self, lib, seed=SEED):
        self.lib, self.f = lib, Field()
        self.heat, self.cooling = (ctypes.c_uint8 * (W * H + GUARD))(), (ctypes.c_uint8 * (MAP_W * MAP_H))()
        lib.fx_fire_init(ctypes.byref(self.f), self.heat, self.cooling, W, H, SEED)
        rand = random.Random(seed)
        for i in range(W * H + GUARD):
            self.heat[i] = rand.randrange(256)

    def cool(self, x0, y0, x1, y1, amount):
        self.lib.fx_fire_cool(ctypes.byref(self.f), x0, y0, x1, y1, amount)

    def cells(self):
        """The field's cells and the guard after them."""
        return list(self.heat)

    def state(self):
        """Every byte of fx_fire_t, the heat and cooling pointers included (a cool moves neither)."""
        return bytes(self.f)


def expected(before, x0, y0, x1, y1, amount):
    """The cells after the cool, in Python: the inclusive rectangle, ends swapped and clipped, each cell less amount,
    and the guard as it was."""
    x0, x1, y0, y1 = min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)
    inside = [i < W * H and x0 <= i % W <= x1 and y0 <= i // W <= y1 for i in range(len(before))]
    return [max(0, v - amount) if k else v for k, v in zip(inside, before)]


@unittest.skipUnless(HAVE_CLANG, "clang is missing: core/fx_fire.c is not built")
@unittest.skipUnless(HAVE_DRIVER, "the driver submodule is missing: fx_fire.c needs its blend565.h "
                                  "(git submodule update --init)")
class FireCoolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = build(core("fx_fire"), STRICT + ("-I", S3LCD_SRC))

    def test_a_cool_lowers_exactly_its_rectangle_by_amount(self):
        field = Heat(self.lib)
        before = field.cells()
        field.cool(5, 3, 9, 11, 40)
        after = field.cells()
        self.assertEqual(after, expected(before, 5, 3, 9, 11, 40))
        changed = {(i % W, i // W) for i, (a, b) in enumerate(zip(before, after)) if a != b}
        self.assertTrue(changed)
        self.assertTrue(all(5 <= x <= 9 and 3 <= y <= 11 for x, y in changed))

    def test_it_saturates_at_0_and_255_clears(self):
        field = Heat(self.lib)
        for i in range(W * H):
            field.heat[i] = 30
        field.cool(0, 0, 3, 3, 50)
        self.assertEqual([field.heat[y * W + x] for y in range(4) for x in range(4)], [0] * 16)
        field = Heat(self.lib)
        before = field.cells()
        field.cool(0, 0, W - 1, H - 1, 255)
        self.assertEqual(field.cells(), [0] * (W * H) + before[W * H:])
        self.assertTrue(any(before))

    def test_ends_swapped_give_the_same_cool(self):
        a, b = Heat(self.lib), Heat(self.lib)
        a.cool(2, 4, 12, 9, 77)
        b.cool(12, 9, 2, 4, 77)
        self.assertEqual(a.cells(), b.cells())

    def test_a_rectangle_off_the_field_is_clipped(self):
        for rect in ((-W, -H, 3, 2), (W - 3, H - 2, 2 * W, 2 * H), (-W, 5, 2 * W, 5), (W, 0, 2 * W, H - 1),
                     (W - 2, 0, W, H), (-W, -H, -1, -1)):
            with self.subTest(rect=rect):
                field = Heat(self.lib)
                before = field.cells()
                field.cool(*rect, 100)
                self.assertEqual(field.cells(), expected(before, *rect, 100))

    def test_embers_inside_die_and_the_others_live(self):
        field = Heat(self.lib)
        inside = ((6, 4), (9, 8), (6, 8))  # cells in the rectangle 6..9 x 4..8
        outside = ((5, 4), (10, 8), (7, 3), (7, 9))
        for i, (x, y) in enumerate(inside + outside):
            field.f.ember_x[i], field.f.ember_y[i] = x * 256 + 255, y * 256 + 128  # anywhere in the cell
            field.f.ember_heat[i] = 200
        field.cool(6, 4, 9, 8, 1)
        heats = list(field.f.ember_heat)
        self.assertEqual(heats[:len(inside)], [0] * len(inside))
        self.assertEqual(heats[len(inside):len(inside) + len(outside)], [200] * len(outside))

    def test_amount_0_changes_nothing(self):
        field = Heat(self.lib)
        field.f.ember_x[0], field.f.ember_y[0], field.f.ember_heat[0] = 3 * 256, 3 * 256, 150
        before, state = field.cells(), field.state()
        field.cool(0, 0, 10, 10, 0)
        self.assertEqual(field.cells(), before)
        self.assertEqual(field.state(), state)  # the ember inside lives too

    def test_nothing_but_the_cells_and_the_embers_changes(self):
        field = Heat(self.lib)
        state = field.state()
        field.cool(-3, -3, W + 3, H + 3, 90)  # no ember is live, so fx_fire_t stays byte for byte
        self.assertEqual(field.state(), state)


if __name__ == "__main__":
    unittest.main()
