#pragma once

#include "gesture.h"

/*******************************************************************************
 * Tap Dance / Hold Gestures
 *
 * A single multi-outcome gesture per physical key that resolves to different
 * virtual keys depending on how it is pressed:
 *   - Single tap: falls through to the base keymap (no gesture activation)
 *   - Hold (nth press held past timeout): outcome hold(n)
 *   - Multi-tap (n complete press-release cycles): outcome tap(n)
 *
 * Hold is a special case: tapdance with max_presses=1.
 *
 * Usage:
 *
 *   // Event ID enum (shared with combos):
 *   enum gesture_events {
 *       HOLD_EVENTS(home_a),            // 1 event ID
 *       TAPDANCE_EVENTS(my_td, 3),      // 5 event IDs: hold1, tap2, hold2, tap3, hold3
 *   };
 *
 *   // Data definitions:
 *   DEFINE_HOLD(home_a, KEY_INDEX_A)
 *   DEFINE_TAPDANCE(my_td, KEY_INDEX_X, 3)
 *
 *   // Gesture array (1 gesture per key):
 *   gesture_t gestures[] = {
 *       HOLD_GESTURE(home_a),        // 1 outcome
 *       TAPDANCE_GESTURE(my_td, 3),  // 5 outcomes
 *   };
 ******************************************************************************/

#ifndef TAPDANCE_TIMEOUT
#    define TAPDANCE_TIMEOUT 200
#endif

#ifndef TAPPING_TERM
#    define TAPPING_TERM 200
#endif

typedef struct {
    uint8_t trigger_key;     // Dense key index (matches event_id for KEY events)
    uint8_t max_presses;     // Maximum press count for this tapdance
    uint8_t press_count;     // Current press count (1-indexed)
    bool    key_down;        // Whether trigger key is currently held
} tapdance_data_t;

gesture_timeout_t tapdance_gesture_callback(gesture_id_t id, gesture_query_t query, const gesture_event_t *event, uint16_t remaining_ms, uint8_t current_outcome, void *user_data);

/* TAPDANCE BEHAVIOR OVERRIDES */

/* Returns the timeout for this gesture. Default: TAPDANCE_TIMEOUT. */
uint16_t get_tapdance_timeout(gesture_id_t id, const gesture_event_t *trigger_event);

/* Called on every event while the tapdance is PARTIAL or COMPLETE.
 * The callback has already updated press_count/key_down before calling this.
 *
 * @param id           Gesture ID
 * @param event        The event that just arrived
 * @param data         Tapdance state (press_count, key_down already updated)
 * @param remaining_ms Time remaining on current timeout
 * @return             Override result, or {.timeout = GESTURE_TIMEOUT_NEVER}
 *                     to use the default behavior
 *
 * Default: returns GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, 0) (use default). */
gesture_timeout_t get_tapdance_on_event(gesture_id_t id, const gesture_event_t *event, const tapdance_data_t *data, uint16_t remaining_ms);

/* Convenience: standard "hold on other key" behavior for use in overrides.
 * Returns ripen-hold when a non-trigger key press arrives while key is held. */
gesture_timeout_t tapdance_hold_on_other_key(const gesture_event_t *event, const tapdance_data_t *data, uint16_t remaining_ms);

/*******************************************************************************
 * Event ID enum macros
 *
 * These generate enum constants for gesture event IDs. Use in an enum
 * alongside COMBO_EVENTS to assign unique event IDs to all gestures.
 *
 * HOLD_EVENTS(name)           → GE_HOLD_name                    (1 event)
 * TAPDANCE_EVENTS(name, max)  → GE_TD_name_HOLD1, ...           (2*max-1 events)
 *   Layout: HOLD(1), TAP(2), HOLD(2), ..., TAP(max), HOLD(max)
 ******************************************************************************/

#define HOLD_EVENTS(name)              GE_HOLD_##name

#define TAPDANCE_EVENTS(name, max)     _TD_EVENTS_##max(name)
#define _TD_EVENTS_1(name)             GE_TD_##name##_HOLD1
#define _TD_EVENTS_2(name)             _TD_EVENTS_1(name), GE_TD_##name##_TAP2, GE_TD_##name##_HOLD2
#define _TD_EVENTS_3(name)             _TD_EVENTS_2(name), GE_TD_##name##_TAP3, GE_TD_##name##_HOLD3
#define _TD_EVENTS_4(name)             _TD_EVENTS_3(name), GE_TD_##name##_TAP4, GE_TD_##name##_HOLD4
#define _TD_EVENTS_5(name)             _TD_EVENTS_4(name), GE_TD_##name##_TAP5, GE_TD_##name##_HOLD5

/*******************************************************************************
 * Outcome encoding
 *
 * For max_presses=N, there are 2N-1 outcomes:
 *   0 = cancel (no match)
 *   1 = hold(1), 2 = tap(2), 3 = hold(2), 4 = tap(3), 5 = hold(3), ...
 *   General: hold(n) = 2n-1, tap(n) = 2(n-1) for n>=2
 *
 * The TAPDANCE_EVENTS enum generates constants in the same order.
 ******************************************************************************/

/*******************************************************************************
 * Hold convenience (tapdance with max_presses=1, only a hold gesture)
 ******************************************************************************/

#define DEFINE_HOLD(name, trigger) \
    tapdance_data_t _td_data_##name = { .trigger_key = trigger, .max_presses = 1 };

#define HOLD_GESTURE(name) \
    GESTURE(&tapdance_gesture_callback, &_td_data_##name, GE_HOLD_##name, 1)

/*******************************************************************************
 * Tapdance macros
 ******************************************************************************/

/* Define tapdance data for a key with the given max presses. */
#define DEFINE_TAPDANCE(name, trigger, max) \
    tapdance_data_t _td_data_##name = { .trigger_key = trigger, .max_presses = max };

/* Single multi-outcome gesture entry for a tapdance.
 * num_outcomes = 2*max - 1. */
#define TAPDANCE_GESTURE(name, max) \
    GESTURE(&tapdance_gesture_callback, &_td_data_##name, GE_TD_##name##_HOLD1, 2*(max)-1)
