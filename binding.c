// The engine module. The world part is the P589 probe: the thin MicroPython binding of engine/core/world.c, for
// probe589.py to time a C step, its events and a call back into Python. One float world and one fixed world, loaded
// together. spawn and read (P593) are the rain demo's, on the float world only. --- probe, throwaway (P589) ---
// The HeatField part is P595's and is kept (D8): the fire of engine/core/fx_fire.c as a native type, drawn into the
// s3lcd frame, opaque or blended (P603). --- MVP (P595) ---
#include "py/runtime.h"

#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"

#include "fx_fire.h"
#include "s3lcd.h"
#include "world.h"

extern const mp_obj_type_t s3lcd_type;  // s3lcd.c's ESPLCD, so s3lcd stays unchanged

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
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("more than %d bodies"), WORLD_MAX_BODIES);
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

// spawn(i, x, y, vx, vy, rest_y): body i of the float world at this state, awake; n grows to i + 1 when i is past it.
static mp_obj_t engine_spawn(size_t n_args, const mp_obj_t *args) {
    float v[5];
    for (size_t k = 0; k < 5; k++) {
        v[k] = (float)mp_obj_get_float(args[k + 1]);
    }
    if (!world_spawn_float(&world_f, (int32_t)mp_obj_get_int(args[0]), v[0], v[1], v[2], v[3], v[4])) {
        mp_raise_msg_varg(&mp_type_IndexError, MP_ERROR_TEXT("a body index is 0 to %d"), WORLD_MAX_BODIES - 1);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(engine_spawn_obj, 6, 6, engine_spawn);

// read(out): the float world's x, y and asleep per body into an array('h') of at least 3n, as ints; returns n.
static mp_obj_t engine_read(mp_obj_t out_in) {
    mp_buffer_info_t info;
    mp_get_buffer_raise(out_in, &info, MP_BUFFER_WRITE);
    if (info.typecode != 'h' || info.len < (size_t)world_f.s.n * 3 * sizeof(int16_t)) {
        mp_raise_ValueError(MP_ERROR_TEXT("an array('h') of 3n ints is needed"));
    }
    world_read_float(&world_f, info.buf);
    return MP_OBJ_NEW_SMALL_INT(world_f.s.n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(engine_read_obj, engine_read);

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

// --- MVP (P595): engine.HeatField ---

typedef struct {
    mp_obj_base_t base;
    fx_fire_t fire;  // fire.heat is NULL once freed
    bool psram;
} heat_field_obj_t;

static const mp_obj_type_t heat_field_type;

// HeatField(w, h, seed): a field of w x h cells and its cooling map after it in one block, in internal RAM when a
// block is free there, else in PSRAM. The finaliser frees it. App.switch drops the scene and collects, so leaving the
// scene frees the field, and a soft reset sweeps with finalisers too. A conservative stack scan can keep the old scene
// until the next App.collect().
static mp_obj_t heat_field_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 3, 3, false);
    mp_int_t w = mp_obj_get_int(args[0]);
    mp_int_t h = mp_obj_get_int(args[1]);
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
        mp_raise_ValueError(MP_ERROR_TEXT("w and h are 1 to 1024"));
    }
    uint32_t seed = (uint32_t)mp_obj_get_int_truncated(args[2]);  // before the block, so a wrong type leaks nothing
    heat_field_obj_t *self = mp_obj_malloc_with_finaliser(heat_field_obj_t, type);
    self->fire.heat = NULL;  // the object first: a failed allocation then leaks nothing
    size_t n = (size_t)w * (size_t)h + FX_FIRE_MAP_W * FX_FIRE_MAP_H;
    uint8_t *heat = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    self->psram = heat == NULL;
    if (heat == NULL) {
        heat = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    }
    if (heat == NULL) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("no room for the heat field"));
    }
    fx_fire_init(&self->fire, heat, heat + (size_t)w * (size_t)h, (int32_t)w, (int32_t)h, seed);
    return MP_OBJ_FROM_PTR(self);
}

static heat_field_obj_t *field(mp_obj_t self_in) {  // a HeatField called through its type with another object raises
    if (!mp_obj_is_type(self_in, &heat_field_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("a HeatField is needed"));
    }
    return MP_OBJ_TO_PTR(self_in);
}

static heat_field_obj_t *live(mp_obj_t self_in) {
    heat_field_obj_t *self = field(self_in);
    if (self->fire.heat == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("the heat field is freed"));
    }
    return self;
}

// set_look(decay, cool, sway, spots, spread, fast, embers, fade): see fx_fire_set_look, each number in its range.
static mp_obj_t heat_field_set_look(size_t n_args, const mp_obj_t *args) {
    heat_field_obj_t *self = live(args[0]);
    mp_int_t v[8];
    for (size_t k = 0; k < 8; k++) {
        v[k] = mp_obj_get_int(args[k + 1]);
    }
    if (v[0] < 0 || v[0] > 255 || v[1] < 0 || v[1] > 255 || v[2] < 0 || v[2] > 1 || v[3] < 0
        || v[3] > FX_FIRE_MAX_SPOTS || v[4] < 1 || v[4] > 64 || v[5] < 0 || v[5] > 255
        || v[6] < 0 || v[6] > 255 || v[7] < 0 || v[7] > 255) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("decay, cool, fast, embers and fade are 0 to 255, sway 0 or 1, spots 0 to %d, "
                "spread 1 to 64"),
            FX_FIRE_MAX_SPOTS);
    }
    fx_fire_set_look(&self->fire, (int32_t)v[0], (int32_t)v[1], (int32_t)v[2], (int32_t)v[3], (int32_t)v[4],
        (int32_t)v[5], (int32_t)v[6], (int32_t)v[7]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(heat_field_set_look_obj, 9, 9, heat_field_set_look);

// step(steps, wind): see fx_fire_step. A frame takes at most 2 steps, so more than 64 is a mistake.
static mp_obj_t heat_field_step(mp_obj_t self_in, mp_obj_t steps_in, mp_obj_t wind_in) {
    heat_field_obj_t *self = live(self_in);
    mp_int_t steps = mp_obj_get_int(steps_in);
    mp_int_t wind = mp_obj_get_int(wind_in);
    if (steps < 0 || steps > 64 || wind < -255 || wind > 255) {
        mp_raise_ValueError(MP_ERROR_TEXT("steps is 0 to 64, wind -255 to 255"));
    }
    fx_fire_step(&self->fire, (int32_t)steps, (int32_t)wind);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(heat_field_step_obj, heat_field_step);

// draw(tft, palette{, alpha}): the whole panel into an s3lcd ESPLCD's frame, all of it marked to send. palette is an
// array('H') of 256. Without alpha, each pixel is palette[heat] (path B). With alpha, an array('B') of 256, each pixel
// is blended over what the frame holds at alpha[heat]: the picture mode over the sky (P603). Path A, a buffer blitted
// opaque, was dropped (P595), and the picture mode's buffer with it (P603).
static mp_obj_t heat_field_draw(size_t n_args, const mp_obj_t *args) {
    heat_field_obj_t *self = live(args[0]);
    int32_t pw = 2 * self->fire.h, ph = 2 * self->fire.w;
    if (!mp_obj_is_type(args[1], &s3lcd_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("an s3lcd display is needed"));
    }
    s3lcd_obj_t *tft = MP_OBJ_TO_PTR(args[1]);
    if (tft->frame_buffer == NULL || tft->width != pw || tft->height != ph) {
        mp_raise_ValueError(MP_ERROR_TEXT("the display is not 2h x 2w, or not initialised"));
    }
    mp_buffer_info_t palette;
    mp_get_buffer_raise(args[2], &palette, MP_BUFFER_READ);
    if (palette.typecode != 'H' || palette.len < 256 * sizeof(uint16_t)) {
        mp_raise_ValueError(MP_ERROR_TEXT("an array('H') of 256 colours is needed"));
    }
    if (n_args > 3 && args[3] != mp_const_none) {
        mp_buffer_info_t alpha;
        mp_get_buffer_raise(args[3], &alpha, MP_BUFFER_READ);
        if (alpha.typecode != 'B' || alpha.len < 256) {
            mp_raise_ValueError(MP_ERROR_TEXT("an array('B') of 256 alphas is needed"));
        }
        fx_fire_blend(&self->fire, tft->frame_buffer, palette.buf, alpha.buf);
    } else {
        fx_fire_draw(&self->fire, tft->frame_buffer, palette.buf);
    }
    frame_dirty_all(&tft->frame);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(heat_field_draw_obj, 3, 4, heat_field_draw);

// __del__(): free the field once.
static mp_obj_t heat_field_del(mp_obj_t self_in) {
    heat_field_obj_t *self = field(self_in);
    if (self->fire.heat != NULL) {
        heap_caps_free(self->fire.heat);
        self->fire.heat = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(heat_field_del_obj, heat_field_del);

// where: "internal" or "psram". Every other name goes on to the methods.
static void heat_field_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    if (attr == MP_QSTR_where && dest[0] == MP_OBJ_NULL) {
        heat_field_obj_t *self = MP_OBJ_TO_PTR(self_in);
        dest[0] = MP_OBJ_NEW_QSTR(self->psram ? MP_QSTR_psram : MP_QSTR_internal);
    } else {
        dest[1] = MP_OBJ_SENTINEL;
    }
}

static const mp_rom_map_elem_t heat_field_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_set_look), MP_ROM_PTR(&heat_field_set_look_obj) },
    { MP_ROM_QSTR(MP_QSTR_step), MP_ROM_PTR(&heat_field_step_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw), MP_ROM_PTR(&heat_field_draw_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&heat_field_del_obj) },
};
static MP_DEFINE_CONST_DICT(heat_field_locals, heat_field_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    heat_field_type,
    MP_QSTR_HeatField,
    MP_TYPE_FLAG_NONE,
    make_new, heat_field_make_new,
    attr, heat_field_attr,
    locals_dict, &heat_field_locals);

// largest_free_internal(): the largest free block of internal RAM in bytes (A9).
static mp_obj_t engine_largest_free_internal(void) {
    return mp_obj_new_int_from_uint(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}
static MP_DEFINE_CONST_FUN_OBJ_0(engine_largest_free_internal_obj, engine_largest_free_internal);

static const mp_rom_map_elem_t engine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_engine) },
    { MP_ROM_QSTR(MP_QSTR_FLOAT), MP_ROM_INT(MODE_FLOAT) },
    { MP_ROM_QSTR(MP_QSTR_FIXED), MP_ROM_INT(MODE_FIXED) },
    { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&engine_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_step), MP_ROM_PTR(&engine_step_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_y), MP_ROM_PTR(&engine_read_y_obj) },
    { MP_ROM_QSTR(MP_QSTR_spawn), MP_ROM_PTR(&engine_spawn_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&engine_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_drain), MP_ROM_PTR(&engine_drain_obj) },
    { MP_ROM_QSTR(MP_QSTR_call_back), MP_ROM_PTR(&engine_call_back_obj) },
    { MP_ROM_QSTR(MP_QSTR_nothing), MP_ROM_PTR(&engine_nothing_obj) },
    { MP_ROM_QSTR(MP_QSTR_HeatField), MP_ROM_PTR(&heat_field_type) },
    { MP_ROM_QSTR(MP_QSTR_largest_free_internal), MP_ROM_PTR(&engine_largest_free_internal_obj) },
};
static MP_DEFINE_CONST_DICT(engine_module_globals, engine_module_globals_table);

const mp_obj_module_t engine_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&engine_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_engine, engine_user_cmodule);
