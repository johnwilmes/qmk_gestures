/* Minimal gesture/layer definitions for basic tests.
 * No gestures defined — all key events pass through immediately.
 */

#include "gesture_test.h"

gesture_t *gesture_get(gesture_id_t index) {
    return NULL;
}

uint16_t gesture_count(void) {
    return 0;
}

DEFINE_DENSE_LAYER(key, 0,
    KC_A, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_K, KC_L, KC_M, KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T,
    KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z, KC_1, KC_2, KC_3, KC_4,
    KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_NO, KC_NO, KC_NO, KC_NO
);
