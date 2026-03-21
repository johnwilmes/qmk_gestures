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

#include "gesture.h"
#include "layer.h"
#include "quantum.h"

ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);

/*******************************************************************************
 * User-Provided Functions
 ******************************************************************************/

/**
 * Map a QMK matrix position to a dense key index for the gesture system.
 * Declared in layer.h; repeated here because gestures.c does not include it.
 */
extern uint8_t gesture_key_index(keypos_t pos);

/*******************************************************************************
 * QMK Module Hooks
 ******************************************************************************/

/**
 * Intercept key and encoder events before normal QMK processing.
 *
 * Converts QMK events to gesture_event_t and routes them through the
 * gesture pipeline. Returns false to suppress the event; the gesture
 * system re-emits it later via gesture_emit_event -> layer system ->
 * process_record.
 *
 * Non-key/encoder events (DIP switches, etc.) pass through to normal
 * QMK processing.
 */
bool pre_process_record_gestures(uint16_t keycode, keyrecord_t *record) {
    if (IS_KEYEVENT(record->event)) {
        gesture_event_t event = {
            .event_id = gesture_key_index(record->event.key),
            .time     = record->event.time,
            .type     = EVENT_TYPE_KEY,
            .pressed  = record->event.pressed,
        };
        gesture_process_event(event);
        return false;
    }
#ifdef ENCODER_MAP_ENABLE
    if (IS_ENCODEREVENT(record->event)) {
        bool cw = (record->event.type == ENCODER_CW_EVENT);
        gesture_event_t event = {
            .encoder = {
                .count      = 1,
                .encoder_id = record->event.key.col,
                .clockwise  = cw,
            },
            .time    = record->event.time,
            .type    = EVENT_TYPE_ENCODER,
            .pressed = true,
        };
        gesture_process_event(event);
        return false;
    }
#endif
    return true;
}

/**
 * Poll for gesture timeouts every housekeeping cycle.
 */
void housekeeping_task_gestures(void) {
    gesture_tick();
#ifndef NO_ACTION_ONESHOT
    if (has_oneshot_mods_timed_out()) {
        clear_oneshot_mods();
    }
#endif
}

/**
 * Initialize gesture and layer systems on keyboard startup.
 */
void keyboard_post_init_gestures(void) {
    layers_init();
    gestures_init();
}
