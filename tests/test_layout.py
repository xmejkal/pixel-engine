"""The repository's layout (P632): every path that micropython.cmake names exists, and core/fx_fire.c builds against
the driver submodule's blend565.h, so a wrong CORE or S3LCD_SRC fails here. The two modules' names start with `_`
(P847), so a Python package `engine` can load beside the C. Run from the repository root:
python3 -m unittest discover tests. --- MVP (P632, P847) ---"""

import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from clang_lib import HAVE_CLANG, REPO, S3LCD_SRC, build, core  # noqa: E402

CMAKE_PATH = re.compile(r"\$\{CMAKE_CURRENT_LIST_DIR\}/(\S+)")
MODULE_NAMES = re.compile(r"MP_QSTR___name__\), MP_ROM_QSTR\(MP_QSTR_(\w+)\)|MP_REGISTER_MODULE\(MP_QSTR_(\w+),")
NATMOD_NAME = re.compile(r"^MOD = (\S+)$", re.MULTILINE)


class CmakePaths(unittest.TestCase):
    def test_each_named_path_exists(self):
        with open(os.path.join(REPO, "micropython.cmake")) as f:
            paths = CMAKE_PATH.findall(f.read())
        self.assertIn("driver/src", paths)
        for path in paths:
            self.assertTrue(os.path.exists(os.path.join(REPO, path)), path)


class ModuleNames(unittest.TestCase):
    """A built-in module wins over a file system package of the same name, so the C names start with `_` (P847)."""

    def read(self, *parts):
        with open(os.path.join(REPO, *parts)) as f:
            return f.read()

    def test_the_c_module_registers_and_names_itself_underscore_engine(self):
        names = [a or b for a, b in MODULE_NAMES.findall(self.read("binding.c"))]
        self.assertEqual(names, ["_engine", "_engine"])

    def test_the_native_module_builds_with_a_name_that_starts_with_an_underscore(self):
        self.assertEqual(NATMOD_NAME.findall(self.read("natmod", "crackle", "Makefile")), ["_crackle"])


@unittest.skipUnless(HAVE_CLANG, "clang is missing: core/fx_fire.c is not built")
class DriverHeader(unittest.TestCase):
    def test_the_fire_builds_against_the_driver(self):
        self.assertTrue(os.path.isfile(os.path.join(S3LCD_SRC, "blend565.h")))
        self.assertIsNotNone(build(core("fx_fire"), ("-I", S3LCD_SRC)).fx_fire_blend)


if __name__ == "__main__":
    unittest.main()
