"""core/fx_noise.c (P753) on the Mac, through tests/grains_lib.py, against a Python reference of it: the same seed and the
same calls give the same samples and the same state. The reference asserts that every intermediate stays under 2^31,
and the worst input (the signs of the impulse response) is checked for the bounds the header writes. Run from the
repository root: python3 -m unittest discover tests. --- MVP (P753) ---"""

import os
import random
import sys
import unittest
from array import array

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from grains_lib import HAVE_CLANG, VOICES, GrainsModule, NoiseState  # noqa: E402

LIMIT = 1 << 31
MAX_LEVEL, MAX_BED, MAX_WHOOSH = 32768, 16384, 25080  # FX_NOISE_MAX_LEVEL, _MAX_BED_COEF, _MAX_WHOOSH_COEF
FIELDS = [name for name, _ in NoiseState._fields_]


def fits(*values):
    """Each intermediate of the C stays under 2^31."""
    for v in values:
        assert -LIMIT < v < LIMIT, v
    return values[-1]


class Reference:
    """fx_noise_t and its functions in Python. noise_override: an iterator of x for the worst-input test."""

    def __init__(self, seed):
        self.rng = (seed & 0xFFFF) or 1
        self.bed_level = self.bed_level_to = self.bed_coef = self.bed_coef_to = self.y1 = self.y2 = 0
        self.level, self.level_to, self.coef, self.coef_to = ([0] * VOICES for _ in range(4))
        self.low, self.band = [0] * VOICES, [0] * VOICES

    def noise(self):
        x = self.rng
        x ^= (x << 7) & 0xFFFF
        x ^= x >> 9
        x ^= (x << 8) & 0xFFFF
        self.rng = x
        return (x - 32768) >> 2

    def bed(self, level, coef):
        self.bed_level_to = min(max(level, 0), MAX_LEVEL) << 8
        self.bed_coef_to = min(max(coef, 0), MAX_BED) << 8

    def whoosh(self, i, level, coef):
        self.level_to[i] = min(max(level, 0), MAX_LEVEL) << 8
        self.coef_to[i] = min(max(coef, 0), MAX_WHOOSH) << 8

    def mix(self, buf):
        out = []
        for v in buf:
            total = v
            if self.bed_level or self.bed_level_to:
                self.bed_level += (self.bed_level_to - self.bed_level) >> 9
                self.bed_coef += (self.bed_coef_to - self.bed_coef) >> 9
                c, x = self.bed_coef >> 8, self.noise()
                self.y1 += fits(c * (x - self.y1)) >> 15
                self.y2 += fits(c * (self.y1 - self.y2)) >> 15
                total += fits(self.y2 * (self.bed_level >> 8)) >> 12
            for i in range(VOICES):
                if not (self.level[i] or self.level_to[i]):
                    continue
                self.level[i] += (self.level_to[i] - self.level[i]) >> 9
                self.coef[i] += (self.coef_to[i] - self.coef[i]) >> 9
                f, x = self.coef[i] >> 8, self.noise()
                high = fits(x - self.low[i] - self.band[i])
                self.band[i] += fits(f * high) >> 15
                self.low[i] += fits(f * self.band[i]) >> 15
                total += fits(self.band[i] * (self.level[i] >> 8)) >> 14
            fits(total)
            out.append(max(-32767, min(32767, total)) if total != v else v)
        return out

    def fields(self):
        flat = [self.rng, self.bed_level, self.bed_level_to, self.bed_coef, self.bed_coef_to, self.y1, self.y2]
        return flat + self.level + self.level_to + self.coef + self.coef_to + self.low + self.band


class Noise:
    """GrainsModule's noise functions in one object."""

    def __init__(self, seed=1):
        self.m = GrainsModule()
        self.state = bytearray(self.m.NOISE_SIZE)
        self.m.noise_init(self.state, seed)

    def fields(self):
        st = NoiseState.from_buffer(self.state)
        flat = []
        for name in FIELDS:
            v = getattr(st, name)
            flat += list(v) if hasattr(v, "__len__") else [v]
        return flat

    def get(self, name):
        v = getattr(NoiseState.from_buffer(self.state), name)
        return list(v) if hasattr(v, "__len__") else v

    def mix(self, buf):
        arr = array("h", buf)
        self.m.noise_mix(self.state, arr)
        return list(arr)


def rms(x):
    return (sum(v * v for v in x) / len(x)) ** 0.5


def worst(update, n=3000):
    """For each variable of a linear filter, update(x, state) -> (low, band, high): the signs of its impulse response
    read backward as an input of |x| = 8192, which drives that variable to its largest |value|. Returns the three
    inputs (lists of n values) and the three largest values the real-number filter reaches."""
    feeds, peaks = [], []
    for k in range(3):
        state, resp = (0, 0, 0), []
        for t in range(n):
            state = update(1 << 20 if t == 0 else 0, state)
            resp.append(state[k])
        feeds.append([8192 if resp[n - 1 - t] > 0 else -8192 for t in range(n)])
        peaks.append(int(8192 * sum(abs(r) for r in resp) / (1 << 20)))
    return feeds, peaks


@unittest.skipUnless(HAVE_CLANG, "clang is needed")
class NoiseCoreTest(unittest.TestCase):
    def test_c_equals_the_reference_over_20000_random_target_steps(self):
        rg = random.Random(753)
        c, ref = Noise(5), Reference(5)
        for step in range(20000):
            what = rg.randrange(4)
            if what == 0:
                level, coef = rg.choice((0, rg.randrange(0, 9000), rg.randrange(-5, 40000))), rg.randrange(-9, 20000)
                c.m.noise_bed(c.state, level, coef)
                ref.bed(level, coef)
            elif what == 1:
                i, level = rg.randrange(VOICES), rg.choice((0, 0, rg.randrange(0, 4000), rg.randrange(-5, 40000)))
                coef = rg.randrange(-9, 30000)
                c.m.noise_whoosh(c.state, i, level, coef)
                ref.whoosh(i, level, coef)
            buf = [rg.randrange(-32768, 32768) if rg.random() < 0.3 else rg.randrange(-3000, 3000)
                   for _ in range(rg.randrange(1, 40))]
            self.assertEqual(c.mix(buf), ref.mix(buf), step)
            self.assertEqual(c.fields(), ref.fields(), step)

    def test_a_long_run_at_the_top_of_every_range_equals_the_reference(self):
        c, ref = Noise(0), Reference(0)  # a seed of 0 becomes 1
        c.m.noise_bed(c.state, MAX_LEVEL, MAX_BED)
        ref.bed(MAX_LEVEL, MAX_BED)
        for i in range(VOICES):
            c.m.noise_whoosh(c.state, i, MAX_LEVEL, MAX_WHOOSH)
            ref.whoosh(i, MAX_LEVEL, MAX_WHOOSH)
        buf = [32767, -32767] * 4000
        self.assertEqual(c.mix(buf), ref.mix(buf))
        self.assertEqual(c.fields(), ref.fields())

    def test_the_worst_input_stays_inside_the_headers_bounds(self):
        f = MAX_WHOOSH

        def whoosh(x, s):  # the filter without the floors, in real numbers
            low, band, _ = s
            high = x - low - band
            band += f * high / 32768
            low += f * band / 32768
            return low, band, high

        feeds, (low, band, high) = worst(whoosh)
        self.assertLessEqual(low, 11200)
        self.assertLessEqual(band, 13500)
        self.assertLessEqual(high, 24500)
        self.assertLess(f * high, LIMIT)
        self.assertLess(band * MAX_LEVEL, LIMIT)
        # the real filter (with floors) at the top of every range on each worst input: the reference asserts that every
        # intermediate stays under 2^31, and the peaks stay near the real-number peaks
        for feed in feeds:
            ref = Reference(1)
            ref.level[0] = ref.level_to[0] = MAX_LEVEL << 8
            ref.coef[0] = ref.coef_to[0] = f << 8
            ref.noise = iter(feed).__next__
            peaks = []
            for _ in feed:
                ref.mix([0])
                peaks.append((abs(ref.low[0]), abs(ref.band[0])))
            self.assertLessEqual(max(p[0] for p in peaks), 11200 + 16)
            self.assertLessEqual(max(p[1] for p in peaks), 13500 + 16)
        for x in (8192, -8192):  # the bed at its largest coefficient and level, on the largest input
            ref = Reference(1)
            ref.bed_level = ref.bed_level_to = MAX_LEVEL << 8
            ref.bed_coef = ref.bed_coef_to = MAX_BED << 8
            ref.noise = lambda x=x: x
            out = ref.mix([0] * 200)
            self.assertLessEqual(max(abs(v) for v in out), 32767)
            self.assertLessEqual(abs(ref.y2), 8192)
        # the bed at its largest coefficient: |x - y| <= 16384, so a product is at most 2^28
        self.assertLess(MAX_BED * 16384, LIMIT)
        self.assertLess(8192 * MAX_LEVEL, LIMIT)

    def test_the_glide_moves_at_most_a_512th_of_the_way_a_sample_and_arrives(self):
        c = Noise()
        c.m.noise_bed(c.state, 8000, 3000)
        c.m.noise_whoosh(c.state, 3, 3000, 20000)
        start = {k: c.get(k) for k in ("bed_level", "bed_coef")}
        gap = {"bed_level": (8000 << 8) - start["bed_level"], "bed_coef": (3000 << 8) - start["bed_coef"]}
        for _ in range(5 * 512):
            before = c.fields()
            c.mix([0])
            after = c.fields()
            for idx, to_idx in ((1, 2), (3, 4)):
                moved, left = after[idx] - before[idx], before[to_idx] - before[idx]
                self.assertLessEqual(abs(moved), -(-abs(left) // 512))
                self.assertTrue(moved == 0 or (moved > 0) == (left > 0))
        for key in gap:
            self.assertLessEqual(abs((8000 << 8 if key == "bed_level" else 3000 << 8) - c.get(key)),
                                 abs(gap[key]) // 100 + 512)
        self.assertLessEqual(abs((3000 << 8) - c.get("level")[3]), (3000 << 8) // 100 + 512)
        self.assertLessEqual(abs((20000 << 8) - c.get("coef")[3]), (20000 << 8) // 100 + 512)

    def test_a_level_target_of_0_is_reached_exactly_and_then_the_source_is_idle(self):
        c = Noise(9)
        c.m.noise_bed(c.state, 5000, 4000)
        c.m.noise_whoosh(c.state, 0, 2000, 9000)
        c.mix([0] * 3000)
        c.m.noise_bed(c.state, 0, 4000)
        c.m.noise_whoosh(c.state, 0, 0, 9000)
        c.mix([0] * 20000)
        self.assertEqual((c.get("bed_level"), c.get("level")[0]), (0, 0))
        rng = c.get("rng")
        buf = [random.Random(1).randrange(-32768, 32768) for _ in range(500)]
        self.assertEqual(c.mix(buf), buf)  # level 0 everywhere leaves buf as it is
        self.assertEqual(c.get("rng"), rng)  # an idle source takes no draw

    def test_a_new_state_is_silent(self):
        c = Noise()
        buf = [7, -7, 0, 32767, -32768] * 10
        self.assertEqual(c.mix(buf), buf)

    def test_targets_out_of_range_are_clamped_and_a_bad_voice_is_refused(self):
        c = Noise()
        c.m.noise_bed(c.state, 99999, 99999)
        c.m.noise_whoosh(c.state, 7, -1, 99999)
        self.assertEqual((c.get("bed_level_to"), c.get("bed_coef_to")), (MAX_LEVEL << 8, MAX_BED << 8))
        self.assertEqual((c.get("level_to")[7], c.get("coef_to")[7]), (0, MAX_WHOOSH << 8))
        for i in (-1, VOICES):
            with self.assertRaises(ValueError):
                c.m.noise_whoosh(c.state, i, 100, 100)

    def test_the_bed_is_as_loud_when_dark_as_when_bright_by_the_gain_the_games_apply(self):
        """rms of a bed at level 8000 and 250 Hz against level 4216 (sqrt(250 / 900) of it) and 900 Hz: about equal."""
        out = []
        for level, coef in ((8000, 3064), (4216, 9755)):
            c = Noise(3)
            c.m.noise_bed(c.state, level, coef)
            c.mix([0] * 4000)
            out.append(rms(c.mix([0] * 32000)))
        self.assertLess(abs(out[0] - out[1]) / out[0], 0.1)
        self.assertTrue(1000 < out[0] < 2000, out)

    def test_the_whoosh_rises_with_its_centre_and_stays_in_range(self):
        out = []
        for coef in (3858, 12785, 25079):
            c = Noise(4)
            c.m.noise_whoosh(c.state, 0, 3000, coef)
            c.mix([0] * 4000)
            x = c.mix([0] * 32000)
            self.assertLess(max(abs(v) for v in x), 3000)
            out.append(rms(x))
        self.assertTrue(out[0] < out[1] < out[2], out)


if __name__ == "__main__":
    unittest.main()
