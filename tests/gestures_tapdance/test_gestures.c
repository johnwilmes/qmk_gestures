/* Gesture and layer definitions for tap-dance tests.
 *
 * Layout (4x10 matrix, only first few keys used):
 *   (0,0) = plain key    → KC_A
 *   (0,1) = tapdance key → tap: KC_B (base), double-tap: KC_X, hold: KC_LSFT
 *   (0,2) = plain key    → KC_C
 *
 * Gestures (1 tapdance with max_presses=2, 3 outcomes):
 *   outcome 1: hold(1)   → KC_LSFT
 *   outcome 2: tap(2)    → KC_X (double-tap)
 *   outcome 3: hold(2)   → KC_LCTL (tap-then-hold)
 */

#include "gesture_test.h"

DEFINE_TAPDANCE(td, KEY_POS(0, 1), 2);

enum { GS(td) };
DEFINE_GESTURES_MANUAL(
    GESTURE_ENTRY(td),
);

/* Layer 0 key mappings: single tap keycode in base keymap */
DEFINE_DENSE_LAYER(base_keys,
    KC_A, KC_B, KC_C, KC_D, KC_E, KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_K, KC_L, KC_M, KC_N, KC_O, KC_P, KC_Q, KC_R, KC_S, KC_T,
    KC_U, KC_V, KC_W, KC_X, KC_Y, KC_Z, KC_1, KC_2, KC_3, KC_4,
    KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_NO, KC_NO, KC_NO, KC_NO
);

/* Layer 0 gesture mappings */
DEFINE_GESTURE_LAYER(base_gestures,
    GESTURE_MAP(td, KC_LSFT, KC_X, KC_LCTL),
);

DEFINE_LAYER_TABLE(
    [0] = { .key = &base_keys, .gesture = &base_gestures },
);
