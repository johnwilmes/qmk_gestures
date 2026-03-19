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

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Configuration
 ******************************************************************************/

/* Maximum number of gestures supported */
#ifndef MAX_GESTURES
#    define MAX_GESTURES 64
#endif

/* Number of physical key positions (dense index 0..NUM_KEY_POSITIONS-1).
 * Defaults to the full matrix size. Override if the user's key index
 * mapping uses fewer positions. */
#ifndef NUM_KEY_POSITIONS
#    define NUM_KEY_POSITIONS (MATRIX_ROWS * MATRIX_COLS)
#endif

/* Event buffer size - increase for complex sequences (leader key, tap dance) */
#ifndef GESTURE_BUFFER_SIZE
#    define GESTURE_BUFFER_SIZE 12
#endif

/* Maximum number of gesture events (virtual key IDs).
 * Defaults to MAX_GESTURES. Override when multi-outcome gestures need more IDs. */
#ifndef NUM_GESTURE_EVENTS
#    define NUM_GESTURE_EVENTS MAX_GESTURES
#endif

/* Null gesture ID for linked list termination (must fit in 10-bit next field) */
#define GESTURE_NULL_ID ((gesture_id_t)0x3FF)

/* Timeout value meaning "never timeout" */
#define GESTURE_TIMEOUT_NEVER ((uint16_t)0xFFFF)

/*******************************************************************************
 * Event Types
 ******************************************************************************/

/**
 * Event type namespace. Each type has its own dense index space.
 * Used by both the gesture system and the layer system.
 */
typedef enum {
    EVENT_TYPE_KEY,      // Physical key: event_id is dense key index (0..NUM_KEY_POSITIONS-1)
    EVENT_TYPE_GESTURE,  // Virtual gesture: event_id is gesture ID (0..MAX_GESTURES-1)
    EVENT_TYPE_ENCODER,  // Encoder action: encoder field has id/direction/count
    EVENT_TYPE_COUNT,
} event_type_t;

/**
 * Unified event type used throughout the gesture and layer systems.
 *
 * Physical key events (EVENT_TYPE_KEY) are created at the pipeline entry
 * point by mapping QMK's (row, col) to a dense key index via user-defined
 * mapping. Gesture events (EVENT_TYPE_GESTURE) are created internally when
 * gestures activate/deactivate. Encoder events (EVENT_TYPE_ENCODER) are
 * created at the pipeline entry point and flow through the gesture system.
 *
 * For encoder events, the event_id is a packed representation accessible
 * via the encoder union member: encoder_id (7 bits), clockwise (1 bit),
 * and count (8 bits, for aggregation of consecutive same-direction ticks).
 *
 * Encoder events are press-only (no corresponding release). They can
 * trigger and participate in gestures, and are emitted without press
 * history tracking. The layer system expands the count into N
 * press+release pairs on emission.
 */
typedef struct {
    union {
        uint16_t event_id;       // Dense index within the event type's namespace
        struct {
            uint8_t count;       // Aggregated tick count (1..255)
            uint8_t encoder_id : 7;  // Encoder index (0..127)
            bool    clockwise  : 1;  // Rotation direction
        } encoder;
    };
    uint16_t     time;
    event_type_t type;
    bool         pressed;
} gesture_event_t;

/*******************************************************************************
 * Gesture Type Definitions
 ******************************************************************************/

/**
 * Gesture lifecycle states
 */
typedef enum {
    GS_INACTIVE,    // Gesture not participating; waiting for trigger
    GS_PARTIAL,     // Gesture triggered but not yet ready to activate
    GS_ACTIVE,      // Gesture activated; virtual key is pressed
    GS_DISABLED,    // Gesture administratively disabled
} gesture_state_t;

/**
 * Passed to the gesture callback to indicate the current query context
 */
typedef enum {
    GS_QUERY_INITIAL,              // Gesture was inactive; new press at front of buffer
    GS_QUERY_PARTIAL,              // Gesture is partial, would cancel on timeout
    GS_QUERY_COMPLETE,             // Gesture is partial, would activate on timeout
    GS_QUERY_ACTIVATION_INITIAL,   // Gesture activating; first event (triggering press)
    GS_QUERY_ACTIVATION_REPLAY,    // Gesture activating; subsequent buffered events
    GS_QUERY_ACTIVE,               // Gesture is active; new event arrived
} gesture_query_t;

/**
 * Return value from gesture callbacks indicating timeout and desired action.
 *
 * Timeout always indicates when to change state, relative to current event.
 *  - timeout=0: immediately, timeout=GESTURE_TIMEOUT_NEVER: never
 *
 * GS_QUERY_INITIAL/GS_QUERY_PARTIAL/GS_QUERY_COMPLETE:
 *   Before timeout, stay/become GS_PARTIAL
 *      outcome=0      -> After timeout, transition to GS_INACTIVE (cancel)
 *      outcome=1..15  -> After timeout, transition to GS_ACTIVE (which outcome)
 *
 * GS_QUERY_ACTIVATION_INITIAL/GS_QUERY_ACTIVATION_REPLAY/GS_QUERY_ACTIVE:
 *   Release events: outcome is ignored
 *   Press events:
 *      outcome=0      -> Do NOT consume this press
 *      outcome!=0     -> Do consume this press
 */
typedef struct {
    uint16_t timeout;   // ms until state change (0=immediate, 0xFFFF=never)
    uint8_t  outcome;   // 0=cancel/don't-consume, nonzero=activate/consume
} gesture_timeout_t;

/**
 * Gesture ID: array index, used for linked lists and callback identification.
 * Also serves as priority (higher ID = higher priority).
 */
typedef uint16_t gesture_id_t;

/**
 * Gesture event ID: the virtual key ID emitted when a gesture activates.
 * For multi-outcome gestures, the emitted event_id is
 * base_event_id + timeout_outcome - 1.
 */
typedef uint16_t gesture_event_id_t;

/**
 * Gesture callback function type.
 *
 * @param id              Id of the gesture
 * @param query           Current query context
 * @param event           Event to process (EVENT_TYPE_KEY or EVENT_TYPE_ENCODER for buffered events)
 * @param remaining_ms    Time remaining on previous timeout (0 if none/expired)
 * @param current_outcome Current outcome (timeout_outcome for ACTIVATION/ACTIVE, 0 for INITIAL)
 * @param user_data       Gesture-specific state pointer
 * @return                Timeout and outcome indicating next action
 */
typedef gesture_timeout_t (*gesture_callback_t)(
    gesture_id_t          id,
    gesture_query_t       query,
    const gesture_event_t *event,
    uint16_t              remaining_ms,
    uint8_t               current_outcome,
    void                  *user_data
);

/**
 * Gesture definition structure.
 *
 * Gestures are organized into linked lists by state. The 'next' field
 * points to the next gesture in the same state queue.
 */
typedef struct {
    gesture_state_t    state : 2;              // Current state
    gesture_id_t       next : 10;              // Next gesture in queue (linked list, max 1023)
    uint8_t            timeout_outcome : 4;    // 0=cancel, 1..15=which outcome
    gesture_event_id_t base_event_id;          // First virtual key event ID for this gesture
    uint8_t            num_outcomes;            // Number of possible outcomes (1 for single-outcome)
    uint16_t           expiry;                 // Absolute expiry time (0 = none, GESTURE_TIMEOUT_NEVER = never)
    gesture_callback_t callback;               // Gesture logic
    void              *user_data;              // Gesture-specific state
} gesture_t;

#define GESTURE(cb, ud, base_eid, n_outcomes) \
    (gesture_t){.callback = cb, .user_data = ud, .base_event_id = base_eid, .num_outcomes = n_outcomes}

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
 * Public API
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
 * Enable a previously disabled gesture.
 */
void gesture_enable(gesture_id_t gesture_id);

/**
 * Disable a gesture.
 * If active, it will be deactivated. If partial, it will be cancelled.
 */
void gesture_disable(gesture_id_t gesture_id);

/**
 * Check if a gesture is currently enabled.
 */
bool gesture_is_enabled(gesture_id_t gesture_id);

/**
 * Check if a gesture is currently active (virtual key pressed).
 */
bool gesture_is_active(gesture_id_t gesture_id);

/**
 * User must define: return pointer to gesture definition at index.
 */
gesture_t *gesture_get(gesture_id_t index);

/**
 * User must define: return total number of gestures.
 */
uint16_t gesture_count(void);

/**
 * Emit a resolved event to the layer system.
 *
 * Provided by the layer system (layers/layer.c). Called when a physical
 * key event or encoder event is released from the buffer
 * (EVENT_TYPE_KEY, EVENT_TYPE_ENCODER) or when a gesture
 * activates/deactivates (EVENT_TYPE_GESTURE). The layer system resolves
 * the event to a keycode and hands it to QMK for execution. For encoder
 * events, the count field indicates how many ticks to emit.
 */
void gesture_emit_event(gesture_event_t event);

/*******************************************************************************
 * Helper Macros
 ******************************************************************************/

/**
 * Create a gesture_timeout_t with specified values.
 */
#define GESTURE_TIMEOUT(t, o) ((gesture_timeout_t){.timeout = (t), .outcome = (uint8_t)(o)})

/**
 * Define a minimal QMK keymaps array. The gesture module bypasses QMK's
 * layer lookup, but the keymaps symbol must exist for the build to link.
 */
#define GESTURES_EMPTY_KEYMAP \
    const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {[0] = {0}}
