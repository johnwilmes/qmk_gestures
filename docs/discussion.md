# Virtual Key Layer System — Discussion

## Context

The gesture system outputs a stream of resolved key events via
`gesture_emit_event`. Each event is a `gesture_event_t` with:
- `event_type_t type`: `EVENT_TYPE_KEY` (physical) or `EVENT_TYPE_GESTURE` (virtual)
- `uint16_t event_id`: dense index within that type's namespace
- `uint16_t time`: event timestamp
- `bool pressed`: press or release

This layer system resolves those events to concrete keycodes, then hands the
keycode back to QMK for HID report generation. We replace QMK's layer lookup
and action-keycode processing but reuse its downstream keycode execution
(`register_code`, modifier handling, HID reports, consumer keys, etc.).

## Goals

1. **Layers over both physical and virtual keys.** A single layer map covers
   physical matrix positions and gesture-generated virtual keys uniformly.

2. **No action keycodes.** Keycodes like `LT()`, `MT()`, `TD()` encode
   trigger conditions (tap-hold, layer-tap). The gesture system already
   resolved those. Every keycode in the layer map is a concrete outcome:
   a HID usage, a modifier, a layer toggle, etc.

3. **Dense and sparse layer definitions.** Physical keys are a small fixed
   set (e.g., 48 for Kyria). Virtual keys may number in the dozens or
   hundreds (combos, tapdance variants). Most layers only override a few
   virtual keys. We need both compact full-matrix definitions and sparse
   per-key overrides to avoid bloated tables.

4. **Binding safety.** A press binds to a concrete keycode at press time.
   The release always undoes that exact binding, even if layers changed in
   between. No stuck keys.

5. **Hand back to QMK.** After resolving the keycode, pass it to QMK's
   existing keycode execution. No custom serializer/HID logic.

## QMK reentry strategy

### The `record->keycode` override

When `COMBO_ENABLE` or `REPEAT_KEY_ENABLE` is defined, `keyrecord_t` has a
`keycode` field. QMK checks this in two places:

```c
// quantum.c:246 — keycode resolution
uint16_t get_record_keycode(keyrecord_t *record, bool update_layer_cache) {
    if (record->keycode) {          // ← bypasses layer lookup
        return record->keycode;
    }
    return get_event_keycode(record->event, update_layer_cache);
}

// action.c:301 — action resolution
void process_record_handler(keyrecord_t *record) {
    if (record->keycode) {
        action = action_for_keycode(record->keycode);  // ← bypasses layer cache
    } else {
        action = store_or_get_action(...);
    }
    process_action(record, action);
}
```

If `record->keycode` is set, **both** the keycode lookup and the action
lookup bypass QMK's layer system entirely. This is exactly the combo
system's mechanism for injecting resolved keycodes.

### Emission path

```c
void gesture_emit_event(gesture_event_t event) {
    // 1. Resolve keycode through our layer system
    uint16_t keycode = layer_resolve(event.type, event.event_id);

    // 2. Handle layer keycodes ourselves (MO, TG, DF, etc.)
    if (is_layer_keycode(keycode)) {
        layer_handle_keycode(keycode, event.pressed);
        return;
    }

    // 3. Construct QMK event for reentry
    //    Pack event_id into keypos_t (cast as uint16) so downstream
    //    process_record_user can retrieve trigger data if needed.
    keyevent_t qmk_event = {
        .key     = *(keypos_t *)&(uint16_t){event.event_id},
        .pressed = event.pressed,
        .time    = event.time,
        .type    = (event.type == EVENT_TYPE_GESTURE) ? COMBO_EVENT : KEY_EVENT,
    };

    // 4. Hand to QMK with keycode pre-resolved
    keyrecord_t record = {
        .event = qmk_event,
        .keycode = keycode,
    };
    process_record(&record);
}
```

This calls `process_record` directly (not `action_exec`), which:
- Skips `pre_process_record_quantum` (no reentrancy into our gesture hook)
- Skips `action_tapping_process` (we're calling `process_record`, not going
  through `action_exec`'s `#ifndef NO_ACTION_TAPPING` path)
- Runs `process_record_quantum` which picks up `record->keycode` via
  `get_record_keycode` — layer lookup bypassed
- Runs `process_record_handler` which picks up `record->keycode` — action
  cache bypassed
- QMK handles the actual `register_code`/`unregister_code`, modifier
  management, HID reports

### QMK features to disable

| Feature | Define | Why disable |
|---------|--------|-------------|
| Action tapping | `NO_ACTION_TAPPING` | Gesture system handles tap-hold |
| QMK combos | disable `COMBO_ENABLE` | Gesture system handles combos |
| Tap dance | disable `TAP_DANCE_ENABLE` | Gesture system handles tap dance |
| Leader key | disable `LEADER_ENABLE` | Gesture system handles leader keys |

We need `REPEAT_KEY_ENABLE` for the `keyrecord_t.keycode` field. Repeat key
hooks into `process_record_quantum` (via `process_last_key` and
`process_repeat_key`), which is in our call path — it works correctly with
the `process_record` reentry point.

### Reentrancy

Calling `process_record` directly (rather than `action_exec`) avoids
reentrancy because `process_record` does not call
`pre_process_record_quantum`, so our `pre_process_record_user` hook is
never reached. The call chain is:

```
gesture_emit_event
  → layer_resolve (our code)
  → process_record (QMK)
    → process_record_quantum (QMK — uses record->keycode, skips layer lookup)
    → process_record_handler (QMK — uses record->keycode, skips layer cache)
      → process_action (QMK — register/unregister/HID)
```

No path back to `pre_process_record_user`. No guard needed.

### One-shot mod compatibility

QMK's OSM core logic lives in `process_action` (which we hit). However,
`action_exec` lines 109-125 handle oneshot timeout clearing
(`has_oneshot_mods_timed_out`), which we skip. Fix: add the timeout check
in `matrix_scan_user` alongside `gesture_tick()`:

```c
void matrix_scan_user(void) {
    gesture_tick();
#ifndef NO_ACTION_ONESHOT
    // OSM timeout clearing (normally in action_exec, which we bypass)
    if (has_oneshot_mods_timed_out()) {
        clear_oneshot_mods();
    }
#endif
}
```

### Layer state synchronization

Our layer system maintains its own layer state. We sync it into QMK's
`layer_state` global via `layer_state_set()` whenever our state changes.
This:
- Triggers QMK's `layer_state_set_kb` / `layer_state_set_user` callbacks
  (LED/OLED hooks work)
- Ensures QMK processors that read `layer_state` see consistent values
- Is safe: QMK reads `layer_state` at keycode lookup time (which we bypass)
  and during feature checks (which see the synced state)

QMK's own layer keycodes (`MO()`, `TG()`) won't modify `layer_state` behind
our back because we intercept layer keycodes before they reach QMK.

## Event types and key space

### Event pipeline

```
1. QMK matrix scan → keyevent_t (row, col)
2. pre_process_record_user intercepts KEY_EVENTs, maps to gesture_event_t:
   - event_id = key_index_from_pos(row, col)  [user-defined mapping]
   - type = EVENT_TYPE_KEY
3. Gesture system buffers, resolves → emits gesture_event_t:
   - Physical events: type = EVENT_TYPE_KEY, event_id = dense key index
   - Virtual events:  type = EVENT_TYPE_GESTURE, event_id = gesture_id
   - Encoder events:  type = EVENT_TYPE_ENCODER, encoder.{id,clockwise,count}
4. Layer system receives gesture_event_t:
   - event.type selects layers[event_type]
   - event.event_id is the dense index within that type's namespace
   - Layer lookup resolves to keycode
5. Layer keycodes handled directly; others → process_record with keycode set
```

The user maps matrix positions to dense key indices at the pipeline entry
point (in `pre_process_record_user`), before calling `gesture_process_event`.
Gesture IDs are a separate dense namespace (0..MAX_GESTURES-1). Encoder
events flow through the gesture pipeline for correct chronological ordering
with key events, and may participate in gestures. For QMK reentry,
`event_id` is packed into `keypos_t` (as uint16) so downstream
`process_record_user` can retrieve trigger data if needed.

After step 5, downstream QMK only cares about `record->keycode` plus
`event.pressed` and `event.time`. The raw key position is passed through
for physical events (QMK uses it in some feature processors).

### Encoder events

Encoder events enter the pipeline via `pre_process_record_user` (with
`ENCODER_MAP_ENABLE`). They are mapped to `gesture_event_t` with
`EVENT_TYPE_ENCODER` and flow through the gesture system like key presses:
they can trigger gestures, be consumed by active gestures, and are
chronologically ordered with key events in the buffer.

Encoder events are press-only (no release). Consecutive same-direction
ticks are aggregated in the buffer (`encoder.count` field). The layer
system expands each emitted encoder event into N press+release pairs:

```c
// In gesture_emit_event, for EVENT_TYPE_ENCODER:
uint8_t count = event.encoder.count;
uint16_t keycode = layer_resolve(EVENT_TYPE_ENCODER, ...);
if (keycode == KC_TRNS || keycode == KC_NO) return;

keyevent_t qmk_event = {
    .pressed = true, .time = event.time,
    .type = event.encoder.clockwise ? ENCODER_CW_EVENT : ENCODER_CCW_EVENT,
    .key = { .row = event.encoder.clockwise ? KEYLOC_ENCODER_CW : KEYLOC_ENCODER_CCW,
             .col = event.encoder.encoder_id }
};
keyrecord_t record = { .event = qmk_event, .keycode = keycode };

for (uint8_t i = 0; i < count; i++) {
    record.event.pressed = true;
    process_record(&record);
    record.event.pressed = false;
    process_record(&record);
}
```

No binding table entry needed — press and release are emitted atomically.

### Non-key/encoder events (DIP switches)

DIP switch events and other non-key/encoder events pass through
`pre_process_record_user` to normal QMK processing.

## Layer model

### Layer definition

A layer maps key indices to keycodes. Two storage formats, used as
**separate layers** (not combined into a single composite struct):

**Dense layer** — a flat array covering a contiguous range of key indices.
Suitable for the physical matrix where every key has a mapping. Transparent
entries (`KC_TRNS`) fall through to lower layers.

```c
typedef struct {
    uint16_t base_index;   // First key index covered
    uint16_t count;        // Number of entries
    const uint16_t *map;   // Keycode array, indexed by (key_index - base_index)
} dense_layer_t;
```

**Sparse layer** — a sorted array of (key_index, keycode) pairs. Suitable
for virtual key overrides where only a handful of keys have non-transparent
mappings on a given layer.

```c
typedef struct {
    uint16_t count;
    const sparse_entry_t *entries;  // Sorted by key_index for binary search
} sparse_layer_t;

typedef struct {
    uint16_t key_index;
    uint16_t keycode;
} sparse_entry_t;
```

A layer in the stack is tagged as either dense or sparse:

```c
typedef enum { LAYER_DENSE, LAYER_SPARSE } layer_type_t;

typedef struct {
    layer_type_t type;
    union {
        dense_layer_t  dense;
        sparse_layer_t sparse;
    };
} layer_t;
```

### Layer stack

Layers are numbered 0..N. Layer 0 is the base layer (always active). Higher
layers are activated/deactivated by layer keycodes.

Layer state is a bitmask: `uint32_t layer_state` (supports up to 32 layers,
matching QMK convention). Synced to QMK's `layer_state` global.

To resolve a key index to a keycode:
1. Iterate active layers from highest to lowest.
2. For each layer:
   - Dense: if `key_index` is in `[base_index, base_index + count)`, look up
     directly. Otherwise treat as transparent.
   - Sparse: binary search for `key_index` in entries. Miss = transparent.
3. If the result is `KC_TRNS`, continue to next lower layer.
4. First non-transparent result wins.

This is the standard QMK model, but applied uniformly to physical + virtual
keys, with efficient sparse lookup for virtual-heavy layers.

### Layer default value

A layer can specify a default keycode for keys it doesn't explicitly map.
This defaults to `KC_TRNS` (fall through), but setting it to `KC_NO` creates
a "blocking" layer that disables everything below it. Useful for gaming
layers or locked modes where unmapped keys should do nothing.

```c
typedef struct {
    layer_type_t type;
    uint16_t     default_keycode;  // KC_TRNS (default) or KC_NO, etc.
    union {
        dense_layer_t  dense;
        sparse_layer_t sparse;
    };
} layer_t;
```

### Event types

Different event types (physical keys, gestures, encoders) have independent
dense index namespaces. All event types are handled uniformly through a
single 2D layer array, with one `layer_state` bitmask shared across all
types:

```c
typedef enum {
    EVENT_TYPE_KEY,      // Physical keys: 0..MATRIX_ROWS*MATRIX_COLS-1
    EVENT_TYPE_GESTURE,  // Gesture virtual keys: 0..MAX_GESTURES-1
    EVENT_TYPE_ENCODER,  // Encoder actions: 0..NUM_ENCODERS*2-1 (CW/CCW)
    EVENT_TYPE_COUNT,
} event_type_t;

layer_t layers[EVENT_TYPE_COUNT][NUM_LAYERS];
```

When layer 2 is active, `layers[EVENT_TYPE_KEY][2]`,
`layers[EVENT_TYPE_GESTURE][2]`, and `layers[EVENT_TYPE_ENCODER][2]` are all
active. Index 0 in each event type is a different key — no collision, no
need to offset gesture IDs.

Most layers are sparse or empty for a given event type. A layer that only
adds gesture overrides has empty key and encoder layers (`default_keycode =
KC_TRNS`, no entries) which fall through instantly. No wasted storage.

Layer resolution is identical for all event types:
`resolve(event_type, event_id)` picks `layers[event_type]`, iterates
active layers highest-to-lowest, returns first non-default keycode.

The only behavioral difference is **transient vs persistent** event types:

```c
bool is_transient[EVENT_TYPE_COUNT] = {
    [EVENT_TYPE_KEY]     = false,  // press/release are separate events
    [EVENT_TYPE_GESTURE] = false,  // press/release are separate events
    [EVENT_TYPE_ENCODER] = true,   // emit press+release atomically
};
```

- **Persistent** (key, gesture): press resolves keycode and stores it in the
  binding table. Release looks up the binding, emits it, clears the entry.
- **Transient** (encoder): resolve keycode, emit press+release immediately.
  No binding table entry.

## Keycodes

Since the gesture system handles all temporal resolution, keycodes are purely
declarative outcomes.

### Keycodes we handle (layer management)

Intercepted by our layer system and never reach QMK:

| Keycode | Effect |
|---------|--------|
| `MO(n)` | Layer n active while key held |
| `TG(n)` | Toggle layer n on/off |
| `DF(n)` | Set base (default) layer to n |
| `OSL(n)` | Layer active for next key only |

Keycodes we don't use (`LT()`, `MT()`, `LM()`, `TD()`) can be ignored
entirely — the gesture system resolves those patterns before the layer
system sees the event.

### Keycodes we pass to QMK

Everything else goes to QMK's `process_record` with `record->keycode` set:

- Basic keycodes (`KC_A`, `KC_1`, `KC_ENTER`, etc.)
- Modifiers (`KC_LSFT`, `KC_RCTL`, etc.)
- Modified keys (`S(KC_1)`, etc.)
- One-shot mods (`OSM(mod)`)
- Consumer/system keycodes (media keys, etc.)
- Mouse keys
- Macros (QMK's macro support works — it goes through `process_record_quantum`)
- `KC_TRNS` / `KC_NO`

## Binding state

Persistent event types need bindings so the release always matches the press
keycode, even if layers changed in between. Transient types (encoders) don't
need bindings — press and release are emitted atomically.

Since at most a handful of keys are physically held at once (across all
event types combined), bindings use a small fixed-size pool rather than a
dense array per event type:

```c
typedef struct {
    event_type_t event_type;
    uint16_t     event_id;
    uint16_t     keycode;
} binding_entry_t;

#define MAX_ACTIVE_BINDINGS 16
binding_entry_t active_bindings[MAX_ACTIVE_BINDINGS];
```

On press (persistent): linear scan for an empty slot, store
`{event_type, event_id, keycode}`.
On release (persistent): linear scan for matching `{event_type, event_id}`,
read keycode, clear the entry.
On event (transient): resolve keycode, emit press+release, no binding.

Linear scan over 16 entries is trivially fast and avoids allocating dense
arrays sized to the total gesture count (which could be hundreds, of which
at most a few are simultaneously active). Storage: 16 × 5 = 80 bytes total,
regardless of key space size.

Layer keycodes (`MO`, `TG`, etc.) also use bindings. When `MO(2)` is
released, the binding tells us to deactivate layer 2 — even if the layer
state changed in between.
