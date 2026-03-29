# Gesture System Case Studies

Worked examples showing how combos, hold, tap dance, and precognition
(home-row mods) are implemented on top of the gesture system.

## 1. Combo

**Pattern**: Multiple physical keys pressed within a time window produce a
single virtual key.

**Source**: `types/combo.h`, `types/combo.c`

### Data model

```c
typedef struct {
    combo_active_mask_t active_state;  // Bitmask of which trigger keys are pressed
    gs_keys_t keys;                    // PROGMEM array: [count, key0, key1, ...]
} combo_data_t;
```

`DEFINE_COMBO(name, ...)` generates the PROGMEM key array, a `combo_data_t`,
and a `gesture_t _gs_##name` with the combo callback and 1 outcome.

### Definition

```c
DEFINE_COMBO(esc_combo, POS_Q, POS_W)

enum { GS(esc_combo) };
DEFINE_GESTURES_MANUAL(GESTURE_ENTRY(esc_combo));

DEFINE_GESTURE_LAYER(base_gestures,
    GESTURE_MAP(esc_combo, KC_ESC),
);
```

### Callback logic (combo_gesture_callback)

**GS_QUERY_INITIAL**: If the event is a press of a trigger key, record it
in `active_state` and return a timeout. Non-trigger keys and releases are
rejected (timeout=0, outcome=0).

**GS_QUERY_PARTIAL / GS_QUERY_COMPLETE**: Track trigger key presses and
releases. If all trigger keys are now pressed (combo complete), delegate
to `get_unripe_combo_timeout` for ripening behavior. Release of a trigger
before completion cancels.

**GS_QUERY_ACTIVATION_INITIAL / GS_QUERY_ACTIVATION_REPLAY**: Delegate to
`get_ripe_combo_activation`. Default: consume trigger key presses, pass
everything else.

**GS_QUERY_ACTIVE**: Delegate to `get_active_combo_release`. Default:
deactivate when any trigger key is released.

### Override points

| Function | Phase | Default |
|----------|-------|---------|
| `get_partial_combo_timeout` | Pre-completion | `combo_contiguous`: interrupt on non-trigger press |
| `get_unripe_combo_timeout` | Complete, not yet activated | Activate immediately |
| `get_ripe_combo_activation` | Activation buffer scan | `combo_consume_triggers`: consume trigger presses |
| `get_active_combo_release` | Active, checking for release | `combo_release_triggers`: deactivate on trigger release |

### Example: A+B combo

```c
DEFINE_COMBO(ab, POS_A, POS_B)
```

Event sequence: A press at t=0, B press at t=20, A release at t=100,
B release at t=110.

1. A press: `GS_QUERY_INITIAL` -> trigger key, `active_state = 0b01`,
   returns (COMBO_TIMEOUT, 0). Gesture becomes PARTIAL.
2. B press: `GS_QUERY_PARTIAL` -> trigger key, `active_state = 0b11`,
   combo complete. `get_unripe_combo_timeout` returns (0, 1).
   Gesture activates with outcome=1.
3. Activation: virtual press at t=0. Buffer scan: A press consumed,
   B press consumed (both are triggers).
4. A release at t=100: `GS_QUERY_ACTIVE` ->
   `get_active_combo_release` -> trigger release, deactivate.
   Virtual release at t=100.

---

## 2. Hold

**Pattern**: A key tapped produces one virtual key; the same key held
produces a different virtual key.

**Source**: `types/tapdance.h`, `types/tapdance.c` (hold is tapdance with
max_presses=1)

### Data model

```c
typedef struct {
    uint8_t trigger_key;     // Dense key index
    uint8_t max_presses;     // 1 for hold
    uint8_t press_count;     // Current press count (1-indexed)
    bool    key_down;        // Whether trigger key is currently held
} tapdance_data_t;
```

`DEFINE_HOLD(name, trigger)` creates a `tapdance_data_t` with
`max_presses=1` and a `gesture_t _gs_##name` with 1 outcome.

### Definition

```c
DEFINE_HOLD(home_a, POS_A)

enum { GS(home_a) };
DEFINE_GESTURES_MANUAL(GESTURE_ENTRY(home_a));

DEFINE_GESTURE_LAYER(base_gestures,
    GESTURE_MAP(home_a, KC_LGUI),
);
```

### Multi-outcome model

Unlike the old per-tap gesture model, hold is a single gesture with a
single outcome (outcome=1 = hold). A single tap (press and release within
the timeout) causes the gesture to cancel (outcome=0), and the physical
key events pass through unconsumed to the base keymap.

### Callback logic

**GS_QUERY_INITIAL**: Trigger press sets `press_count=1`, `key_down=true`.
Returns timeout with `hold_outcome(1)` = outcome 1.

**GS_QUERY_PARTIAL/COMPLETE**: Trigger release sets `key_down=false`. Since
`max_presses=1` and the key is released, the gesture is fully resolved ->
cancel (timeout=0, outcome=0). Non-trigger events route through
`get_tapdance_on_event` override.

If the timeout fires while the key is held, the gesture activates with
outcome=1 (hold).

**GS_QUERY_ACTIVATION_INITIAL/REPLAY**: Consume trigger press events. Stay
active while key is held.

**GS_QUERY_ACTIVE**: Deactivate on trigger release.

### Override points

| Function | Purpose | Default |
|----------|---------|---------|
| `get_tapdance_timeout` | Timeout duration | TAPDANCE_TIMEOUT (200ms) |
| `get_tapdance_on_event` | Non-trigger event during PARTIAL | Continue waiting |

### Example: hold on home row A

```c
DEFINE_HOLD(home_a, POS_A)
```

**Tap scenario**: A press at t=0, A release at t=80.

1. A press: `GS_QUERY_INITIAL`. `press_count=1`, `key_down=true`.
   Returns (200, 1). PARTIAL, would activate hold on timeout.
2. A release at t=80: `GS_QUERY_COMPLETE`. `key_down=false`.
   Resolved: `press_count=1`, not held -> cancel. Returns (0, 0).
   Gesture returns to INACTIVE. Buffer unblocked. A press and A release
   pass through to base keymap as KC_A.

**Hold scenario**: A press at t=0, timeout at t=200, A release at t=300.

1. A press: Same as above. PARTIAL with timeout at t=200.
2. Timeout at t=200: `timeout_outcome=1` -> activate. Virtual press with
   `{gesture_id=GS_home_a, outcome=1}`. Layer resolves to KC_LGUI.
   Buffer scan: A press consumed.
3. A release at t=300: `GS_QUERY_ACTIVE` -> trigger release -> deactivate.
   Virtual release. KC_LGUI unregistered.

**Permissive-hold override**:

```c
gesture_timeout_t get_tapdance_on_event(gesture_id_t id,
    const gesture_event_t *event, const tapdance_data_t *data,
    uint16_t remaining_ms) {
    return tapdance_hold_on_other_key(event, data, remaining_ms);
}
```

Now any non-trigger key pressed while the trigger is held causes immediate
activation (hold outcome), without waiting for the timeout.

---

## 3. Tap Dance

**Pattern**: Same key tapped N times produces different virtual keys based
on count. Each count has a hold variant (activate while held) and a tap
variant (activate on release after count completes).

**Source**: `types/tapdance.h`, `types/tapdance.c`

### Data model

Same struct as hold, but `max_presses > 1`:

```c
DEFINE_TAPDANCE(my_td, POS_X, 3)
// Creates tapdance_data_t with max_presses=3
// Creates gesture_t _gs_my_td with num_outcomes = 2*3-1 = 5
```

### Outcome encoding

For `max_presses=N`, there are `2N-1` outcomes:

| Outcome | Meaning |
|---------|---------|
| 0 | Cancel (single tap falls through) |
| 1 | hold(1) |
| 2 | tap(2) (double-tap) |
| 3 | hold(2) (tap then hold) |
| 4 | tap(3) (triple-tap) |
| 5 | hold(3) (double-tap then hold) |

General: `hold(n) = 2n-1`, `tap(n) = 2(n-1)` for n>=2.

### Layer mapping

```c
DEFINE_GESTURE_LAYER(base_gestures,
    GESTURE_MAP(my_td, KC_LSFT, KC_X, KC_LCTL, KC_Y, KC_LALT),
    //                hold(1)  tap(2) hold(2)  tap(3) hold(3)
);
```

`GESTURE_MAP` expands to a compound literal keycode array with the correct
outcome numbers.

### Dance resolution

The single gesture tracks the full press sequence:

1. First press: PARTIAL. `press_count=1`, `key_down=true`.
2. First release: `key_down=false`. `press_count=1`, not held ->
   single tap -> cancel (outcome=0). But if timeout hasn't fired yet and
   `max_presses>1`, the gesture stays PARTIAL waiting for more taps.
3. Second press: `press_count=2`, `key_down=true`. Timeout resets.
   `current_best_outcome` = hold(2) = outcome 3.
4. Second release: `press_count=2`, not held.
   `current_best_outcome` = tap(2) = outcome 2.
5. If max reached (3 releases): immediate activation with the best
   matching outcome.

### Example: double-tap

```c
DEFINE_TAPDANCE(td, POS_B, 2)

DEFINE_GESTURE_LAYER(base_gestures,
    GESTURE_MAP(td, KC_LSFT, KC_X, KC_LCTL),
);
```

Event sequence: B press at t=0, B release at t=50, B press at t=100,
B release at t=130.

1. B press at t=0: PARTIAL. `press_count=1`, `key_down=true`.
   Returns (200, 1) -- hold(1).
2. B release at t=50: `key_down=false`. Not resolved yet (max_presses=2).
   Returns (200, 0) -- cancel direction (single tap).
3. B press at t=100: `press_count=2`, `key_down=true`.
   Returns (200, 3) -- hold(2).
4. B release at t=130: `press_count=2`, `key_down=false`. Fully resolved
   (`press_count == max_presses && !key_down`).
   Returns (0, 2) -- tap(2). Activate immediately.
5. Activation: virtual press with outcome=2. Layer resolves to KC_X.
   Buffer scan: all B presses consumed. Gesture deactivates (tap outcome).
   Virtual release. Instant tap of KC_X.

---

## 4. Precognition (Home-Row Mods via Combo Overrides)

**Pattern**: Thumb + home-row key combo activates a modifier. Trigger
keys are NOT consumed -- they pass through so the home-row key gets its
modified meaning.

**Source**: `types/precog.h`, `types/precog.c`, built on `types/combo.h/c`

### Architecture

Precog is not a separate gesture type. It is a set of combo override
utility functions. Each precog gesture is a standard combo (thumb + one
home-row key) with customized ripening, activation, and release behavior.

### Definition

```c
DEFINE_COMBO(precog_li, POS_L_THUMB, POS_L_INDEX_H)
DEFINE_COMBO(precog_lm, POS_L_THUMB, POS_L_MIDDLE_H)

static precog_state_t precog_li_state = {0};
static precog_state_t precog_lm_state = {0};
```

### Key classification

The precog callback receives a user-provided classifier function:

```c
typedef precog_key_class_t (*precog_classify_t)(gesture_id_t id, uint16_t key_index);
```

Classifications: `PRECOG_KEY_THUMB`, `PRECOG_KEY_HOME_ROW`,
`PRECOG_KEY_SAME_HAND`, `PRECOG_KEY_OPP_HAND`.

### Override wiring

```c
gesture_timeout_t get_unripe_combo_timeout(gesture_id_t combo, ...) {
    switch (combo) {
        case GS(precog_li):
            return precog_unripe_timeout(combo, event, which_key,
                next_state, remaining_ms, &precog_li_state, &classify_left);
        // ... one case per precog combo ...
        default:
            return GESTURE_TIMEOUT(0, 1);  // Other combos: activate immediately
    }
}

combo_active_update_t get_ripe_combo_activation(gesture_id_t combo, ...) {
    switch (combo) {
        case GS(precog_li): case GS(precog_lm): /* ... */
            return precog_activation(combo, event, which_key, prev_state);
        default:
            return combo_consume_triggers(event, which_key, prev_state);
    }
}

combo_active_update_t get_active_combo_release(gesture_id_t combo, ...) {
    switch (combo) {
        case GS(precog_li): case GS(precog_lm): /* ... */
            return precog_release(combo, event, which_key, prev_state);
        default:
            return combo_release_triggers(event, which_key, prev_state);
    }
}
```

### Ripening conditions (precog_unripe_timeout)

After both trigger keys are pressed (combo complete), the precog gesture
waits for one of several conditions:

| Condition | Action |
|-----------|--------|
| Hold for PRECOG_T1 (150ms) | Activate on timeout |
| Additional same-hand home-row key pressed | Activate immediately |
| Opposite-hand key pressed | Activate immediately |
| Same-hand non-home-row key pressed | Cancel |
| Trigger key released | Cancel (handled by combo callback) |

### Activation (precog_activation)

Returns `consume_press = false` for all events. Trigger keys pass through
to downstream processing. The virtual press (layer activation) happens
before the trigger keys are emitted, so downstream sees the triggers with
the mod layer active.

### Release (precog_release)

Deactivates when the gating key (index 0, thumb) releases. Other trigger
key releases update the state but keep the gesture active.

### Example: Left index precog

Event sequence: L_THUMB press at t=0, L_INDEX press at t=15,
R_INDEX press at t=60 (opposite hand).

1. L_THUMB press: combo triggers, PARTIAL.
2. L_INDEX press at t=15: combo complete. `precog_unripe_timeout` sets
   `combo_complete_time=15`. Returns (150, 1). PARTIAL with activate-on-timeout.
3. R_INDEX press at t=60: `precog_unripe_timeout` classifies as
   `PRECOG_KEY_OPP_HAND`. Returns (0, 1). Activate immediately.
4. Activation: virtual press at t=0. Buffer scan: L_THUMB, L_INDEX,
   R_INDEX all NOT consumed (precog_activation returns consume=false).
   All three pass through with the mod layer active.
5. L_THUMB release: `precog_release` -> gating key -> deactivate.

---

## 5. Design patterns

### Gesture-per-key with multi-outcome

Each physical key that participates in gestures has exactly one gesture
object (one `DEFINE_HOLD`, `DEFINE_TAPDANCE`, or `DEFINE_COMBO`). Multiple
possible behaviors (tap, hold, double-tap, etc.) are encoded as different
outcomes of that single gesture.

This is simpler than having separate gesture objects for tap vs hold:
one callback tracks the full state, and the outcome tells the layer system
which keycode to use.

### Weak overrides

The combo and tap dance systems define weak default functions for behavior
customization. User code overrides these with switch statements on gesture
ID, routing different gestures to different utility functions. This avoids
callback proliferation while keeping the core logic generic.

### Combo as a platform

Precog demonstrates that the combo callback + weak overrides is a
composable platform. Any combo variant can be implemented as utility
functions wired through the same override pattern, without modifying the
combo callback itself.

### Layer mapping macros

The `GESTURE_MAP` and `ENCODER_MAP` macros abstract away outcome numbering.
`GESTURE_MAP(name, kc1, kc2, ...)` expands to a compound literal keycode
array. Users list keycodes in outcome order without needing to know the
encoding scheme.
