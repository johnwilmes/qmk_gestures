/* Shared C utilities for gesture module tests.
 *
 * Provides macros for test gesture/layer definitions.
 * Include from test_gestures.c files.
 */

#pragma once

#include "gesture.h"
#include "layer.h"
#include "quantum.h"

/* Dense key index from matrix position.
 * Matches the identity mapping used by gesture_key_index in tests. */
#define KEY_POS(row, col) ((row) * MATRIX_COLS + (col))

/* Identity key index mapping for tests.
 * Defined as non-static so it's visible to gestures.c (which declares it extern).
 * The 'used' attribute prevents the linker from discarding it. */
__attribute__((used)) uint8_t gesture_key_index(keypos_t pos) {
    return pos.row * MATRIX_COLS + pos.col;
}

/* Define a dense layer from a PROGMEM keycode array covering all key positions.
 * Usage:
 *   static const uint16_t PROGMEM my_map[] = { KC_A, KC_B, ... };
 *   DEFINE_DENSE_LAYER(my_layer, my_map);
 */
#define DEFINE_DENSE_LAYER(name, map_array) \
    static const gesture_layer_t name = { \
        .type = LAYER_DENSE, \
        .default_keycode = KC_TRNS, \
        .dense = { \
            .base_index = 0, \
            .count = sizeof(map_array) / sizeof(uint16_t), \
            .map = (map_array), \
        }, \
    }

/* Define a sparse layer from a PROGMEM sparse_entry_t array.
 * Usage:
 *   static const sparse_entry_t PROGMEM my_entries[] = {
 *       {0, KC_LSFT}, {1, KC_B},
 *   };
 *   DEFINE_SPARSE_LAYER(my_layer, my_entries);
 */
#define DEFINE_SPARSE_LAYER(name, entries_array) \
    static const gesture_layer_t name = { \
        .type = LAYER_SPARSE, \
        .default_keycode = KC_TRNS, \
        .sparse = { \
            .count = sizeof(entries_array) / sizeof(sparse_entry_t), \
            .entries = (entries_array), \
        }, \
    }
