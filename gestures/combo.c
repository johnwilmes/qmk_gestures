#include "combo.h"

int8_t combo_find_key(const uint8_t *keys, uint8_t target) {
    uint8_t n_keys = pgm_read_byte(keys);
    for (uint8_t i = 0; i < n_keys; i++) {
        uint8_t key = pgm_read_byte(&keys[i+1]);
        if (key == target) {
            return i;
        }
    }
    return -1;
}

int8_t combo_num_unpressed_keys(combo_data_t *data) {
    uint8_t n_keys = pgm_read_byte(data->keys);
    uint8_t n_pressed = 0;
    for (uint8_t i = 0; i < n_keys; i++) {
        if (data->active_state & (1 << i)) {
            n_pressed++;
        }
    }
    return (int8_t)(n_keys - n_pressed);
}

/* COMBO BEHAVIOR OVERRIDES - default implementations */
__attribute__((weak)) uint16_t get_partial_combo_timeout(gesture_id_t combo, const gesture_event_t *event, int8_t which_key, combo_active_mask_t next_state, uint16_t remaining_ms) {
    return combo_contiguous(event, which_key, remaining_ms, COMBO_TIMEOUT);
}

__attribute__((weak)) gesture_timeout_t get_unripe_combo_timeout(gesture_id_t combo, const gesture_event_t *event, int8_t which_key, combo_active_mask_t next_state, uint16_t remaining_ms) {
    // Default behavior: no ripening conditions, simply activate
    return GESTURE_TIMEOUT(0, true);
}

__attribute__((weak)) combo_active_update_t get_ripe_combo_activation(gesture_id_t combo, const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state) {
    return combo_consume_triggers(event, which_key, prev_state);
}

__attribute__((weak)) combo_active_update_t get_active_combo_release(gesture_id_t combo, const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state) {
    return combo_release_triggers(event, which_key, prev_state);
}

gesture_timeout_t combo_gesture_callback(gesture_id_t id, gesture_query_t state, const gesture_event_t *event, uint16_t remaining_ms, void *user_data) {
    combo_data_t *data = (combo_data_t *)user_data;
    int8_t which_key = (event->type == EVENT_TYPE_KEY)
        ? combo_find_key(data->keys, event->event_id)
        : -1;
    int8_t n_unpressed;

    switch (state) {
        case GS_QUERY_INITIAL:
            if (which_key < 0) {
                return GESTURE_TIMEOUT(0, false);
            }
            data->active_state = 0;
            remaining_ms = 0;
            /* fallthrough */
        case GS_QUERY_PARTIAL:
        case GS_QUERY_COMPLETE:
            /* Note that "PARTIAL" and "COMPLETE" query state are from the perspective of the gesture coordinator and do not necessarily correspond to combo concepts */
            n_unpressed = combo_num_unpressed_keys(data);
            if (n_unpressed == 0) {
                // Combo is complete and is ripening
                if (which_key >= 0) {
                    if (event->pressed) {
                        // Repress of trigger key before activation always cancels
                        return GESTURE_TIMEOUT(0, false);
                    }
                    // release from active state
                    data->active_state &= ~(1<<which_key);
                }
                return get_unripe_combo_timeout(id, event, which_key, data->active_state, remaining_ms);
            } else {
                if (which_key >= 0) {
                    if (!event->pressed) {
                        // Release of trigger before completion always cancels
                        return GESTURE_TIMEOUT(0, false);
                    }
                    data->active_state |= (1<<which_key);
                    if (n_unpressed == 1) {
                        // Combo is now complete
                        return get_unripe_combo_timeout(id, event, which_key, data->active_state, 0);
                    }
                }
                return GESTURE_TIMEOUT(get_partial_combo_timeout(id, event, which_key, data->active_state, remaining_ms), false);
            }
            // Above cases always return before reaching this point
            break;

        case GS_QUERY_ACTIVATION_INITIAL:
            data->active_state = 0;
            /* fallthrough */
        case GS_QUERY_ACTIVATION_REPLAY: {
            combo_active_update_t update = get_ripe_combo_activation(id, event, which_key, data->active_state);
            data->active_state = update.active_state;
            return GESTURE_TIMEOUT((data->active_state == 0) ? 0 : GESTURE_TIMEOUT_NEVER, update.consume_press);
        }

        case GS_QUERY_ACTIVE: {
            combo_active_update_t update = get_active_combo_release(id, event, which_key, data->active_state);
            data->active_state = update.active_state;
            return GESTURE_TIMEOUT((data->active_state == 0) ? 0 : GESTURE_TIMEOUT_NEVER, update.consume_press);
        }
    }
    return GESTURE_TIMEOUT(0, false);
}

/* COMBO BEHAVIOR UTILITY FUNCTIONS */
uint16_t combo_contiguous(const gesture_event_t *event, int8_t which_key, uint16_t remaining_ms, uint16_t initial_timeout) {
    if (remaining_ms == 0) {
        return initial_timeout;
    }
    // Default continuing behavior: non-trigger presses interrupt, otherwise continue same timeout
    if ((which_key < 0) && event->pressed) {
        return 0;
    }
    return remaining_ms;
}

combo_active_update_t combo_consume_triggers(const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state) {
    if (which_key >= 0) {
        if (event->pressed) {
            return (combo_active_update_t){.active_state = (prev_state | (1<<which_key)), .consume_press=true};
        } else {
            return (combo_active_update_t){.active_state = (prev_state & ~(1<<which_key)), .consume_press=false};
        }
    }
    return (combo_active_update_t){.active_state = prev_state, .consume_press=false};
}

combo_active_update_t combo_release_triggers(const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state) {
    if (which_key >= 0) {
        if (!event->pressed) {
            return (combo_active_update_t){.active_state = (prev_state & ~(1<<which_key)), .consume_press=false};
        }
    }
    return (combo_active_update_t){.active_state = prev_state, .consume_press=false};
}
