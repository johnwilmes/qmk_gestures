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

/* Null gesture ID for linked list termination (must fit in 14-bit next field) */
#define GESTURE_NULL_ID ((gesture_id_t)0x3FFF)

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
    EVENT_TYPE_GESTURE,  // Virtual gesture: packed (gesture_id, outcome)
    EVENT_TYPE_ENCODER,  // Encoder action: encoder field has id/direction/count
    NUM_EVENT_TYPES,
} event_type_t;

/**
 * Unified event type used throughout the gesture and layer systems.
 *
 * Physical key events (EVENT_TYPE_KEY) are created at the pipeline entry
 * point by mapping QMK's (row, col) to a dense key index via user-defined
 * mapping. Gesture events (EVENT_TYPE_GESTURE) are created internally when
 * gestures activate/deactivate, packing gesture_id and outcome into the
 * event_id field. Encoder events (EVENT_TYPE_ENCODER) are created at the
 * pipeline entry point and flow through the gesture system.
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
        struct {
            uint16_t outcome    : 4;   // 1-15 = outcome, 0 = unused
            uint16_t gesture_id : 12;  // Gesture array index (0..4095)
        } gesture;
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
    gesture_id_t       next : 14;              // Next gesture in queue (linked list, max 16383)
    uint8_t            timeout_outcome;        // 0=cancel, 1+=which outcome
    uint8_t            num_outcomes;           // Number of possible outcomes (1 for single-outcome)
    uint16_t           expiry;                 // Absolute expiry time (0 = none, GESTURE_TIMEOUT_NEVER = never)
    gesture_callback_t callback;               // Gesture logic
    void              *user_data;              // Gesture-specific state
} gesture_t;

#define GESTURE(cb, ud, n_outcomes) \
    (gesture_t){.callback = cb, .user_data = ud, .num_outcomes = n_outcomes}

/*******************************************************************************
 * Gesture ID Reference
 ******************************************************************************/

/**
 * Reference a gesture by name. Use in enums, switch cases, and layer mappings.
 *
 *   enum { GS(my_hold), GS(my_combo), GS(my_td) };
 *   case GS(my_hold): ...
 */
#define GS(name) GS_##name

/*******************************************************************************
 * Public API
 ******************************************************************************/

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
 * Return pointer to gesture definition at index.
 * Override for custom lookup logic; default provided by DEFINE_GESTURES.
 */
gesture_t *gesture_get(gesture_id_t index);

/**
 * Return total number of gestures.
 * Override for custom lookup logic; default provided by DEFINE_GESTURES.
 */
uint16_t gesture_count(void);

/**
 * User must define: map a QMK matrix position to a dense key index
 * (0..NUM_KEY_POSITIONS-1). Called at pipeline entry and by the layer
 * system's keymap introspection override. Declared in gesture_layers.h
 * (where the QMK keypos_t type is available).
 */

/*******************************************************************************
 * Helper Macros
 ******************************************************************************/

/**
 * Create a gesture_timeout_t with specified values.
 */
#define GESTURE_TIMEOUT(t, o) ((gesture_timeout_t){.timeout = (t), .outcome = (uint8_t)(o)})

/*******************************************************************************
 * Gesture Registration
 ******************************************************************************/

/**
 * Internal helpers for _GS_MAP-based DEFINE_GESTURES.
 */
#define _GS_ENUM(name) GS_##name,
#define _GS_PTR(name)  [GS_##name] = &_gs_##name,

/**
 * Register gestures by name and provide gesture_get / gesture_count.
 * Requires _GS_MAP (generated by gen_macros.py --gestures).
 *
 * Generates an enum of gesture IDs and a pointer array. Order determines
 * priority (higher index = higher priority). Designated initializers
 * ensure enum values match array indices regardless of listing order.
 *
 * Usage:
 *   DEFINE_GESTURES(my_hold, my_combo, my_td);
 */
#define DEFINE_GESTURES(...) \
    enum { _GS_MAP(_GS_ENUM, __VA_ARGS__) }; \
    static gesture_t *const _gs_ptrs[] = { \
        _GS_MAP(_GS_PTR, __VA_ARGS__) \
    }; \
    gesture_t *gesture_get(gesture_id_t id) { return _gs_ptrs[id]; } \
    uint16_t gesture_count(void) { \
        return sizeof(_gs_ptrs) / sizeof(gesture_t*); \
    }

/**
 * Manual gesture registration entry (fallback when _GS_MAP is unavailable).
 * Use with a hand-written enum and DEFINE_GESTURES_MANUAL.
 *
 *   enum { GS(my_hold), GS(my_combo) };
 *   DEFINE_GESTURES_MANUAL(
 *       GESTURE_ENTRY(my_hold),
 *       GESTURE_ENTRY(my_combo),
 *   );
 */
#define GESTURE_ENTRY(name) [GS_##name] = &_gs_##name

#define DEFINE_GESTURES_MANUAL(...) \
    static gesture_t *const _gs_ptrs[] = { __VA_ARGS__ }; \
    gesture_t *gesture_get(gesture_id_t id) { return _gs_ptrs[id]; } \
    uint16_t gesture_count(void) { \
        return sizeof(_gs_ptrs) / sizeof(gesture_t*); \
    }
