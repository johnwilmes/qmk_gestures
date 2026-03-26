# QMK Integration -- Design Discussion

## Context

The gesture system outputs resolved events via `gesture_emit_event`. Each
event is a `gesture_event_t` with:
- `event_type_t type`: `EVENT_TYPE_KEY`, `EVENT_TYPE_GESTURE`, or
  `EVENT_TYPE_ENCODER`
- Type-specific union: `event_id` (key), `gesture.{gesture_id, outcome}`
  (gesture), `encoder.{encoder_id, clockwise, count}` (encoder)
- `uint16_t time`: event timestamp
- `bool pressed`: press or release

The layer system resolves these to keycodes and hands them to QMK.

## Goals

1. **Layers over all event types.** A single layer stack covers physical
   keys, gesture outcomes, and encoders uniformly.

2. **No action keycodes.** Keycodes like `LT()`, `MT()`, `TD()` encode
   trigger conditions the gesture system already resolved. Every keycode
   in the layer map is a concrete outcome.

3. **Type-specific layer formats.** Physical keys use dense or sparse
   arrays. Gestures use `(gesture_id, outcome, keycode)` triples. Encoders
   use `(encoder_id, ccw, cw)` pairs. Each format is optimized for its
   use case.

4. **Binding safety.** Press binds to a keycode. Release undoes that exact
   keycode regardless of layer changes.

5. **Hand back to QMK.** After resolving, pass to QMK's existing keycode
   execution.

## QMK reentry strategy

### The `record->keycode` override

When `REPEAT_KEY_ENABLE` is defined, `keyrecord_t` has a `keycode` field.
QMK checks this in `get_record_keycode` and `process_record_handler` --
if set, both keycode and action lookup bypass QMK's layer system entirely.

### Emission path

```c
void gesture_emit_event(gesture_event_t event) {
    // Dispatch by event type to the appropriate resolver
    if (event.type == EVENT_TYPE_ENCODER) {
        uint16_t keycode = resolve_encoder(event.encoder.encoder_id,
                                            event.encoder.clockwise);
        // Expand count into N press+release pairs
        emit_encoder_to_qmk(event, keycode);
        return;
    }

    if (event.pressed) {
        uint16_t keycode;
        if (event.type == EVENT_TYPE_GESTURE) {
            keycode = resolve_gesture(event.gesture.gesture_id,
                                       event.gesture.outcome);
        } else {
            keycode = resolve_key(event.event_id);
        }
        binding_store(event.type, event.event_id, keycode);
        emit_to_qmk(event, keycode);
    } else {
        uint16_t keycode = binding_lookup_and_clear(event.type, event.event_id);
        emit_to_qmk(event, keycode);
    }
}
```

`emit_to_qmk` constructs a `keyrecord_t` with `record->keycode` set and
calls `process_record` directly. This:
- Skips `pre_process_record_quantum` (no reentrancy into our gesture hook)
- Skips `action_tapping_process` (not going through `action_exec`)
- Runs `process_record_quantum` which uses `record->keycode`
- QMK handles `register_code`/`unregister_code`, HID reports

### Reentrancy safety

Calling `process_record` directly (not `action_exec`) avoids reentrancy
because `process_record` does not call `pre_process_record_quantum`. Our
`pre_process_record_gestures` hook is never reached:

```
gesture_emit_event
  -> resolve_key / resolve_gesture / resolve_encoder
  -> process_record (QMK)
    -> process_record_quantum (uses record->keycode)
    -> process_record_handler (uses record->keycode)
      -> process_action (register/unregister/HID)
```

### QMK features to disable

| Feature | Define | Why |
|---------|--------|-----|
| Action tapping | `NO_ACTION_TAPPING` | Gesture system handles tap-hold |
| QMK combos | disable `COMBO_ENABLE` | Gesture system handles combos |
| Tap dance | disable `TAP_DANCE_ENABLE` | Gesture system handles tap dance |
| Leader key | disable `LEADER_ENABLE` | Gesture system handles leader keys |

`REPEAT_KEY_ENABLE` is required for the `keyrecord_t.keycode` field.

### One-shot mod compatibility

QMK's OSM core logic lives in `process_action` (which we hit). However,
`action_exec` handles `has_oneshot_mods_timed_out()` which we skip. Fix:
the timeout check runs in `housekeeping_task_gestures`:

```c
void housekeeping_task_gestures(void) {
    gesture_tick();
#ifndef NO_ACTION_ONESHOT
    if (has_oneshot_mods_timed_out()) {
        clear_oneshot_mods();
    }
#endif
}
```

### Layer state synchronization

The layer system reads QMK's `layer_state` and `default_layer_state`
globals directly. Layer keycodes (`MO`, `TG`, etc.) modify these via
QMK's standard functions, which trigger `layer_state_set_user` callbacks
for LED/OLED updates.

QMK's own layer keycodes won't modify state behind our back because we
intercept them before they reach QMK.

## Event pipeline

```
1. QMK matrix scan -> keyevent_t (row, col)
2. pre_process_record_gestures intercepts key/encoder events:
   - Key events: event_id = gesture_key_index(row, col)
   - Encoder events: encoder.{id, clockwise, count=1}
3. Gesture system buffers, resolves -> emits gesture_event_t:
   - Physical: EVENT_TYPE_KEY, event_id = dense key index
   - Virtual: EVENT_TYPE_GESTURE, gesture.{gesture_id, outcome}
   - Encoder: EVENT_TYPE_ENCODER, encoder.{id, clockwise, count}
4. Layer system resolves to keycode:
   - Keys: key_layer_get(layer) -> dense or sparse lookup
   - Gestures: gesture_layer_get(layer) -> (gesture_id, outcome) match
   - Encoders: encoder_layer_get(layer) -> (encoder_id) match, select cw/ccw
5. Layer keycodes handled directly; others -> process_record
```

### Key index mapping

The user maps matrix positions to dense key indices via
`gesture_key_index(keypos_t pos)`. This function is typically generated
by `DEFINE_KEY_INDICES` using a `GESTURE_LAYOUT_CALL` macro from
`gen_macros.py --layout`.

### Encoder events

Encoder events enter via `pre_process_record_gestures` (with
`ENCODER_MAP_ENABLE`). They flow through the gesture system like key
presses: they can trigger gestures, be consumed, and are chronologically
ordered with key events. Consecutive same-direction ticks are aggregated.

The layer system expands each emitted encoder event into N press+release
pairs using QMK's native `ENCODER_CW_EVENT`/`ENCODER_CCW_EVENT` types.

### QMK reentry event construction

For physical keys and gestures, `emit_to_qmk` packs the `event_id` into
`keypos_t` via `memcpy` so downstream `process_record_user` can retrieve
trigger data. Gesture events use `COMBO_EVENT` type; physical keys use
`KEY_EVENT`.

For encoders, `emit_encoder_to_qmk` uses QMK's native encoder key
positions (`KEYLOC_ENCODER_CW`/`KEYLOC_ENCODER_CCW`) and event types.

## QMK keymap introspection

The module overrides `keymap_layer_count()` and
`keycode_at_keymap_location()` to report from the gesture layer system
rather than QMK's built-in keymap. This enables VIA, key testing, and
similar tools to work with the gesture module's layer definitions.

## Community module integration

The gesture system is packaged as a QMK community module. The module
entry point (`gestures.c`) declares hooks:
- `pre_process_record_gestures` (intercept events)
- `housekeeping_task_gestures` (tick timeouts, OSM cleanup)
- `keyboard_post_init_gestures` (initialize)

QMK's community module system auto-generates calls to these hooks. No
manual wiring needed in `keymap.c`.
