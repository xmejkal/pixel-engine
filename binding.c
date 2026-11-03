// The engine module (P589 probe): the thin MicroPython binding of engine/core/world.c, for probe589.py to time a C
// step, its events and a call back into Python. One float world and one fixed world, loaded together.
// --- probe, throwaway (P589) ---
#include "py/runtime.h"

#include "world.h"

enum { MODE_FLOAT = 0, MODE_FIXED = 1 };

static world_float_t world_f;
static world_fixed_t world_q;

static const void *floats(mp_obj_t obj, size_t *n) {  // an array('f'): its data, and its length in floats
    mp_buffer_info_t info;
    mp_get_buffer_raise(obj, &info, MP_BUFFER_READ);
    if (info.typecode != 'f') {
        mp_raise_TypeError(MP_ERROR_TEXT("an array('f') is needed"));
    }
    *n = info.len / sizeof(float);
    return info.buf;
}

// load(x, y, vx, vy, rest_y): five array('f') of one length, into both worlds.
static mp_obj_t engine_load(size_t n_args, const mp_obj_t *args) {
    const float *c[5];
    size_t n = 0;
    for (size_t k = 0; k < 5; k++) {
        size_t len;
        c[k] = floats(args[k], &len);
        if (k > 0 && len != n) {
            mp_raise_ValueError(MP_ERROR_TEXT("the arrays differ in length"));
        }
        n = len;
    }
    if (n > WORLD_MAX_BODIES) {
        mp_raise_ValueError(MP_ERROR_TEXT("more than 64 bodies"));
    }
    world_load_float(&world_f, (int32_t)n, c[0], c[1], c[2], c[3], c[4]);
    world_load_fixed(&world_q, (int32_t)n, c[0], c[1], c[2], c[3], c[4]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(engine_load_obj, 5, 5, engine_load);

// step(mode, steps, rate, gravity, restitution, friction): the world's awake body-steps since the load.
static mp_obj_t engine_step(size_t n_args, const mp_obj_t *args) {
    mp_int_t mode = mp_obj_get_int(args[0]);
    mp_int_t steps = mp_obj_get_int(args[1]);
    mp_int_t rate = mp_obj_get_int(args[2]);
    float gravity = (float)mp_obj_get_float(args[3]);
    float restitution = (float)mp_obj_get_float(args[4]);
    float friction = (float)mp_obj_get_float(args[5]);
    if (rate <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("rate must be over 0"));
    }
    if (mode == MODE_FLOAT) {
        world_step_float(&world_f, (int32_t)steps, (int32_t)rate, gravity, restitution, friction);
        return mp_obj_new_int_from_uint(world_f.s.awake_steps);
    }
    if (mode == MODE_FIXED) {
        world_step_fixed(&world_q, (int32_t)steps, (int32_t)rate, gravity, restitution, friction);
        return mp_obj_new_int_from_uint(world_q.s.awake_steps);
    }
    mp_raise_ValueError(MP_ERROR_TEXT("mode is engine.FLOAT or engine.FIXED"));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(engine_step_obj, 6, 6, engine_step);

// read_y(mode, out): each body's y into an array('f') of at least n floats.
static mp_obj_t engine_read_y(mp_obj_t mode_in, mp_obj_t out_in) {
    mp_buffer_info_t info;
    mp_get_buffer_raise(out_in, &info, MP_BUFFER_WRITE);
    mp_int_t mode = mp_obj_get_int(mode_in);
    int32_t n = mode == MODE_FIXED ? world_q.s.n : world_f.s.n;
    if (info.typecode != 'f' || info.len < (size_t)n * sizeof(float)) {
        mp_raise_ValueError(MP_ERROR_TEXT("an array('f') of n floats is needed"));
    }
    if (mode == MODE_FIXED) {
        world_read_y_fixed(&world_q, info.buf);
    } else {
        world_read_y_float(&world_f, info.buf);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(engine_read_y_obj, engine_read_y);

// drain(buf, n): up to n of the float world's last step-call events into an array('i') as id, kind, speed; how many.
static mp_obj_t engine_drain(mp_obj_t buf_in, mp_obj_t n_in) {
    mp_buffer_info_t info;
    mp_get_buffer_raise(buf_in, &info, MP_BUFFER_WRITE);
    if (info.typecode != 'i' || sizeof(int) != sizeof(int32_t)) {
        mp_raise_TypeError(MP_ERROR_TEXT("an array('i') is needed"));
    }
    mp_int_t n = mp_obj_get_int(n_in);
    mp_int_t room = (mp_int_t)(info.len / (3 * sizeof(int32_t)));
    return MP_OBJ_NEW_SMALL_INT(world_drain(&world_f.s, info.buf, (int32_t)(n < room ? n : room)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(engine_drain_obj, engine_drain);

// call_back(fn, n): call fn(i) for i in range(n), the cost of C calling Python.
static mp_obj_t engine_call_back(mp_obj_t fn, mp_obj_t n_in) {
    mp_int_t n = mp_obj_get_int(n_in);
    for (mp_int_t i = 0; i < n; i++) {
        mp_call_function_1(fn, MP_OBJ_NEW_SMALL_INT(i));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(engine_call_back_obj, engine_call_back);

// nothing(): the empty Python-to-C call.
static mp_obj_t engine_nothing(void) {
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(engine_nothing_obj, engine_nothing);

static const mp_rom_map_elem_t engine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_engine) },
    { MP_ROM_QSTR(MP_QSTR_FLOAT), MP_ROM_INT(MODE_FLOAT) },
    { MP_ROM_QSTR(MP_QSTR_FIXED), MP_ROM_INT(MODE_FIXED) },
    { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&engine_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_step), MP_ROM_PTR(&engine_step_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_y), MP_ROM_PTR(&engine_read_y_obj) },
    { MP_ROM_QSTR(MP_QSTR_drain), MP_ROM_PTR(&engine_drain_obj) },
    { MP_ROM_QSTR(MP_QSTR_call_back), MP_ROM_PTR(&engine_call_back_obj) },
    { MP_ROM_QSTR(MP_QSTR_nothing), MP_ROM_PTR(&engine_nothing_obj) },
};
static MP_DEFINE_CONST_DICT(engine_module_globals, engine_module_globals_table);

const mp_obj_module_t engine_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&engine_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_engine, engine_user_cmodule);
