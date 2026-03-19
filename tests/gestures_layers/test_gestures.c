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
#include "tapdance.h"

DEFINE_HOLD(mo1, KEY_POS(1, 0));
DEFINE_HOLD(tg2, KEY_POS(1, 1));

static gesture_t gestures[] = {
    HOLD_GESTURE(mo1),  /* 0: hold → MO(1) */
    HOLD_GESTURE(tg2),  /* 1: hold → TG(2) */
};

gesture_t *gesture_get(gesture_id_t index) {
    return &gestures[index];
}

uint16_t gesture_count(void) {
    return sizeof(gestures) / sizeof(gesture_t);
}

/* --- Key layers --- */

static const uint16_t PROGMEM layer0_keys[] = {
    KC_A,   KC_B,   KC_C,   KC_D,   KC_E,  KC_F, KC_G, KC_H, KC_I, KC_J,
    KC_TAB, KC_ESC, KC_K,   KC_L,   KC_M,  KC_N, KC_O, KC_P, KC_Q, KC_R,
    KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
};
DEFINE_DENSE_LAYER(layer0_key, layer0_keys);

static const uint16_t PROGMEM layer1_keys[] = {
    KC_1,     KC_2,     KC_TRNS,  KC_4,     KC_5,     KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,
};
DEFINE_DENSE_LAYER(layer1_key, layer1_keys);

static const sparse_entry_t PROGMEM layer2_key_entries[] = {
    {KEY_POS(0, 0), KC_F1},
    {KEY_POS(0, 1), KC_F2},
};
DEFINE_SPARSE_LAYER(layer2_key, layer2_key_entries);

/* --- Gesture layers --- */

static const sparse_entry_t PROGMEM layer0_gesture_entries[] = {
    {0, MO(1)},    /* mo1 hold → momentary layer 1 */
    {1, TG(2)},    /* tg2 hold → toggle layer 2 */
};
DEFINE_SPARSE_LAYER(layer0_gesture, layer0_gesture_entries);

const gesture_layer_t *layer_get(event_type_t type, uint8_t layer_id) {
    if (type == EVENT_TYPE_KEY) {
        switch (layer_id) {
            case 0: return &layer0_key;
            case 1: return &layer1_key;
            case 2: return &layer2_key;
            default: return NULL;
        }
    }
    if (type == EVENT_TYPE_GESTURE && layer_id == 0) {
        return &layer0_gesture;
    }
    return NULL;
}

uint8_t layer_count(void) {
    return 3;
}
