# Layer System Overview

The layer system resolves gesture events to concrete keycodes and hands them
to QMK for execution. It replaces QMK's built-in layer lookup and
action-keycode processing while reusing QMK's downstream keycode execution
(`register_code`, modifier handling, HID reports).

## What it does

The layer system sits between the gesture coordinator and QMK's keycode
execution. It receives resolved `gesture_event_t` values from the gesture
pipeline, looks up keycodes through a stack of user-defined layers, and
either handles layer management keycodes directly or passes everything else
to QMK's `process_record` with the keycode pre-resolved.

```
gesture system ──→ layer system ──→ QMK keycode execution ──→ HID output
                       |
                       +── resolve event to keycode (layer stack)
                       +── layer keycodes (MO, TG, DF, OSL) handled directly
                       +── binding table tracks press→release keycode
                       +── all other keycodes → process_record(record)
```

## Core concepts

### Event type namespaces

Different event types have independent dense index namespaces, all sharing
a single `layer_state` bitmask:

| Event type | Index range | Behavior |
|------------|-------------|----------|
| `EVENT_TYPE_KEY` | Physical key positions | Persistent: binding table |
| `EVENT_TYPE_GESTURE` | Virtual gesture IDs | Persistent: binding table |
| `EVENT_TYPE_ENCODER` | `encoder_id * 2 + direction` | Transient: atomic press+release |

The user provides layer definitions per event type via `layer_get(type, layer_id)`.
When layer 2 is active, all three event types see layer 2's mappings.
Most layers are sparse or NULL for a given event type, falling through
instantly with no wasted storage.

### Dense and sparse layers

Two storage formats for layer definitions:

**Dense** — flat keycode array covering a contiguous range of key indices.
Used for physical matrix layers where every key has a mapping. Stored in
PROGMEM.

**Sparse** — sorted array of `(key_index, keycode)` pairs, looked up by
binary search. Used for virtual key overrides where few keys have
non-transparent mappings. Stored in PROGMEM.

A layer also has a `default_keycode` for unmapped keys:
- `KC_TRNS` (default): fall through to lower layers
- `KC_NO`: block — unmapped keys do nothing (useful for gaming layers)

### Layer resolution

To resolve an event to a keycode:
1. Iterate active layers from highest to lowest.
2. For each layer, look up the event's index (dense: range check + array
   read; sparse: binary search).
3. Apply `default_keycode` for misses.
4. First non-`KC_TRNS` result wins.

### Binding table

Persistent events (keys, gestures) have separate press and release. The
keycode resolved at press time is stored in a fixed-size binding table
(16 entries). On release, the binding is looked up and cleared, ensuring
the release always undoes the exact keycode from press time — even if
layers changed in between.

Transient events (encoders) resolve and emit press+release atomically.
No binding needed.

## Layer keycodes

Layer management keycodes are intercepted and never reach QMK:

| Keycode | Press | Release |
|---------|-------|---------|
| `MO(n)` | Activate layer n | Deactivate layer n |
| `TG(n)` | Toggle layer n | — |
| `DF(n)` | Set default layer to n | — |
| `OSL(n)` | Activate one-shot layer n | See below |

`MO(n)` is disallowed on encoder events (encoders have no release to
deactivate the layer). Use `TG(n)` for encoder-driven layer switching.

### One-shot layers

Two modes depending on timing:

- **Tap**: press OSL, release OSL, then press a key → key uses the layer,
  then layer deactivates.
- **Hold**: press OSL, press a key while OSL is held → key uses the layer,
  layer stays active until OSL is released.

Tracked by `osl_held` (key physically held) and `osl_used` (a key was
pressed while held).

## QMK reentry

Non-layer keycodes are passed to QMK via `process_record()` with
`record->keycode` set. This bypasses QMK's layer lookup (the `keycode`
field is checked first in `get_record_keycode` and `process_record_handler`)
while reusing QMK's full keycode execution pipeline.

For physical keys and gestures, `keyevent_t` uses `KEY_EVENT` or
`COMBO_EVENT` type with the `event_id` packed into `keypos_t`.

For encoder events, the layer system expands the aggregated tick count
into N individual press+release pairs, using QMK's native
`ENCODER_CW_EVENT`/`ENCODER_CCW_EVENT` types with proper `KEYLOC_ENCODER`
key positions.

Calling `process_record` directly (not `action_exec`) avoids reentrancy:
`process_record` does not call `pre_process_record_quantum`, so our
gesture hook is never reached.

## Layer state synchronization

The layer system maintains its own `layer_state` bitmask and syncs it
to QMK's global via `layer_state_set()` on every change. This ensures
QMK's layer callbacks (`layer_state_set_user`) fire correctly for
LED/OLED updates, while QMK's own layer keycodes never run because we
intercept them before they reach QMK.

## User-provided functions

The layer system requires two functions from the user:

- `const layer_t *layer_get(event_type_t type, uint8_t layer_id)` —
  return the layer definition for the given event type and layer number.
  May return NULL (treated as all-transparent).
- `uint8_t layer_count(void)` — return the total number of layers.

## Source files

| File | Purpose |
|------|---------|
| `layer.h` | Types (dense/sparse layers, binding table, layer system state), public API |
| `layer.c` | Layer resolution, binding table, layer state management, OSL, QMK reentry |
| `discussion.md` | Design rationale, QMK reentry analysis, keycode audit |
| `audit.md` | QMK feature compatibility audit |

## Memory model

All storage is static:
- `layer_system_t`: layer state bitmask, default layer, OSL state
- Binding table: 16 × 5 = 80 bytes (fixed pool, linear scan)
- Layer definitions: user-provided PROGMEM arrays (dense maps, sparse entries)
