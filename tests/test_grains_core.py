"""core/fx_grains.c (P751) on the Mac, through tests/grains_lib.py, with a made-up bank: a noise bed and 12 pops that
are each one constant level, so a test sees which pop played. pixel-games' tests/test_crackle.py runs the real bank.
Run from the repository root: python3 -m unittest discover tests. --- MVP (P751) ---"""

import os
import random
import subprocess
import sys
import tempfile
import unittest
from array import array

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from grains_lib import CORE_SOURCES, HAVE_CLANG, GrainsModule  # noqa: E402

BED = 8192
LEVELS = (1000, 5000, 20000)  # the pop of each size is this level for 100 samples
INDEX = array("i", [BED] + [v for p in range(12) for v in (BED + 100 * p, 100, p % 3)])
LOOK = array("i", [32768, 32768])


def bank(bed_level=8000):
    rg = random.Random(1)
    out = array("h", (rg.randrange(-bed_level, bed_level + 1) for _ in range(BED)))
    for p in range(12):
        out.extend([LEVELS[p % 3]] * 100)
    return out


class Grains:
    """GrainsModule in the shape of GrainCrackle: add_pops, render, counts."""

    def __init__(self, seed=1, look=LOOK, index=INDEX, samples=None):
        self.m, self.bank = GrainsModule(), samples or bank()
        self.state = bytearray(self.m.GRAINS_SIZE)
        self.m.grains_init(self.state, seed, look, index)

    def add_pops(self, n, rises=None):
        self.m.grains_add_pops(self.state, n, rises)

    def render(self, n):
        buf = array("h", bytes(2 * n))
        self.m.grains_render(self.state, self.bank, buf)
        return buf

    def counts(self):
        return self.m.grains_counts(self.state)


def rms(x):
    return (sum(v * v for v in x) / len(x)) ** 0.5


@unittest.skipUnless(HAVE_CLANG, "clang is needed")
class GrainsCoreTest(unittest.TestCase):
    def test_a_look_or_an_index_out_of_range_is_refused(self):
        for look in (array("i", [-1, 0]), array("i", [0, 32769])):
            with self.assertRaises(ValueError):
                Grains(look=look)
        bad = []
        for k, v in ((0, 4095), (0, 131072), (1, 8191), (2, 0), (2, 2401), (3, 3)):
            index = array("i", INDEX)
            index[k] = v
            bad.append(index)
        bad.append(array("i", [BED, BED, 100, 0, BED + 100, 100, 1]))  # no pop of size 2
        bad.append(INDEX[:-1])  # not a whole pop
        for index in bad:
            with self.assertRaises(ValueError, msg=list(index)):
                Grains(index=index)

    def test_a_short_bank_is_refused(self):
        g = Grains()
        with self.assertRaises(ValueError):
            g.m.grains_render(g.state, bank()[:-1], array("h", bytes(512)))

    def test_the_bed_is_never_silent_and_stays_in_range(self):
        out = Grains().render(16000 * 4)
        for k in range(0, len(out), 256):
            self.assertGreater(rms(out[k:k + 256]), 200)
        self.assertLessEqual(max(map(abs, out)), 32767)

    def test_a_bed_gain_of_zero_is_silent_without_pops(self):
        self.assertEqual(max(map(abs, Grains(look=array("i", [0, 32768])).render(4096))), 0)

    def test_a_pop_starts_in_the_next_render_and_ends(self):
        g = Grains(look=array("i", [0, 32768]))
        g.add_pops(1)
        out = g.render(256)
        self.assertGreater(out[0], 0)
        self.assertEqual(max(map(abs, out[100:])), 0)  # 100 samples long
        self.assertEqual(g.counts(), (1, 1, 0))

    def test_a_ninth_pop_at_once_is_dropped(self):
        g = Grains()
        g.add_pops(9)
        g.render(64)
        self.assertEqual(g.counts(), (9, 8, 1))
        g.render(64)
        self.assertEqual(g.counts(), (9, 8, 1))  # none starts twice

    def test_pop_sizes_follow_the_u_cubed_rule(self):
        g, seen = Grains(seed=7, look=array("i", [0, 32768])), [0, 0, 0]
        for _ in range(3000):
            g.add_pops(1)
            first = g.render(120)[0]
            seen[0 if first < 2000 else 1 if first < 8000 else 2] += 1
        self.assertEqual(sum(seen), 3000)
        for got, want in zip(seen, (0.63, 0.21, 0.16)):
            self.assertAlmostEqual(got / 3000, want, delta=0.04)

    def test_size_of_rise_maps_5_and_4_and_keeps_the_draw_for_the_rest(self):  # --- MVP (P751) ---
        lib = Grains().m.lib
        for drawn in range(3):
            got = [lib.fx_grains_size_of_rise(r, drawn) for r in (0, 1, 2, 3, 4, 5, 6)]
            self.assertEqual([drawn] * 4 + [1, 2, drawn], got)

    def test_rises_5_and_4_make_the_biggest_and_the_middle_pop_and_the_rest_draw(self):  # --- MVP (P751) ---
        for rise, level in ((5, 20000), (4, 5000)):
            g = Grains(look=array("i", [0, 32768]))
            for _ in range(40):
                g.add_pops(1, array("b", [rise]))
                self.assertGreater(g.render(120)[0], 0.55 * level)
                self.assertEqual(g.render(8)[0], 0)
        seen = {0: 0, 1: 0, 2: 0}
        for rise in (0, 2, 3):
            g = Grains(seed=3, look=array("i", [0, 32768]))
            for _ in range(300):
                g.add_pops(1, array("b", [rise]))
                first = g.render(120)[0]
                seen[0 if first < 2000 else 1 if first < 8000 else 2] += 1
        self.assertTrue(all(v > 0 for v in seen.values()))
        self.assertGreater(seen[0], seen[2])

    def test_each_pop_of_one_call_has_its_own_rise_and_a_short_array_leaves_draws(self):  # --- MVP (P751) ---
        g = Grains(look=array("i", [0, 32768]))
        g.add_pops(2, array("b", [5, 4]))
        out = g.render(120)
        self.assertEqual(g.counts(), (2, 2, 0))
        self.assertGreater(out[0], 0.55 * (5000 + 20000))  # a 5 and a 4 at once: both loud, in the sum

    def test_more_than_8_pops_in_a_call_store_0_past_the_ring(self):  # --- MVP (P751) ---
        g = Grains()
        g.add_pops(12, array("b", [5] * 12))
        self.assertEqual(list(g.m._state(g.state).rise), [0] * 4 + [5] * 4)  # pops 8 to 11 store 0 over pops 0 to 3
        g.add_pops(10)  # more than the ring: all 8 overwritten with 0
        self.assertEqual(list(g.m._state(g.state).rise), [0] * 8)

    def test_the_rise_ring_overwrites_the_oldest_when_over_8_wait(self):  # --- MVP (P751) ---
        g = Grains()
        g.add_pops(6, array("b", [2, 2, 2, 2, 2, 2]))
        g.add_pops(4, array("b", [5, 5, 5, 5]))  # pops 6 to 9: slots 6, 7, 0, 1
        self.assertEqual(list(g.m._state(g.state).rise), [5, 5, 2, 2, 2, 2, 5, 5])

    def test_rises_0_to_5_are_stored_as_given_and_none_is_all_draw(self):  # --- MVP (P751) ---
        g = Grains()
        g.add_pops(6, array("b", range(6)))
        self.assertEqual(list(g.m._state(g.state).rise)[:6], list(range(6)))
        g.add_pops(8, None)
        self.assertEqual(list(g.m._state(g.state).rise), [0] * 8)

    def test_peak_clamps_under_a_loud_bank(self):
        g = Grains(samples=bank(32767))
        out = []
        for _ in range(100):
            g.add_pops(4)
            out.extend(g.render(256))
        self.assertLessEqual(max(map(abs, out)), 32767)

    def test_same_seed_same_sound(self):
        self.assertEqual(Grains(5).render(4096), Grains(5).render(4096))
        self.assertNotEqual(Grains(5).render(4096), Grains(6).render(4096))

    def test_no_libc_or_libgcc_call(self):
        """The board's native module links with no runtime library, so the object names no outside symbol."""
        with tempfile.TemporaryDirectory() as tmp:
            for source in CORE_SOURCES:  # fx_grains.c and, since P753, fx_noise.c
                obj = os.path.join(tmp, os.path.basename(source) + ".o")
                subprocess.run(["clang", "-std=c99", "-O2", "-fno-builtin", "-c", "-o", obj, source], check=True)
                undefined = subprocess.run(["nm", "-u", obj], capture_output=True, text=True, check=True).stdout
                self.assertEqual(undefined.strip(), "", source)


if __name__ == "__main__":
    unittest.main()
