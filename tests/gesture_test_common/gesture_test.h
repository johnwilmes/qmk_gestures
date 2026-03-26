/* Shared C utilities for gesture module tests.
 *
 * Provides macros for test gesture/layer definitions.
 * Include from test_gestures.c files.
 */

#pragma once

#include "gestures.h"
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

/* DEFINE_DENSE_LAYER(type_name, id, ...) and DEFINE_SPARSE_LAYER(type_name, id, ...)
 * are provided by layer.h. They create layer definitions with standardized names
 * for the weak default layer_get to find via the layer table. */
