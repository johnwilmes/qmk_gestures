#pragma once

#include "gesture_api.h"

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
 *   DEFINE_HOLD(home_a, KEY_INDEX_A)
 *   DEFINE_TAPDANCE(my_td, KEY_INDEX_X, 3)
 *
 *   DEFINE_GESTURES(home_a, my_td);
 *
 *   DEFINE_GESTURE_LAYER(base_gestures,
 *       HOLD_MAP(home_a,   KC_LGUI),
 *       TD_MAP(my_td, 3,   KC_LSFT, KC_X, KC_LCTL, KC_Y, KC_LALT),
 *   );
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
 * Outcome encoding
 *
 * For max_presses=N, there are 2N-1 outcomes:
 *   0 = cancel (no match)
 *   1 = hold(1), 2 = tap(2), 3 = hold(2), 4 = tap(3), 5 = hold(3), ...
 *   General: hold(n) = 2n-1, tap(n) = 2(n-1) for n>=2
 ******************************************************************************/

/*******************************************************************************
 * Hold convenience (tapdance with max_presses=1, only a hold outcome)
 ******************************************************************************/

#define DEFINE_HOLD(name, trigger) \
    static tapdance_data_t _td_data_##name = { .trigger_key = trigger, .max_presses = 1 }; \
    static gesture_t _gs_##name = GESTURE(&tapdance_gesture_callback, &_td_data_##name, 1)

/*******************************************************************************
 * Tapdance macros
 ******************************************************************************/

/* Define tapdance data and gesture for a key with the given max presses.
 * num_outcomes = 2*max - 1. */
#define DEFINE_TAPDANCE(name, trigger, max) \
    static tapdance_data_t _td_data_##name = { .trigger_key = trigger, .max_presses = max }; \
    static gesture_t _gs_##name = GESTURE(&tapdance_gesture_callback, &_td_data_##name, 2*(max)-1)
