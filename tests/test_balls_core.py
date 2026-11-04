"""engine/core/grid.c and fx_balls.c (P598) on the Mac: tests/balls_lib.py builds them with clang and steps them
through CBalls, the stand-in for engine.Balls. Run from the repository root: python3 -m unittest discover tests.
--- MVP (P598) ---"""

import os
import random
import sys
import unittest
from array import array

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from balls_lib import COLOURS, H, HAVE_CLANG, HIT, MAX_BALLS, PANEL_H, PANEL_W, W, CBalls, Grid  # noqa: E402

DT, SUBSTEPS, GRAVITY, DAMPING, HIT_MIN = 0.01, 4, 2000.0, 0.98, 60.0  # balls.py's feel numbers (A7's gravity)
PHYSICS = (SUBSTEPS, DT, GRAVITY, DAMPING, HIT_MIN)
OVERLAP_PX = 1.5  # the pile's bar at a step boundary (the plan's round 2 simulation: 0.74 to 1.08 px at 4 substeps)
REST_SPEED = 20.0  # px/s: the settled pile's mean speed stays under this
MATERIAL = (0.5, 0.7, 100.0)  # balls.py's FRICTION, RESTITUTION, RESTITUTION_THRESHOLD
POUR, MAX = 25, 300


class Tft:
    """The s3lcd display's frame."""

    def __init__(self):
        self.frame = bytearray(PANEL_W * PANEL_H * 2)


def pour(balls, rng, colour, n=POUR):
    """n balls across the top, as balls.py pours them."""
    for _ in range(n):
        r = rng.randrange(2, 6)
        balls.spawn(rng.uniform(r, W - r), -r - rng.uniform(0, 40), 0.0, 0.0, r, colour)


def run(balls, steps):
    """steps steps, in calls of at most 64."""
    for _ in range(steps // 64):
        balls.step(64, *PHYSICS)
    balls.step(steps % 64, *PHYSICS)


def pile(seconds=4.0, pours=12, material=(0.0, 0.0, 0.0)):
    """pours pours a second apart, then seconds of settling: the balls."""
    rng, balls = random.Random(598), CBalls(MAX)
    balls.set_material(*material)
    for k in range(pours):
        pour(balls, rng, k % COLOURS)
        run(balls, 100)
    run(balls, int(seconds * 100))
    return balls


def radii(balls):
    out = array("h", [0] * (3 * balls.n))
    balls.read(out)
    return [out[3 * i + 2] for i in range(balls.count)]


def worst_overlap(balls):
    """The deepest overlap of any pair, in px."""
    xs, ys = balls.xy()
    rs, worst = radii(balls), 0.0
    for i in range(len(xs)):
        for j in range(i + 1, len(xs)):
            d = ((xs[i] - xs[j]) ** 2 + (ys[i] - ys[j]) ** 2) ** 0.5
            worst = max(worst, rs[i] + rs[j] - d)
    return worst


def events(balls):
    out = array("i", [0] * (3 * 64))
    return [tuple(out[3 * k:3 * k + 3]) for k in range(balls.drain(out))]


@unittest.skipUnless(HAVE_CLANG, "clang is missing: engine/core/fx_balls.c is not built")
class BallsCoreTest(unittest.TestCase):
    def test_a_ball_falls_hits_the_floor_and_rests_on_it(self):
        balls = CBalls(4)
        self.assertEqual(0, balls.spawn(160.0, 100.0, 0.0, 0.0, 4, 1))
        hits = []
        for _ in range(100):  # 1 s
            balls.step(1, *PHYSICS)
            hits += events(balls)
        xs, ys = balls.xy()
        self.assertAlmostEqual(160.0, xs[0], places=3)
        self.assertAlmostEqual(H - 4, ys[0], delta=0.05)
        self.assertEqual(1, balls.count)
        self.assertTrue(hits and all(kind == HIT and speed >= HIT_MIN for _, kind, speed in hits), hits)
        self.assertEqual(0, hits[0][0])

    def test_a_kick_throws_a_resting_ball_up_and_it_falls_back(self):
        balls = CBalls(4)
        balls.spawn(160.0, H - 4.0, 0.0, 0.0, 4, 1)
        for _ in range(20):
            balls.step(1, *PHYSICS)
        balls.kick(100.0, -400.0)  # px/s: a bump up and to the right, as the pit's shake
        highest = H
        for _ in range(100):  # 1 s
            balls.step(1, *PHYSICS)
            xs, ys = balls.xy()
            highest = min(highest, ys[0])
        self.assertLess(highest, H - 4 - 20)  # 400 px/s under 2,000 px/s2 rises about 40 px, less the damping
        self.assertGreater(xs[0], 165.0)
        self.assertAlmostEqual(H - 4, ys[0], delta=0.05)

    def test_the_settled_pile_overlaps_under_the_bar_and_rests(self):
        balls = pile()
        self.assertEqual(MAX, balls.count)
        self.assertLessEqual(worst_overlap(balls), OVERLAP_PX)
        x0, y0 = balls.xy()
        balls.step(1, *PHYSICS)
        x1, y1 = balls.xy()
        speed = sum(((a - b) ** 2 + (c - d) ** 2) ** 0.5 for a, b, c, d in zip(x1, x0, y1, y0)) / MAX / DT
        self.assertLess(speed, REST_SPEED)
        self.assertEqual([], events(balls))  # a resting pile fires no hit
        xs, ys = balls.xy()
        rs = radii(balls)
        self.assertTrue(all(r <= x <= W - r and y <= H - r for x, y, r in zip(xs, ys, rs)))

    def test_a_60_s_pour_passes_no_ball_through_another_or_a_wall(self):
        """A7: 60 s at 2,000 px/s2, 12 pours to 300 balls, read after every substep: no touching pair swaps sides
        (its relative vector turns by more than 90 degrees in one substep), and no ball leaves the box."""
        self.pour_60_s((0.0, 0.0, 0.0))

    def test_a_60_s_pour_at_the_game_s_material_passes_no_ball_through_another_or_a_wall(self):
        self.pour_60_s(MATERIAL)

    def pour_60_s(self, material):
        rng, balls, rs, reach = random.Random(7), CBalls(MAX), [], 10.5  # reach: two touching centres, plus a margin
        balls.set_material(*material)
        last = {}
        for k in range(60 * 100 * SUBSTEPS):
            if k % (5 * 100 * SUBSTEPS) == 0 and balls.count < MAX:
                pour(balls, rng, 1)
                rs = radii(balls)
            balls.step(1, 1, DT / SUBSTEPS, GRAVITY, DAMPING, HIT_MIN)
            xs, ys = balls.xy()
            n = len(xs)
            for i in range(n):
                self.assertTrue(rs[i] - 1e-3 <= xs[i] <= W - rs[i] + 1e-3 and ys[i] <= H - rs[i] + 1e-3, (k, i))
            order, touching = sorted(range(n), key=xs.__getitem__), {}
            for a in range(n):  # a sweep along x: each pair within reach once
                i, xi = order[a], xs[order[a]]
                for b in range(a + 1, n):
                    j = order[b]
                    dx = xs[j] - xi
                    if dx > reach:
                        break
                    dy = ys[j] - ys[i]
                    if dx * dx + dy * dy <= (rs[i] + rs[j] + 0.5) ** 2:
                        lo, hi = (i, j) if i < j else (j, i)
                        now, was = (xs[hi] - xs[lo], ys[hi] - ys[lo]), last.get((lo, hi))
                        touching[lo, hi] = now
                        self.assertFalse(was and was[0] * now[0] + was[1] * now[1] < 0, (k, lo, hi, was, now))
            last = touching
        self.assertEqual(MAX, balls.count)

    def test_a_full_pit_refuses_a_spawn_and_clear_empties_it(self):
        balls = CBalls(2)
        self.assertEqual((0, 1, -1), (balls.spawn(10.0, 10.0, 0.0, 0.0, 2, 0), balls.spawn(20.0, 10.0, 0.0, 0.0, 2, 0),
                                      balls.spawn(30.0, 10.0, 0.0, 0.0, 2, 0)))
        balls.clear()
        self.assertEqual(0, balls.count)
        self.assertEqual(0, balls.spawn(10.0, 10.0, 0.0, 0.0, 2, 0))

    def test_events_carry_the_speed_and_the_rest_only_count_in_dropped(self):
        balls = CBalls(80)
        for i in range(70):  # 70 balls of r 2 a hair apart, 1 px over the floor, falling at 500 px/s: 70 hits in one call
            balls.spawn(3.0 + 4.5 * i, H - 3.0, 0.0, 500.0, 2, 0)
        balls.step(1, *PHYSICS)
        hits = events(balls)
        self.assertEqual(64, len(hits))
        self.assertEqual(6, balls.dropped)
        self.assertTrue(all(kind == HIT and 480 <= speed <= 520 for _, kind, speed in hits), hits)
        self.assertEqual(sorted(i for i, _, _ in hits), [i for i, _, _ in hits])
        balls.step(0, *PHYSICS)  # a call clears the last call's events, also with 0 steps
        self.assertEqual(([], 0), (events(balls), balls.dropped))

    def test_the_binding_s_checks(self):
        with self.assertRaises(ValueError):
            CBalls(0)
        with self.assertRaises(ValueError):
            CBalls(MAX_BALLS + 1)
        balls = CBalls(1)
        for bad in ((10.0, 10.0, 0.0, 0.0, 1, 0), (10.0, 10.0, 0.0, 0.0, 6, 0), (10.0, 10.0, 0.0, 0.0, 2, COLOURS)):
            with self.assertRaises(ValueError):
                balls.spawn(*bad)
        with self.assertRaises(ValueError):
            balls.step(65, *PHYSICS)
        with self.assertRaises(ValueError):
            balls.step(1, 9, DT, GRAVITY, DAMPING, HIT_MIN)
        with self.assertRaises(ValueError):
            balls.read(array("h", [0, 0]))
        with self.assertRaises(ValueError):
            balls.drain(array("h", [0]))
        for bad in ((-0.1, 0.5, 100.0), (0.5, -0.1, 100.0), (0.5, 1.1, 100.0), (0.5, 0.5, -1.0)):
            with self.assertRaises(ValueError):
                balls.set_material(*bad)
        balls.set_material(0.0, 1.0, 0.0)  # the edges hold

    def test_draw_fills_the_panel_and_paints_each_ball_at_its_mapped_place(self):
        """Game (x, y) is panel (y, PANEL_H - 1 - x), lcd.TftCanvas.fill's map at ORIENTATION 0."""
        balls, tft = CBalls(2), Tft()
        palette = array("H", [0x1000 + 0x111 * k for k in range(COLOURS)])
        balls.spawn(100.0, 50.0, 0.0, 0.0, 3, 2)
        balls.draw(tft, palette, 0x00FF)

        def panel(gx, gy):
            k = 2 * ((PANEL_H - 1 - gx) * PANEL_W + gy)
            return tft.frame[k] | tft.frame[k + 1] << 8  # as the C wrote it, uint16 in the Mac's byte order

        self.assertEqual(palette[2], panel(100, 50))
        self.assertEqual((palette[2], palette[2], palette[2], palette[2]),
                         (panel(103, 50), panel(97, 50), panel(100, 53), panel(100, 47)))
        self.assertEqual((0x00FF, 0x00FF, 0x00FF), (panel(104, 50), panel(100, 54), panel(0, 0)))
        self.assertEqual(0x00FF, panel(103, 53))  # the corner outside the disc
        self.assertEqual(palette[2], panel(102, 52))  # 2 * 2 + 2 * 2 = 8 <= 9 + 3: inside


@unittest.skipUnless(HAVE_CLANG, "clang is missing: engine/core/fx_balls.c is not built")
class MaterialTest(unittest.TestCase):
    """The material (P598, the PO 2026-11-04: "the balls really don't bounce"): friction and restitution."""

    def rise_after_landing(self, balls, i, steps=100):
        """Step steps, and return how far ball i climbs back up after its lowest place, in px."""
        lowest, rise = 0.0, 0.0
        for _ in range(steps):
            balls.step(1, *PHYSICS)
            y = balls.xy()[1][i]
            lowest, rise = max(lowest, y), max(rise, max(lowest, y) - y)
        return rise

    def test_a_ball_bounces_off_the_floor_and_a_slow_one_rests(self):
        balls = CBalls(1)
        balls.set_material(*MATERIAL)
        balls.spawn(160.0, H - 4 - 40.0, 0.0, 0.0, 4, 1)  # 40 px up: it lands at about 350 px/s
        self.assertTrue(8.0 <= self.rise_after_landing(balls, 0) <= 20.0)  # 0.7 x 350 px/s rises about 14 px, less the damping
        slow = CBalls(1)
        slow.set_material(*MATERIAL)
        slow.spawn(160.0, H - 4 - 1.0, 0.0, 0.0, 4, 1)  # 1 px up: about 60 px/s, under the 100 px/s threshold
        self.assertLess(self.rise_after_landing(slow, 0), 0.05)
        self.assertAlmostEqual(H - 4, slow.xy()[1][0], delta=0.05)

    def test_a_ball_bounces_off_a_resting_ball_as_off_the_floor(self):
        """Any radius on any radius: the upper ball leaves at restitution x its closing speed, within 20 %."""
        restitution = MATERIAL[1]
        for top, low in ((3, 5), (4, 4), (5, 3), (2, 5)):
            balls = CBalls(2)
            balls.set_material(*MATERIAL)
            balls.spawn(160.0, H - low, 0.0, 0.0, low, 1)
            for _ in range(20):
                balls.step(1, *PHYSICS)
            balls.spawn(160.0, H - 2 * low - top - 40.0, 0.0, 0.0, top, 2)
            ys, closing = [balls.xy()[1][1]], None
            for _ in range(60):
                balls.step(1, *PHYSICS)
                ys.append(balls.xy()[1][1])
                closing = closing or next((speed for i, _, speed in events(balls) if i == 0), None)
            leaving = max(a - b for a, b in zip(ys, ys[1:])) / DT
            self.assertTrue(0.8 * restitution * closing <= leaving <= 1.2 * restitution * closing,
                            (top, low, closing, leaving))

    def test_friction_stops_a_sliding_ball(self):
        slid = {}
        for friction in (0.0, 0.5):
            balls = CBalls(1)
            balls.set_material(friction, 0.0, 0.0)
            balls.spawn(100.0, H - 4.0, 300.0, 0.0, 4, 1)
            for _ in range(100):  # 1 s
                balls.step(1, *PHYSICS)
            slid[friction] = balls.xy()[0][0] - 100.0
        self.assertLess(slid[0.5], 60.0)  # 300^2 / (2 x 0.5 x 2,000) = 45 px, less the damping
        self.assertGreater(slid[0.0], 120.0)  # the damping alone caps the slide at 150 px

    def test_friction_piles_the_heap_taller(self):
        """100 balls from a spout 6 px wide: 43 px high at friction 0.5, 22 at 0 (Mac run 2026-11-04)."""
        high = {}
        for friction in (0.0, 0.5):
            rng, balls = random.Random(5), CBalls(100)
            balls.set_material(friction, 0.0, 100.0)
            for _ in range(100):
                r = rng.randrange(2, 6)
                balls.spawn(160.0 + rng.uniform(-3.0, 3.0), -r, 0.0, 0.0, r, 1)
                run(balls, 5)
            run(balls, 400)
            ys, rs = balls.xy()[1], radii(balls)
            high[friction] = H - min(y - r for y, r in zip(ys, rs))
        self.assertGreater(high[0.5], 1.5 * high[0.0], high)

    def test_the_settled_pile_at_the_game_s_material_rests(self):
        balls = pile(material=MATERIAL)
        self.assertLessEqual(worst_overlap(balls), OVERLAP_PX)
        x0, y0 = balls.xy()
        balls.step(1, *PHYSICS)
        x1, y1 = balls.xy()
        speed = sum(((a - b) ** 2 + (c - d) ** 2) ** 0.5 for a, b, c, d in zip(x1, x0, y1, y0)) / MAX / DT
        self.assertLess(speed, REST_SPEED)
        self.assertEqual([], events(balls))


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
