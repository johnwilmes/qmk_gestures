/* Gesture and layer definitions for tap-hold tests.
 *
 * Layout (4x10 matrix, only first few keys used):
 *   (0,0) = plain key    → KC_A
 *   (0,1) = hold key     → tap: KC_B (base), hold: KC_LSFT (gesture)
 *   (0,2) = plain key    → KC_C
 *   (0,3) = hold key     → tap: KC_D (base), hold: KC_LCTL (gesture)
 *
 * Gestures (2 total — single tap falls through to base keymap):
 *   0: th_b hold  (trigger: key index 1, hold → KC_LSFT)
 *   1: th_d hold  (trigger: key index 3, hold → KC_LCTL)
 */

#include "gesture_test.h"
#include "tapdance.h"

/* Hold definitions */
DEFINE_HOLD(th_b, KEY_POS(0, 1));
DEFINE_HOLD(th_d, KEY_POS(0, 3));

/* Gesture array */
static gesture_t gestures[] = {
    HOLD_GESTURE(th_b),  /* 0: hold → KC_LSFT */
    HOLD_GESTURE(th_d),  /* 1: hold → KC_LCTL */
};

gesture_t *gesture_get(gesture_id_t index) {
    return &gestures[index];
}

uint16_t gesture_count(void) {
    return sizeof(gestures) / sizeof(gesture_t);
}

/* Layer 0 key mappings: tap keycodes are in the base keymap */
static const uint16_t PROGMEM layer0_keys[] = {
    KC_A, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_K, KC_L, KC_M, KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T,
    KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z, KC_1, KC_2, KC_3, KC_4,
    KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_NO, KC_NO, KC_NO, KC_NO,
};
DEFINE_DENSE_LAYER(layer0_key_layer, layer0_keys);

/* Layer 0 gesture mappings: only hold keycodes */
static const sparse_entry_t PROGMEM layer0_gesture_entries[] = {
    {0, KC_LSFT},  /* th_b hold → left shift */
    {1, KC_LCTL},  /* th_d hold → left ctrl */
};
DEFINE_SPARSE_LAYER(layer0_gesture_layer, layer0_gesture_entries);

const gesture_layer_t *layer_get(event_type_t type, uint8_t layer_id) {
    if (layer_id != 0) return NULL;
    switch (type) {
        case EVENT_TYPE_KEY:     return &layer0_key_layer;
        case EVENT_TYPE_GESTURE: return &layer0_gesture_layer;
        default:                 return NULL;
    }
}

uint8_t layer_count(void) {
    return 1;
}
