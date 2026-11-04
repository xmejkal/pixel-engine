"""engine/core/grid.c and fx_balls.c (P598) on the Mac: tests/balls_lib.py builds them with clang and steps them
through CBalls, the stand-in for engine.Balls. Run from the repository root: python3 -m unittest discover tests.
--- MVP (P598) ---"""

import os
import random
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from balls_lib import HAVE_CLANG, Grid  # noqa: E402


@unittest.skipUnless(HAVE_CLANG, "clang is missing: engine/core/grid.c is not built")
class GridTest(unittest.TestCase):
    COLS, ROWS, CELL = 32, 24, 10

    def test_each_item_is_in_the_cell_of_its_centre_once(self):
        rng = random.Random(598)
        xs = [rng.uniform(-5, 325) for _ in range(300)]  # some past the walls: clamped into the edge cells
        ys = [rng.uniform(-5, 245) for _ in range(300)]
        grid = Grid(self.COLS, self.ROWS, self.CELL, 300)
        grid.build(xs, ys)
        seen = []
        for cy in range(self.ROWS):
            for cx in range(self.COLS):
                for i in grid.cell(cx, cy):
                    self.assertEqual(cx, min(self.COLS - 1, max(0, int(xs[i] // self.CELL))))
                    self.assertEqual(cy, min(self.ROWS - 1, max(0, int(ys[i] // self.CELL))))
                    seen.append(i)
        self.assertEqual(list(range(300)), sorted(seen))

    def test_the_3_x_3_cells_around_an_item_hold_every_neighbour_within_a_cell(self):
        rng = random.Random(599)
        xs = [rng.uniform(0, 320) for _ in range(300)]
        ys = [rng.uniform(0, 240) for _ in range(300)]
        grid = Grid(self.COLS, self.ROWS, self.CELL, 300)
        grid.build(xs, ys)
        pairs = {(i, j) for i in range(300) for j in range(i + 1, 300)
                 if (xs[i] - xs[j]) ** 2 + (ys[i] - ys[j]) ** 2 <= self.CELL ** 2}
        found = set()
        for i in range(300):
            cx, cy = int(xs[i] // self.CELL), int(ys[i] // self.CELL)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if 0 <= cx + dx < self.COLS and 0 <= cy + dy < self.ROWS:
                        for j in grid.cell(cx + dx, cy + dy):
                            if j > i:
                                found.add((i, j))
        self.assertTrue(pairs <= found)
        self.assertGreater(len(pairs), 100)  # the fixture holds real neighbours

    def test_a_second_build_forgets_the_first(self):
        grid = Grid(self.COLS, self.ROWS, self.CELL, 4)
        grid.build([5.0, 15.0, 25.0, 35.0], [5.0] * 4)
        grid.build([5.0, 5.0], [5.0, 5.0])
        self.assertEqual([0, 1], sorted(grid.cell(0, 0)))
        self.assertEqual([], grid.cell(1, 0))
