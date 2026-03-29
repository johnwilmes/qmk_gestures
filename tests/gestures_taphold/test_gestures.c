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

/* Hold definitions */
DEFINE_HOLD(th_b, KEY_POS(0, 1));
DEFINE_HOLD(th_d, KEY_POS(0, 3));

enum { GS(th_b), GS(th_d) };
DEFINE_GESTURES_MANUAL(
    GESTURE_ENTRY(th_b),
    GESTURE_ENTRY(th_d),
);

/* Layer 0 key mappings: tap keycodes are in the base keymap */
DEFINE_DENSE_LAYER(base_keys,
    KC_A, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_K, KC_L, KC_M, KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T,
    KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z, KC_1, KC_2, KC_3, KC_4,
    KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_NO, KC_NO, KC_NO, KC_NO
);

/* Layer 0 gesture mappings */
DEFINE_GESTURE_LAYER(base_gestures,
    GESTURE_MAP(th_b, KC_LSFT),
    GESTURE_MAP(th_d, KC_LCTL),
);

DEFINE_LAYER_TABLE(
    [0] = { .key = &base_keys, .gesture = &base_gestures },
);
