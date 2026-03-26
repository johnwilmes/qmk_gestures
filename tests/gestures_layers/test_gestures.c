/* Gesture and layer definitions for layer system tests.
 *
 * Layout (4x10 matrix, only first few keys used):
 *   (0,0) = KC_A on layer 0, KC_1 on layer 1
 *   (0,1) = KC_B on layer 0, KC_2 on layer 1
 *   (0,2) = KC_C on layer 0, KC_TRNS on layer 1 (falls through to KC_C)
 *   (1,0) = hold: tap → KC_TAB (base), hold → MO(1) (gesture)
 *   (1,1) = hold: tap → KC_ESC (base), hold → TG(2) (gesture)
 *   Layer 2: KC_F1 for (0,0), KC_F2 for (0,1), rest transparent
 *
 * Gestures (2 total — single tap falls through to base keymap):
 *   0: mo1 hold  (trigger: key index 10, hold → MO(1))
 *   1: tg2 hold  (trigger: key index 11, hold → TG(2))
 */

#include "gesture_test.h"

/* Event IDs for gesture virtual keys */
enum gesture_events {
    HOLD_EVENTS(mo1),
    HOLD_EVENTS(tg2),
};

DEFINE_HOLD(mo1, KEY_POS(1, 0));
DEFINE_HOLD(tg2, KEY_POS(1, 1));

DEFINE_GESTURES(
    HOLD_GESTURE(mo1),  /* 0: hold → MO(1) */
    HOLD_GESTURE(tg2),  /* 1: hold → TG(2) */
);

/* --- Key layers --- */

DEFINE_DENSE_LAYER(key, 0,
    KC_A,   KC_B,   KC_C,   KC_D,   KC_E,  KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_TAB, KC_ESC, KC_K,   KC_L,   KC_M,  KC_N, KC_O, KC_P, KC_Q, KC_R,
    KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO
);

DEFINE_DENSE_LAYER(key, 1,
    KC_1,     KC_2,     KC_TRNS,  KC_4,     KC_5,     KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO
);

DEFINE_SPARSE_LAYER(key, 2,
    {KEY_POS(0, 0), KC_F1},
    {KEY_POS(0, 1), KC_F2}
);

/* --- Gesture layers --- */

DEFINE_SPARSE_LAYER(gesture, 0,
    {GE_HOLD_mo1, MO(1)},    /* mo1 hold → momentary layer 1 */
    {GE_HOLD_tg2, TG(2)}     /* tg2 hold → toggle layer 2 */
);
