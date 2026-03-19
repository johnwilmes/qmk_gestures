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

#include "gesture.h"
#include "action_layer.h"

/*******************************************************************************
 * Configuration
 ******************************************************************************/

#ifndef NUM_LAYERS
#    define NUM_LAYERS 8
#endif

#ifndef MAX_ACTIVE_BINDINGS
#    define MAX_ACTIVE_BINDINGS 16
#endif

/*******************************************************************************
 * Layer Definition Types
 ******************************************************************************/

typedef struct {
    uint16_t key_index;
    uint16_t keycode;
} sparse_entry_t;

/**
 * Dense layer: flat keycode array covering a contiguous range of key indices.
 * Suitable for physical matrix layers where every key has a mapping.
 * The map array is stored in PROGMEM.
 */
typedef struct {
    uint16_t       base_index;
    uint16_t       count;
    const uint16_t *map;       // PROGMEM: indexed by (event_id - base_index)
} dense_layer_t;

/**
 * Sparse layer: sorted array of (key_index, keycode) pairs.
 * Suitable for virtual key overrides where few keys have non-transparent
 * mappings on a given layer. Entries are sorted by key_index for binary search.
 * The entries array is stored in PROGMEM.
 */
typedef struct {
    uint16_t             count;
    const sparse_entry_t *entries;  // PROGMEM: sorted by key_index
} sparse_layer_t;

typedef enum {
    LAYER_DENSE,
    LAYER_SPARSE,
} layer_type_t;

/**
 * A layer definition. Tagged union of dense or sparse format.
 * default_keycode applies to keys not explicitly mapped:
 *   KC_TRNS (default) = fall through to lower layers
 *   KC_NO             = block (unmapped keys do nothing)
 */
typedef struct {
    layer_type_t type;
    uint16_t     default_keycode;
    union {
        dense_layer_t  dense;
        sparse_layer_t sparse;
    };
} gesture_layer_t;

/*******************************************************************************
 * Binding Table
 ******************************************************************************/

/**
 * Tracks which keycode was resolved at press time, so the matching release
 * always undoes the same keycode regardless of layer changes in between.
 *
 * Layer keycodes (MO, TG, etc.) also use bindings so the release knows
 * which layer to deactivate.
 */
typedef struct {
    event_type_t event_type;
    uint16_t     event_id;
    uint16_t     keycode;
} binding_entry_t;

/*******************************************************************************
 * Layer System State
 ******************************************************************************/

typedef struct {
    layer_state_t  layer_state;      // Bitmask of active layers (layer 0 always active)
    uint8_t        default_layer;    // Base layer (DF target)
    binding_entry_t bindings[MAX_ACTIVE_BINDINGS];

    // One-shot layer state
    uint8_t        osl_layer;        // Active one-shot layer (0xFF = none)
    bool           osl_held;         // OSL key is physically held
    bool           osl_used;         // A key was pressed while OSL was active
} layer_system_t;

/*******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * Initialize the layer system. Call from keyboard_post_init_user.
 */
void layers_init(void);

/**
 * Resolve a key event to a keycode by iterating active layers.
 *
 * @param type      Event type namespace
 * @param event_id  Dense index within that namespace
 * @return          Resolved keycode (KC_TRNS if no layer has a mapping)
 */
uint16_t layer_resolve(event_type_t type, uint16_t event_id);

/**
 * Check if a keycode is a layer management keycode (MO, TG, DF, OSL).
 */
bool is_layer_keycode(uint16_t keycode);

/*******************************************************************************
 * User-Provided Functions
 ******************************************************************************/

/**
 * Return the layer definition for the given event type and layer number.
 * May return NULL if the layer has no mapping for this event type
 * (treated as all-transparent).
 */
extern const gesture_layer_t *layer_get(event_type_t type, uint8_t layer_id);

/**
 * Return the total number of layers.
 */
extern uint8_t layer_count(void);
