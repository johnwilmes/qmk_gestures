#pragma once

#include "combo.h"

/*******************************************************************************
 * Precognition: Home-Row Mods via Combo Overrides
 *
 * Precog is a combo (thumb + home-row key) with customized ripening,
 * activation, and release behavior. The combo trigger invariant is preserved:
 * all trigger keys must be pressed. Thumb-only activation is handled by
 * separate gestures (tapdance/taphold on the thumb key).
 *
 * Convention: trigger key index 0 is the "gating" key (thumb).
 *
 * Override points used:
 *   get_unripe_combo_timeout: Complex ripening conditions
 *   get_ripe_combo_activation: Don't consume triggers (pass through to layer)
 *   get_active_combo_release: Deactivate on thumb release, don't consume
 *
 * Per-combo precog state is stored in static precog_state_t variables,
 * looked up by gesture ID in the override switch statements.
 *
 * Usage:
 *
 *   // 8 combos: 4 per hand (thumb + each home-row key)
 *   DEFINE_COMBO_KEYS(precog_li, POS_L_THUMB, POS_L_INDEX_H)
 *   DEFINE_COMBO_KEYS(precog_lm, POS_L_THUMB, POS_L_MIDDLE_H)
 *   // ...
 *
 *   static precog_state_t precog_li_state = {0};
 *   static precog_state_t precog_lm_state = {0};
 *   // ...
 *
 *   // In combo behavior overrides, switch on gesture ID:
 *   gesture_timeout_t get_unripe_combo_timeout(...) {
 *       switch (combo) {
 *           case GS_PRECOG_LI:
 *               return precog_unripe_timeout(combo, event, which_key,
 *                   next_state, remaining_ms, &precog_li_state, &classify_left);
 *           // ...
 *           default:
 *               return GESTURE_TIMEOUT(0, true);
 *       }
 *   }
 ******************************************************************************/

typedef uint8_t precog_key_class_t;
#define PRECOG_KEY_NONE      0
#define PRECOG_KEY_THUMB     1
#define PRECOG_KEY_HOME_ROW  2
#define PRECOG_KEY_SAME_HAND 3
#define PRECOG_KEY_OPP_HAND  4

typedef struct {
    uint8_t  home_row_count;       // Additional same-hand home-row keys pressed
    uint8_t  opp_hand_presses;     // Opposite-hand presses seen during ripening
    uint16_t combo_complete_time;  // When trigger completed
} precog_state_t;

/* User must provide: classify a key by its dense index for a given precog gesture. */
typedef precog_key_class_t (*precog_classify_t)(gesture_id_t id, uint16_t key_index);

#ifndef PRECOG_T1
#    define PRECOG_T1 150  // Hold timeout (thumb + single home-row key)
#endif

/*
 * Precog-specific utility functions for use in combo behavior overrides.
 */

gesture_timeout_t precog_unripe_timeout(gesture_id_t id, const gesture_event_t *event, int8_t which_key, combo_active_mask_t next_state, uint16_t remaining_ms,
                                        precog_state_t *state, precog_classify_t classify);

combo_active_update_t precog_activation(gesture_id_t id, const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state);

combo_active_update_t precog_release(gesture_id_t id, const gesture_event_t *event, int8_t which_key, combo_active_mask_t prev_state);
