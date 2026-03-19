# Gesture System Architecture

Detailed design of the gesture coordinator, callback protocol, event buffer,
and timeout model.

## 1. Data Structures

### gesture_t

```c
typedef struct {
    gesture_state_t    state : 2;           // GS_INACTIVE/PARTIAL/ACTIVE/DISABLED
    gesture_id_t       next : 13;           // Linked list pointer (GESTURE_NULL_ID = 0x1FFF)
    bool               timeout_outcome : 1; // What happens on timeout
    uint16_t           expiry;              // Absolute expiry time (16-bit, wrapping)
    gesture_callback_t callback;
    void              *user_data;
} gesture_t;
```

Gestures are stored in a user-provided array. The `next` field forms singly
linked lists for four queues: inactive, partial, active, and disabled. The
13-bit `next` field supports up to 8191 gestures.

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
- `buffer_head..unprocessed_head`: events that have been scanned by gesture
  callbacks, but may still be blocked from emission (e.g., a press blocked
  while PARTIAL gestures exist, or a candidate is pending activation)
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
    void                  *user_data
);
```

### Query values

| Query | When called | Meaning |
|-------|------------|---------|
| `GS_QUERY_INITIAL` | Gesture was INACTIVE, new initial press at front of buffer | Should this gesture participate? |
| `GS_QUERY_PARTIAL` | Gesture is PARTIAL, `timeout_outcome` is false (would cancel on timeout) | Update with new event |
| `GS_QUERY_COMPLETE` | Gesture is PARTIAL, `timeout_outcome` is true (would activate on timeout) | Update with new event |
| `GS_QUERY_ACTIVATION_INITIAL` | Gesture just activated, first callback during buffer replay | Initialize active state |
| `GS_QUERY_ACTIVATION_REPLAY` | Gesture activated, subsequent events during buffer replay | Consume or pass events |
| `GS_QUERY_ACTIVE` | Gesture is active, new event arrived | Consume, update timeout, or deactivate |

The distinction between PARTIAL and COMPLETE lets callbacks know whether
they are currently on track to activate or cancel at timeout. This is
critical for the MECE tap/hold pattern: a hold callback and a tap callback
share state and see the same events, but the coordinator tells each one
what its current timeout outcome is.

### Return value semantics

The callback returns `gesture_timeout_t { uint16_t timeout; bool outcome; }`.

**During INACTIVE/PARTIAL/COMPLETE (pre-activation):**

| timeout | outcome | Effect |
|---------|---------|--------|
| 0 | false | Not interested / cancel. Return to INACTIVE. |
| 0 | true | Ready to activate immediately. |
| >0 | false | Stay/become PARTIAL. Cancel on timeout. |
| >0 | true | Stay/become PARTIAL. Activate on timeout. |
| NEVER | false | Stay PARTIAL indefinitely, cancel direction. |
| NEVER | true | Stay PARTIAL indefinitely, activate direction. |

**During ACTIVATION_INITIAL/ACTIVATION_REPLAY (buffer scan):**

| timeout | outcome (press) | outcome (release) | Effect |
|---------|---------|---------|--------|
| 0 | true: consume | ignored | Deactivate after scan completes. |
| 0 | false: pass | ignored | Deactivate after scan completes. |
| >0 | true: consume | ignored | Continue scan. Rolling timeout checked against next event. |
| >0 | false: pass | ignored | Continue scan. |
| NEVER | true: consume | ignored | Continue scan, no timeout. |
| NEVER | false: pass | ignored | Continue scan. |

**During ACTIVE (post-activation):**

| timeout | outcome (press) | outcome (release) | Effect |
|---------|---------|---------|--------|
| 0 | n/a | n/a | Deactivate immediately. |
| >0 | true: consume press | ignored | Stay active, update timeout. |
| >0 | false: pass | ignored | Stay active, update timeout. |
| NEVER | true/false | ignored | Stay active forever (until callback says otherwise). |

Press consumption is the only action outcome controls for active gestures.
Releases are never consumed -- they always pass through the buffer.
Encoder events are press-only and consumable like key presses.

## 3. Event Processing Pipeline

The gesture system has two entry points from QMK:
- `gesture_process_event(event)` -- called from `pre_process_record_user` for
  every key event and encoder event. This is the main processing path.
- `gesture_tick()` -- called from `matrix_scan_user` on every scan cycle
  (~1-5ms). Checks `next_timeout` and calls `process_pending_timeouts` if
  any gesture has expired.

Resolved events are emitted downstream via `gesture_emit_event` to custom
virtual key / action processing that replaces QMK's built-in pipeline.

### Trigger events

A "trigger event" is an event that can start a new gesture trigger sequence:
unconsumed key presses and unconsumed encoder ticks. Key releases and consumed
events are non-triggers. The `is_trigger_event()` predicate encapsulates this.

Trigger events block the buffer while PARTIAL gestures exist (they might be
claimed by the resolving gesture). Non-trigger events emit immediately.

### Main entry: gesture_process_event

```
1. Process pending timeouts (if any expired before this event)
2. Notify active gestures (oldest first)
   - Expired gestures are skipped (try_emit_head handles their release)
   - If a trigger event is consumed, skip buffering
3. Buffer the event (if not consumed by an active gesture)
   - Encoder events: coalesce with tail if same direction and unscanned
4. Loop: try_emit_head() || try_activate_gesture()
```

### Encoder aggregation

Consecutive same-direction encoder ticks are coalesced into a single buffer
entry. When a new encoder event arrives and the buffer tail is an unscanned
(`unprocessed_head` hasn't reached it) same-direction encoder event, the
count is incremented instead of pushing a new entry. Once an encoder event
has been scanned by gesture callbacks, it is sealed and cannot be coalesced
further.

This limits buffer pressure from encoder bursts: regardless of how many
same-direction ticks arrive while the buffer is blocked, they occupy at most
one entry (two for alternating CW/CCW).

### try_activate_gesture

Advances `unprocessed_head` by one event. Routes to:

- **scan_inactive_gestures** if no partial gestures and event is a trigger
  event (unconsumed key press or encoder tick). Scans all inactive gestures.
  Those returning timeout>0 move to PARTIAL. The highest-priority
  immediate-activate candidate is proposed.

- **scan_partial_gestures** if partial gestures exist. All partial gestures
  see the event. Gestures requesting activation become candidates; those
  canceling return to INACTIVE. The highest-priority candidate is proposed.

### propose_candidate

Sets the candidate gesture and evicts all lower-priority partial gestures
back to INACTIVE. If no higher-priority partial gestures remain, activates
immediately.

This ensures the invariant: at most one gesture activates per triggering
press, and it is always the highest-priority one.

### activate_gesture

```
1. Emit virtual press via gesture_event_t (timestamped at buffer head)
2. Move to ACTIVE queue (append for oldest-first ordering)
3. Call callback with GS_QUERY_ACTIVATION_INITIAL (triggering press event)
   - Presses with outcome=true are marked consumed
4. Replay remaining buffered events through callback (GS_QUERY_ACTIVATION_REPLAY)
   - Rolling timeout: each result's timeout is checked against the NEXT
     event's timestamp. If the timeout would expire before the next event,
     the gesture deactivates at that point.
   - Presses with outcome=true are marked consumed
5. Set expiry from last result. If the gesture expired during the scan,
   `try_emit_head` will deactivate it at the correct buffer position.
6. Set unprocessed_head to buffer_second_position (triggering press is
   claimed; don't let other gestures trigger on it)
```

The rolling timeout model simulates real-time playback. A callback returning
timeout=50 on event A at t=100 means "deactivate at t=150 if nothing else
happens." If event B arrives at t=120, the gesture sees B. If B's callback
returns timeout=50, the new deadline is t=170, and so on.

### try_emit_head

Returns false if:
- Buffer is empty
- `buffer_head == unprocessed_head` (nothing processed yet)
- Head is a trigger event and PARTIAL gestures exist (blocked)

Otherwise:
1. Check all active gesture expiries against the head event's timestamp.
   Deactivate any whose expiry falls before this event. This is the mechanism
   for correctly interleaving virtual releases with physical events.
2. Emit the head event:
   - Consumed: silently dropped
   - Encoder: emit via `emit_press` (no history tracking)
   - Key press: emit via `emit_press` (sets history bit)
   - Key release: emit via `emit_release` (checks history bit; suppresses
     orphaned releases)
3. Advance buffer head.

### deactivate_gesture

Emits virtual release immediately (not buffered). The caller is responsible
for calling it at the correct point:
- `try_emit_head`: expiry check before each emission
- `process_pending_timeouts`: timer-based expiry
- `gesture_process_event`: cleanup pass for expired gestures when buffer is empty

After emitting, the gesture moves to INACTIVE (or stays DISABLED if
`gesture_disable` was called while it was active).

### notify_active_gestures

Called on every new event before buffering. Iterates the active queue
(oldest-activated first). Each gesture's callback is called with
GS_QUERY_ACTIVE. If timeout=0, the gesture's expiry is set to
`event.time` -- `try_emit_head` will deactivate it at the correct
buffer position. If a trigger event (key press or encoder tick) has
outcome=true, the event is consumed (not buffered) and no further active
gestures see it (tenure model: oldest gesture has first claim).

Gestures whose expiry is already past are skipped -- their release will
be handled by `try_emit_head` at the correct buffer position.

Edge case: if the trigger event is consumed and the buffer is empty,
there is nothing for `try_emit_head` to process. `gesture_process_event`
runs a cleanup pass after the main loop to deactivate expired active
gestures when the buffer is empty.

## 4. Timeout Model

All timeouts are stored as absolute 16-bit expiry times on `gesture_t.expiry`.
Callbacks return relative timeouts; the coordinator converts:
`expiry = event.time + timeout`.

The only special value is `GESTURE_TIMEOUT_NEVER (0xFFFF)`: never expires.
When a callback returns `timeout = 0`, the expiry is set to `event_time`
(a real timestamp that is already in the past), so the normal expiry checks
handle it. If the computed expiry wraps to `0xFFFF`, it is clamped to
`0xFFFE` to avoid collision with the NEVER sentinel.

The coordinator tracks a single `next_timeout` -- the soonest expiry across
all PARTIAL and ACTIVE gestures. `gesture_tick` checks this on every matrix
scan iteration. If expired, `process_pending_timeouts` scans both queues.

16-bit timer wraps every ~65 seconds. `timer_expired()` handles wraparound
correctly for intervals under ~32 seconds, which is well beyond any
practical gesture timeout.

### Remaining time for callbacks

When calling a callback, the coordinator passes `remaining_ms`:
`remaining = compute_remaining(g->expiry, event.time)`. If the expiry has
passed, remaining is 0. If expiry is NEVER, remaining is NEVER. Otherwise
it is the arithmetic difference. `timer_expired()` handles 16-bit
wraparound for the "is it expired?" check. The `expiry - now` subtraction
also handles wraparound correctly via unsigned arithmetic, since
`timer_expired` already confirmed the expiry is in the future (within the
~32s window). The NEVER sentinel is checked first.

## 5. Press History Bitmap

Maps every physical key position and every gesture ID to a dense bit index:

```c
physical: event.event_id                   // 0..NUM_KEY_POSITIONS-1 (EVENT_TYPE_KEY)
gesture:  GESTURE_OFFSET + event.event_id  // NUM_KEY_POSITIONS..NUM_KEY_POSITIONS+MAX_GESTURES-1 (EVENT_TYPE_GESTURE)
encoder:  (not tracked)                    // Encoder events are press-only, no history needed
```

Physical key indices arrive as `event_id` in `gesture_event_t` events of
type `EVENT_TYPE_KEY`. The user maps matrix positions to dense key indices
at the pipeline entry point (`pre_process_record_user`) before calling
`gesture_process_event`. See overview.md for the wiring pattern.

When a key press is emitted, its bit is set. When a release is emitted, the
bit is checked: if set, clear it and emit the release. If not set, the
corresponding press was consumed, so the release is silently dropped.

Encoder events (`EVENT_TYPE_ENCODER`) bypass history tracking entirely.
They are press-only — there is no release to match. `emit_press` skips
the history bit for encoder events, and `emit_release` is a no-op.

Size: `(NUM_KEY_POSITIONS + MAX_GESTURES + 7) / 8` bytes.

## 6. Buffer Overflow

When the buffer is full:
1. Cancel all PARTIAL gestures (return to INACTIVE)
2. If a candidate exists, activate it immediately
3. Force-drain the buffer via `try_emit_head`
4. Last resort: drop the head event

This ensures forward progress. Overflow is unlikely with typical buffer
sizes (12-16 events) but can occur with complex leader-key sequences or
extremely fast typing during gesture resolution.

## 7. Queue Semantics

| Queue | Order | Purpose |
|-------|-------|---------|
| Inactive | Unsorted | Pool of gestures waiting for triggers |
| Partial | Ascending by ID (lowest priority first) | Gestures currently being resolved |
| Active | Ordered by activation time (oldest first) | Active gestures consuming events |
| Disabled | Unordered | Administratively disabled gestures |

The partial queue's ascending sort order is load-bearing for two reasons:

1. **Eviction efficiency in `propose_candidate`**: lower-priority gestures
   are at the head, so evicting all gestures below the candidate is a simple
   head-pop loop rather than scattered `queue_remove` calls.

2. **Iteration safety in `process_pending_timeouts`**: the loop visits
   low-priority gestures first. When a higher-priority gesture later calls
   `propose_candidate`, it only evicts entries behind the iteration cursor
   (already visited). This guarantees the saved `next_id` is never removed
   from the queue mid-iteration, so no gestures are skipped.

Disabled gestures in the inactive queue are discovered lazily during
`scan_inactive_gestures` and moved to the disabled queue. This avoids
an O(n) search of the unsorted inactive queue during `gesture_disable`.

## 8. Invariants

1. The triggering event is always at `buffer_head` when `activate_gesture`
   runs.
2. `try_emit_head` blocks trigger events while PARTIAL gestures exist.
3. At most one gesture activates per triggering event.
4. A consumed key press's release is silently dropped (press history).
5. Once a trigger event is consumed, it cannot trigger new gestures.
6. Virtual releases are emitted at the correct chronological position
   relative to buffered events (via expiry check in `try_emit_head`).
7. Active gestures see events oldest-first (tenure model).
8. Encoder events are press-only and bypass press history tracking.
