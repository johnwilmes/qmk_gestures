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

/* Event IDs for gesture virtual keys */
enum gesture_events {
    HOLD_EVENTS(th_b),
    HOLD_EVENTS(th_d),
};

/* Hold definitions */
DEFINE_HOLD(th_b, KEY_POS(0, 1));
DEFINE_HOLD(th_d, KEY_POS(0, 3));

DEFINE_GESTURES(
    HOLD_GESTURE(th_b),  /* 0: hold → KC_LSFT */
    HOLD_GESTURE(th_d),  /* 1: hold → KC_LCTL */
);

/* Layer 0 key mappings: tap keycodes are in the base keymap */
DEFINE_DENSE_LAYER(key, 0,
    KC_A, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_K, KC_L, KC_M, KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T,
    KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z, KC_1, KC_2, KC_3, KC_4,
    KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_NO, KC_NO, KC_NO, KC_NO
);

/* Layer 0 gesture mappings: only hold keycodes */
DEFINE_SPARSE_LAYER(gesture, 0,
    {GE_HOLD_th_b, KC_LSFT},  /* th_b hold → left shift */
    {GE_HOLD_th_d, KC_LCTL}   /* th_d hold → left ctrl */
);
