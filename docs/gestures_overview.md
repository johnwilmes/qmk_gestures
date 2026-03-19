# Gesture System Overview

The gesture system is a unified mechanism for transforming sequences of
physical key and encoder events into virtual key events. It replaces several
disjoint QMK features -- combos, tap-hold, tap dance, and leader keys -- with
a single callback-based architecture.

## What it does

Physical keys and encoder events are identified by dense indices. The gesture
system sits between the matrix scan and the downstream layer/action processing.
It buffers key and encoder events, watches for patterns defined by gesture
callbacks, and emits virtual key events when those patterns are recognized.
Unconsumed events pass through to downstream processing alongside the virtual
events.

```
matrix scan ──→ gesture system ──→ layer / action processing ──→ HID output
encoder ticks ─┘     |
                     +── key and encoder events buffered
                     +── gestures watch for patterns
                     +── virtual press/release emitted when patterns match
                     +── unconsumed events pass through
```

## Core abstraction

Every gesture is defined by a single callback function and a pointer to
gesture-specific state. The coordinator calls the callback at different points
in the gesture's lifecycle, passing context about what phase the gesture is in.
The callback returns a `{timeout, outcome}` pair that tells the coordinator
what to do next.

The same callback signature handles combos (multiple keys pressed together),
tap-hold (same key producing different output based on timing), tap dance
(repeated taps producing different output based on count), and any other
temporal key pattern.

## Lifecycle

```
INACTIVE --trigger--> PARTIAL --ripen--> ACTIVE --release--> INACTIVE
                         |
                      cancel
                         |
                         v
                      INACTIVE
```

- **INACTIVE**: Gesture is waiting. Scanned when there is an unconsumed press at
the front of the buffer to see if it wants to participate.
- **PARTIAL**: Gesture has started but is not yet resolved. The buffer is
  blocked: the triggering press cannot emit until all partial gestures resolve.
- **ACTIVE**: Gesture's virtual key is pressed. The gesture sees new events
  and can consume presses. It deactivates when its timeout expires or its
  callback requests it.
- **DISABLED**: Administrative state. The gesture is excluded from scanning
  until re-enabled.

## Virtual events

When a gesture activates, its virtual press is emitted immediately --
positioned in time at the original initial triggering press. Downstream processing
sees the virtual press before any of the buffered physical events that
followed the trigger.

Virtual releases are emitted when the gesture deactivates. The coordinator
checks active gesture expiry times before emitting each buffered event,
ensuring virtual releases are correctly interleaved with physical events
in chronological order.

## Priority and conflict resolution

Gesture ID doubles as priority: higher ID = higher priority.

1. **Tenure**: Active gestures consume events before inactive/partial gestures
   can trigger on them.
2. **First trigger wins**: A single initial press can trigger at most one gesture's
   activation. Once partial gestures exist, no new gestures can trigger until
   the partial set resolves.
3. **Priority as tiebreaker**: When multiple gestures want to activate on
   the same press, the highest-priority one wins. Lower-priority partial
   gestures are evicted.

## Press history

A bitmap tracks which presses have been emitted downstream. When a physical
press is consumed by a gesture, its bit is never set, so the corresponding
release is silently swallowed. This prevents orphaned releases from reaching
downstream processing.

## Memory model

All storage is static:
- Circular event buffer (configurable size, default 12)
- Gesture array (user-defined, up to 8191 entries)
- Linked lists embedded in `gesture_t.next` fields (no heap allocation)
- Press history bitmap: `(MATRIX_ROWS * MATRIX_COLS + MAX_GESTURES) / 8` bytes
- Single `uint16_t` for global next-timeout tracking

## Source files

| File | Purpose |
|------|---------|
| `gesture.h` | Core types, coordinator state, public API |
| `gesture.c` | Coordinator implementation |
| `combo.h/c` | Combo gesture: multiple keys pressed together |
| `tapdance.h/c` | Tap dance and tap-hold gestures |
| `precog.h/c` | Precognition: home-row mods via combo overrides |
| `util.h` | COUNT_ARGS variadic macro |

## QMK integration

The gesture system replaces QMK's built-in combo, tap-hold, and tap dance
processing. It intercepts raw key events early in the processing chain,
buffers them, and emits resolved events downstream.

### Entry points

Three hooks connect the gesture system to QMK:

```
pre_process_record_user()   →  gesture_process_event(event)
                                  intercepts key and encoder events before
                                  action processing; returns false to suppress

matrix_scan_user()          →  gesture_tick()
                                  checks for expired gesture timeouts
                                  called every matrix scan cycle (~1-5ms)

gesture_emit_event()        →  layer_resolve() → process_record()
                                  provided by layer system (../layers/);
                                  resolves keycode and hands back to QMK
                                  for keycode execution
```

### Wiring it up

```c
#include "gesture.h"

// User-defined key position mapping.
// Dense key indices used by combo trigger lists, tapdance triggers, etc.
enum key_positions {
    POS_L_PINKY_T, POS_L_RING_T, POS_L_MIDDLE_T, POS_L_INDEX_T,
    POS_L_INDEX_H, POS_L_MIDDLE_H, POS_L_RING_H, POS_L_PINKY_H,
    POS_L_THUMB,
    // ...
};

const uint8_t PROGMEM key_index_map[MATRIX_ROWS][MATRIX_COLS] = {
    [0] = { POS_L_PINKY_T, POS_L_RING_T, POS_L_MIDDLE_T, ... },
    // ...
};

static uint8_t key_index_from_pos(keypos_t pos) {
    return pgm_read_byte(&key_index_map[pos.row][pos.col]);
}

// 1. Intercept key and encoder events before normal processing.
//    Convert QMK events to gesture_event_t. Returning false suppresses
//    the event; the gesture system re-emits it later via gesture_emit_event.
bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (IS_KEYEVENT(record->event)) {
        gesture_event_t event = {
            .event_id = key_index_from_pos(record->event.key),
            .time     = record->event.time,
            .type     = EVENT_TYPE_KEY,
            .pressed  = record->event.pressed,
        };
        gesture_process_event(event);
        return false;
    }
    if (IS_ENCODEREVENT(record->event)) {
        bool cw = (record->event.type == ENCODER_CW_EVENT);
        gesture_event_t event = {
            .encoder = {
                .count      = 1,
                .encoder_id = record->event.key.col,
                .clockwise  = cw,
            },
            .time    = record->event.time,
            .type    = EVENT_TYPE_ENCODER,
            .pressed = true,  // Encoder ticks are press-only
        };
        gesture_process_event(event);
        return false;
    }
    return true;  // Non-key/encoder events (dip switches) pass through
}

// 2. Poll for gesture timeouts every matrix scan cycle.
void matrix_scan_user(void) {
    gesture_tick();
}

// 3. Initialize on keyboard startup.
void keyboard_post_init_user(void) {
    layers_init();
    gestures_init();
}
```

### Why `pre_process_record_user`

The gesture system must see raw key events before QMK resolves keycodes or
runs its own tap-hold / combo logic. `pre_process_record_user` is the
earliest user-level hook in the processing chain. Using `process_record_user`
would be too late -- QMK's built-in features would have already acted on the
event. Encoder events (with `ENCODER_MAP_ENABLE`) also pass through this
hook and are routed into the gesture pipeline for correct chronological
ordering with key events. DIP switch events and other non-key/encoder events
pass through to normal QMK processing.

### Downstream emission

`gesture_emit_event` is the boundary between the gesture system and
downstream processing. It receives `gesture_event_t` values:
- Physical key events (`EVENT_TYPE_KEY`): passed through or released from
  the buffer, with press/release pairs.
- Virtual gesture events (`EVENT_TYPE_GESTURE`): press/release of a gesture
  ID, emitted when gestures activate/deactivate.
- Encoder events (`EVENT_TYPE_ENCODER`): press-only, with an aggregated
  tick count in `event.encoder.count`. The layer system expands these into
  N press+release pairs for QMK.

The layer system resolves each event to a concrete keycode via
`layers[event.type][layer_id]`, handles layer management keycodes directly,
and passes everything else to QMK's `process_record` with `record->keycode`
pre-set. For QMK reentry, the layer system constructs a `keyevent_t` with
`COMBO_EVENT` type for gestures, `KEY_EVENT` for physical keys, and
`ENCODER_CW_EVENT`/`ENCODER_CCW_EVENT` for encoders, packing the `event_id`
into `keypos_t` (cast as uint16) so downstream `process_record_user` can
retrieve it if needed. See `../layers/` for the layer system design.

## User-provided functions

The gesture system requires two functions from the user:

- `gesture_t *gesture_get(gesture_id_t id)` -- return a pointer to the
  gesture definition at the given index.
- `uint16_t gesture_count(void)` -- return the total number of gestures.

The layer system provides `gesture_emit_event` (see `../layers/`).
