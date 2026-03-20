#include "tapdance.h"

/*
 * Outcome encoding:
 *   0 = cancel
 *   hold(n) = 2n-1   (n >= 1)
 *   tap(n)  = 2(n-1) (n >= 2)
 */
static inline uint8_t hold_outcome(uint8_t press_count) {
    return 2 * press_count - 1;
}

static inline uint8_t tap_outcome(uint8_t press_count) {
    return 2 * (press_count - 1);
}

/* TAPDANCE BEHAVIOR OVERRIDES - default implementations */

__attribute__((weak)) uint16_t get_tapdance_timeout(gesture_id_t id, const gesture_event_t *trigger_event) {
    return TAPDANCE_TIMEOUT;
}

__attribute__((weak)) gesture_timeout_t get_tapdance_on_event(gesture_id_t id, const gesture_event_t *event, const tapdance_data_t *data, uint16_t remaining_ms) {
    // Default: use standard behavior (return NEVER to signal "no override")
    return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, 0);
}

gesture_timeout_t tapdance_hold_on_other_key(const gesture_event_t *event, const tapdance_data_t *data, uint16_t remaining_ms) {
    // If a non-trigger key is pressed while the trigger is held, ripen the hold
    if (event->type == EVENT_TYPE_KEY && event->event_id != data->trigger_key &&
        event->pressed && data->key_down) {
        return GESTURE_TIMEOUT(0, hold_outcome(data->press_count));
    }
    return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, 0);
}

/*
 * Compute the current best outcome from press_count and key_down state.
 * Returns the outcome that would activate if the timeout fires now.
 */
static uint8_t current_best_outcome(tapdance_data_t *s) {
    if (s->press_count == 0) return 0;
    if (s->key_down) {
        return hold_outcome(s->press_count);
    }
    // Key up: if press_count >= 2, it's a tap(press_count)
    if (s->press_count >= 2) {
        return tap_outcome(s->press_count);
    }
    // Single tap (press_count=1, key up): falls through to base keymap
    return 0;
}

/*
 * Check if the gesture can still match something or is fully resolved.
 */
static bool is_resolved(tapdance_data_t *s) {
    if (s->press_count > s->max_presses) return true;
    if (s->press_count == s->max_presses && !s->key_down) return true;
    return false;
}

/*
 * Single multi-outcome tapdance callback.
 *
 * One gesture tracks the full press sequence. The outcome encodes which
 * virtual key to emit: hold(n) or tap(n). The coordinator uses
 * base_event_id + outcome - 1 to derive the event_id.
 */
gesture_timeout_t tapdance_gesture_callback(gesture_id_t id, gesture_query_t query, const gesture_event_t *event, uint16_t remaining_ms, uint8_t current_outcome, void *user_data) {
    tapdance_data_t *s = (tapdance_data_t *)user_data;
    bool is_trigger = (event->type == EVENT_TYPE_KEY && event->event_id == s->trigger_key);

    switch (query) {
        case GS_QUERY_INITIAL:
            if (is_trigger && event->pressed) {
                s->press_count = 1;
                s->key_down = true;
                return GESTURE_TIMEOUT(get_tapdance_timeout(id, event),
                                       current_best_outcome(s));
            }
            return GESTURE_TIMEOUT(0, 0);

        case GS_QUERY_PARTIAL:
        case GS_QUERY_COMPLETE:
            if (is_trigger) {
                if (event->pressed) {
                    s->press_count++;
                    s->key_down = true;
                } else {
                    s->key_down = false;
                }

                if (is_resolved(s)) {
                    return GESTURE_TIMEOUT(0, current_best_outcome(s));
                }

                return GESTURE_TIMEOUT(get_tapdance_timeout(id, event),
                                       current_best_outcome(s));
            }

            // Non-trigger event: call override
            {
                gesture_timeout_t override = get_tapdance_on_event(id, event, s, remaining_ms);
                if (override.timeout != GESTURE_TIMEOUT_NEVER) {
                    return override;
                }
                // Default: continue waiting with current outcome
                return GESTURE_TIMEOUT(remaining_ms, current_best_outcome(s));
            }

        case GS_QUERY_ACTIVATION_INITIAL:
            s->press_count = 0;
            s->key_down = false;
            // Fall through
        case GS_QUERY_ACTIVATION_REPLAY:
            if (is_trigger) {
                if (event->pressed) {
                    s->press_count++;
                    s->key_down = true;
                } else {
                    s->key_down = false;
                }

                // For hold outcomes: consume all trigger events, stay active while key held
                if (current_outcome & 1) {  // odd = hold
                    return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, 1);
                }
                // For tap outcomes: consume all trigger events, deactivate after final release
                if (!event->pressed && s->press_count >= (current_outcome / 2 + 1)) {
                    return GESTURE_TIMEOUT(0, 1);  // consume + done
                }
                return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, 1);  // consume, continue
            }
            // Non-trigger: don't consume
            return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, 0);

        case GS_QUERY_ACTIVE:
            if (!(current_outcome & 1)) {
                // Tap: should not stay active (instant press+release)
                return GESTURE_TIMEOUT(0, 0);
            }
            // Hold: deactivate on trigger release
            if (is_trigger && !event->pressed) {
                s->key_down = false;
                return GESTURE_TIMEOUT(0, 0);
            }
            return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, 0);
    }
    return GESTURE_TIMEOUT(0, 0);
}
