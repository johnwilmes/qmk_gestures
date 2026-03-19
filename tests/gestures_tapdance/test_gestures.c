/* Gesture and layer definitions for tap-dance tests.
 *
 * Layout (4x10 matrix, only first few keys used):
 *   (0,0) = plain key    → KC_A
 *   (0,1) = tapdance key → tap: KC_B (base), double-tap: KC_X, hold: KC_LSFT
 *   (0,2) = plain key    → KC_C
 *
 * Gestures (3 total, using TAPDANCE_GESTURES with max_presses=2):
 *   0: td hold(1)   (trigger: key index 1, hold → KC_LSFT)
 *   1: td tap(2)    (trigger: key index 1, double-tap → KC_X)
 *   2: td hold(2)   (trigger: key index 1, tap-then-hold → KC_LCTL)
 */

#include "gesture_test.h"
#include "tapdance.h"

/* Event IDs for gesture virtual keys */
enum gesture_events {
    TAPDANCE_EVENTS(td, 2),
};

DEFINE_TAPDANCE(td, KEY_POS(0, 1), 2);

static gesture_t gestures[] = {
    TAPDANCE_GESTURE(td, 2),  /* single gesture: hold(1), tap(2), hold(2) */
};

gesture_t *gesture_get(gesture_id_t index) {
    return &gestures[index];
}

uint16_t gesture_count(void) {
    return sizeof(gestures) / sizeof(gesture_t);
}

/* Layer 0 key mappings: single tap keycode in base keymap */
static const uint16_t PROGMEM layer0_keys[] = {
    KC_A, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_K, KC_L, KC_M, KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T,
    KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z, KC_1, KC_2, KC_3, KC_4,
    KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_NO, KC_NO, KC_NO, KC_NO,
};
DEFINE_DENSE_LAYER(layer0_key_layer, layer0_keys);

/* Layer 0 gesture mappings */
static const sparse_entry_t PROGMEM layer0_gesture_entries[] = {
    {GE_TD_td_HOLD1, KC_LSFT},  /* hold(1) → left shift */
    {GE_TD_td_TAP2,  KC_X},     /* tap(2) → X (double-tap) */
    {GE_TD_td_HOLD2, KC_LCTL},  /* hold(2) → left ctrl (tap-then-hold) */
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
