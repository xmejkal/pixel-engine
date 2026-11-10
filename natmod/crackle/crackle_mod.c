// crackle_c (P747): core/fx_crackle.c as a MicroPython native module, loaded from crackle_c.mpy with no firmware
// rebuild. The state is a bytearray of SIZE bytes that the caller owns, so the module keeps no globals.
// games/crackle.py's NativeCrackle is its one caller. --- MVP (P747) ---
#include "py/dynruntime.h"
#include "fx_crackle.h"

// The state inside a bytearray, which must hold SIZE bytes.
static fx_crackle_t *state(mp_obj_t state_in) {
    mp_buffer_info_t info;
    mp_get_buffer_raise(state_in, &info, MP_BUFFER_RW);
    if (info.len < sizeof(fx_crackle_t)) {
        mp_raise_ValueError(MP_ERROR_TEXT("a bytearray of SIZE bytes is needed"));
    }
    return info.buf;
}

// init(state, seed, roar_shift, roar_gain, pop_min, pop_max, decay_min, decay_max, tails, tail_gap, gain)
static mp_obj_t init(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    int32_t look[9];
    for (int i = 0; i < 9; i++) {
        look[i] = mp_obj_get_int(args[2 + i]);
    }
    fx_crackle_init(state(args[0]), (uint32_t)mp_obj_get_int(args[1]), look[0], look[1], look[2], look[3], look[4],
                    look[5], look[6], look[7], look[8]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(init_obj, 11, 11, init);

// add_pops(state, n)
static mp_obj_t add_pops(mp_obj_t state_in, mp_obj_t n_in) {
    fx_crackle_add_pops(state(state_in), mp_obj_get_int(n_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(add_pops_obj, add_pops);

// render(state, buf): buf is an array("h"), filled whole.
static mp_obj_t render(mp_obj_t state_in, mp_obj_t buf_in) {
    fx_crackle_t *c = state(state_in);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_in, &buf, MP_BUFFER_RW);
    if (buf.typecode != 'h') {
        mp_raise_ValueError(MP_ERROR_TEXT("an array('h') is needed"));
    }
    fx_crackle_render(c, buf.buf, (int32_t)(buf.len / 2));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(render_obj, render);

// counts(state): (popped, started, dropped)
static mp_obj_t counts(mp_obj_t state_in) {
    fx_crackle_t *c = state(state_in);
    mp_obj_t items[3] = { mp_obj_new_int(c->popped), mp_obj_new_int(c->started), mp_obj_new_int(c->dropped) };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(counts_obj, counts);

mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    MP_DYNRUNTIME_INIT_ENTRY
    mp_store_global(MP_QSTR_SIZE, MP_OBJ_NEW_SMALL_INT(sizeof(fx_crackle_t)));
    mp_store_global(MP_QSTR_init, MP_OBJ_FROM_PTR(&init_obj));
    mp_store_global(MP_QSTR_add_pops, MP_OBJ_FROM_PTR(&add_pops_obj));
    mp_store_global(MP_QSTR_render, MP_OBJ_FROM_PTR(&render_obj));
    mp_store_global(MP_QSTR_counts, MP_OBJ_FROM_PTR(&counts_obj));
    MP_DYNRUNTIME_INIT_EXIT
}
