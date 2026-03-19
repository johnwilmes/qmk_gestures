# Gesture System Case Studies

Worked examples showing how combos, tap-hold, tap dance, and precognition
(home-row mods) are implemented on top of the gesture system.

## 1. Combo

**Pattern**: Multiple physical keys pressed within a time window produce a
single virtual key.

**Source**: `combo.h`, `combo.c`

### Data model

```c
typedef struct {
    combo_active_mask_t active_state;  // Bitmask of which trigger keys are pressed
    gs_keys_t keys;                    // PROGMEM array: [count, key0, key1, ...]
} combo_data_t;
```

`DEFINE_COMBO_KEYS(name, ...)` generates the PROGMEM key array and a
`combo_data_t` with the correct pointer. `COMBO_GESTURE(name)` produces
the `gesture_t` initializer.

### Callback logic (combo_gesture_callback)

The combo callback uses a single function for all lifecycle phases. The
coordinator provides `query` to distinguish them.

**GS_QUERY_INITIAL**: If the event is a press of a trigger key, record it
in `active_state` and return a timeout. Non-trigger keys and releases are
rejected (timeout=0, outcome=false).

**GS_QUERY_PARTIAL / GS_QUERY_COMPLETE**: Track trigger key presses and
releases. If all trigger keys are now pressed (combo complete), delegate
to `get_unripe_combo_timeout` for ripening behavior. Release of a trigger
before completion cancels. Repress of a trigger cancels (hardcoded
invariant: combo requires simultaneous hold of all triggers).

**GS_QUERY_ACTIVATION_INITIAL / GS_QUERY_ACTIVATION_REPLAY**: Delegate to
`get_ripe_combo_activation`. Default: consume trigger key presses, pass
everything else.

**GS_QUERY_ACTIVE**: Delegate to `get_active_combo_release`. Default:
deactivate when any trigger key is released.

### Override points

The combo system provides four weak functions that can be overridden per
gesture ID via switch statements:

| Function | Phase | Default |
|----------|-------|---------|
| `get_partial_combo_timeout` | Partial (pre-completion) | `combo_contiguous`: interrupt on non-trigger press |
| `get_unripe_combo_timeout` | Complete but not yet activated | Activate immediately (timeout=0, outcome=true) |
| `get_ripe_combo_activation` | Activation buffer scan | `combo_consume_triggers`: consume trigger presses |
| `get_active_combo_release` | Active, checking for release | `combo_release_triggers`: deactivate on trigger release |

This separation allows different combos to have different trigger timing,
ripening conditions, consumption behavior, and release behavior -- all
without touching the core callback.

### Utility functions

- `combo_contiguous(event, which_key, remaining_ms, initial_timeout)`:
  Standard trigger timing. Returns `initial_timeout` on first call, continues
  on trigger events, interrupts on non-trigger presses.

- `combo_consume_triggers(event, which_key, prev_state)`: Consumes trigger
  key presses during activation, tracks state via bitmask.

- `combo_release_triggers(event, which_key, prev_state)`: Deactivates when
  any trigger key releases.

### Example: A+B combo

```c
DEFINE_COMBO_KEYS(ab, POS_A, POS_B)

gesture_t gestures[] = {
    COMBO_GESTURE(ab),  // gesture ID 0
};
```

Event sequence: A press at t=0, B press at t=20, A release at t=100,
B release at t=110.

1. A press: `GS_QUERY_INITIAL` -> trigger key, `active_state = 0b01`,
   returns (COMBO_TIMEOUT, false). Gesture becomes PARTIAL.
2. B press: `GS_QUERY_PARTIAL` -> trigger key, `active_state = 0b11`,
   combo complete. `get_unripe_combo_timeout` returns (0, true).
   Gesture proposes as candidate, activates.
3. Activation: virtual press at t=0. Buffer scan: A press consumed,
   B press consumed (both are triggers).
4. A release at t=100: `GS_QUERY_ACTIVE` ->
   `get_active_combo_release` -> trigger release, deactivate.
   Virtual release at t=100.

---

## 2. Tap-Hold

**Pattern**: A key tapped produces one virtual key; the same key held
produces a different virtual key.

**Source**: `tapdance.h`, `tapdance.c` (tap-hold is tapdance with max_taps=1)

### Data model

```c
// Shared per physical key
typedef struct {
    uint8_t  trigger_key;
    uint8_t  max_taps;       // 1 for tap-hold
    uint8_t  tap_count;
    bool     key_down : 1;
    bool     awaiting_press : 1;
} tapdance_data_t;

// Per gesture variant (one for hold, one for tap)
typedef struct {
    uint8_t         target_taps;
    bool            is_hold;
    tapdance_data_t *shared;
} tapdance_per_tap_t;
```

`DEFINE_TAPHOLD(name, trigger)` generates shared data and two per-tap
structs (hold at count 0, tap at count 1). `TAPHOLD_GESTURES(name)`
expands to two gesture_t initializers.

### MECE tap/hold

Tap and hold are mutually exclusive and collectively exhaustive. A single
override (`get_tapdance_hold_on_event`) defines hold behavior; tap behavior
is its negation. This means:
- If the override says "ripen hold" (outcome=true), the tap variant sees
  outcome=false (cancel tap).
- If the override says "cancel hold" (outcome=false), the tap variant sees
  outcome=true (ripen tap).

The coordinator distinguishes them by passing `GS_QUERY_COMPLETE` to
whichever variant would activate on timeout, and `GS_QUERY_PARTIAL` to
the other. Both share the same `tapdance_data_t` state and see the same
events.

### Callback logic (tapdance_gesture_callback)

`tapdance_should_activate(per_tap, shared)` determines if this variant
matches the current state:
- Hold: `tap_count >= target_taps && key_down`
- Tap: `tap_count >= target_taps && awaiting_press` (key was released)

**GS_QUERY_INITIAL**: Trigger press sets `key_down=true`, `tap_count=0`.
Returns timeout with `should_activate` as outcome.

**GS_QUERY_PARTIAL/COMPLETE**: Trigger release increments `tap_count` and
sets `awaiting_press=true`. Trigger re-press continues the dance. Non-trigger
events route through `get_tapdance_hold_on_event`, and tap variants negate
the outcome.

**GS_QUERY_ACTIVATION_INITIAL/REPLAY**: Hold variants consume trigger
presses and stay active (GESTURE_TIMEOUT_NEVER). Tap variants consume
trigger presses and deactivate immediately (timeout=0).

**GS_QUERY_ACTIVE**: Hold variants deactivate on trigger release. Tap
variants never reach this state (they deactivate during activation scan).

### Override points

| Function | Purpose | Default |
|----------|---------|---------|
| `get_tapdance_timeout` | Timeout duration per gesture | TAPDANCE_TIMEOUT (200ms) |
| `get_tapdance_hold_on_event` | Non-trigger event during PARTIAL | Continue waiting |

### Example: tap-hold on home row A

```c
DEFINE_TAPHOLD(home_a, KEY_INDEX_A)

gesture_t gestures[] = {
    TAPHOLD_GESTURES(home_a),
    // Expands to: hold(0) at index 0, tap(1) at index 1
    // Tap has higher priority (index 1 > index 0)
};
```

**Tap scenario**: A press at t=0, A release at t=80.

1. A press: Both gestures see `GS_QUERY_INITIAL`.
   - hold(0): `should_activate` = true (key_down, 0 >= 0). Returns (200, true). PARTIAL.
   - tap(1): `should_activate` = false (awaiting_press=false). Returns (200, false). PARTIAL.
2. A release at t=80: Both see event.
   - hold(0): `GS_QUERY_COMPLETE` (was going to activate on timeout). key_down=false,
     tap_count=1. `should_activate` = false (not key_down). Returns (0, false). Cancel.
   - tap(1): `GS_QUERY_PARTIAL` (was going to cancel on timeout). tap_count=1,
     awaiting_press=true. `should_activate` = true (1 >= 1, awaiting_press). Returns (0, true). Activate.
3. tap(1) activates. Virtual press at t=0. Activation scan: A press consumed, A release
   consumed, timeout=0. Deactivates. Virtual release. Instant tap.

**Hold scenario**: A press at t=0, B press at t=50 (opposite hand), A release at t=200.

1. A press: Both become PARTIAL (same as above).
2. B press at t=50: Non-trigger event.
   - hold(0): `get_tapdance_hold_on_event` returns (remaining, true) (default: continue).
     `should_activate` = true (key_down). Returns (remaining, true).
   - tap(1): Same override, but outcome negated. `should_activate` = false. Returns (remaining, false).
3. Timeout at t=200: hold(0) has `timeout_outcome=true` -> `propose_candidate(0)`.
   tap(1) has `timeout_outcome=false` -> cancel (return to INACTIVE).
   hold(0) activates. Virtual press at t=0. A press consumed, B press NOT consumed.
   Stays active (GESTURE_TIMEOUT_NEVER).
4. A release at t=200: `GS_QUERY_ACTIVE` -> deactivate. Virtual release.

**Hold with permissive-hold override**:

```c
gesture_timeout_t get_tapdance_hold_on_event(gesture_id_t id, const gesture_event_t *event, uint16_t remaining_ms) {
    if (event->pressed) {
        return GESTURE_TIMEOUT(0, true);  // Other key pressed -> ripen hold
    }
    return GESTURE_TIMEOUT(remaining_ms, true);
}
```

Now B press at t=50 causes hold(0) to return (0, true) -> activate immediately.
tap(1) sees (0, !true) = (0, false) -> cancel. No waiting for timeout.

---

## 3. Tap Dance

**Pattern**: Same key tapped N times produces different virtual keys based
on the count. Each count can have a tap variant (activate on release) and
a hold variant (activate on timeout while held).

**Source**: `tapdance.h`, `tapdance.c`

### Data model

Same as tap-hold, but `max_taps > 1` and multiple per-tap structs:

```c
DEFINE_TAPDANCE(my_td, KEY_INDEX_X, 3)

gesture_t gestures[] = {
    TAPDANCE_HOLD(my_td, 0),  // Just hold (no taps)
    TAPDANCE_TAP(my_td, 1),   // Single tap
    TAPDANCE_HOLD(my_td, 1),  // Tap then hold
    TAPDANCE_TAP(my_td, 2),   // Double tap
    TAPDANCE_HOLD(my_td, 2),  // Double tap then hold
    TAPDANCE_TAP(my_td, 3),   // Triple tap (max)
};
```

All six gestures share one `tapdance_data_t` and trigger on the same key.
Priority order: last entry has highest priority.

### Dance resolution

The dance unfolds through the PARTIAL phase:

1. First press: all variants become PARTIAL. `tap_count=0`, `key_down=true`.
2. First release: `tap_count` increments to 1. Timeout resets.
   - TAP(1) now has `should_activate` = true (tap_count >= 1, awaiting_press).
   - If timeout fires: TAP(1) has highest priority among "would activate"
     variants -> activates.
3. Second press: `key_down=true`, `awaiting_press=false`. Timeout resets.
   - HOLD(1) now has `should_activate` = true (tap_count >= 1, key_down).
4. Second release: `tap_count` increments to 2. Timeout resets.
   - TAP(2) now matches. If timeout fires: TAP(2) wins (higher priority than TAP(1)).
5. Max reached (3 releases): `tap_count >= max_taps` -> immediate activation
   with timeout=0, activating the highest-matching variant.

### Priority determines which count wins

When timeout fires, multiple variants may have `should_activate` = true
(e.g., both TAP(1) and TAP(2) match after two taps). The coordinator's
priority system resolves this: TAP(2) has a higher gesture ID, so it wins
and TAP(1) is evicted.

### Tap-only convenience

```c
gesture_t gestures[] = {
    TAPDANCE_TAPS(my_td, 3),
    // Expands to: TAP(1), TAP(2), TAP(3)
    // No hold variants -- timeout always activates the highest matching tap
};
```

---

## 4. Precognition (Home-Row Mods via Combo Overrides)

**Pattern**: Thumb + home-row key combo activates a modifier layer for
that hand. The trigger keys are NOT consumed -- they pass through to the
downstream layer so the home-row key gets its modified meaning.

**Source**: `precog.h`, `precog.c`, built on `combo.h/c`

### Architecture

Precog is not a separate gesture type. It is a set of combo override
utility functions. Each precog gesture is a standard combo (thumb + one
home-row key) with customized ripening, activation, and release behavior.

There are typically 8 precog combos: 4 per hand (thumb + index, thumb +
middle, thumb + ring, thumb + pinky).

### Key classification

The precog callback receives a user-provided classifier function:

```c
typedef precog_key_class_t (*precog_classify_t)(gesture_id_t id, uint8_t key_index);
```

Classifications:
- `PRECOG_KEY_HOME_ROW`: Same-hand home-row key (not a trigger)
- `PRECOG_KEY_SAME_HAND`: Same-hand non-home-row key
- `PRECOG_KEY_OPP_HAND`: Opposite-hand key

### Override wiring

```c
gesture_timeout_t get_unripe_combo_timeout(gesture_id_t combo, ...) {
    switch (combo) {
        case GS_PRECOG_LI:
            return precog_unripe_timeout(combo, event, which_key,
                next_state, remaining_ms, &precog_li_state, &classify_left);
        // ... one case per precog combo ...
        default:
            return GESTURE_TIMEOUT(0, true);  // Other combos: activate immediately
    }
}

combo_active_update_t get_ripe_combo_activation(gesture_id_t combo, ...) {
    switch (combo) {
        case GS_PRECOG_LI: case GS_PRECOG_LM: /* ... */
            return precog_activation(combo, event, which_key, prev_state);
        default:
            return combo_consume_triggers(event, which_key, prev_state);
    }
}

combo_active_update_t get_active_combo_release(gesture_id_t combo, ...) {
    switch (combo) {
        case GS_PRECOG_LI: case GS_PRECOG_LM: /* ... */
            return precog_release(combo, event, which_key, prev_state);
        default:
            return combo_release_triggers(event, which_key, prev_state);
    }
}
```

### Ripening conditions (precog_unripe_timeout)

After both trigger keys are pressed (combo complete), the precog
gesture waits for one of several conditions:

| Condition | Action |
|-----------|--------|
| Hold for PRECOG_T1 (150ms) | Activate on timeout |
| Additional same-hand home-row key pressed | Activate immediately |
| Opposite-hand key pressed | Activate immediately |
| Same-hand non-home-row key pressed | Cancel |
| Trigger key released | Cancel (handled by combo callback before precog is called) |

### Activation (precog_activation)

Returns `consume_press = false` for all events. This is the key difference
from normal combos: trigger keys pass through to downstream processing.
The virtual press (layer activation) happens before the trigger keys
are emitted, so downstream sees the triggers with the mod layer active.

Convention: trigger key index 0 is the "gating" key (thumb).

### Release (precog_release)

Deactivates when the gating key (index 0, thumb) releases. Other trigger
key releases update the state bitmask but keep the gesture active.

### Per-combo state

```c
typedef struct {
    uint8_t  home_row_count;       // Additional same-hand home-row keys pressed
    uint8_t  opp_hand_presses;     // Opposite-hand presses during ripening
    uint16_t combo_complete_time;  // When all triggers were pressed
} precog_state_t;
```

Each precog combo has its own static `precog_state_t`, looked up by gesture
ID in the override switch statements.

### Example: Left index precog

```c
DEFINE_COMBO_KEYS(precog_li, POS_L_THUMB, POS_L_INDEX_H)
static precog_state_t precog_li_state = {0};
```

Event sequence: L_THUMB press at t=0, L_INDEX press at t=15,
R_INDEX press at t=60 (opposite hand).

1. L_THUMB press: combo triggers, PARTIAL. Waiting for L_INDEX.
2. L_INDEX press at t=15: combo complete. `precog_unripe_timeout` called
   with remaining_ms=0 (just completed). Sets `combo_complete_time=15`.
   Returns (PRECOG_T1=150, true). PARTIAL with activate-on-timeout.
3. R_INDEX press at t=60: `precog_unripe_timeout` called. Classifier
   returns `PRECOG_KEY_OPP_HAND`. `opp_hand_presses++`. Returns (0, true).
   Activate immediately.
4. Activation: virtual press at t=0. Buffer scan: L_THUMB press -- not
   consumed (precog_activation returns consume=false). L_INDEX press -- not
   consumed. R_INDEX press -- not consumed. All three pass through with the
   mod layer active.
5. L_THUMB release: `precog_release` -> gating key released -> deactivate.
   Virtual release. Mod layer off.

---

## 5. Design patterns

### Shared user_data

Multiple gestures can share the same `user_data` pointer. Tap-hold and
tap dance use this: the hold and tap variants for the same physical key
share a `tapdance_data_t` so they track press/release state together.

### Weak overrides

The combo and tap dance systems define weak default functions for behavior
customization. User code overrides these with switch statements on gesture
ID, routing different gestures to different behavior functions. This avoids
callback proliferation while keeping the core logic generic.

### Combo as a platform

Precog demonstrates that the combo callback + weak overrides is a
composable platform. Any combo variant -- auto-shift, one-shot, leader
combos -- can be implemented as a set of utility functions wired through
the same override pattern, without modifying the combo callback itself.

### MECE duality

The tap/hold pattern (and by extension, tap-dance) uses a MECE duality:
one override controls both variants. This eliminates the class of bugs
where tap and hold conditions overlap or leave gaps. It also halves the
number of overrides users need to think about.
