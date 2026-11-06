/* NAOMI: the cabinet's controls as port A's pad (naomi.h).
 *
 * The game reads pads through enDjinn: enj_ctrl_map_states() refreshes every port at the top of each frame and the
 * readers take enj_ctrl_get_states(). On a NAOMI the I/O board is behind the MIE, a Maple device of its own
 * (MAPLE_FUNC_MIE), which enDjinn does not enumerate. Both calls are wrapped at link time (local.cfg.mk, as
 * tests/pad_monkey.c does; enj_ctrl.o and this file are kept out of the LTO unit, where a --wrap would be lost) and
 * port A becomes the cabinet:
 *
 *   wheel                      stick X (joyx), linear: the build sets NATIVE_STEER_CURVE=1, NATIVE_STEER_DEADZONE=2
 *   accelerator / brake        right / left trigger, 0..255
 *   shifter forward / back     A / X (the port's shift up / down)
 *   START                      START
 *   START + accelerator        Y (the next view: VR1, VR2, VR3, VR4); START is not passed on then, because START held
 *                              keeps the view (practice.c practice_view_tick)
 *   START + both pedals        START with both triggers (the operator settings, select_mode.c)
 *   coin chute, SERVICE        A for a few frames: a credit in the attract and the title (practice.c)
 *   TEST held 3 s              the console's sound and file lines on the screen (a diagnosis; the game stops)
 *
 * Raw JVS values: KOS's own mapping (mie_default_cont_map) needs a calibration run first and otherwise reads a
 * pedal at rest as a fifth pressed, so the raw channels are mapped here, from the values measured on a Crazy Taxi
 * upright (platform/dreamcast/game/naomi.h on the naomi branch), widening by themselves when a control goes further.
 * Twin or deluxe Daytona cabinets wire a four-position shifter and VR buttons differently: see naomi/README.md. */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/mie.h>
#include <enDjinn/enj_ctrl.h>
#include "naomi.h"

enj_ctrlr_state_t **__real_enj_ctrl_get_states(void);
size_t __real_enj_ctrl_map_states(void);

static enj_ctrlr_state_t cabinet;                          /* port A's state the game reads */
static enj_ctrlr_state_t *refs[MAPLE_PORT_COUNT];         /* the array handed out: port A the cabinet, the rest real */
static int have_cabinet;

static struct {
    int wheel_min, wheel_centre, wheel_max;
    int accel_min, accel_max;
    int brake_min, brake_max;
} cal = {8448, 33792, 60160, 11776, 47104, 14080, 47360};

static int coin_frames;
static int test_frames; /* TEST held: after 3 s the log screen (naomi_log.c) */

static uint32_t edge(uint32_t previous, int down) {
    switch (previous) {
    case ENJ_BUTTON_UP: return down ? ENJ_BUTTON_DOWN_THIS_FRAME : ENJ_BUTTON_UP;
    case ENJ_BUTTON_DOWN: return down ? ENJ_BUTTON_DOWN : ENJ_BUTTON_UP_THIS_FRAME;
    case ENJ_BUTTON_DOWN_THIS_FRAME: return down ? ENJ_BUTTON_DOWN : ENJ_BUTTON_UP_THIS_FRAME;
    default: return down ? ENJ_BUTTON_DOWN_THIS_FRAME : ENJ_BUTTON_UP;
    }
}

static int clean(uint16_t raw) { return (int)(raw & MIE_JVS_ANALOG_RAW_MASK); }

static int8_t steer(int raw) {
    if (raw < cal.wheel_min) cal.wheel_min = raw;
    if (raw > cal.wheel_max) cal.wheel_max = raw;
    int v;
    if (raw < cal.wheel_centre) v = -((cal.wheel_centre - raw) * 128) / (cal.wheel_centre - cal.wheel_min);
    else v = ((raw - cal.wheel_centre) * 127) / (cal.wheel_max - cal.wheel_centre);
    if (v < -128) v = -128;
    if (v > 127) v = 127;
    return (int8_t)v;
}

static uint8_t pedal(int raw, int *lo, int *hi) {
    if (raw < *lo) *lo = raw;
    if (raw > *hi) *hi = raw;
    const int margin = (*hi - *lo) * 6 / 100; /* rest jitter reads 0, a full press reaches 255 */
    const int from = *lo + margin, to = *hi - margin;
    if (raw <= from) return 0;
    if (raw >= to) return 255;
    return (uint8_t)((raw - from) * 255 / (to - from));
}

static void read_cabinet(void) {
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_MIE);
    const mie_state_t *st = dev && dev->valid ? (const mie_state_t *)maple_dev_status(dev) : NULL;
    have_cabinet = st != NULL;
    if (!st) return;
    const mie_jvs_inputs_t *j = &st->jvs;
    cabinet.joyx = steer(clean(j->wheel));
    cabinet.joyy = 0;
    cabinet.rtrigger = pedal(clean(j->accel), &cal.accel_min, &cal.accel_max);
    cabinet.ltrigger = pedal(clean(j->brake), &cal.brake_min, &cal.brake_max);
    test_frames = (j->panel.psw.test || j->system.test) ? test_frames + 1 : 0;
    if (test_frames >= 180) nm_naomi_show_sound_log();
    const int start = j->p1.start;
    const int accel = cabinet.rtrigger >= 128, brake = cabinet.ltrigger >= 128;
    const int view = start && accel && !brake;
    if (j->coin_pulse || j->p1.service || j->panel.psw.service) coin_frames = 4;
    const int coin = coin_frames > 0;
    if (coin_frames > 0) --coin_frames;
    cabinet.button.START = edge(cabinet.button.START, start && !view);
    cabinet.button.Y = edge(cabinet.button.Y, view);
    cabinet.button.A = edge(cabinet.button.A, j->p1.up || coin);
    cabinet.button.X = edge(cabinet.button.X, j->p1.down);
    cabinet.button.B = edge(cabinet.button.B, 0);
    cabinet.button.UP = edge(cabinet.button.UP, 0);
    cabinet.button.DOWN = edge(cabinet.button.DOWN, 0);
    cabinet.button.LEFT = edge(cabinet.button.LEFT, 0);
    cabinet.button.RIGHT = edge(cabinet.button.RIGHT, 0);
    cabinet.portnum = 0;
    cabinet.state = NULL;
}

size_t __wrap_enj_ctrl_map_states(void) {
    const size_t mapped = __real_enj_ctrl_map_states();
    read_cabinet();
    return mapped + (have_cabinet ? 1u : 0u);
}

enj_ctrlr_state_t **__wrap_enj_ctrl_get_states(void) {
    enj_ctrlr_state_t **real = __real_enj_ctrl_get_states();
    for (size_t i = 0; i < MAPLE_PORT_COUNT; ++i) refs[i] = real[i];
    if (have_cabinet) refs[0] = &cabinet;
    return refs;
}
