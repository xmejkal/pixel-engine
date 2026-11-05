"""One clang build for the Mac tests (P598): build(sources, flags) compiles core/ sources once into a temporary
folder, removed at exit, and returns the ctypes library. world_lib.py, balls_lib.py and tools/render_scene.py share it,
so the flags of one build live in one place. --- MVP (P598) ---"""

import atexit
import ctypes
import os
import shutil
import subprocess
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(REPO, "core")
S3LCD_SRC = os.path.join(REPO, "driver", "src")  # the s3lcd driver submodule: blend565.h, which fx_fire.c reads
HAVE_CLANG = shutil.which("clang") is not None
STRICT = ("-Wall", "-Wextra", "-Werror")
NO_FMA = ("-ffp-contract=off",)  # no fused multiply-add, which a twin's Python maths never does

_libs = {}


def core(*names):
    """The paths of core/<name>.c."""
    return tuple(os.path.join(CORE, name + ".c") for name in names)


def build(sources, flags=()):
    """The library of these C sources at -O2, built on the first call with these flags."""
    key = (tuple(sources), tuple(flags))
    if key not in _libs:
        tmp = tempfile.mkdtemp()
        atexit.register(shutil.rmtree, tmp, True)
        lib_path = os.path.join(tmp, "lib%d.so" % len(_libs))
        subprocess.run(["clang", "-std=c99", "-O2", "-shared", "-fPIC", "-I", CORE, *flags, "-o", lib_path, *sources],
                       check=True)
        _libs[key] = ctypes.CDLL(lib_path)
    return _libs[key]
