# QMK Feature Audit

How QMK quantum features interact with our reentry strategy
(`process_record` with `record->keycode` pre-set, skipping
`pre_process_record_quantum` and `action_exec`).

## Features we replace

| Feature | Define to disable | Notes |
|---------|-------------------|-------|
| Action tapping | `NO_ACTION_TAPPING` | Our gesture system handles tap-hold |
| Combos | remove `COMBO_ENABLE` | Our gesture system handles combos |
| Tap dance | remove `TAP_DANCE_ENABLE` | Our gesture system handles tap dance |
| Leader key | remove `LEADER_ENABLE` | Our gesture system handles leader |
| Layer lookup | n/a (bypassed via `record->keycode`) | Our layer system resolves keycodes |

## Features we keep and reuse

### Repeat Key (`REPEAT_KEY_ENABLE`) — REQUIRED

Provides the `keyrecord_t.keycode` field we need. Hooks into
`process_record_quantum` via `process_last_key` and `process_repeat_key`.
Works correctly with our `process_record` entry point.

Repeat key internally calls `process_record(&registered_record)` to replay,
which goes through the same chain. Uses `processing_repeat_count` flag to
prevent recursion.

**Verdict: works, required.**

### One-shot mods (`NO_ACTION_ONESHOT` NOT defined) — WORKS WITH FIX

Core logic in `process_action` (which we hit via `process_record_handler`).
Handles `OSM()` keycodes, sticky/release behavior, modifier application.

**Missing piece:** `action_exec` lines 109-125 check
`has_oneshot_mods_timed_out()` and `has_oneshot_layer_timed_out()` on every
event. We skip `action_exec`. Fix: add timeout checks in `matrix_scan_user`.

**Verdict: works with matrix_scan_user timeout fix.**

### Key Override (`KEY_OVERRIDE_ENABLE`)

Hooks into `process_record_quantum` via `process_key_override`. Intercepts
keycodes and substitutes overrides. Reads `get_record_keycode` which returns
our pre-set keycode.

Could be useful for locale-specific overrides (e.g., Shift+2 = @ on US
layout). Works transparently since it sees our resolved keycode.

**Verdict: works, potentially useful.**

### Caps Word (`CAPS_WORD_ENABLE`)

Hooks into `process_record_quantum` via `process_caps_word`. Checks keycodes
to decide whether to add shift or deactivate caps word. Uses
`get_record_keycode` which returns our pre-set keycode.

**Verdict: works transparently.**

### Autocorrect (`AUTOCORRECT_ENABLE`)

Hooks into `process_record_quantum`. Monitors typed keycodes against a
dictionary, injects corrections via `tap_code`. Uses `get_record_keycode`.

**Verdict: works transparently.**

### Auto Shift (`AUTO_SHIFT_ENABLE`)

Hooks into `process_record_quantum` via `process_auto_shift`. Adds its own
timing logic to decide if a key should be shifted. This conflicts with our
gesture system's timing model — auto shift wants to buffer and delay keys
based on hold duration, which we've already resolved.

**Verdict: CONFLICTS. Disable if using gesture-based tap-hold.**

### Dynamic Macros (`DYNAMIC_MACRO_ENABLE`)

Hooks into `process_record_quantum` via `process_dynamic_macro`. Records
and plays back keystrokes. Saves/restores `layer_state` during playback.
Works with our synced layer state.

**Verdict: works.**

### Send String / Macros

QMK's `SEND_STRING` and macro system use `register_code`/`unregister_code`
directly, bypassing the record processing chain. These work regardless of
our reentry strategy since they operate at the HID level.

Custom macros triggered via `process_record_user` or `process_record_kb`
would still fire in `process_record_quantum` — but we'd need to be careful
that macro trigger keycodes are in our layer maps.

**Verdict: works.**

### Swap Hands (`SWAP_HANDS_ENABLE`)

Processed in `action_exec` (lines 100-105) BEFORE `process_record`. We skip
`action_exec`, so swap hands events are never processed.

If swap hands is needed, either:
- Add `process_hand_swap(&event)` in `gesture_emit_event` before calling
  `process_record`, or
- Handle hand swapping in our layer system (swap physical key indices).

**Verdict: broken if needed. Fixable.**

### Mouse Keys (`MOUSEKEY_ENABLE`)

Mouse key keycodes (`KC_MS_U`, etc.) are handled in `process_action` via
the `ACT_MOUSEKEY` action type. This is in our call path.

Acceleration/deceleration is tick-driven via `mousekey_task()` called from
`quantum_task()`, which runs independently of our processing.

**Verdict: works.**

### RGB / LED / OLED

These respond to `layer_state` changes via callbacks
(`layer_state_set_user`). Since we sync our layer state into QMK's global,
these work automatically.

**Verdict: works via layer_state sync.**

## Features in `pre_process_record_quantum` that we skip

Our `process_record` entry point bypasses `pre_process_record_quantum`.
Here's what runs there:

| Feature | Function | Impact of skipping |
|---------|----------|--------------------|
| Module pre-processing | `pre_process_record_modules` | Depends on enabled modules |
| `pre_process_record_kb/user` | User hooks | Our gesture hook lives here — MUST skip |
| Combo processing | `process_combo` | We handle combos — correct to skip |

The `pre_process_record_modules` call is part of QMK's community modules
system — a build-time code generation system where modules are listed in
`keymap.json`. The halcyon keyboard does not use community modules; it uses
traditional feature flags (`ENCODER_ENABLE`, `OLED_ENABLE`, etc.). These
traditional features run from `keyboard_task()` in the main loop, not
through the modules hook. The `pre_process_record_modules` weak default
returns true (no-op), so skipping it is safe.

Note: splitkb's "modules" (OLEDs, encoders, trackpads) are hardware modules
that plug into the halcyon PCB. They are configured as standard QMK features,
not as community modules.

## Features in `action_exec` that we skip

| Feature | Lines | Impact | Fix |
|---------|-------|--------|-----|
| Retro tapping | 84-92 | Irrelevant (we handle tap-hold) | None needed |
| Weak mods clearing | 95-98 | Mods cleared on press | Add to gesture_emit_event if needed |
| Swap hands | 100-105 | Broken if used | Add swap processing |
| Oneshot timeout | 109-125 | Stuck OSM after timeout | Add to matrix_scan_user |

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
| Auto shift | Conflicts — disable |
| Swap hands | Broken — fixable if needed |
| Action tapping | Replaced — disable |
| Combos | Replaced — disable |
| Tap dance | Replaced — disable |
| Leader key | Replaced — disable |
