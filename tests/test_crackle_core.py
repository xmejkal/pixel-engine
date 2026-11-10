"""core/fx_crackle.c (P747) on the Mac, through tests/crackle_lib.py. pixel-games' tests/test_crackle.py wants its
Python Crackle equal to this C, sample for sample. Run from the repository root: python3 -m unittest discover tests.
--- MVP (P747) ---"""

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from crackle_lib import CORE_SOURCES, HAVE_CLANG, CCrackle, samples  # noqa: E402

LOOK = (4, 0, 8000, 12000, 28400, 31600, 3, 4000, 64)  # no roar, so a pop ends in silence
RATE = 16000


@unittest.skipUnless(HAVE_CLANG, "clang is needed")
class CrackleCoreTest(unittest.TestCase):
    def test_silent_without_pops_or_roar(self):
        self.assertEqual(max(map(abs, samples(CCrackle(1, LOOK), RATE))), 0)

    def test_a_pop_starts_in_the_next_render_and_ends(self):
        c = CCrackle(7, LOOK)
        c.add_pops(1)
        out = samples(c, RATE)
        self.assertGreater(max(map(abs, out[:129 + 48])), 1000)  # starts within 128 samples, loud for 3 ms
        self.assertEqual(max(map(abs, out[RATE // 2:])), 0)  # the burst and its tails are over in half a second
        self.assertEqual(c.counts(), (1, 1, 0))

    def test_a_ninth_pop_at_once_is_dropped(self):
        c = CCrackle(3, LOOK)
        c.add_pops(9)
        samples(c, 256)
        self.assertEqual(c.counts(), (9, 8, 1))

    def test_peak_stays_under_90_percent(self):
        c = CCrackle(11, (4, 64) + LOOK[2:])
        out = []
        for second in range(10):
            c.add_pops(4)
            out.extend(samples(c, RATE))
        self.assertLessEqual(max(map(abs, out)), 32767 * 9 // 10)

    def test_no_libc_or_libgcc_call(self):
        """The board's native module links with no runtime library, so the object names no outside symbol."""
        with tempfile.TemporaryDirectory() as tmp:
            obj = os.path.join(tmp, "fx_crackle.o")
            subprocess.run(["clang", "-std=c99", "-O2", "-fno-builtin", "-c", "-o", obj, *CORE_SOURCES], check=True)
            undefined = subprocess.run(["nm", "-u", obj], capture_output=True, text=True, check=True).stdout
        self.assertEqual(undefined.strip(), "")


if __name__ == "__main__":
    unittest.main()
