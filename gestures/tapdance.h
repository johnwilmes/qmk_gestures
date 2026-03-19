#pragma once

#include "gesture.h"

/*******************************************************************************
 * Tap Dance / Hold Gestures
 *
 * A physical key can produce different virtual keys depending on how it is
 * pressed:
 *   - Single tap: the base keymap keycode (no gesture needed)
 *   - Hold (nth press held past timeout): TAPDANCE_HOLD(name, n)
 *   - Multi-tap (n complete press-release cycles): TAPDANCE_TAP(name, n)
 *
 * The parameter `n` always means "number of presses" for both tap and hold:
 *   - TAPDANCE_TAP(td, 3)  = 3 complete press-release cycles
 *   - TAPDANCE_HOLD(td, 2) = tap once, hold the 2nd press
 *
 * Single tap (n=1) falls through to the base keymap — no gesture is needed.
 *
 * Hold is a special case: tapdance with only a hold gesture.
 *
 * Each gesture is fully independent — no shared state between tap counts.
 * A gesture self-resolves when it can determine its own outcome:
 *   - press_count > target_presses → can never match → fail
 *   - press_count == target_presses && !key_down && is_hold → released
 *     the target press without holding → fail
 *   - press_count == target_presses && !key_down && !is_hold → tap matched
 *
 * Usage:
 *
 *   // Simple hold (tap = base key):
 *   DEFINE_HOLD(home_a, KEY_INDEX_A)
 *
 *   gesture_t gestures[] = {
 *       HOLD_GESTURE(home_a),    // 1 gesture
 *   };
 *
 *   // Full tap dance (max 3 presses):
 *   DEFINE_TAPDANCE(my_td, KEY_INDEX_X, 3)
 *
 *   gesture_t gestures[] = {
 *       TAPDANCE_GESTURES(my_td, 3),
 *       // expands to: HOLD(1), TAP(2), HOLD(2), TAP(3), HOLD(3)
 *   };
 *
 *   // Or pick individual gestures:
 *   gesture_t gestures[] = {
 *       TAPDANCE_HOLD(my_td, 1),   // hold 1st press
 *       TAPDANCE_TAP(my_td, 2),    // double-tap
 *       TAPDANCE_TAP(my_td, 3),    // triple-tap
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
    uint8_t target_presses;  // Activate at this press count
    bool    is_hold;         // true = activate on hold; false = activate on release
    uint8_t press_count;     // Current press count (1-indexed)
    bool    key_down;
} tapdance_data_t;

gesture_timeout_t tapdance_gesture_callback(gesture_id_t id, gesture_query_t query, const gesture_event_t *event, uint16_t remaining_ms, void *user_data);

/* TAPDANCE BEHAVIOR OVERRIDES */

/* Returns the timeout for this gesture. Default: TAPDANCE_TIMEOUT. */
uint16_t get_tapdance_timeout(gesture_id_t id, const gesture_event_t *trigger_event);

/* Called when a non-trigger event arrives while a hold variant is PARTIAL.
 * Controls whether the hold should ripen early, cancel, or continue waiting.
 *
 * Return values:
 *   GESTURE_TIMEOUT(0, true)             -> ripen hold (activate now)
 *   GESTURE_TIMEOUT(0, false)            -> cancel hold
 *   GESTURE_TIMEOUT(remaining_ms, true)  -> continue waiting (default)
 *
 * Default: continue waiting. */
gesture_timeout_t get_tapdance_hold_on_event(gesture_id_t id, const gesture_event_t *event, uint16_t remaining_ms);

/*******************************************************************************
 * Hold convenience (tapdance with only a hold gesture)
 ******************************************************************************/

#define DEFINE_HOLD(name, trigger) \
    _TD_DEFINE_HOLD(name, 1, trigger)

#define HOLD_GESTURE(name) TAPDANCE_HOLD(name, 1)

/*******************************************************************************
 * Tapdance macros
 ******************************************************************************/

/* Define all gesture data for a tapdance with the given max presses. */
#define DEFINE_TAPDANCE(name, trigger, max) \
    _TD_DEFINE_ALL_##max(name, trigger)

/* Single gesture array entries. */
#define TAPDANCE_TAP(name, n)  GESTURE(&tapdance_gesture_callback, &_td_tap_##name##_##n)
#define TAPDANCE_HOLD(name, n) GESTURE(&tapdance_gesture_callback, &_td_hold_##name##_##n)

/* All gestures for a tapdance, lowest count first:
 *   HOLD(1), TAP(2), HOLD(2), ..., TAP(max), HOLD(max) */
#define TAPDANCE_GESTURES(name, max) _TD_GESTURES_##max(name)

/*******************************************************************************
 * Internal macro machinery
 ******************************************************************************/

#define _TD_DEFINE_HOLD(name, n, trigger) \
    tapdance_data_t _td_hold_##name##_##n = { \
        .trigger_key = trigger, .target_presses = n, .is_hold = true \
    };

#define _TD_DEFINE_TAP(name, n, trigger) \
    tapdance_data_t _td_tap_##name##_##n = { \
        .trigger_key = trigger, .target_presses = n, .is_hold = false \
    };

#define _TD_DEFINE_PAIR(name, n, trigger) \
    _TD_DEFINE_TAP(name, n, trigger) \
    _TD_DEFINE_HOLD(name, n, trigger)

/* _TD_DEFINE_ALL_N: define all gesture data for max_presses=N.
 * N=1: only HOLD(1). N>1: HOLD(1) + TAP/HOLD pairs for 2..N. */
#define _TD_DEFINE_ALL_1(name, trigger) _TD_DEFINE_HOLD(name, 1, trigger)
#define _TD_DEFINE_ALL_2(name, trigger) _TD_DEFINE_ALL_1(name, trigger) _TD_DEFINE_PAIR(name, 2, trigger)
#define _TD_DEFINE_ALL_3(name, trigger) _TD_DEFINE_ALL_2(name, trigger) _TD_DEFINE_PAIR(name, 3, trigger)
#define _TD_DEFINE_ALL_4(name, trigger) _TD_DEFINE_ALL_3(name, trigger) _TD_DEFINE_PAIR(name, 4, trigger)
#define _TD_DEFINE_ALL_5(name, trigger) _TD_DEFINE_ALL_4(name, trigger) _TD_DEFINE_PAIR(name, 5, trigger)

/* _TD_GESTURES_N: all gesture entries for max_presses=N. */
#define _TD_GESTURES_1(name) TAPDANCE_HOLD(name, 1)
#define _TD_GESTURES_2(name) _TD_GESTURES_1(name), TAPDANCE_TAP(name, 2), TAPDANCE_HOLD(name, 2)
#define _TD_GESTURES_3(name) _TD_GESTURES_2(name), TAPDANCE_TAP(name, 3), TAPDANCE_HOLD(name, 3)
#define _TD_GESTURES_4(name) _TD_GESTURES_3(name), TAPDANCE_TAP(name, 4), TAPDANCE_HOLD(name, 4)
#define _TD_GESTURES_5(name) _TD_GESTURES_4(name), TAPDANCE_TAP(name, 5), TAPDANCE_HOLD(name, 5)
