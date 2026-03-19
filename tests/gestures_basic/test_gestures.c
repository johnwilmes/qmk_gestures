/* Minimal gesture/layer definitions for basic tests.
 * No gestures defined — all key events pass through immediately.
 */

#include "gesture_test.h"

static gesture_t gestures[1];

gesture_t *gesture_get(gesture_id_t index) {
    return &gestures[index];
}

uint16_t gesture_count(void) {
    return 0;
}

static const uint16_t PROGMEM layer0_map[] = {
    KC_A, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_K, KC_L, KC_M, KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T,
    KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z, KC_1, KC_2, KC_3, KC_4,
    KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_NO, KC_NO, KC_NO, KC_NO,
};
DEFINE_DENSE_LAYER(layer0, layer0_map);

const gesture_layer_t *layer_get(event_type_t type, uint8_t layer_id) {
    if (type == EVENT_TYPE_KEY && layer_id == 0) return &layer0;
    return NULL;
}

uint8_t layer_count(void) {
    return 1;
}
