#include "precog.h"

/*
 * Precog ripening: called from get_unripe_combo_timeout override.
 *
 * All trigger keys are already pressed (combo complete). Decides when to
 * activate based on:
 *   - Hold for T1: activate on timeout
 *   - Additional same-hand home-row key pressed: activate immediately
 *   - Opposite-hand key pressed: activate immediately
 *   - Same-hand non-home-row key pressed: cancel
 *   - Trigger key release: cancel (via active_state tracking in combo callback)
 */
gesture_timeout_t precog_unripe_timeout(gesture_id_t id, const gesture_event_t *event, int8_t which_key, combo_active_mask_t next_state, uint16_t remaining_ms,
                                        precog_state_t *state, precog_classify_t classify) {
    if (remaining_ms == 0) {
        // Just completed trigger
        state->home_row_count = 0;
        state->opp_hand_presses = 0;
        state->combo_complete_time = event->time;
        return GESTURE_TIMEOUT(PRECOG_T1, true);  // Activate on hold T1
    }

    // Trigger key events are handled by the combo callback before we're called
    // (repress cancels via hardcoded check, release updates active_state).
    // We only need to handle non-trigger events here.
    if (which_key >= 0) {
        // Trigger release with remaining triggers held -- continue
        return GESTURE_TIMEOUT(remaining_ms, true);
    }

    // Only classify physical key events; encoder event_id is a packed
    // struct, not a key index.
    if (event->type != EVENT_TYPE_KEY) {
        return GESTURE_TIMEOUT(remaining_ms, true);
    }

    precog_key_class_t kc = classify(id, event->event_id);

    if (kc == PRECOG_KEY_HOME_ROW && event->pressed) {
        state->home_row_count++;
        // Additional home-row key: ripen immediately
        return GESTURE_TIMEOUT(0, true);
    }

    if (kc == PRECOG_KEY_OPP_HAND && event->pressed) {
        state->opp_hand_presses++;
        // Opposite-hand key: ripen immediately
        return GESTURE_TIMEOUT(0, true);
    }

    if (kc == PRECOG_KEY_SAME_HAND && event->pressed) {
        // Non-home-row same-hand key: cancel
        return GESTURE_TIMEOUT(0, false);
    }

    // Releases of non-trigger keys, unknown keys: continue
    return GESTURE_TIMEOUT(remaining_ms, true);
}

/*
 * Precog activation: called from get_ripe_combo_activation override.
 *
 * Don't consume any keys. Track trigger key state so we know when to release.
 */
combo_active_update_t precog_activation(gesture_id_t id, const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state) {
    if (which_key >= 0) {
        if (event->pressed) {
            return (combo_active_update_t){.active_state = prev_state | (1 << which_key), .consume_press = false};
        } else {
            return (combo_active_update_t){.active_state = prev_state & ~(1 << which_key), .consume_press = false};
        }
    }
    return (combo_active_update_t){.active_state = prev_state, .consume_press = false};
}

/*
 * Precog release: called from get_active_combo_release override.
 *
 * Deactivate when the gating key (index 0, thumb) releases. Never consume.
 */
combo_active_update_t precog_release(gesture_id_t id, const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state) {
    if (which_key == 0 && !event->pressed) {
        // Thumb released: deactivate
        return (combo_active_update_t){.active_state = 0, .consume_press = false};
    }
    if (which_key > 0 && !event->pressed) {
        // Other trigger released: update state but stay active
        return (combo_active_update_t){.active_state = prev_state & ~(1 << which_key), .consume_press = false};
    }
    // Never consume
    return (combo_active_update_t){.active_state = prev_state, .consume_press = false};
}
