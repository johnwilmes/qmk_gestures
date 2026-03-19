#include "tapdance.h"

/* TAPDANCE BEHAVIOR OVERRIDES - default implementations */

__attribute__((weak)) uint16_t get_tapdance_timeout(gesture_id_t id, const gesture_event_t *trigger_event) {
    return TAPDANCE_TIMEOUT;
}

__attribute__((weak)) gesture_timeout_t get_tapdance_hold_on_event(gesture_id_t id, const gesture_event_t *event, uint16_t remaining_ms) {
    // Default: non-trigger events don't affect hold. Continue waiting.
    return GESTURE_TIMEOUT(remaining_ms, true);
}

/*
 * Tap Dance gesture callback.
 *
 * Each gesture independently tracks press_count (1-indexed).
 * Single tap (press_count=1, released) falls through to the base keymap
 * because no gesture has target_presses=1 with is_hold=false.
 *
 * Hold variants stay active while the key is held (deactivate on release).
 * Tap variants activate instantly (virtual press+release, then deactivate).
 *
 * Self-resolution: a gesture returns timeout=0 as soon as it can determine
 * its own outcome, without needing to know about other gestures.
 */

static bool tapdance_resolved(tapdance_data_t *s) {
    if (s->press_count > s->target_presses) {
        return true;  // Past target — can never match
    }
    if (s->press_count == s->target_presses && !s->key_down) {
        return true;  // Target press released: tap matched, or hold's window passed
    }
    return false;
}

static bool tapdance_should_activate(tapdance_data_t *s) {
    if (s->press_count != s->target_presses) {
        return false;
    }
    return s->is_hold ? s->key_down : !s->key_down;
}

gesture_timeout_t tapdance_gesture_callback(gesture_id_t id, gesture_query_t query, const gesture_event_t *event, uint16_t remaining_ms, void *user_data) {
    tapdance_data_t *s = (tapdance_data_t *)user_data;
    bool is_trigger = (event->type == EVENT_TYPE_KEY && event->event_id == s->trigger_key);

    switch (query) {
        case GS_QUERY_INITIAL:
            if (is_trigger && event->pressed) {
                s->press_count = 1;
                s->key_down = true;
                return GESTURE_TIMEOUT(get_tapdance_timeout(id, event),
                                       tapdance_should_activate(s));
            }
            return GESTURE_TIMEOUT(0, false);

        case GS_QUERY_PARTIAL:
        case GS_QUERY_COMPLETE:
            if (is_trigger) {
                if (event->pressed) {
                    s->press_count++;
                    s->key_down = true;
                } else {
                    s->key_down = false;
                }

                if (tapdance_resolved(s)) {
                    return GESTURE_TIMEOUT(0, tapdance_should_activate(s));
                }

                // Reset timeout, wait for more
                return GESTURE_TIMEOUT(get_tapdance_timeout(id, event),
                                       tapdance_should_activate(s));
            }

            // Non-trigger event: route through hold override
            {
                gesture_timeout_t hold_result = get_tapdance_hold_on_event(id, event, remaining_ms);

                if (hold_result.timeout == 0) {
                    // Override says resolve now.
                    if (s->is_hold) {
                        return hold_result;
                    } else {
                        return GESTURE_TIMEOUT(0, !hold_result.outcome);
                    }
                }

                // Continue waiting. Use should_activate for timeout outcome.
                return GESTURE_TIMEOUT(hold_result.timeout,
                                       tapdance_should_activate(s));
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
                if (s->is_hold) {
                    // Hold: consume all trigger events, stay active while key held
                    return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, true);
                }
                // Tap: consume all trigger events. Deactivate after final release.
                if (!event->pressed && s->press_count >= s->target_presses) {
                    return GESTURE_TIMEOUT(0, true);  // consume + done
                }
                return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, true);  // consume, continue
            }
            // Non-trigger: don't consume
            return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, false);

        case GS_QUERY_ACTIVE:
            if (!s->is_hold) {
                // Tap: should not stay active (instant press+release)
                return GESTURE_TIMEOUT(0, false);
            }
            // Hold: deactivate on trigger release
            if (is_trigger && !event->pressed) {
                s->key_down = false;
                return GESTURE_TIMEOUT(0, false);
            }
            return GESTURE_TIMEOUT(GESTURE_TIMEOUT_NEVER, false);
    }
    return GESTURE_TIMEOUT(0, false);
}
