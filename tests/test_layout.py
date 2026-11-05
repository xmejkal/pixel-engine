"""The repository's layout (P632): every path that micropython.cmake names exists, and core/fx_fire.c builds against
the driver submodule's blend565.h, so a wrong CORE or S3LCD_SRC fails here. Run from the repository root:
python3 -m unittest discover tests. --- MVP (P632) ---"""

import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from clang_lib import HAVE_CLANG, REPO, S3LCD_SRC, build, core  # noqa: E402

CMAKE_PATH = re.compile(r"\$\{CMAKE_CURRENT_LIST_DIR\}/(\S+)")


class CmakePaths(unittest.TestCase):
    def test_each_named_path_exists(self):
        with open(os.path.join(REPO, "micropython.cmake")) as f:
            paths = CMAKE_PATH.findall(f.read())
        self.assertIn("driver/src", paths)
        for path in paths:
            self.assertTrue(os.path.exists(os.path.join(REPO, path)), path)


@unittest.skipUnless(HAVE_CLANG, "clang is missing: core/fx_fire.c is not built")
class DriverHeader(unittest.TestCase):
    def test_the_fire_builds_against_the_driver(self):
        self.assertTrue(os.path.isfile(os.path.join(S3LCD_SRC, "blend565.h")))
        self.assertIsNotNone(build(core("fx_fire"), ("-I", S3LCD_SRC)).fx_fire_blend)


if __name__ == "__main__":
    unittest.main()
