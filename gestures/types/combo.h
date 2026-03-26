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

/* Define combo trigger keys using user-defined key position names.
 * Example: DEFINE_COMBO_KEYS(my_combo, POS_L_INDEX_H, POS_L_MIDDLE_H) */
#define DEFINE_COMBO_KEYS(name, ...) \
    const uint8_t PROGMEM GS_DATA_COMBO_KEYS_##name[] = {COUNT_ARGS(__VA_ARGS__), __VA_ARGS__}; \
    combo_data_t GS_DATA_COMBO_USER_DATA_##name = {.active_state = 0, .keys = &(GS_DATA_COMBO_KEYS_##name)[0]};

/* Event ID enum macro: generates GE_COMBO_name (1 event) */
#define COMBO_EVENTS(name)  GE_COMBO_##name

#define COMBO_GESTURE(name) GESTURE(&combo_gesture_callback, &GS_DATA_COMBO_USER_DATA_##name, GE_COMBO_##name, 1)
