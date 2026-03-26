# QMK Feature Audit

How QMK quantum features interact with the gesture module's reentry
strategy (`process_record` with `record->keycode` pre-set, skipping
`pre_process_record_quantum` and `action_exec`).

## Features we replace

| Feature | Define to disable | Notes |
|---------|-------------------|-------|
| Action tapping | `NO_ACTION_TAPPING` | Gesture system handles tap-hold |
| Combos | remove `COMBO_ENABLE` | Gesture system handles combos |
| Tap dance | remove `TAP_DANCE_ENABLE` | Gesture system handles tap dance |
| Leader key | remove `LEADER_ENABLE` | Gesture system handles leader |
| Layer lookup | n/a (bypassed via `record->keycode`) | Layer system resolves keycodes |

## Features we keep and reuse

### Repeat Key (`REPEAT_KEY_ENABLE`) -- REQUIRED

Provides the `keyrecord_t.keycode` field. Hooks into
`process_record_quantum` via `process_last_key` and `process_repeat_key`.
Works correctly with our `process_record` entry point.

**Verdict: works, required.**

### One-shot mods (`NO_ACTION_ONESHOT` NOT defined) -- WORKS WITH FIX

Core logic in `process_action` (which we hit). Missing piece:
`action_exec` checks `has_oneshot_mods_timed_out()`. Fix: the timeout
check runs in `housekeeping_task_gestures`.

**Verdict: works with housekeeping timeout fix.**

### Key Override (`KEY_OVERRIDE_ENABLE`)

Hooks into `process_record_quantum` via `process_key_override`. Reads
`get_record_keycode` which returns our pre-set keycode.

**Verdict: works.**

### Caps Word (`CAPS_WORD_ENABLE`)

Hooks into `process_record_quantum`. Uses `get_record_keycode`.

**Verdict: works.**

### Autocorrect (`AUTOCORRECT_ENABLE`)

Hooks into `process_record_quantum`. Monitors keycodes, injects
corrections via `tap_code`. Uses `get_record_keycode`.

**Verdict: works.**

### Auto Shift (`AUTO_SHIFT_ENABLE`)

Adds its own timing logic to decide if a key should be shifted. This
conflicts with the gesture system's timing model.

**Verdict: CONFLICTS. Disable if using gesture-based tap-hold.**

### Dynamic Macros (`DYNAMIC_MACRO_ENABLE`)

Records and plays back keystrokes. Works with our synced layer state.

**Verdict: works.**

### Send String / Macros

`SEND_STRING` and macro system use `register_code`/`unregister_code`
directly, bypassing the record processing chain. Custom macros triggered
via `process_record_user` fire in `process_record_quantum`.

**Verdict: works.**

### Swap Hands (`SWAP_HANDS_ENABLE`)

Processed in `action_exec` BEFORE `process_record`. We skip `action_exec`,
so swap hands events are never processed.

**Verdict: broken if needed. Fixable by adding swap processing in
gesture_emit_event.**

### Mouse Keys (`MOUSEKEY_ENABLE`)

Mouse key keycodes handled in `process_action` via `ACT_MOUSEKEY`.
Acceleration is tick-driven via `mousekey_task()`.

**Verdict: works.**

### RGB / LED / OLED

Respond to `layer_state` changes via callbacks (`layer_state_set_user`).
Works because the layer system modifies QMK's `layer_state` via standard
functions.

**Verdict: works.**

## Features in `pre_process_record_quantum` that we skip

Our `process_record` entry bypasses `pre_process_record_quantum`:

| Feature | Function | Impact |
|---------|----------|--------|
| Module pre-processing | `pre_process_record_modules` | Other community modules' pre-processing is skipped for re-emitted events |
| `pre_process_record_kb/user` | User hooks | Our gesture hook lives here -- MUST skip |
| Combo processing | `process_combo` | We handle combos -- correct to skip |

## Features in `action_exec` that we skip

| Feature | Impact | Fix |
|---------|--------|-----|
| Retro tapping | Irrelevant (we handle tap-hold) | None |
| Weak mods clearing | Mods cleared on press | Add to gesture_emit_event if needed |
| Swap hands | Broken if used | Add swap processing |
| Oneshot timeout | Stuck OSM after timeout | Fixed in housekeeping_task |

## Summary

| Category | Status |
|----------|--------|
| Basic keycodes, modifiers, media | Works |
| Repeat key | Works (required) |
| One-shot mods | Works with timeout fix |
| Caps word | Works |
| Key override | Works |
| Autocorrect | Works |
| Dynamic macros | Works |
| Mouse keys | Works |
| RGB/LED/OLED | Works via layer_state sync |
| Auto shift | Conflicts -- disable |
| Swap hands | Broken -- fixable if needed |
| Action tapping | Replaced -- disable |
| Combos | Replaced -- disable |
| Tap dance | Replaced -- disable |
| Leader key | Replaced -- disable |
