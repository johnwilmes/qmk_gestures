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

#include "layer.h"
#include "keycodes.h"
#include "quantum_keycodes.h"
#include "action.h"
#include "keyboard.h"
#include "timer.h"
#include "progmem.h"

static layer_system_t ls;

/*******************************************************************************
 * Forward Declarations
 ******************************************************************************/

static void handle_layer_press(uint16_t keycode);
static void handle_layer_release(uint16_t keycode);
static void sync_layer_state(void);
static void emit_to_qmk(gesture_event_t event, uint16_t keycode);
static void emit_encoder_to_qmk(gesture_event_t event, uint16_t keycode);

/* Binding table operations */
static void     binding_store(event_type_t type, uint16_t event_id, uint16_t keycode);
static uint16_t binding_lookup_and_clear(event_type_t type, uint16_t event_id);

/* Sparse layer binary search */
static uint16_t sparse_lookup(const sparse_layer_t *sl, uint16_t key_index);

/*******************************************************************************
 * Initialization
 ******************************************************************************/

void layers_init(void) {
    ls.default_layer = 0;
    ls.layer_state = 1;  // Layer 0 always active
    ls.osl_layer = 0xFF;

    for (uint8_t i = 0; i < MAX_ACTIVE_BINDINGS; i++) {
        ls.bindings[i].keycode = KC_NO;
        ls.bindings[i].event_id = 0xFFFF;
    }

    sync_layer_state();
}

/*******************************************************************************
 * Layer Resolution
 ******************************************************************************/

uint16_t layer_resolve(event_type_t type, uint16_t event_id) {
    uint8_t count = layer_count();

    // Iterate active layers from highest to lowest
    for (int8_t i = count - 1; i >= 0; i--) {
        if (!(ls.layer_state & ((layer_state_t)1 << i))) {
            continue;  // Layer not active
        }

        const gesture_layer_t *layer = layer_get(type, i);
        if (!layer) {
            continue;  // No mapping for this event type on this layer
        }

        uint16_t keycode;

        if (layer->type == LAYER_DENSE) {
            uint16_t offset = event_id - layer->dense.base_index;
            if (event_id >= layer->dense.base_index &&
                offset < layer->dense.count) {
                keycode = pgm_read_word(&layer->dense.map[offset]);
            } else {
                keycode = layer->default_keycode;
            }
        } else {
            keycode = sparse_lookup(&layer->sparse, event_id);
            if (keycode == KC_TRNS) {
                keycode = layer->default_keycode;
            }
        }

        if (keycode != KC_TRNS) {
            return keycode;
        }
    }

    return KC_TRNS;
}

static uint16_t sparse_lookup(const sparse_layer_t *sl, uint16_t key_index) {
    for (uint16_t i = 0; i < sl->count; i++) {
        if (pgm_read_word(&sl->entries[i].key_index) == key_index) {
            return pgm_read_word(&sl->entries[i].keycode);
        }
    }
    return KC_TRNS;
}

/*******************************************************************************
 * Layer Keycode Detection
 ******************************************************************************/

bool is_layer_keycode(uint16_t keycode) {
    return IS_QK_MOMENTARY(keycode) ||
           IS_QK_TOGGLE_LAYER(keycode) ||
           IS_QK_DEF_LAYER(keycode) ||
           IS_QK_ONE_SHOT_LAYER(keycode);
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
    uint16_t evicted_keycode = ls.bindings[0].keycode;
    gesture_event_t release = {
        .event_id = ls.bindings[0].event_id,
        .type     = ls.bindings[0].event_type,
        .time     = timer_read(),
        .pressed  = false,
    };
    if (is_layer_keycode(evicted_keycode)) {
        handle_layer_release(evicted_keycode);
    } else {
        emit_to_qmk(release, evicted_keycode);
    }
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
 * Layer State Management
 ******************************************************************************/

static void layer_activate(uint8_t layer_id) {
    ls.layer_state |= (layer_state_t)1 << layer_id;
    sync_layer_state();
}

static void layer_deactivate(uint8_t layer_id) {
    if (layer_id == ls.default_layer) return;  // Can't deactivate base layer
    ls.layer_state &= ~((layer_state_t)1 << layer_id);
    sync_layer_state();
}

static void layer_toggle(uint8_t layer_id) {
    ls.layer_state ^= (layer_state_t)1 << layer_id;
    sync_layer_state();
}

static void set_default_layer(uint8_t layer_id) {
    // Deactivate old default, activate new
    ls.layer_state &= ~((layer_state_t)1 << ls.default_layer);
    ls.default_layer = layer_id;
    ls.layer_state |= (layer_state_t)1 << layer_id;
    sync_layer_state();
    default_layer_set((layer_state_t)1 << layer_id);
}

static void sync_layer_state(void) {
    layer_state_set(ls.layer_state);
}

/*******************************************************************************
 * One-Shot Layer
 *
 * Press OSL → layer activates. Next non-layer keypress deactivates it.
 * Hold-as-MO behavior belongs in the gesture system (tap/hold distinction),
 * not here.
 ******************************************************************************/

static void osl_activate(uint8_t layer_id) {
    ls.osl_layer = layer_id;
    layer_activate(layer_id);
}

static void osl_deactivate(void) {
    if (ls.osl_layer != 0xFF) {
        layer_deactivate(ls.osl_layer);
        ls.osl_layer = 0xFF;
    }
}

/* Called after emitting a non-layer keycode press. */
static void osl_on_keypress(void) {
    if (ls.osl_layer == 0xFF) return;
    osl_deactivate();
}

/*******************************************************************************
 * Layer Keycode Handling
 ******************************************************************************/

static void handle_layer_press(uint16_t keycode) {
    if (IS_QK_MOMENTARY(keycode)) {
        layer_activate(QK_MOMENTARY_GET_LAYER(keycode));
    } else if (IS_QK_TOGGLE_LAYER(keycode)) {
        layer_toggle(QK_TOGGLE_LAYER_GET_LAYER(keycode));
    } else if (IS_QK_DEF_LAYER(keycode)) {
        set_default_layer(QK_DEF_LAYER_GET_LAYER(keycode));
    } else if (IS_QK_ONE_SHOT_LAYER(keycode)) {
        osl_activate(QK_ONE_SHOT_LAYER_GET_LAYER(keycode));
    }
}

static void handle_layer_release(uint16_t keycode) {
    if (IS_QK_MOMENTARY(keycode)) {
        layer_deactivate(QK_MOMENTARY_GET_LAYER(keycode));
    }
    // TG, DF, OSL: no release action
}

/*******************************************************************************
 * QMK Reentry
 ******************************************************************************/

static void emit_to_qmk(gesture_event_t event, uint16_t keycode) {
    if (keycode == KC_TRNS || keycode == KC_NO) return;

    // Pack event_id into keypos_t so downstream process_record_user
    // can retrieve trigger data if needed.
    uint16_t packed_id = event.event_id;
    keyevent_t qmk_event = {
        .key     = *(keypos_t *)&packed_id,
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
        // Transient: resolve and emit N press+release pairs, no binding
        uint16_t enc_index = event.encoder.encoder_id * 2 +
                             (event.encoder.clockwise ? 0 : 1);
        uint16_t keycode = layer_resolve(EVENT_TYPE_ENCODER, enc_index);

        // MO(n) requires a release to deactivate — disallow on encoders
        if (IS_QK_MOMENTARY(keycode)) return;

        if (is_layer_keycode(keycode)) {
            handle_layer_press(keycode);
            return;
        }

        emit_encoder_to_qmk(event, keycode);
        osl_on_keypress();
        return;
    }

    // Persistent event types (key, gesture): use binding table
    if (event.pressed) {
        uint16_t keycode = layer_resolve(event.type, event.event_id);
        binding_store(event.type, event.event_id, keycode);

        if (is_layer_keycode(keycode)) {
            handle_layer_press(keycode);
            return;
        }

        emit_to_qmk(event, keycode);
        osl_on_keypress();
    } else {
        uint16_t keycode = binding_lookup_and_clear(event.type, event.event_id);

        if (is_layer_keycode(keycode)) {
            handle_layer_release(keycode);
            return;
        }

        emit_to_qmk(event, keycode);
    }
}
