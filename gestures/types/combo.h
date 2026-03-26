#pragma once

#include "gesture_api.h"
#include "progmem.h"

typedef uint8_t combo_active_mask_t;

/* Pointer to a PROGMEM array of key indices (user-defined dense positions).
 * First byte is the count, followed by that many key index values. */
typedef const uint8_t* gs_keys_t;

typedef struct {
    combo_active_mask_t active_state;
    gs_keys_t keys;
} combo_data_t;

typedef struct {
    combo_active_mask_t active_state;
    bool consume_press;
} combo_active_update_t;

#ifndef COMBO_TIMEOUT
    #define COMBO_TIMEOUT 150
#endif

/* COMBO BEHAVIOR UTILITY FUNCTIONS */
uint16_t combo_contiguous(const gesture_event_t *event, int8_t which_key, uint16_t remaining_ms, uint16_t initial_timeout);
combo_active_update_t combo_consume_triggers(const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state);
combo_active_update_t combo_release_triggers(const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state);

/* COMBO BEHAVIOR OVERRIDES
*
* To change combo behavior, usually for a particular subset of combos (e.g. changing the timeout or adding a hold or tap condition),
* end users can override the following four functions. Usually, the override will look like a switch statement on the combo, passing different combos to
* different behavior utility functions.
*/
uint16_t get_partial_combo_timeout(gesture_id_t combo, const gesture_event_t *event, int8_t which_key, combo_active_mask_t next_state, uint16_t remaining_ms);
gesture_timeout_t get_unripe_combo_timeout(gesture_id_t combo, const gesture_event_t *event, int8_t which_key, combo_active_mask_t next_state, uint16_t remaining_ms);
combo_active_update_t get_ripe_combo_activation(gesture_id_t combo, const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state);
combo_active_update_t get_active_combo_release(gesture_id_t combo, const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state);

gesture_timeout_t combo_gesture_callback(gesture_id_t id, gesture_query_t state, const gesture_event_t *event, uint16_t remaining_ms, uint8_t current_outcome, void *user_data);

/*******************************************************************************
 * Combo definition macro
 *
 * Define combo trigger keys and gesture using user-defined key position names.
 *
 *   DEFINE_COMBO(my_combo, POS_L_INDEX_H, POS_L_MIDDLE_H)
 *
 *   DEFINE_GESTURES(my_combo);
 *
 *   DEFINE_GESTURE_LAYER(base_gestures,
 *       COMBO_MAP(my_combo, KC_ESC),
 *   );
 ******************************************************************************/

#define DEFINE_COMBO(name, ...) \
    static const uint8_t PROGMEM _gs_combo_keys_##name[] = {COUNT_ARGS(__VA_ARGS__), __VA_ARGS__}; \
    static combo_data_t _gs_combo_data_##name = {.active_state = 0, .keys = &(_gs_combo_keys_##name)[0]}; \
    static gesture_t _gs_##name = GESTURE(&combo_gesture_callback, &_gs_combo_data_##name, 1)
