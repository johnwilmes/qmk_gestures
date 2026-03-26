# Gesture System Architecture

Detailed design of the gesture coordinator, callback protocol, event buffer,
and timeout model.

## 1. Data Structures

### gesture_t

```c
typedef struct {
    gesture_state_t    state : 2;              // GS_INACTIVE/PARTIAL/ACTIVE/DISABLED
    gesture_id_t       next : 14;              // Linked list pointer (GESTURE_NULL_ID = 0x3FFF)
    uint8_t            timeout_outcome;        // 0=cancel, 1..15=which outcome activates
    uint8_t            num_outcomes;           // Number of possible outcomes (1 for single-outcome)
    uint16_t           expiry;                 // Absolute expiry time (16-bit, wrapping)
    gesture_callback_t callback;
    void              *user_data;
} gesture_t;
```

Gestures are stored in a user-provided pointer array. The `next` field
forms singly linked lists for four queues: inactive, partial, active, and
disabled. The 14-bit `next` field supports up to 16383 gestures.

The `timeout_outcome` field encodes what happens on timeout: 0 means
cancel (return to INACTIVE), 1..15 means activate with that outcome
number. The `num_outcomes` field records how many outcomes this gesture
can produce.

### gesture_coordinator_t

```c
typedef struct {
    gesture_buffered_event_t buffer[GESTURE_BUFFER_SIZE];
    uint8_t  buffer_head;       // Oldest event
    uint8_t  buffer_tail;       // Next write position
    uint8_t  unprocessed_head;  // Next event for gesture scanning

    gesture_id_t inactive_head;  // Unsorted
    gesture_id_t partial_head;   // Ascending by ID (lowest priority first)
    gesture_id_t active_head;    // Ordered by activation time (oldest first)
    gesture_id_t disabled_head;

    gesture_id_t candidate;      // Gesture awaiting activation (GESTURE_NULL_ID if none)

    uint16_t next_timeout;       // Absolute time of next expiry (GESTURE_TIMEOUT_NEVER = none)

    uint8_t press_history[GESTURE_HISTORY_SIZE];
} gesture_coordinator_t;
```

The buffer is circular. Three pointers partition it:
- `buffer_head..unprocessed_head`: events scanned by gesture callbacks,
  but may still be blocked from emission (e.g., a press blocked while
  PARTIAL gestures exist, or a candidate is pending activation)
- `unprocessed_head..buffer_tail`: events awaiting gesture scanning

### gesture_buffered_event_t

```c
typedef struct {
    gesture_event_t event;
    bool            is_consumed;
} gesture_buffered_event_t;
```

No per-event gesture tracking. Consumption is a simple boolean.

## 2. Callback Protocol

### Signature

```c
typedef gesture_timeout_t (*gesture_callback_t)(
    gesture_id_t          id,
    gesture_query_t       query,
    const gesture_event_t *event,
    uint16_t              remaining_ms,
    uint8_t               current_outcome,
    void                  *user_data
);
```

The `current_outcome` parameter provides the gesture's current
`timeout_outcome` value. For INITIAL queries it is 0. For
ACTIVATION/ACTIVE queries it is the outcome that triggered activation.

### Query values

| Query | When called | Meaning |
|-------|------------|---------|
| `GS_QUERY_INITIAL` | Gesture was INACTIVE, new initial press at front of buffer | Should this gesture participate? |
| `GS_QUERY_PARTIAL` | Gesture is PARTIAL, `timeout_outcome` is 0 (would cancel on timeout) | Update with new event |
| `GS_QUERY_COMPLETE` | Gesture is PARTIAL, `timeout_outcome` is nonzero (would activate on timeout) | Update with new event |
| `GS_QUERY_ACTIVATION_INITIAL` | Gesture just activated, first callback during buffer replay | Initialize active state |
| `GS_QUERY_ACTIVATION_REPLAY` | Gesture activated, subsequent events during buffer replay | Consume or pass events |
| `GS_QUERY_ACTIVE` | Gesture is active, new event arrived | Consume, update timeout, or deactivate |

The distinction between PARTIAL and COMPLETE lets callbacks know whether
they are currently on track to activate or cancel on timeout.

### Return value semantics

The callback returns `gesture_timeout_t { uint16_t timeout; uint8_t outcome; }`.

**During INACTIVE/PARTIAL/COMPLETE (pre-activation):**

| timeout | outcome | Effect |
|---------|---------|--------|
| 0 | 0 | Not interested / cancel. Return to INACTIVE. |
| 0 | nonzero | Ready to activate immediately with this outcome. |
| >0 | 0 | Stay/become PARTIAL. Cancel on timeout. |
| >0 | nonzero | Stay/become PARTIAL. Activate on timeout with this outcome. |
| NEVER | 0 | Stay PARTIAL indefinitely, cancel direction. |
| NEVER | nonzero | Stay PARTIAL indefinitely, activate direction. |

**During ACTIVATION_INITIAL/ACTIVATION_REPLAY (buffer scan):**

| timeout | outcome (press) | outcome (release) | Effect |
|---------|---------|---------|--------|
| 0 | nonzero: consume | ignored | Deactivate after scan completes. |
| 0 | 0: pass | ignored | Deactivate after scan completes. |
| >0 | nonzero: consume | ignored | Continue scan. Rolling timeout. |
| >0 | 0: pass | ignored | Continue scan. |
| NEVER | nonzero: consume | ignored | Continue scan, no timeout. |
| NEVER | 0: pass | ignored | Continue scan. |

**During ACTIVE (post-activation):**

| timeout | outcome (press) | outcome (release) | Effect |
|---------|---------|---------|--------|
| 0 | n/a | n/a | Deactivate immediately. |
| >0 | nonzero: consume | ignored | Stay active, update timeout. |
| >0 | 0: pass | ignored | Stay active, update timeout. |
| NEVER | nonzero/0 | ignored | Stay active forever. |

Press consumption is the only action outcome controls for active gestures.
Releases are never consumed. Encoder events are press-only and consumable
like key presses.

## 3. Event Processing Pipeline

Two entry points from QMK:
- `gesture_process_event(event)` -- called from `pre_process_record_gestures`
  for every key and encoder event. Main processing path.
- `gesture_tick()` -- called from `housekeeping_task_gestures` on every scan
  cycle (~1-5ms). Checks timeouts.

Resolved events are emitted via `gesture_emit_event` to the layer system.

### Trigger events

A "trigger event" is an unconsumed key press or encoder tick. Key releases
and consumed events are non-triggers. Trigger events block the buffer while
PARTIAL gestures exist. Non-trigger events emit immediately.

### Main entry: gesture_process_event

```
1. Process pending timeouts (if any expired before this event)
2. Notify active gestures (oldest first)
   - If a trigger event is consumed, skip buffering
3. Buffer the event (if not consumed by an active gesture)
   - Encoder events: coalesce with tail if same direction and unscanned
4. Loop: try_emit_head() || try_activate_gesture()
```

### Encoder aggregation

Consecutive same-direction encoder ticks are coalesced into a single buffer
entry. When a new encoder event arrives and the buffer tail is an unscanned
same-direction encoder event, the count is incremented instead of pushing a
new entry. Once scanned by gesture callbacks, the entry is sealed.

### try_activate_gesture

Advances `unprocessed_head` by one event. Routes to:

- **scan_inactive_gestures** if no partial gestures and event is a trigger.
  Those returning timeout>0 move to PARTIAL. The highest-priority
  immediate-activate candidate is proposed.

- **scan_partial_gestures** if partial gestures exist. All partial gestures
  see the event. The highest-priority candidate is proposed.

### propose_candidate

Sets the candidate gesture and evicts all lower-priority partial gestures
back to INACTIVE. If no higher-priority partial gestures remain, activates
immediately.

### activate_gesture

```
1. Emit virtual press via gesture_event_t with packed (gesture_id, outcome)
2. Move to ACTIVE queue (append for oldest-first ordering)
3. Call callback with GS_QUERY_ACTIVATION_INITIAL (triggering press)
   - Presses with nonzero outcome are marked consumed
4. Replay remaining buffered events (GS_QUERY_ACTIVATION_REPLAY)
   - Rolling timeout: each result checked against next event's timestamp
5. Set expiry from last result
6. Advance unprocessed_head past the triggering press
```

### try_emit_head

Returns false if buffer is empty, nothing processed, or head is a blocked
trigger event. Otherwise:
1. Check active gesture expiries against head event's timestamp. Deactivate
   expired gestures.
2. Emit the head event (consumed events are silently dropped).
3. Advance buffer head.

### deactivate_gesture

Emits virtual release with packed `(gesture_id, outcome)` immediately (not
buffered).

### notify_active_gestures

Called on every new event before buffering. Iterates active queue
oldest-first. Each callback is called with GS_QUERY_ACTIVE. If timeout=0,
gesture expiry is set for `try_emit_head` to handle. If a trigger event
has nonzero outcome, the event is consumed and no further active gestures
see it (tenure model).

## 4. Timeout Model

All timeouts are stored as absolute 16-bit expiry times. Callbacks return
relative timeouts; the coordinator converts: `expiry = event.time + timeout`.

`GESTURE_TIMEOUT_NEVER (0xFFFF)` means never expires. If computed expiry
wraps to `0xFFFF`, it is clamped to `0xFFFE`.

The coordinator tracks `next_timeout` -- the soonest expiry. `gesture_tick`
checks this every matrix scan. 16-bit timer wraps every ~65 seconds.

### Remaining time for callbacks

The coordinator passes `remaining_ms = compute_remaining(g->expiry, event.time)`.
If expired, 0. If NEVER, NEVER. Otherwise the arithmetic difference.

## 5. Press History Bitmap

Maps key positions and gesture IDs to dense bit indices:

```c
physical key: event.event_id                       // 0..NUM_KEY_POSITIONS-1
gesture:      GESTURE_OFFSET + event.gesture.gesture_id  // one bit per gesture, not per outcome
encoder:      (not tracked)                        // press-only, no release
```

When a press is emitted, its bit is set. When a release is emitted, the
bit is checked: if set, clear and emit; if not set (press was consumed),
silently drop the release.

Size: `(NUM_KEY_POSITIONS + MAX_GESTURES + 7) / 8` bytes.

## 6. Buffer Overflow

When the buffer is full:
1. Cancel all PARTIAL gestures
2. If a candidate exists, activate it immediately
3. Force-drain the buffer
4. Last resort: drop the head event

## 7. Queue Semantics

| Queue | Order | Purpose |
|-------|-------|---------|
| Inactive | Unsorted | Pool waiting for triggers |
| Partial | Ascending by ID (lowest priority first) | Being resolved |
| Active | Ordered by activation time (oldest first) | Consuming events |
| Disabled | Unordered | Administratively disabled |

The partial queue's ascending sort is load-bearing:

1. **Eviction efficiency**: lower-priority gestures at the head allow
   simple head-pop loops in `propose_candidate`.

2. **Iteration safety**: `process_pending_timeouts` visits low-priority
   first. When a higher-priority gesture calls `propose_candidate`, it
   only evicts entries behind the cursor.

## 8. Invariants

1. The triggering event is at `buffer_head` when `activate_gesture` runs.
2. `try_emit_head` blocks trigger events while PARTIAL gestures exist.
3. At most one gesture activates per triggering event.
4. A consumed press's release is silently dropped (press history).
5. Once consumed, a trigger event cannot trigger new gestures.
6. Virtual releases are interleaved correctly with buffered events.
7. Active gestures see events oldest-first (tenure model).
8. Encoder events bypass press history (press-only, no release).
