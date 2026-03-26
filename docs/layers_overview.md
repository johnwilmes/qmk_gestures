# Layer System Overview

The layer system resolves gesture events to concrete keycodes and hands them
to QMK for execution. It replaces QMK's built-in layer lookup while reusing
QMK's downstream keycode execution (`register_code`, modifier handling,
HID reports).

## What it does

```
gesture system ----> layer system ----> QMK keycode execution ----> HID output
                         |
                         +-- resolve event to keycode (layer stack)
                         +-- layer keycodes (MO, TG, DF, OSL) handled directly
                         +-- binding table tracks press->release keycode
                         +-- all other keycodes -> process_record(record)
```

## Layer types

The layer system uses separate, type-specific layer tables for each event
type. A `layer_entry_t` bundles optional pointers to all three:

```c
typedef struct {
    const key_layer_t     *key;      // Physical key mappings (dense or sparse)
    const gesture_layer_t *gesture;  // Gesture outcome mappings
    const encoder_layer_t *encoder;  // Encoder direction mappings
} layer_entry_t;
```

NULL pointers mean "no mapping" (fully transparent for that event type).

### Key layers (physical keys)

Two storage formats, selected by `key_layer_type_t`:

**Dense** -- flat keycode array covering a contiguous range of key indices.
For physical matrix layers where every key has a mapping. Stored in PROGMEM.

```c
typedef struct {
    uint16_t       base_index;
    uint16_t       count;
    const uint16_t *map;        // PROGMEM: indexed by (key_index - base_index)
} dense_key_layer_t;
```

**Sparse** -- array of `(key_index, keycode)` pairs. For overlay layers
where few keys have non-transparent mappings.

```c
typedef struct {
    uint16_t          count;
    const key_entry_t *entries;  // PROGMEM: {key_index, keycode} pairs
} sparse_key_layer_t;
```

A `key_layer_t` is a tagged union with a `default_keycode`:
- `KC_TRNS` (default): fall through to lower layers
- `KC_NO`: block -- unmapped keys do nothing (gaming layers)

### Gesture layers

Sparse array of `(gesture_id, outcome, keycode)` triples:

```c
typedef struct {
    uint8_t  gesture_id;
    uint8_t  outcome;      // 1-based
    uint16_t keycode;
} gesture_entry_t;

typedef struct {
    uint16_t              count;
    const gesture_entry_t *entries;  // PROGMEM
} gesture_layer_t;
```

Lookup matches `(gesture_id, outcome)`. If the matching entry has
`KC_TRNS`, it falls through to the next lower layer -- per-outcome,
not per-gesture.

### Encoder layers

Paired CW/CCW keycodes per encoder:

```c
typedef struct {
    uint8_t  encoder_id;
    uint16_t ccw;
    uint16_t cw;
} encoder_entry_t;

typedef struct {
    uint16_t               count;
    const encoder_entry_t  *entries;  // PROGMEM
} encoder_layer_t;
```

Lookup matches `encoder_id`, then selects `cw` or `ccw` by direction.

## Layer definition macros

```c
// Dense key layer (full matrix)
DEFINE_DENSE_LAYER(base_keys, KC_A, KC_B, KC_C, ...);

// Sparse key layer (overlay)
DEFINE_SPARSE_LAYER(nav_keys, {POS_H, KC_LEFT}, {POS_J, KC_DOWN});

// Gesture layer with outcome grouping
DEFINE_GESTURE_LAYER(base_gestures,
    HOLD_MAP(home_a,    KC_LGUI),           // 1 outcome
    COMBO_MAP(esc_combo, KC_ESC),           // 1 outcome
    TD_MAP(my_td, 2,    KC_LSFT, KC_X, KC_LCTL),  // 3 outcomes
);

// Encoder layer with paired directions
DEFINE_ENCODER_LAYER(base_encoders,
    ENCODER_MAP(0, KC_VOLD, KC_VOLU),
);

// Assemble the layer table
DEFINE_LAYER_TABLE(
    [0] = { .key = &base_keys, .gesture = &base_gestures, .encoder = &base_encoders },
    [1] = { .key = &nav_keys },
);
```

`DEFINE_LAYER_TABLE` generates the typed lookup functions
(`key_layer_get`, `gesture_layer_get`, `encoder_layer_get`) and
`layer_count`.

### Gesture layer mapping macros

| Macro | Usage | Outcomes |
|-------|-------|----------|
| `HOLD_MAP(name, kc)` | Single hold outcome | 1 |
| `COMBO_MAP(name, kc)` | Single combo outcome | 1 |
| `TD_MAP(name, max, kc1, kc2, ...)` | Tap dance: hold(1), tap(2), hold(2), ... | 2*max-1 |
| `ENCODER_MAP(id, ccw, cw)` | Encoder direction pair | n/a |

## Layer resolution

### Key resolution

Iterate active layers from highest to lowest:
1. For each layer, call `key_layer_get(layer_id)`.
2. Dense: range check + array read. Sparse: linear scan.
3. Apply `default_keycode` for misses.
4. First non-`KC_TRNS` result wins.

### Gesture resolution

Iterate active layers from highest to lowest:
1. For each layer, call `gesture_layer_get(layer_id)`.
2. Scan entries for matching `(gesture_id, outcome)`.
3. If match has `KC_TRNS`, continue to next layer (per-outcome fallthrough).
4. First non-`KC_TRNS` result wins.

### Encoder resolution

Iterate active layers from highest to lowest:
1. For each layer, call `encoder_layer_get(layer_id)`.
2. Scan entries for matching `encoder_id`.
3. Select `cw` or `ccw` keycode.
4. If `KC_TRNS`, continue to next layer.

## Binding table

Persistent events (keys, gestures) have separate press and release. The
keycode resolved at press time is stored in a fixed-size binding table
(16 entries). On release, the binding is looked up and cleared, ensuring
the release always undoes the exact keycode from press time.

Transient events (encoders) resolve and emit press+release atomically.
No binding needed.

```c
typedef struct {
    event_type_t event_type;
    uint16_t     event_id;
    uint16_t     keycode;
} binding_entry_t;
```

Layer keycodes (`MO`, `TG`, etc.) also use bindings so the release knows
which layer to deactivate.

When the binding table is full, the oldest entry is evicted with a
synthetic release to prevent stuck keys.

## Layer keycodes

Intercepted and handled directly (never reach QMK):

| Keycode | Press | Release |
|---------|-------|---------|
| `MO(n)` | Activate layer n | Deactivate layer n |
| `TG(n)` | Toggle layer n | -- |
| `DF(n)` | Set default layer to n | -- |
| `OSL(n)` | Activate one-shot layer n | See below |

### One-shot layers

- **Tap**: press OSL, release, then press a key -> key uses the layer,
  then layer deactivates.
- **Hold**: press OSL, press a key while held -> key uses the layer,
  layer stays active until OSL is released.

## QMK reentry

Non-layer keycodes are passed to QMK via `process_record()` with
`record->keycode` set, bypassing QMK's layer lookup.

For physical keys and gestures, `keyevent_t` uses `KEY_EVENT` or
`COMBO_EVENT` type with the `event_id` packed into `keypos_t`.

For encoders, the layer system expands the aggregated tick count into
N individual press+release pairs using QMK's native encoder event types.

## Layer state synchronization

The layer system reads QMK's `layer_state` and `default_layer_state`
globals directly. Layer keycodes modify these via QMK's standard
functions (`layer_on`, `layer_off`, etc.), which trigger QMK's
`layer_state_set_user` callbacks for LED/OLED updates.

## QMK keymap introspection

The module overrides QMK's keymap introspection functions so that features
like VIA, key testing, and RGB matrix can query the keymap:

- `keymap_layer_count()` returns `layer_count()`
- `keycode_at_keymap_location(layer, row, col)` looks up the key layer

## Memory model

All storage is static:
- Binding table: `MAX_ACTIVE_BINDINGS` entries (default 16)
- Layer definitions: user-provided PROGMEM arrays

## Source files

| File | Purpose |
|------|---------|
| `gesture_layers.h` | Layer types, definition macros, key index macros |
| `layer.c` | Layer resolution, binding table, QMK reentry |
