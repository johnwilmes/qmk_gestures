/* Copyright 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "gesture_api.h"
#include "action_layer.h"
#include "progmem.h"

/*******************************************************************************
 * Configuration
 ******************************************************************************/

#ifndef MAX_ACTIVE_BINDINGS
#    define MAX_ACTIVE_BINDINGS 16
#endif

/*******************************************************************************
 * Key Layer Types (physical keys — dense or sparse)
 ******************************************************************************/

typedef struct {
    uint16_t key_index;
    uint16_t keycode;
} key_entry_t;

/**
 * Dense key layer: flat keycode array covering a contiguous range of key indices.
 * Suitable for physical matrix layers where every key has a mapping.
 */
typedef struct {
    uint16_t       base_index;
    uint16_t       count;
    const uint16_t *map;       // PROGMEM: indexed by (key_index - base_index)
} dense_key_layer_t;

/**
 * Sparse key layer: array of (key_index, keycode) pairs.
 * Suitable for overlay layers where few keys have non-transparent mappings.
 */
typedef struct {
    uint16_t          count;
    const key_entry_t *entries;  // PROGMEM
} sparse_key_layer_t;

typedef enum {
    LAYER_DENSE,
    LAYER_SPARSE,
} key_layer_type_t;

/**
 * A key layer definition. Tagged union of dense or sparse format.
 * default_keycode applies to keys not explicitly mapped:
 *   KC_TRNS (default) = fall through to lower layers
 *   KC_NO             = block (unmapped keys do nothing)
 */
typedef struct {
    key_layer_type_t type;
    uint16_t         default_keycode;
    union {
        dense_key_layer_t  dense;
        sparse_key_layer_t sparse;
    };
} key_layer_t;

/*******************************************************************************
 * Gesture Layer Types (per-gesture outcome grouping)
 ******************************************************************************/

/**
 * Dense gesture layer: pointer array indexed by gesture_id.
 * Each pointer -> array of keycodes for that gesture's outcomes (1-indexed).
 * NULL pointer = unmapped (returns default_keycode).
 * Array sized to GESTURE_COUNT (from DEFINE_GESTURES / DEFINE_GESTURES_MANUAL).
 */
typedef struct {
    const uint16_t *const *map;     // map[gesture_id] -> keycode array, NULL = unmapped
} dense_gesture_layer_t;

/**
 * Sparse gesture layer entry: a gesture and its outcome keycodes.
 */
typedef struct {
    uint16_t         gesture_id;
    const uint16_t  *keycodes;     // keycode array (outcome 1 at index 0)
} sparse_gesture_entry_t;

/**
 * Sparse gesture layer: array of entries, linear scan lookup.
 * Suitable for overlay layers where few gestures are remapped.
 */
typedef struct {
    uint16_t                       count;
    const sparse_gesture_entry_t  *entries;
} sparse_gesture_layer_t;

typedef enum {
    GESTURE_LAYER_DENSE,
    GESTURE_LAYER_SPARSE,
} gesture_layer_type_t;

/**
 * A gesture layer definition. Tagged union of dense or sparse format.
 * default_keycode applies to unmapped gestures:
 *   KC_TRNS (default) = fall through to lower layers
 *   KC_NO             = block (unmapped gestures do nothing)
 */
typedef struct {
    gesture_layer_type_t type;
    uint16_t             default_keycode;
    union {
        dense_gesture_layer_t  dense;
        sparse_gesture_layer_t sparse;
    };
} gesture_layer_t;

/*******************************************************************************
 * Encoder Layer Types (paired CW/CCW keycodes)
 ******************************************************************************/

/**
 * A single encoder mapping: one keycode per direction.
 */
typedef struct {
    uint8_t  encoder_id;
    uint16_t ccw;
    uint16_t cw;
} encoder_entry_t;

typedef struct {
    uint16_t               count;
    const encoder_entry_t  *entries;  // PROGMEM
} encoder_layer_t;

/*******************************************************************************
 * Layer Table Entry
 ******************************************************************************/

/**
 * Per-layer entry in the layer table. Each field is a pointer to a
 * type-specific layer (or NULL for no mapping of that event type).
 */
typedef struct {
    const key_layer_t     *key;
    const gesture_layer_t *gesture;
    const encoder_layer_t *encoder;
} layer_entry_t;

/*******************************************************************************
 * User-Provided Functions
 ******************************************************************************/

/**
 * Map a QMK matrix position to a dense key index (0..gesture_key_count()-1).
 * Called at pipeline entry and by the keymap introspection override.
 *
 * Provide your own implementation or use DEFINE_KEY_INDICES with a
 * generated gesture_macros.h to auto-generate this function.
 */
extern uint8_t gesture_key_index(keypos_t pos);

/**
 * Return the number of physical key positions.
 * Default provided by DEFINE_KEY_INDICES; override for custom mappings.
 */
uint16_t gesture_key_count(void);

/*******************************************************************************
 * Key Index Macros
 ******************************************************************************/

/**
 * Helper macros for DEFINE_KEY_INDICES. Used as callbacks to
 * GESTURE_LAYOUT_CALL (generated by gen_macros.py --layout).
 */
#define _KP_ENUM(name, r, c) name,
#define _KP_FORWARD(name, r, c) [(r) * MATRIX_COLS + (c)] = (name) + 1,
#define _KP_REVERSE(name, r, c) [name] = { .row = (r), .col = (c) },

/**
 * Define key index enum, forward lookup, reverse lookup,
 * gesture_key_index, and gesture_key_count from a GESTURE_LAYOUT_CALL-style
 * layout macro.
 *
 * Requires gesture_macros.h (generated by gen_macros.py --layout) which
 * provides GESTURE_LAYOUT_CALL. NUM_KEY_POSITIONS is computed as the
 * enum count.
 */
#define DEFINE_KEY_INDICES(...) \
    enum { GESTURE_LAYOUT_CALL(_KP_ENUM, __VA_ARGS__) NUM_KEY_POSITIONS }; \
    static const uint8_t PROGMEM _gesture_pos_to_index \
        [MATRIX_ROWS * MATRIX_COLS] = { \
        GESTURE_LAYOUT_CALL(_KP_FORWARD, __VA_ARGS__) \
    }; \
    static const keypos_t PROGMEM gesture_index_to_pos \
        [NUM_KEY_POSITIONS] = { \
        GESTURE_LAYOUT_CALL(_KP_REVERSE, __VA_ARGS__) \
    }; \
    uint8_t gesture_key_index(keypos_t pos) { \
        return pgm_read_byte( \
            &_gesture_pos_to_index[pos.row * MATRIX_COLS + pos.col]) - 1; \
    } \
    uint16_t gesture_key_count(void) { return NUM_KEY_POSITIONS; }

/*******************************************************************************
 * Layer Lookup Functions
 ******************************************************************************/

/**
 * Return the key layer for the given layer number. May return NULL.
 * Default provided by DEFINE_LAYER_TABLE.
 */
const key_layer_t *key_layer_get(uint8_t layer_id);

/**
 * Return the gesture layer for the given layer number. May return NULL.
 * Default provided by DEFINE_LAYER_TABLE.
 */
const gesture_layer_t *gesture_layer_get(uint8_t layer_id);

/**
 * Return the encoder layer for the given layer number. May return NULL.
 * Default provided by DEFINE_LAYER_TABLE.
 */
const encoder_layer_t *encoder_layer_get(uint8_t layer_id);

/**
 * Return the total number of layers.
 * Default provided by DEFINE_LAYER_TABLE.
 */
uint8_t layer_count(void);

/*******************************************************************************
 * Key Layer Definition Macros
 ******************************************************************************/

/**
 * Define a dense key layer.
 *
 *   DEFINE_DENSE_LAYER(base_keys, KC_A, KC_B, KC_C, ...)
 */
#define DEFINE_DENSE_LAYER(name, ...) \
    static const uint16_t PROGMEM _gl_##name##_map[] = { __VA_ARGS__ }; \
    static const key_layer_t name = { \
        .type = LAYER_DENSE, \
        .default_keycode = KC_TRNS, \
        .dense = { \
            .base_index = 0, \
            .count = sizeof(_gl_##name##_map) / sizeof(uint16_t), \
            .map = _gl_##name##_map, \
        }, \
    }

/**
 * Define a sparse key layer.
 *
 *   DEFINE_SPARSE_LAYER(nav_keys, {0, KC_LEFT}, {1, KC_DOWN})
 */
#define DEFINE_SPARSE_LAYER(name, ...) \
    static const key_entry_t PROGMEM _gl_##name##_entries[] = { __VA_ARGS__ }; \
    static const key_layer_t name = { \
        .type = LAYER_SPARSE, \
        .default_keycode = KC_TRNS, \
        .sparse = { \
            .count = sizeof(_gl_##name##_entries) / sizeof(key_entry_t), \
            .entries = _gl_##name##_entries, \
        }, \
    }

/*******************************************************************************
 * Gesture Layer Definition Macros
 ******************************************************************************/

/**
 * Map a gesture's outcome keycodes in a dense gesture layer.
 * Keycodes listed in outcome order (outcome 1 first).
 *
 *   GESTURE_MAP(home_a,    KC_LGUI)              // 1 outcome (hold/combo)
 *   GESTURE_MAP(my_td,     KC_LSFT, KC_X, KC_LCTL)  // 3 outcomes (tapdance)
 */
#define GESTURE_MAP(name, ...) \
    [GS_##name] = (const uint16_t[]){ __VA_ARGS__ }

/**
 * Define a dense gesture layer (default). Array sized to GESTURE_COUNT.
 * Use GESTURE_MAP entries. Unmapped gestures get NULL (= default_keycode).
 *
 *   DEFINE_GESTURE_LAYER(base_gestures,
 *       GESTURE_MAP(home_a,    KC_LGUI),
 *       GESTURE_MAP(esc_combo, KC_ESC),
 *       GESTURE_MAP(my_td,     KC_LSFT, KC_X, KC_LCTL),
 *   );
 */
#define DEFINE_GESTURE_LAYER(name, ...) \
    static const uint16_t *const _gl_##name##_map[GESTURE_COUNT] = { __VA_ARGS__ }; \
    static const gesture_layer_t name = { \
        .type = GESTURE_LAYER_DENSE, \
        .default_keycode = KC_TRNS, \
        .dense = { .map = _gl_##name##_map }, \
    }

/**
 * Map a gesture's outcome keycodes in a sparse gesture layer.
 * Keycodes listed in outcome order (outcome 1 first).
 *
 *   GESTURE_SPARSE_MAP(home_a, KC_HOME)
 */
#define GESTURE_SPARSE_MAP(name, ...) { \
    .gesture_id = GS_##name, \
    .keycodes = (const uint16_t[]){ __VA_ARGS__ } \
}

/**
 * Define a sparse gesture layer (overlay). Only stores mapped gestures.
 *
 *   DEFINE_SPARSE_GESTURE_LAYER(nav_gestures,
 *       GESTURE_SPARSE_MAP(home_a, KC_HOME),
 *       GESTURE_SPARSE_MAP(home_s, KC_END),
 *   );
 */
#define DEFINE_SPARSE_GESTURE_LAYER(name, ...) \
    static const sparse_gesture_entry_t _gl_##name##_entries[] = { __VA_ARGS__ }; \
    static const gesture_layer_t name = { \
        .type = GESTURE_LAYER_SPARSE, \
        .default_keycode = KC_TRNS, \
        .sparse = { \
            .count = sizeof(_gl_##name##_entries) / sizeof(sparse_gesture_entry_t), \
            .entries = _gl_##name##_entries, \
        }, \
    }

/*******************************************************************************
 * Encoder Layer Definition Macros
 ******************************************************************************/

/**
 * Define an encoder layer mapping encoders to CW/CCW keycodes.
 *
 *   DEFINE_ENCODER_LAYER(base_encoders,
 *       ENCODER_MAP(0, KC_VOLD, KC_VOLU),
 *   );
 */
#define DEFINE_ENCODER_LAYER(name, ...) \
    static const encoder_entry_t PROGMEM _gl_##name##_entries[] = { __VA_ARGS__ }; \
    static const encoder_layer_t name = { \
        .count = sizeof(_gl_##name##_entries) / sizeof(encoder_entry_t), \
        .entries = _gl_##name##_entries, \
    }

/** Map an encoder to CCW/CW keycodes. */
#define ENCODER_MAP(id, ccw_kc, cw_kc) \
    { .encoder_id = (id), .ccw = (ccw_kc), .cw = (cw_kc) }

/*******************************************************************************
 * Layer Table
 ******************************************************************************/

/**
 * Define the layer table and provide layer lookup functions.
 *
 *   DEFINE_LAYER_TABLE(
 *       [BASE] = { .key = &base_keys, .gesture = &base_gestures },
 *       [NAV]  = { .key = &nav_keys },
 *   );
 */
#define DEFINE_LAYER_TABLE(...) \
    static const layer_entry_t _gl_table[] = { __VA_ARGS__ }; \
    const key_layer_t *key_layer_get(uint8_t layer_id) { \
        if (layer_id >= sizeof(_gl_table) / sizeof(layer_entry_t)) \
            return NULL; \
        return _gl_table[layer_id].key; \
    } \
    const gesture_layer_t *gesture_layer_get(uint8_t layer_id) { \
        if (layer_id >= sizeof(_gl_table) / sizeof(layer_entry_t)) \
            return NULL; \
        return _gl_table[layer_id].gesture; \
    } \
    const encoder_layer_t *encoder_layer_get(uint8_t layer_id) { \
        if (layer_id >= sizeof(_gl_table) / sizeof(layer_entry_t)) \
            return NULL; \
        return _gl_table[layer_id].encoder; \
    } \
    uint8_t layer_count(void) { \
        return sizeof(_gl_table) / sizeof(layer_entry_t); \
    }
