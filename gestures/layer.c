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

#include <string.h>
#include "gesture_internal.h"
#include "keycodes.h"
#include "quantum_keycodes.h"
#include "action.h"
#include "keyboard.h"
#include "timer.h"
#include "progmem.h"

/*******************************************************************************
 * Internal State
 ******************************************************************************/

static layer_system_t ls;

/*******************************************************************************
 * Forward Declarations
 ******************************************************************************/

static void emit_to_qmk(gesture_event_t event, uint16_t keycode);
static void emit_encoder_to_qmk(gesture_event_t event, uint16_t keycode);

/* Binding table operations */
static void     binding_store(event_type_t type, uint16_t event_id, uint16_t keycode);
static uint16_t binding_lookup_and_clear(event_type_t type, uint16_t event_id);

/* Layer lookup */
static uint16_t sparse_lookup(const sparse_layer_t *sl, uint16_t key_index, uint16_t default_keycode);
static uint16_t dense_lookup(const dense_layer_t *dl, uint16_t key_index, uint16_t default_keycode);

/*******************************************************************************
 * QMK Keymap Introspection Overrides
 ******************************************************************************/

uint8_t keymap_layer_count(void) {
    return layer_count();
}

uint16_t keycode_at_keymap_location(uint8_t layer_num, uint8_t row, uint8_t column) {
    const gesture_layer_t *layer = layer_get(EVENT_TYPE_KEY, layer_num);
    if (!layer) return KC_TRNS;
    keypos_t pos = { .row = row, .col = column };
    uint16_t event_id = gesture_key_index(pos);
    if (layer->type == LAYER_DENSE) {
        return dense_lookup(&layer->dense, event_id, layer->default_keycode);
    } else {
        return sparse_lookup(&layer->sparse, event_id, layer->default_keycode);
    }
}

/*******************************************************************************
 * Initialization
 ******************************************************************************/

void layers_init(void) {
    for (uint8_t i = 0; i < MAX_ACTIVE_BINDINGS; i++) {
        ls.bindings[i].keycode = KC_NO;
        ls.bindings[i].event_id = 0xFFFF;
    }
}

/*******************************************************************************
 * Layer Resolution
 ******************************************************************************/

uint16_t layer_resolve(event_type_t type, uint16_t event_id) {
    uint8_t count = layer_count();
    layer_state_t active = layer_state | default_layer_state;

    // Iterate active layers from highest to lowest
    for (int8_t i = count - 1; i >= 0; i--) {
        if (!(active & ((layer_state_t)1 << i))) {
            continue;  // Layer not active
        }

        const gesture_layer_t *layer = layer_get(type, i);
        if (!layer) {
            continue;  // No mapping for this event type on this layer
        }

        uint16_t keycode;

        if (layer->type == LAYER_DENSE) {
            keycode = dense_lookup(&layer->dense, event_id, layer->default_keycode);
        } else {
            keycode = sparse_lookup(&layer->sparse, event_id, layer->default_keycode);
        }

        if (keycode != KC_TRNS) {
            return keycode;
        }
    }

    return KC_TRNS;
}

static uint16_t dense_lookup(const dense_layer_t *dl, uint16_t key_index, uint16_t default_keycode) {
    uint16_t offset = key_index - dl->base_index;
    if (key_index >= dl->base_index && offset < dl->count) {
        return pgm_read_word(&dl->map[offset]);
    }
    return default_keycode;
}

static uint16_t sparse_lookup(const sparse_layer_t *sl, uint16_t key_index, uint16_t default_keycode) {
    for (uint16_t i = 0; i < sl->count; i++) {
        if (pgm_read_word(&sl->entries[i].key_index) == key_index) {
            return pgm_read_word(&sl->entries[i].keycode);
        }
    }
    return default_keycode;
}

/*******************************************************************************
 * Binding Table
 ******************************************************************************/

static void binding_store(event_type_t type, uint16_t event_id, uint16_t keycode) {
    for (uint8_t i = 0; i < MAX_ACTIVE_BINDINGS; i++) {
        if (ls.bindings[i].event_id == 0xFFFF) {
            ls.bindings[i].event_type = type;
            ls.bindings[i].event_id = event_id;
            ls.bindings[i].keycode = keycode;
            return;
        }
    }
    // Binding table full — evict first entry to prevent stuck keys.
    gesture_event_t release = {
        .event_id = ls.bindings[0].event_id,
        .type     = ls.bindings[0].event_type,
        .time     = timer_read(),
        .pressed  = false,
    };
    emit_to_qmk(release, ls.bindings[0].keycode);
    ls.bindings[0].event_type = type;
    ls.bindings[0].event_id = event_id;
    ls.bindings[0].keycode = keycode;
}

static uint16_t binding_lookup_and_clear(event_type_t type, uint16_t event_id) {
    for (uint8_t i = 0; i < MAX_ACTIVE_BINDINGS; i++) {
        if (ls.bindings[i].event_type == type &&
            ls.bindings[i].event_id == event_id) {
            uint16_t keycode = ls.bindings[i].keycode;
            ls.bindings[i].event_id = 0xFFFF;
            return keycode;
        }
    }
    return KC_NO;
}

/*******************************************************************************
 * QMK Reentry
 ******************************************************************************/

static void emit_to_qmk(gesture_event_t event, uint16_t keycode) {
    if (keycode == KC_TRNS || keycode == KC_NO) return;

    // Pack event_id into keypos_t so downstream process_record_user
    // can retrieve trigger data if needed.
    uint16_t packed_id = event.event_id;
    keypos_t packed_key;
    memcpy(&packed_key, &packed_id, sizeof(packed_key));
    keyevent_t qmk_event = {
        .key     = packed_key,
        .pressed = event.pressed,
        .time    = event.time,
        .type    = (event.type == EVENT_TYPE_GESTURE) ? COMBO_EVENT : KEY_EVENT,
    };

    keyrecord_t record = {
        .event   = qmk_event,
        .keycode = keycode,
    };
    process_record(&record);
}

static void emit_encoder_to_qmk(gesture_event_t event, uint16_t keycode) {
    if (keycode == KC_TRNS || keycode == KC_NO) return;

    uint8_t count = event.encoder.count;

    keyevent_t qmk_event = {
        .time = event.time,
        .type = event.encoder.clockwise ? ENCODER_CW_EVENT : ENCODER_CCW_EVENT,
        .key  = {
            .row = event.encoder.clockwise ? KEYLOC_ENCODER_CW : KEYLOC_ENCODER_CCW,
            .col = event.encoder.encoder_id,
        },
    };

    keyrecord_t record = {
        .event   = qmk_event,
        .keycode = keycode,
    };

    for (uint8_t i = 0; i < count; i++) {
        record.event.pressed = true;
        process_record(&record);
        record.event.pressed = false;
        process_record(&record);
    }
}

/*******************************************************************************
 * Main Entry Point: gesture_emit_event
 ******************************************************************************/

void gesture_emit_event(gesture_event_t event) {
    if (event.type == EVENT_TYPE_ENCODER) {
        // Transient: resolve and emit, no binding
        uint16_t enc_index = event.encoder.encoder_id * 2 +
                             (event.encoder.clockwise ? 0 : 1);
        uint16_t keycode = layer_resolve(EVENT_TYPE_ENCODER, enc_index);

        emit_encoder_to_qmk(event, keycode);
        return;
    }

    // Persistent event types (key, gesture): use binding table
    if (event.pressed) {
        uint16_t keycode = layer_resolve(event.type, event.event_id);
        binding_store(event.type, event.event_id, keycode);
        emit_to_qmk(event, keycode);
    } else {
        uint16_t keycode = binding_lookup_and_clear(event.type, event.event_id);
        emit_to_qmk(event, keycode);
    }
}
