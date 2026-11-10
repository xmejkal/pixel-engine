"""core/fx_crackle.c on the Mac (P747): CCrackle builds it with clang and has NativeCrackle's methods, so the tests
and pixel-games' tools/crackle_wav.py hear the C. --- MVP (P747) ---"""

import ctypes
from array import array

from clang_lib import HAVE_CLANG, STRICT, build, core

SLOTS = 8
CORE_SOURCES = core("fx_crackle")


class State(ctypes.Structure):
    """fx_crackle_t, field for field."""

    _fields_ = [(name, ctypes.c_int32) for name in (
        "rng", "roar1", "roar2", "roar_shift", "roar_gain", "pop_min", "pop_max", "decay_min", "decay_max", "tails",
        "tail_gap", "gain", "popped", "started", "dropped")] + [
        (name, ctypes.c_int32 * SLOTS) for name in ("wait", "armed", "env", "decay", "lp", "shift", "tails_left")]


class CCrackle:
    """The C crackle: add_pops(n), render(buf) into an array("h"), counts() as (popped, started, dropped)."""

    pending = None

    def __init__(self, seed, look):
        self.lib = build(CORE_SOURCES, STRICT + ("-fno-builtin",))
        self.state = State()
        self.lib.fx_crackle_init(ctypes.byref(self.state), ctypes.c_uint32(seed), *look)

    def add_pops(self, n):
        self.lib.fx_crackle_add_pops(ctypes.byref(self.state), n)

    def render(self, buf):
        address, n = buf.buffer_info()
        self.lib.fx_crackle_render(ctypes.byref(self.state), ctypes.c_void_p(address), n)

    def counts(self):
        return self.state.popped, self.state.started, self.state.dropped


def samples(crackle, n):
    """n samples rendered in one call, as an array("h")."""
    buf = array("h", bytes(2 * n))
    crackle.render(buf)
    return buf
