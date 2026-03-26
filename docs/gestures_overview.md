# Gesture System Overview

The gesture system transforms sequences of physical key and encoder events
into virtual key events. It replaces QMK's combos, tap-hold, tap dance, and
leader keys with a single callback-based architecture.

## What it does

Physical keys and encoder ticks enter the gesture pipeline, are buffered,
and matched against gesture callbacks. When a pattern is recognized, a
virtual press/release pair is emitted. Unconsumed events pass through
alongside the virtual events.

```
matrix scan ----> gesture system ----> layer system ----> HID output
encoder ticks -/       |
                       +-- key and encoder events buffered
                       +-- gestures watch for patterns
                       +-- virtual press/release emitted on match
                       +-- unconsumed events pass through
```

## Core abstraction

Every gesture is defined by a callback function and a pointer to
gesture-specific state. The coordinator calls the callback at lifecycle
transitions, passing context about what phase the gesture is in. The
callback returns `{timeout, outcome}` telling the coordinator what to do
next.

The same callback signature handles combos, tap-hold, tap dance, and any
other temporal key pattern.

## Event model

Events are represented as `gesture_event_t` with a type-specific union:

| Event type | Union member | Identifies |
|------------|-------------|------------|
| `EVENT_TYPE_KEY` | `event_id` | Dense key index (0..NUM_KEY_POSITIONS-1) |
| `EVENT_TYPE_GESTURE` | `gesture.gesture_id`, `gesture.outcome` | Packed 12-bit gesture ID + 4-bit outcome |
| `EVENT_TYPE_ENCODER` | `encoder.encoder_id`, `encoder.clockwise`, `encoder.count` | 7-bit encoder ID, direction, aggregated tick count |

Gesture events pack the gesture ID (12 bits, up to 4095 gestures) and
outcome (4 bits, up to 15 outcomes per gesture) into the same `event_id`
field. This eliminates the need for a flat event ID namespace.

## Lifecycle

```
INACTIVE --trigger--> PARTIAL --ripen--> ACTIVE --release--> INACTIVE
                         |
                      cancel
                         |
                         v
                      INACTIVE
```

- **INACTIVE**: Waiting. Scanned when an unconsumed press arrives at the
  front of the buffer.
- **PARTIAL**: Started but not yet resolved. The buffer is blocked: the
  triggering press cannot emit until all partial gestures resolve.
- **ACTIVE**: Virtual key is pressed. The gesture sees new events and can
  consume presses. Deactivates when its timeout expires or its callback
  requests it.
- **DISABLED**: Administrative state. Excluded from scanning until
  re-enabled via `gesture_enable()`.

## Virtual events

When a gesture activates, its virtual press is emitted immediately,
positioned in time at the original triggering press. Downstream processing
sees the virtual press before any buffered physical events that followed.

Virtual releases are emitted when the gesture deactivates. The coordinator
checks active gesture expiry times before emitting each buffered event,
ensuring virtual releases are correctly interleaved with physical events
in chronological order.

## Multi-outcome gestures

A gesture can have multiple outcomes (up to 15). The outcome is determined
by the callback during the PARTIAL phase and stored in
`gesture_t.timeout_outcome`. When the gesture activates, the outcome is
packed into the emitted gesture event as `gesture.outcome`.

The layer system resolves `(gesture_id, outcome)` pairs to keycodes. This
allows a single gesture to produce different keycodes depending on how it
was triggered. For example, a tap dance gesture with `max_presses=3` has
5 outcomes: hold(1), tap(2), hold(2), tap(3), hold(3).

## Priority and conflict resolution

Gesture ID doubles as priority: higher ID = higher priority.

1. **Tenure**: Active gestures consume events before inactive/partial
   gestures can trigger on them.
2. **First trigger wins**: A single initial press triggers at most one
   gesture's activation. Once partial gestures exist, no new gestures can
   trigger until the partial set resolves.
3. **Priority as tiebreaker**: When multiple gestures want to activate on
   the same press, the highest-priority one wins. Lower-priority partial
   gestures are evicted.

## Press history

A bitmap tracks which presses have been emitted downstream. When a physical
press is consumed by a gesture, its bit is never set, so the corresponding
release is silently swallowed. This prevents orphaned releases from reaching
downstream processing.

Encoder events bypass history tracking (they are press-only, no release).
Gesture events index by gesture_id (one bit per gesture, not per outcome).

## Gesture registration

Gestures are registered by name. Each `DEFINE_*` macro (e.g., `DEFINE_HOLD`,
`DEFINE_COMBO`, `DEFINE_TAPDANCE`) creates a `static gesture_t _gs_##name`.

Registration collects these into a pointer array and generates the enum:

```c
// With generated _GS_MAP macro (from gen_macros.py --gestures):
DEFINE_GESTURES(home_a, esc_combo, my_td);
// Expands to: enum { GS_home_a, GS_esc_combo, GS_my_td };
//             pointer array with designated initializers

// Without generated macros (manual fallback):
enum { GS(home_a), GS(esc_combo), GS(my_td) };
DEFINE_GESTURES_MANUAL(
    GESTURE_ENTRY(home_a),  // [GS_home_a] = &_gs_home_a
    GESTURE_ENTRY(esc_combo),
    GESTURE_ENTRY(my_td),
);
```

Order determines priority (last = highest). Designated initializers
ensure enum values match array indices regardless of listing order.

The `GS(name)` macro (expands to `GS_##name`) provides a unified reference
for use in enums, switch cases, and layer mappings.

## Memory model

All storage is static:
- Circular event buffer (configurable size, default 12)
- Gesture pointer array (user-defined, up to 4095 entries)
- Linked lists embedded in `gesture_t.next` fields (no heap allocation)
- Press history bitmap: `(NUM_KEY_POSITIONS + MAX_GESTURES + 7) / 8` bytes
- Single `uint16_t` for global next-timeout tracking

## QMK integration

The module is a QMK community module. Three hooks connect it to QMK:

```
pre_process_record_gestures()  ->  gesture_process_event(event)
                                     intercepts key/encoder events;
                                     returns false to suppress

housekeeping_task_gestures()   ->  gesture_tick()
                                     checks for expired timeouts

keyboard_post_init_gestures()  ->  layers_init() + gestures_init()
                                     initializes on startup
```

The entry point (`gestures.c`) converts QMK's `keyrecord_t` to
`gesture_event_t` by mapping matrix positions to dense key indices via
the user-provided `gesture_key_index()` function.

### Downstream emission

`gesture_emit_event` is the boundary between the gesture system and the
layer system. It receives `gesture_event_t` values:
- **Physical key events** (`EVENT_TYPE_KEY`): resolved through key layers
- **Gesture events** (`EVENT_TYPE_GESTURE`): resolved through gesture
  layers using `(gesture_id, outcome)` pairs
- **Encoder events** (`EVENT_TYPE_ENCODER`): resolved through encoder
  layers, expanded into N press+release pairs

The layer system resolves each event to a keycode and hands it to QMK's
`process_record` for execution.

## Source files

| File | Purpose |
|------|---------|
| `gesture_api.h` | Core types, callback protocol, gesture registration macros |
| `gesture_internal.h` | Coordinator state, internal API |
| `coordinator.c` | Coordinator implementation |
| `gestures.c` | QMK community module hooks (pipeline entry point) |
| `types/combo.h`, `types/combo.c` | Combo gesture type |
| `types/tapdance.h`, `types/tapdance.c` | Tap dance and hold gesture types |
| `types/precog.h`, `types/precog.c` | Precognition (home-row mods via combo overrides) |

## User-provided functions

The gesture system requires two functions from the user (defaults provided
by `DEFINE_GESTURES` / `DEFINE_GESTURES_MANUAL`):

- `gesture_t *gesture_get(gesture_id_t id)` -- return the gesture at the
  given index.
- `uint16_t gesture_count(void)` -- return the total number of gestures.

The layer system provides `gesture_emit_event` (see layers_overview.md).

The user must also provide `gesture_key_index(keypos_t pos)` to map matrix
positions to dense key indices (default provided by `DEFINE_KEY_INDICES`).
