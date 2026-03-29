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

Two formats, selected by `gesture_layer_type_t`:

**Dense** -- pointer array indexed by gesture_id. Each pointer leads to a
keycode array for that gesture's outcomes (outcome 1 at index 0). NULL
pointer means unmapped (returns `default_keycode`). Array is sized to
`GESTURE_COUNT`. For base layers where most/all gestures are mapped.

```c
typedef struct {
    const uint16_t *const *map;   // map[gesture_id] -> outcome keycode array
} dense_gesture_layer_t;
```

**Sparse** -- array of `(gesture_id, keycodes*)` entries. Each entry maps
one gesture to its outcome keycodes. Linear scan lookup. For overlay
layers where few gestures are remapped.

```c
typedef struct {
    uint16_t         gesture_id;
    const uint16_t  *keycodes;   // outcome keycode array
} sparse_gesture_entry_t;

typedef struct {
    uint16_t                       count;
    const sparse_gesture_entry_t  *entries;
} sparse_gesture_layer_t;
```

A `gesture_layer_t` is a tagged union with a `default_keycode`:
- `KC_TRNS` (default): fall through to lower layers
- `KC_NO`: block

Lookup uses `gesture_get(id)->num_outcomes` for bounds checking (not
stored in the layer). `KC_TRNS` in a keycode array falls through to the
next lower layer.

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

// Dense gesture layer (default — base layer with all gestures)
DEFINE_GESTURE_LAYER(base_gestures,
    GESTURE_MAP(home_a,    KC_LGUI),
    GESTURE_MAP(esc_combo, KC_ESC),
    GESTURE_MAP(my_td,     KC_LSFT, KC_X, KC_LCTL),
);

// Sparse gesture layer (overlay — only remap a few)
DEFINE_SPARSE_GESTURE_LAYER(nav_gestures,
    GESTURE_SPARSE_MAP(home_a, KC_HOME),
);

// Encoder layer with paired directions
DEFINE_ENCODER_LAYER(base_encoders,
    ENCODER_MAP(0, KC_VOLD, KC_VOLU),
);

// Assemble the layer table
DEFINE_LAYER_TABLE(
    [0] = { .key = &base_keys, .gesture = &base_gestures, .encoder = &base_encoders },
    [1] = { .key = &nav_keys,  .gesture = &nav_gestures },
);
```

`DEFINE_LAYER_TABLE` generates the typed lookup functions
(`key_layer_get`, `gesture_layer_get`, `encoder_layer_get`) and
`layer_count`.

### Gesture layer mapping macros

| Macro | Context | Usage |
|-------|---------|-------|
| `GESTURE_MAP(name, kc1, ...)` | Dense layer | Keycodes in outcome order |
| `GESTURE_SPARSE_MAP(name, kc1, ...)` | Sparse layer | Keycodes in outcome order |
| `ENCODER_MAP(id, ccw, cw)` | Encoder layer | Encoder direction pair |

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
2. Dense: index by gesture_id, read `keycodes[outcome - 1]`.
   Sparse: linear scan for matching gesture_id, then read outcome.
3. NULL pointer (dense) or missing entry (sparse) returns `default_keycode`.
4. `KC_TRNS` falls through to next layer. First non-`KC_TRNS` result wins.

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
