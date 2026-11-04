// The engine module. engine.Bodies (P590) is the world of engine/core/world.c as a native type: C steps the falling
// bodies and keeps the facts of a step as events, which Python drains once a frame (D11 to D13). Its twin is
// games/motion.py's Bodies, with the same API. --- MVP (P590) ---
// The HeatField part is P595's and is kept (D8): the fire of engine/core/fx_fire.c as a native type, drawn into the
// s3lcd frame, opaque or blended (P603). --- MVP (P595) ---
// engine.Balls (P598) is the circle solver of engine/core/fx_balls.c as a native type: balls that fall, knock and pile
// in a box, their hits as events, drawn into the s3lcd frame. --- MVP (P598) ---
#include "py/runtime.h"

#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"

#include "fx_balls.h"
#include "fx_fire.h"
#include "s3lcd.h"
#include "world.h"

_Static_assert(sizeof(int) == sizeof(int32_t), "an array('i') holds int32 events");

extern const mp_obj_type_t s3lcd_type;  // s3lcd.c's ESPLCD, so s3lcd stays unchanged

// --- MVP (P590): engine.Bodies ---

#define BODIES_MAX 256  // n is 1 to 256: the rain's 160 and room
#define BODIES_MAX_STEPS 64  // a frame takes at most 8 steps (StepClock's cap), so more than 64 is a mistake

typedef struct {
    mp_obj_base_t base;
    world_t world;  // then its arrays in the same block: world_bytes(cap) bytes from here
} bodies_obj_t;

static const mp_obj_type_t bodies_type;

// Bodies(n): n bodies, each asleep at (0, 0). The world lives in the object's own block of the GC heap (in PSRAM), so
// the GC frees it with the object and no finaliser runs; it stores no pointer, so nothing points into it.
static mp_obj_t bodies_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    mp_int_t n = mp_obj_get_int(args[0]);
    if (n < 1 || n > BODIES_MAX) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("n is 1 to %d"), BODIES_MAX);
    }
    bodies_obj_t *self = mp_obj_malloc_var(bodies_obj_t, world, uint8_t, world_bytes((int32_t)n), type);
    world_init(&self->world, (int32_t)n);
    return MP_OBJ_FROM_PTR(self);
}

static world_t *bodies_world(mp_obj_t self_in) {  // a Bodies method called through its type with another object raises
    if (!mp_obj_is_type(self_in, &bodies_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("a Bodies is needed"));
    }
    return &((bodies_obj_t *)MP_OBJ_TO_PTR(self_in))->world;
}

// spawn(i, x, y, vx, vy, rest_y): body i at this state, awake.
static mp_obj_t bodies_spawn(size_t n_args, const mp_obj_t *args) {
    world_t *w = bodies_world(args[0]);
    mp_int_t i = mp_obj_get_int(args[1]);
    if (i < 0 || i >= w->cap) {
        mp_raise_msg(&mp_type_IndexError, MP_ERROR_TEXT("a body index is 0 to n - 1"));
    }
    float v[5];
    for (size_t k = 0; k < 5; k++) {
        v[k] = (float)mp_obj_get_float(args[k + 2]);
    }
    world_spawn(w, (int32_t)i, v[0], v[1], v[2], v[3], v[4]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bodies_spawn_obj, 7, 7, bodies_spawn);

// step(steps, dt, gravity, restitution, friction): see world_step. It clears the events first, also for 0 steps.
static mp_obj_t bodies_step(size_t n_args, const mp_obj_t *args) {
    world_t *w = bodies_world(args[0]);
    mp_int_t steps = mp_obj_get_int(args[1]);
    if (steps < 0 || steps > BODIES_MAX_STEPS) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("steps is 0 to %d"), BODIES_MAX_STEPS);
    }
    float v[4];
    for (size_t k = 0; k < 4; k++) {
        v[k] = (float)mp_obj_get_float(args[k + 2]);
    }
    world_step(w, (int32_t)steps, v[0], v[1], v[2], v[3]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bodies_step_obj, 6, 6, bodies_step);

// read(out): each body's x, y and asleep into an array('h') of at least 3n, as ints; returns n.
static mp_obj_t bodies_read(mp_obj_t self_in, mp_obj_t out_in) {
    world_t *w = bodies_world(self_in);
    mp_buffer_info_t info;
    mp_get_buffer_raise(out_in, &info, MP_BUFFER_WRITE);
    if (info.typecode != 'h' || info.len < (size_t)w->cap * 3 * sizeof(int16_t)) {
        mp_raise_ValueError(MP_ERROR_TEXT("an array('h') of 3n ints is needed"));
    }
    return MP_OBJ_NEW_SMALL_INT(world_read(w, info.buf));
}
static MP_DEFINE_CONST_FUN_OBJ_2(bodies_read_obj, bodies_read);

// drain(out): up to len(out) // 3 of the last step call's events into an array('i') as id, kind, speed; how many.
static mp_obj_t bodies_drain(mp_obj_t self_in, mp_obj_t out_in) {
    world_t *w = bodies_world(self_in);
    mp_buffer_info_t info;
    mp_get_buffer_raise(out_in, &info, MP_BUFFER_WRITE);
    if (info.typecode != 'i') {
        mp_raise_ValueError(MP_ERROR_TEXT("an array('i') is needed"));
    }
    return MP_OBJ_NEW_SMALL_INT(world_drain(w, info.buf, (int32_t)(info.len / (3 * sizeof(int32_t)))));
}
static MP_DEFINE_CONST_FUN_OBJ_2(bodies_drain_obj, bodies_drain);

// dropped: the last step call's events past EVENTS_MAX. Every other name goes on to the methods.
static void bodies_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    if (attr == MP_QSTR_dropped && dest[0] == MP_OBJ_NULL) {
        dest[0] = MP_OBJ_NEW_SMALL_INT(bodies_world(self_in)->events.dropped);
    } else {
        dest[1] = MP_OBJ_SENTINEL;
    }
}

static const mp_rom_map_elem_t bodies_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_spawn), MP_ROM_PTR(&bodies_spawn_obj) },
    { MP_ROM_QSTR(MP_QSTR_step), MP_ROM_PTR(&bodies_step_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&bodies_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_drain), MP_ROM_PTR(&bodies_drain_obj) },
};
static MP_DEFINE_CONST_DICT(bodies_locals, bodies_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    bodies_type,
    MP_QSTR_Bodies,
    MP_TYPE_FLAG_NONE,
    make_new, bodies_make_new,
    attr, bodies_attr,
    locals_dict, &bodies_locals);

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

// --- MVP (P596) ---
// set_base(on): see fx_fire_set_base. A false on clears the bottom row each step, so only burn puts heat in.
static mp_obj_t heat_field_set_base(mp_obj_t self_in, mp_obj_t on_in) {
    fx_fire_set_base(&live(self_in)->fire, mp_obj_is_true(on_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(heat_field_set_base_obj, heat_field_set_base);

// --- MVP (P608) ---
// set_filter(filter): see fx_fire_set_filter. filter is engine.NEAREST or engine.LINEAR.
static mp_obj_t heat_field_set_filter(mp_obj_t self_in, mp_obj_t filter_in) {
    heat_field_obj_t *self = live(self_in);
    mp_int_t filter = mp_obj_get_int(filter_in);
    if (filter != FX_FIRE_NEAREST && filter != FX_FIRE_LINEAR) {
        mp_raise_ValueError(MP_ERROR_TEXT("filter is engine.NEAREST or engine.LINEAR"));
    }
    fx_fire_set_filter(&self->fire, (int32_t)filter);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(heat_field_set_filter_obj, heat_field_set_filter);

// burn(x0, y0, x1, y1, r, heat): see fx_fire_burn, in cells. The ends may lie off the field by up to a field's size
// (a body above the screen), r is 0 to 16 and heat 0 to 255.
static mp_obj_t heat_field_burn(size_t n_args, const mp_obj_t *args) {
    heat_field_obj_t *self = live(args[0]);
    int32_t w = self->fire.w, h = self->fire.h;
    mp_int_t v[6];
    for (size_t k = 0; k < 6; k++) {
        v[k] = mp_obj_get_int(args[k + 1]);
    }
    if (v[0] < -w || v[0] > 2 * w || v[2] < -w || v[2] > 2 * w || v[1] < -h || v[1] > 2 * h || v[3] < -h
        || v[3] > 2 * h || v[4] < 0 || v[4] > 16 || v[5] < 0 || v[5] > 255) {
        mp_raise_ValueError(MP_ERROR_TEXT("x within -w to 2w, y within -h to 2h, r 0 to 16, heat 0 to 255"));
    }
    fx_fire_burn(&self->fire, (int32_t)v[0], (int32_t)v[1], (int32_t)v[2], (int32_t)v[3], (int32_t)v[4],
        (uint8_t)v[5]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(heat_field_burn_obj, 7, 7, heat_field_burn);

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

// --- Later, not MVP: kept for other games, the PO 2026-11-04 (P602) ---
// scroll(dx): see fx_fire_scroll. The world moves under the screen by dx cells, negative to the left; -1024 to 1024,
// as no field is wider than 1024.
static mp_obj_t heat_field_scroll(mp_obj_t self_in, mp_obj_t dx_in) {
    heat_field_obj_t *self = live(self_in);
    mp_int_t dx = mp_obj_get_int(dx_in);
    if (dx < -1024 || dx > 1024) {
        mp_raise_ValueError(MP_ERROR_TEXT("dx is -1024 to 1024"));
    }
    fx_fire_scroll(&self->fire, (int32_t)dx);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(heat_field_scroll_obj, heat_field_scroll);

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
    { MP_ROM_QSTR(MP_QSTR_set_base), MP_ROM_PTR(&heat_field_set_base_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_filter), MP_ROM_PTR(&heat_field_set_filter_obj) },  // P608
    { MP_ROM_QSTR(MP_QSTR_burn), MP_ROM_PTR(&heat_field_burn_obj) },
    { MP_ROM_QSTR(MP_QSTR_scroll), MP_ROM_PTR(&heat_field_scroll_obj) },  // P602
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

// --- MVP (P598): engine.Balls ---

#define BALLS_MAX 2000  // n is 1 to 2,000, about 60 KB: more than fill the screen (the PO: "as many as I want")
#define BALLS_MAX_STEPS 64  // a frame takes at most 8 steps (StepClock's cap), so more than 64 is a mistake
#define BALLS_MAX_SUBSTEPS 8

typedef struct {
    mp_obj_base_t base;
    fx_balls_t balls;  // then its grid and arrays in the same block: fx_balls_bytes(cap) bytes from here
} balls_obj_t;

static const mp_obj_type_t balls_type;

// Balls(n): room for n balls, none yet. The block lives on the GC heap as Bodies' does: the GC frees it with the
// object, and it stores no pointer.
static mp_obj_t balls_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    mp_int_t n = mp_obj_get_int(args[0]);
    if (n < 1 || n > BALLS_MAX) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("n is 1 to %d"), BALLS_MAX);
    }
    balls_obj_t *self = mp_obj_malloc_var(balls_obj_t, balls, uint8_t, fx_balls_bytes((int32_t)n), type);
    fx_balls_init(&self->balls, (int32_t)n);
    return MP_OBJ_FROM_PTR(self);
}

static fx_balls_t *balls_of(mp_obj_t self_in) {  // a Balls method called through its type with another object raises
    if (!mp_obj_is_type(self_in, &balls_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("a Balls is needed"));
    }
    return &((balls_obj_t *)MP_OBJ_TO_PTR(self_in))->balls;
}

// spawn(x, y, vx, vy, r, colour): one more ball; its id, or -1 when full. r is FX_BALLS_R_MIN to FX_BALLS_R_MAX,
// colour 0 to FX_BALLS_COLOURS - 1.
static mp_obj_t balls_spawn(size_t n_args, const mp_obj_t *args) {
    fx_balls_t *b = balls_of(args[0]);
    float v[4];
    for (size_t k = 0; k < 4; k++) {
        v[k] = (float)mp_obj_get_float(args[k + 1]);
    }
    mp_int_t r = mp_obj_get_int(args[5]), colour = mp_obj_get_int(args[6]);
    if (r < FX_BALLS_R_MIN || r > FX_BALLS_R_MAX || colour < 0 || colour >= FX_BALLS_COLOURS) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("r is %d to %d, colour 0 to %d"), FX_BALLS_R_MIN,
            FX_BALLS_R_MAX, FX_BALLS_COLOURS - 1);
    }
    return MP_OBJ_NEW_SMALL_INT(fx_balls_spawn(b, v[0], v[1], v[2], v[3], (int32_t)r, (int32_t)colour));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(balls_spawn_obj, 7, 7, balls_spawn);

// step(steps, substeps, dt, gravity, damping, hit_min): see fx_balls_step. It clears the events first, also for 0 steps.
static mp_obj_t balls_step(size_t n_args, const mp_obj_t *args) {
    fx_balls_t *b = balls_of(args[0]);
    mp_int_t steps = mp_obj_get_int(args[1]), substeps = mp_obj_get_int(args[2]);
    if (steps < 0 || steps > BALLS_MAX_STEPS || substeps < 1 || substeps > BALLS_MAX_SUBSTEPS) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("steps is 0 to %d, substeps 1 to %d"), BALLS_MAX_STEPS,
            BALLS_MAX_SUBSTEPS);
    }
    float v[4];
    for (size_t k = 0; k < 4; k++) {
        v[k] = (float)mp_obj_get_float(args[k + 3]);
    }
    fx_balls_step(b, (int32_t)steps, (int32_t)substeps, v[0], v[1], v[2], v[3]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(balls_step_obj, 7, 7, balls_step);

// read(out): each ball's x, y and r into an array('h') of at least 3n, as ints; returns the count.
static mp_obj_t balls_read(mp_obj_t self_in, mp_obj_t out_in) {
    fx_balls_t *b = balls_of(self_in);
    mp_buffer_info_t info;
    mp_get_buffer_raise(out_in, &info, MP_BUFFER_WRITE);
    if (info.typecode != 'h' || info.len < (size_t)b->cap * 3 * sizeof(int16_t)) {
        mp_raise_ValueError(MP_ERROR_TEXT("an array('h') of 3n ints is needed"));
    }
    return MP_OBJ_NEW_SMALL_INT(fx_balls_read(b, info.buf));
}
static MP_DEFINE_CONST_FUN_OBJ_2(balls_read_obj, balls_read);

// drain(out): up to len(out) // 3 of the last step call's events into an array('i') as id, kind, speed; how many.
static mp_obj_t balls_drain(mp_obj_t self_in, mp_obj_t out_in) {
    fx_balls_t *b = balls_of(self_in);
    mp_buffer_info_t info;
    mp_get_buffer_raise(out_in, &info, MP_BUFFER_WRITE);
    if (info.typecode != 'i') {
        mp_raise_ValueError(MP_ERROR_TEXT("an array('i') is needed"));
    }
    return MP_OBJ_NEW_SMALL_INT(fx_balls_drain(b, info.buf, (int32_t)(info.len / (3 * sizeof(int32_t)))));
}
static MP_DEFINE_CONST_FUN_OBJ_2(balls_drain_obj, balls_drain);

// draw(tft, palette, bg): the whole panel into an s3lcd ESPLCD's frame (heat_field_draw's checks), all of it marked
// to send: bg everywhere, then each ball in palette[colour], an array('H') of FX_BALLS_COLOURS in the panel's byte
// order, as bg is.
static mp_obj_t balls_draw(size_t n_args, const mp_obj_t *args) {
    fx_balls_t *b = balls_of(args[0]);
    if (!mp_obj_is_type(args[1], &s3lcd_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("an s3lcd display is needed"));
    }
    s3lcd_obj_t *tft = MP_OBJ_TO_PTR(args[1]);
    if (tft->frame_buffer == NULL || tft->width != FX_BALLS_H || tft->height != FX_BALLS_W) {
        mp_raise_ValueError(MP_ERROR_TEXT("the display is not 240 x 320, or not initialised"));
    }
    mp_buffer_info_t palette;
    mp_get_buffer_raise(args[2], &palette, MP_BUFFER_READ);
    if (palette.typecode != 'H' || palette.len < FX_BALLS_COLOURS * sizeof(uint16_t)) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("an array('H') of %d colours is needed"),
            FX_BALLS_COLOURS);
    }
    mp_int_t bg = mp_obj_get_int(args[3]);
    if (bg < 0 || bg > 0xFFFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("bg is a 16-bit colour"));
    }
    fx_balls_draw(b, tft->frame_buffer, palette.buf, (uint16_t)bg);
    frame_dirty_all(&tft->frame);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(balls_draw_obj, 4, 4, balls_draw);

// clear(): no balls.
static mp_obj_t balls_clear(mp_obj_t self_in) {
    fx_balls_clear(balls_of(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(balls_clear_obj, balls_clear);

// kick(vx, vy): see fx_balls_kick, the pit's shake (the PO 2026-11-04).
static mp_obj_t balls_kick(mp_obj_t self_in, mp_obj_t vx_in, mp_obj_t vy_in) {
    fx_balls_kick(balls_of(self_in), mp_obj_get_float(vx_in), mp_obj_get_float(vy_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(balls_kick_obj, balls_kick);

// set_material(friction, restitution, restitution_threshold): see fx_balls_set_material (the PO 2026-11-04: "the balls
// really don't bounce").
static mp_obj_t balls_set_material(size_t n_args, const mp_obj_t *args) {
    float v[3];
    for (size_t k = 0; k < 3; k++) {
        v[k] = (float)mp_obj_get_float(args[k + 1]);
    }
    if (!(v[0] >= 0.0f) || !(v[1] >= 0.0f && v[1] <= 1.0f) || !(v[2] >= 0.0f)) {  // a NaN fails each
        mp_raise_ValueError(MP_ERROR_TEXT("friction is 0 or more, restitution 0 to 1, restitution_threshold 0 or more"));
    }
    fx_balls_set_material(balls_of(args[0]), v[0], v[1], v[2]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(balls_set_material_obj, 4, 4, balls_set_material);

// count: the balls spawned; dropped: the last step call's events past EVENTS_MAX. Every other name goes on to the
// methods.
static void balls_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    if (attr == MP_QSTR_count && dest[0] == MP_OBJ_NULL) {
        dest[0] = MP_OBJ_NEW_SMALL_INT(balls_of(self_in)->n);
    } else if (attr == MP_QSTR_dropped && dest[0] == MP_OBJ_NULL) {
        dest[0] = MP_OBJ_NEW_SMALL_INT(balls_of(self_in)->events.dropped);
    } else {
        dest[1] = MP_OBJ_SENTINEL;
    }
}

static const mp_rom_map_elem_t balls_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_spawn), MP_ROM_PTR(&balls_spawn_obj) },
    { MP_ROM_QSTR(MP_QSTR_step), MP_ROM_PTR(&balls_step_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&balls_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_drain), MP_ROM_PTR(&balls_drain_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw), MP_ROM_PTR(&balls_draw_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&balls_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_kick), MP_ROM_PTR(&balls_kick_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_material), MP_ROM_PTR(&balls_set_material_obj) },
};
static MP_DEFINE_CONST_DICT(balls_locals, balls_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    balls_type,
    MP_QSTR_Balls,
    MP_TYPE_FLAG_NONE,
    make_new, balls_make_new,
    attr, balls_attr,
    locals_dict, &balls_locals);

// largest_free_internal(): the largest free block of internal RAM in bytes (A9).
static mp_obj_t engine_largest_free_internal(void) {
    return mp_obj_new_int_from_uint(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}
static MP_DEFINE_CONST_FUN_OBJ_0(engine_largest_free_internal_obj, engine_largest_free_internal);

static const mp_rom_map_elem_t engine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_engine) },
    { MP_ROM_QSTR(MP_QSTR_Bodies), MP_ROM_PTR(&bodies_type) },
    { MP_ROM_QSTR(MP_QSTR_HIT), MP_ROM_INT(EVENT_HIT) },
    { MP_ROM_QSTR(MP_QSTR_SLEEP), MP_ROM_INT(EVENT_SLEEP) },
    { MP_ROM_QSTR(MP_QSTR_HeatField), MP_ROM_PTR(&heat_field_type) },
    { MP_ROM_QSTR(MP_QSTR_Balls), MP_ROM_PTR(&balls_type) },  // P598
    { MP_ROM_QSTR(MP_QSTR_NEAREST), MP_ROM_INT(FX_FIRE_NEAREST) },  // P608: HeatField.set_filter's filters
    { MP_ROM_QSTR(MP_QSTR_LINEAR), MP_ROM_INT(FX_FIRE_LINEAR) },
    { MP_ROM_QSTR(MP_QSTR_largest_free_internal), MP_ROM_PTR(&engine_largest_free_internal_obj) },
};
static MP_DEFINE_CONST_DICT(engine_module_globals, engine_module_globals_table);

const mp_obj_module_t engine_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&engine_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_engine, engine_user_cmodule);
