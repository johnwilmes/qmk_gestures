/* Shared C++ utilities for gesture module tests.
 *
 * Provides common includes and helpers for test .cpp files.
 * Does NOT include gesture_test.h (which defines gesture_key_index
 * and layer macros for the C test_gestures.c files).
 */

#pragma once

#include <utility>
#include <vector>

#include "keyboard_report_util.hpp"
#include "keycode.h"
#include "test_common.hpp"
#include "test_fixture.hpp"
#include "test_keymap_key.hpp"

using testing::_;
using testing::InSequence;

/* Dense key index from matrix position (matches gesture_key_index). */
#define KEY_POS(row, col) ((row) * MATRIX_COLS + (col))

/* Build a keymap with KC_NO entries on every layer for each position.
 *
 * QMK's test fixture calls keymap_key_to_keycode for the currently active
 * layer before pre_process_record can intercept the event. Without entries
 * for every layer, the fixture FAILs with "no key is mapped". We register
 * KC_NO on all layers so that lookup succeeds (the gesture module ignores
 * QMK keycodes anyway).
 *
 * Returns a vector of KeymapKey. Layer 0 keys come first in position order,
 * so keys[i] corresponds to positions[i] on layer 0 (use these for
 * press/release in tests).
 *
 * Usage (inside TEST_F):
 *   auto keys = gesture_keymap({{0,0}, {0,1}, {1,0}}, 3);
 *   auto& key_a = keys[0];
 */
inline std::vector<KeymapKey> gesture_keymap(
        std::initializer_list<std::pair<uint8_t,uint8_t>> positions,
        uint8_t num_layers) {
    std::vector<KeymapKey> keys;
    /* Layer 0 keys first (these are the ones tests press/release). */
    for (auto [row, col] : positions) {
        keys.push_back(KeymapKey(0, col, row, KC_NO));
    }
    /* Additional layers so QMK's keycode lookup doesn't FAIL. */
    for (uint8_t layer = 1; layer < num_layers; layer++) {
        for (auto [row, col] : positions) {
            keys.push_back(KeymapKey(layer, col, row, KC_NO));
        }
    }
    return keys;
}

/* Register all keys from a gesture_keymap vector with the test fixture.
 * Use inside TEST_F body after calling gesture_keymap. */
#define SET_GESTURE_KEYMAP(keys) \
    do { for (auto& k : (keys)) add_key(k); } while (0)
