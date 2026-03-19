/* Shared QMK hook wiring for gesture module tests.
 *
 * In a real build, the community module system auto-generates calls to
 * the module hooks (pre_process_record_gestures, housekeeping_task_gestures,
 * keyboard_post_init_gestures). In tests, we wire them manually through
 * the _user hooks.
 */

#include "gesture.h"
#include "layer.h"
#include "quantum.h"

extern bool pre_process_record_gestures(uint16_t keycode, keyrecord_t *record);
extern void housekeeping_task_gestures(void);

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    return pre_process_record_gestures(keycode, record);
}

void keyboard_post_init_user(void) {
    layers_init();
    gestures_init();
}

void housekeeping_task_user(void) {
    housekeeping_task_gestures();
}
