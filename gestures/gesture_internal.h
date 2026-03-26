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
#include "gesture_layers.h"

/*******************************************************************************
 * Buffered Event (internal to gesture system)
 ******************************************************************************/

/**
 * Buffered event with consumption tracking.
 */
typedef struct {
    gesture_event_t event;
    bool            is_consumed;
} gesture_buffered_event_t;

/*******************************************************************************
 * History Bitmap (internal to gesture system)
 ******************************************************************************/

/* Dense index mapping for press history bitmap.
 * Physical keys: 0..NUM_KEY_POSITIONS-1
 * Gesture virtual keys: GESTURE_OFFSET..GESTURE_OFFSET+NUM_GESTURE_EVENTS-1 */
#define GESTURE_OFFSET         NUM_KEY_POSITIONS
#define GESTURE_HISTORY_SIZE   ((GESTURE_OFFSET + NUM_GESTURE_EVENTS + 7) / 8)

/*******************************************************************************
 * Coordinator State
 ******************************************************************************/

/**
 * Central gesture coordinator state.
 *
 * Maintains event buffer, gesture queues, and timeout tracking.
 */
typedef struct {
    // Event buffer (circular)
    gesture_buffered_event_t buffer[GESTURE_BUFFER_SIZE];
    uint8_t                  buffer_head;       // Oldest event index
    uint8_t                  buffer_tail;       // Next write position
    uint8_t                  unprocessed_head;  // Next event for gesture processing

    // Gesture queues (linked lists via gesture_t.next)
    gesture_id_t             inactive_head;     // Unsorted
    gesture_id_t             partial_head;      // Ascending by ID (lowest priority first)
    gesture_id_t             active_head;       // Ordered by activation time (oldest first)
    gesture_id_t             disabled_head;     // Disabled gestures

    // Candidate gesture awaiting activation
    gesture_id_t             candidate;         // GESTURE_NULL_ID if none

    // Timeout tracking
    uint16_t                 next_timeout;      // Absolute time of next expiration (GESTURE_TIMEOUT_NEVER = none)

    // Press history bitmap (for release matching)
    uint8_t                  press_history[GESTURE_HISTORY_SIZE];
} gesture_coordinator_t;

/*******************************************************************************
 * Coordinator API (internal)
 ******************************************************************************/

/**
 * Initialize the gesture system.
 * Must be called before any other gesture functions.
 */
void gestures_init(void);

/**
 * Process an event through the gesture system.
 *
 * Main entry point for physical key events and encoder events. The caller
 * converts QMK events to gesture_event_t at the pipeline entry point.
 * Encoder events (EVENT_TYPE_ENCODER) are aggregated: consecutive
 * same-direction ticks are coalesced into a single buffer entry.
 *
 * @param event  The event to process (EVENT_TYPE_KEY or EVENT_TYPE_ENCODER)
 * @return       false (events are released from buffer asynchronously)
 */
bool gesture_process_event(gesture_event_t event);

/**
 * Periodic tick handler for gesture timeouts.
 *
 * Call this from matrix_scan_user or a similar periodic function.
 * Handles timeout-based gesture state transitions.
 */
void gesture_tick(void);

/**
 * Emit a resolved event to the layer system.
 *
 * Provided by the layer system (layer.c). Called when a physical
 * key event or encoder event is released from the buffer
 * (EVENT_TYPE_KEY, EVENT_TYPE_ENCODER) or when a gesture
 * activates/deactivates (EVENT_TYPE_GESTURE). The layer system resolves
 * the event to a keycode and hands it to QMK for execution. For encoder
 * events, the count field indicates how many ticks to emit.
 */
void gesture_emit_event(gesture_event_t event);

/*******************************************************************************
 * Layer System Internals
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

typedef struct {
    binding_entry_t bindings[MAX_ACTIVE_BINDINGS];
} layer_system_t;

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
