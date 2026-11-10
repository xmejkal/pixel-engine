"""core/fx_grains.c on the Mac (P751): GrainsModule builds it with clang and has the functions of the native module
crackle_c (grains_init, grains_add_pops, grains_render, grains_counts, GRAINS_SIZE), so pixel-games' GrainCrackle runs
on the Mac unchanged, and its tests and tools/crackle_wav.py hear the C. --- MVP (P751) ---"""

import ctypes
from array import array

from clang_lib import HAVE_CLANG, STRICT, build, core

BED_SLOTS, POP_SLOTS, POPS, RISES = 3, 8, 12, 8
CORE_SOURCES = core("fx_grains")


class State(ctypes.Structure):
    """fx_grains_t, field for field."""

    _fields_ = [(name, ctypes.c_int32) for name in (
        "rng", "bed_len", "bank_len", "n_pops", "bed_gain", "pop_gain", "popped", "started", "dropped")] + [
        ("rise", ctypes.c_int32 * RISES)] + [(name, ctypes.c_int32) for name in ("next_in", "lev1", "lev2")] + [
        (name, ctypes.c_int32 * BED_SLOTS) for name in ("g_pos", "g_len", "g_log", "g_start", "g_gain")] + [
        (name, ctypes.c_int32 * POPS) for name in ("p_off", "p_len", "p_size")] + [
        (name, ctypes.c_int32 * POP_SLOTS) for name in ("s_pos", "s_len", "s_off", "s_gain")]


def address(buf):
    """The address of a bytearray or an array."""
    if isinstance(buf, bytearray):
        return ctypes.addressof(ctypes.c_char.from_buffer(buf))
    return buf.buffer_info()[0]


class GrainsModule:
    """The native module's functions on the Mac. The state is a bytearray of GRAINS_SIZE bytes, as on the board."""

    GRAINS_SIZE = ctypes.sizeof(State)

    def __init__(self):
        self.lib = build(CORE_SOURCES, STRICT + ("-fno-builtin",))
        self.lib.fx_grains_bank_len.restype = ctypes.c_int32

    def _state(self, state):
        return State.from_buffer(state)

    def grains_init(self, state, seed, look, index):
        """ValueError for a look or an index out of range, as the module raises it."""
        if self.lib.fx_grains_init(ctypes.byref(self._state(state)), ctypes.c_uint32(seed),
                                   ctypes.c_void_p(address(look)), ctypes.c_void_p(address(index)), len(index)):
            raise ValueError("a look or an index out of fx_grains_init's ranges")

    def grains_add_pops(self, state, n, rises=None):
        """rises: None, or an array("b"), of which the first RISES entries count, as the native module takes it."""
        padded = array("b", bytes(RISES))
        for i, v in enumerate((rises or ())[:RISES]):
            padded[i] = v
        self.lib.fx_grains_add_pops(ctypes.byref(self._state(state)), n,
                                    ctypes.c_void_p(None if rises is None else address(padded)))

    def grains_render(self, state, bank, buf):
        """buf an array("h"); bank int16 samples in a bytearray or an array("h")."""
        st = self._state(state)
        if len(bank) // (2 if isinstance(bank, bytearray) else 1) < self.lib.fx_grains_bank_len(
                ctypes.byref(st)):
            raise ValueError("the bank is shorter than the index says")
        self.lib.fx_grains_render(ctypes.byref(st), ctypes.c_void_p(address(bank)), ctypes.c_void_p(address(buf)),
                                  len(buf))

    def grains_counts(self, state):
        st = self._state(state)
        return st.popped, st.started, st.dropped
