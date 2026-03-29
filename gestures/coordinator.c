/* Copyright 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include "gesture_internal.h"
#include "timer.h"

/*******************************************************************************
 * Static Coordinator State
 ******************************************************************************/

extern uint8_t _gs_press_history[];

static gesture_coordinator_t coordinator;

/*******************************************************************************
 * Forward Declarations (Internal Functions)
 ******************************************************************************/

static bool notify_active_gestures(gesture_event_t event);
static void buffer_append(gesture_event_t event);
static bool buffer_is_full(void);
static void process_buffer_overflow(void);
static bool try_emit_head(void);
static bool try_activate_gesture(void);
static void scan_inactive_gestures(gesture_event_t event);
static void scan_partial_gestures(gesture_event_t event);
static void propose_candidate(gesture_id_t gesture_id);
static void activate_gesture(gesture_id_t gesture_id);
static void deactivate_gesture(gesture_id_t gesture_id);
static void process_pending_timeouts(uint16_t now);

/* Timeout helper functions */
static void update_next_timeout(uint16_t absolute_expiry);
static void recalculate_next_timeout(void);
static uint16_t compute_remaining(uint16_t expiry, uint16_t now);
static void gesture_set_expiry(gesture_t *g, uint16_t event_time, gesture_timeout_t result);

/* History bitmap operations */
static void history_set(uint16_t index);
static void history_clear(uint16_t index);
static bool history_test(uint16_t index);

/* Event emission */
static void emit_press(gesture_event_t event);
static void emit_release(gesture_event_t event);

/* Queue operations */
static bool queue_remove(gesture_id_t *head, gesture_id_t gesture_id);
static void queue_push(gesture_id_t *head, gesture_id_t gesture_id);
static void queue_insert_ascending(gesture_id_t *head, gesture_id_t gesture_id);
static void queue_append(gesture_id_t *head, gesture_id_t gesture_id);

/*******************************************************************************
 * Buffer Helper Functions
 ******************************************************************************/

static inline uint8_t buffer_size(void) {
    if (coordinator.buffer_tail >= coordinator.buffer_head) {
        return coordinator.buffer_tail - coordinator.buffer_head;
    }
    return GESTURE_BUFFER_SIZE - coordinator.buffer_head + coordinator.buffer_tail;
}

static inline bool buffer_is_empty(void) {
    return coordinator.buffer_head == coordinator.buffer_tail;
}

static bool buffer_is_full(void) {
    uint8_t next_tail = (coordinator.buffer_tail + 1) % GESTURE_BUFFER_SIZE;
    return next_tail == coordinator.buffer_head;
}

static inline gesture_buffered_event_t *buffer_get(uint8_t index) {
    return &coordinator.buffer[(coordinator.buffer_head + index) % GESTURE_BUFFER_SIZE];
}

static inline gesture_buffered_event_t *buffer_head_event(void) {
    return &coordinator.buffer[coordinator.buffer_head];
}

static inline void buffer_advance_head(void) {
    coordinator.buffer_head = (coordinator.buffer_head + 1) % GESTURE_BUFFER_SIZE;
}

static inline uint8_t buffer_second_position(void) {
    if (buffer_size() <= 1) {
        return coordinator.buffer_tail;
    }
    return (coordinator.buffer_head + 1) % GESTURE_BUFFER_SIZE;
}

static void buffer_append(gesture_event_t event) {
    /* Coalesce consecutive same-direction encoder ticks at the buffer tail,
     * but only if the tail entry hasn't been scanned by gestures yet. */
    if (event.type == EVENT_TYPE_ENCODER &&
        coordinator.unprocessed_head != coordinator.buffer_tail) {
        uint8_t prev_idx = (coordinator.buffer_tail + GESTURE_BUFFER_SIZE - 1) % GESTURE_BUFFER_SIZE;
        gesture_buffered_event_t *tail = &coordinator.buffer[prev_idx];
        if (tail->event.type == EVENT_TYPE_ENCODER &&
            tail->event.encoder.encoder_id == event.encoder.encoder_id &&
            tail->event.encoder.clockwise == event.encoder.clockwise &&
            !tail->is_consumed &&
            tail->event.encoder.count < 255) {
            tail->event.encoder.count += event.encoder.count;
            tail->event.time = event.time;
            return;
        }
    }

    coordinator.buffer[coordinator.buffer_tail].event = event;
    coordinator.buffer[coordinator.buffer_tail].is_consumed = false;
    coordinator.buffer_tail = (coordinator.buffer_tail + 1) % GESTURE_BUFFER_SIZE;
}

/*******************************************************************************
 * Public API Implementation
 ******************************************************************************/

void gestures_init(void) {
    // Initialize buffer
    coordinator.buffer_head = 0;
    coordinator.buffer_tail = 0;
    coordinator.unprocessed_head = 0;

    // Initialize queues - all gestures start in inactive queue
    coordinator.inactive_head = (gesture_count() > 0) ? 0 : GESTURE_NULL_ID;
    coordinator.partial_head = GESTURE_NULL_ID;
    coordinator.active_head = GESTURE_NULL_ID;
    coordinator.disabled_head = GESTURE_NULL_ID;
    coordinator.candidate = GESTURE_NULL_ID;

    // Link all gestures into inactive queue
    uint16_t count = gesture_count();
    for (gesture_id_t i = 0; i < count; i++) {
        gesture_t *g = gesture_get(i);
        g->state = GS_INACTIVE;
        g->next = (i + 1 < count) ? (i + 1) : GESTURE_NULL_ID;
    }

    // Initialize timeout tracking
    coordinator.next_timeout = GESTURE_TIMEOUT_NEVER;

    // Assign and clear press history
    coordinator.press_history = _gs_press_history;
    memset(coordinator.press_history, 0, (gesture_key_count() + gesture_count() + 7) / 8);
}

bool gesture_process_event(gesture_event_t event) {
    // Handle pending timeouts before processing new event
    if (coordinator.next_timeout != GESTURE_TIMEOUT_NEVER &&
        timer_expired(event.time, coordinator.next_timeout)) {
        process_pending_timeouts(event.time);
    }

    // Active gestures see every new event first
    bool consumed = notify_active_gestures(event);
    if (consumed) {
        // Trigger event was consumed by an active gesture; don't buffer
    } else {
        if (buffer_is_full()) {
            process_buffer_overflow();
        }
        buffer_append(event);
    }

    // Process until stable
    while (try_emit_head() || try_activate_gesture()) {
        // Keep processing
    }

    // If the buffer is empty but active gestures have expired (e.g., a press
    // was consumed so the triggering event was never buffered), deactivate now.
    if (buffer_is_empty()) {
        gesture_id_t id = coordinator.active_head;
        while (id != GESTURE_NULL_ID) {
            gesture_t *g = gesture_get(id);
            gesture_id_t next_id = g->next;
            if (g->expiry != GESTURE_TIMEOUT_NEVER &&
                timer_expired(event.time, g->expiry)) {
                deactivate_gesture(id);
            }
            id = next_id;
        }
    }

    return false;  // Events are released from buffer asynchronously
}

void gesture_tick(void) {
    if (coordinator.next_timeout == GESTURE_TIMEOUT_NEVER) {
        return;  // No pending timeouts
    }

    uint16_t now = timer_read();
    if (!timer_expired(now, coordinator.next_timeout)) {
        return;  // Fast path: not yet expired
    }

    process_pending_timeouts(now);
}

void gesture_enable(gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);
    if (g->state == GS_DISABLED) {
        g->state = GS_INACTIVE;
        // If the gesture was lazily disabled (still in inactive queue,
        // never moved to disabled queue), just flip the state back.
        if (queue_remove(&coordinator.disabled_head, gesture_id)) {
            queue_push(&coordinator.inactive_head, gesture_id);
        }
    }
}

void gesture_disable(gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);

    switch (g->state) {
        case GS_ACTIVE:
            // Deactivate emits release and moves to INACTIVE
            deactivate_gesture(gesture_id);
            // Now in INACTIVE queue; move to DISABLED
            queue_remove(&coordinator.inactive_head, gesture_id);
            queue_push(&coordinator.disabled_head, gesture_id);
            break;

        case GS_PARTIAL:
            queue_remove(&coordinator.partial_head, gesture_id);
            queue_push(&coordinator.disabled_head, gesture_id);
            break;

        case GS_INACTIVE:
            // Don't search inactive queue - will be lazily moved when found
            // (scan_inactive_gestures checks g->state)
            break;

        case GS_DISABLED:
            // Already disabled
            return;
    }

    g->state = GS_DISABLED;
}

bool gesture_is_enabled(gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);
    return g->state != GS_DISABLED;
}

bool gesture_is_active(gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);
    return g->state == GS_ACTIVE;
}

/*******************************************************************************
 * Timeout Helper Functions
 ******************************************************************************/

static uint16_t compute_remaining(uint16_t expiry, uint16_t now) {
    if (expiry == GESTURE_TIMEOUT_NEVER) {
        return GESTURE_TIMEOUT_NEVER;
    }
    if (timer_expired(now, expiry)) {
        return 0;
    }
    return expiry - now;
}

static void gesture_set_expiry(gesture_t *g, uint16_t event_time, gesture_timeout_t result) {
    g->timeout_outcome = result.outcome;
    if (result.timeout == GESTURE_TIMEOUT_NEVER) {
        g->expiry = GESTURE_TIMEOUT_NEVER;
    } else {
        g->expiry = event_time + result.timeout;
        /* Clamp to avoid collision with the NEVER sentinel */
        if (g->expiry == GESTURE_TIMEOUT_NEVER) {
            g->expiry = GESTURE_TIMEOUT_NEVER - 1;
        }
    }
}

static void update_next_timeout(uint16_t absolute_expiry) {
    if (absolute_expiry == GESTURE_TIMEOUT_NEVER) {
        return;
    }

    if (coordinator.next_timeout == GESTURE_TIMEOUT_NEVER ||
        timer_expired(coordinator.next_timeout, absolute_expiry)) {
        coordinator.next_timeout = absolute_expiry;
    }
}

static void recalculate_next_timeout(void) {
    coordinator.next_timeout = GESTURE_TIMEOUT_NEVER;

    gesture_id_t id = coordinator.partial_head;
    while (id != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(id);
        update_next_timeout(g->expiry);
        id = g->next;
    }

    id = coordinator.active_head;
    while (id != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(id);
        update_next_timeout(g->expiry);
        id = g->next;
    }
}

/*******************************************************************************
 * History Bitmap Operations
 ******************************************************************************/

static void history_set(uint16_t index) {
    coordinator.press_history[index / 8] |= (1 << (index % 8));
}

static void history_clear(uint16_t index) {
    coordinator.press_history[index / 8] &= ~(1 << (index % 8));
}

static bool history_test(uint16_t index) {
    return coordinator.press_history[index / 8] & (1 << (index % 8));
}

/*******************************************************************************
 * Event Emission Helpers
 ******************************************************************************/

/* Map event to its dense history bitmap index.
 * Encoder events don't participate in press history (press-only, no release).
 * Gesture events index by gesture_id (one bit per gesture, not per outcome). */
static inline uint16_t history_index(gesture_event_t event) {
    if (event.type == EVENT_TYPE_GESTURE) {
        return gesture_key_count() + event.gesture.gesture_id;
    }
    return event.event_id;
}

/* Can this event start a new gesture trigger sequence? */
static inline bool is_trigger_event(gesture_buffered_event_t *entry) {
    if (entry->is_consumed) return false;
    if (entry->event.type == EVENT_TYPE_ENCODER) return true;
    return entry->event.pressed;
}

static void emit_press(gesture_event_t event) {
    if (event.type == EVENT_TYPE_ENCODER) {
        gesture_emit_event(event);
        return;
    }
    history_set(history_index(event));
    gesture_emit_event(event);
}

static void emit_release(gesture_event_t event) {
    /* Encoder events are press-only; no release to emit */
    if (event.type == EVENT_TYPE_ENCODER) return;
    uint16_t idx = history_index(event);
    if (history_test(idx)) {
        history_clear(idx);
        gesture_emit_event(event);
    }
}

/*******************************************************************************
 * Queue Operations
 ******************************************************************************/

static bool queue_remove(gesture_id_t *head, gesture_id_t gesture_id) {
    if (*head == GESTURE_NULL_ID) {
        return false;
    }

    if (*head == gesture_id) {
        gesture_t *g = gesture_get(gesture_id);
        *head = g->next;
        g->next = GESTURE_NULL_ID;
        return true;
    }

    gesture_id_t current = *head;
    while (current != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(current);
        if (g->next == gesture_id) {
            gesture_t *target = gesture_get(gesture_id);
            g->next = target->next;
            target->next = GESTURE_NULL_ID;
            return true;
        }
        current = g->next;
    }
    return false;
}

static void queue_push(gesture_id_t *head, gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);
    g->next = *head;
    *head = gesture_id;
}

static void queue_insert_ascending(gesture_id_t *head, gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);
    if (*head == GESTURE_NULL_ID || gesture_id < *head) {
        g->next = *head;
        *head = gesture_id;
        return;
    }
    gesture_id_t current = *head;
    gesture_t *curr_g = gesture_get(current);
    while (curr_g->next != GESTURE_NULL_ID && curr_g->next < gesture_id) {
        current = curr_g->next;
        curr_g = gesture_get(current);
    }
    g->next = curr_g->next;
    curr_g->next = gesture_id;
}

static void queue_append(gesture_id_t *head, gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);
    g->next = GESTURE_NULL_ID;

    if (*head == GESTURE_NULL_ID) {
        *head = gesture_id;
        return;
    }

    gesture_id_t current = *head;
    gesture_t *curr_g = gesture_get(current);
    while (curr_g->next != GESTURE_NULL_ID) {
        current = curr_g->next;
        curr_g = gesture_get(current);
    }
    curr_g->next = gesture_id;
}

/*******************************************************************************
 * Internal Functions
 ******************************************************************************/

/**
 * Notify active gestures of a new event (oldest-first, tenure model).
 * If a press is consumed (outcome=true), remaining gestures are skipped.
 * Returns true if the event was consumed.
 */
static bool notify_active_gestures(gesture_event_t event) {
    gesture_id_t id = coordinator.active_head;
    while (id != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(id);
        gesture_id_t next_id = g->next;  // Save before potential deactivation

        // Skip already-expired gestures; try_emit_head handles their release
        if (g->expiry != GESTURE_TIMEOUT_NEVER &&
            timer_expired(event.time, g->expiry)) {
            id = next_id;
            continue;
        }

        uint16_t remaining = compute_remaining(g->expiry, event.time);
        gesture_timeout_t result = g->callback(id, GS_QUERY_ACTIVE, &event, remaining, g->timeout_outcome, g->user_data);

        // Update expiry without overwriting timeout_outcome (which must
        // persist from activation through the ACTIVE phase)
        if (result.timeout == 0) {
            g->expiry = event.time;
        } else if (result.timeout == GESTURE_TIMEOUT_NEVER) {
            g->expiry = GESTURE_TIMEOUT_NEVER;
        } else {
            g->expiry = event.time + result.timeout;
            if (g->expiry == GESTURE_TIMEOUT_NEVER) {
                g->expiry = GESTURE_TIMEOUT_NEVER - 1;
            }
        }
        update_next_timeout(g->expiry);

        // Trigger events (key presses and encoder ticks) can be consumed
        if (result.outcome &&
            (event.type == EVENT_TYPE_ENCODER || event.pressed)) {
            return true;  // Consumed
        }

        id = next_id;
    }
    return false;
}

/**
 * Force resolution when buffer is full.
 */
static void process_buffer_overflow(void) {
    while (coordinator.partial_head != GESTURE_NULL_ID) {
        gesture_id_t id = coordinator.partial_head;
        gesture_t *g = gesture_get(id);
        coordinator.partial_head = g->next;
        g->state = GS_INACTIVE;
        g->next = GESTURE_NULL_ID;
        queue_push(&coordinator.inactive_head, id);
    }

    if (coordinator.candidate != GESTURE_NULL_ID) {
        gesture_id_t cand = coordinator.candidate;
        coordinator.candidate = GESTURE_NULL_ID;
        activate_gesture(cand);
    }

    while (buffer_is_full()) {
        if (!try_emit_head()) {
            buffer_advance_head();
        }
    }
}

/**
 * Try to emit the event at the head of the buffer.
 * Returns true if the buffer was advanced.
 */
static bool try_emit_head(void) {
    if (buffer_is_empty()) {
        return false;
    }
    if (coordinator.buffer_head == coordinator.unprocessed_head) {
        return false;
    }

    gesture_buffered_event_t *head = buffer_head_event();

    // Trigger events (unconsumed key presses and encoder ticks) are blocked
    // while PARTIAL gestures exist
    if (is_trigger_event(head) && coordinator.partial_head != GESTURE_NULL_ID) {
        return false;
    }

    // Before emitting, release any active gestures whose expiry falls
    // before this event's timestamp
    gesture_id_t id = coordinator.active_head;
    while (id != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(id);
        gesture_id_t next_id = g->next;
        if (g->expiry != GESTURE_TIMEOUT_NEVER &&
            timer_expired(head->event.time, g->expiry)) {
            deactivate_gesture(id);
        }
        id = next_id;
    }

    // Emit the event
    if (head->is_consumed) {
        // Consumed events are silently dropped (key releases of consumed
        // presses are handled by history bitmap — emit_release is a no-op)
    } else if (head->event.type == EVENT_TYPE_ENCODER) {
        emit_press(head->event);
    } else if (head->event.pressed) {
        emit_press(head->event);
    } else {
        emit_release(head->event);
    }

    buffer_advance_head();
    return true;
}

/**
 * Try to activate a gesture from the unprocessed events.
 * Returns true if work was done.
 */
static bool try_activate_gesture(void) {
    if (coordinator.unprocessed_head == coordinator.buffer_tail) {
        return false;
    }

    gesture_buffered_event_t *be = &coordinator.buffer[coordinator.unprocessed_head];
    coordinator.unprocessed_head = (coordinator.unprocessed_head + 1) % GESTURE_BUFFER_SIZE;

    if (be->is_consumed) {
        return true;
    }

    if (coordinator.partial_head == GESTURE_NULL_ID &&
        coordinator.candidate == GESTURE_NULL_ID) {
        if (is_trigger_event(be)) {
            scan_inactive_gestures(be->event);
        }
    } else {
        scan_partial_gestures(be->event);
    }

    return true;
}

/**
 * Scan inactive gestures for potential triggers.
 */
static void scan_inactive_gestures(gesture_event_t event) {
    gesture_id_t best_candidate = GESTURE_NULL_ID;
    gesture_id_t id = coordinator.inactive_head;
    gesture_id_t prev_id = GESTURE_NULL_ID;

    while (id != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(id);
        gesture_id_t next_id = g->next;

        if (g->state == GS_DISABLED) {
            if (prev_id == GESTURE_NULL_ID) {
                coordinator.inactive_head = next_id;
            } else {
                gesture_get(prev_id)->next = next_id;
            }
            g->next = GESTURE_NULL_ID;
            queue_push(&coordinator.disabled_head, id);
            id = next_id;
            continue;
        }

        if (best_candidate != GESTURE_NULL_ID && id < best_candidate) {
            prev_id = id;
            id = next_id;
            continue;
        }

        gesture_timeout_t result = g->callback(id, GS_QUERY_INITIAL, &event, 0, 0, g->user_data);

        if (result.timeout == 0 && !result.outcome) {
            prev_id = id;
            id = next_id;
            continue;
        }

        if (result.timeout > 0) {
            if (prev_id == GESTURE_NULL_ID) {
                coordinator.inactive_head = next_id;
            } else {
                gesture_get(prev_id)->next = next_id;
            }
            g->state = GS_PARTIAL;
            gesture_set_expiry(g, event.time, result);
            queue_insert_ascending(&coordinator.partial_head, id);
            id = next_id;
            continue;
        }

        // timeout == 0 && outcome != 0: wants immediate activation
        g->timeout_outcome = result.outcome;
        best_candidate = id;
        prev_id = id;
        id = next_id;
    }

    if (best_candidate != GESTURE_NULL_ID) {
        propose_candidate(best_candidate);
    }

    recalculate_next_timeout();
}

/**
 * Update partial gestures with a new event.
 */
static void scan_partial_gestures(gesture_event_t event) {
    gesture_id_t best_candidate = coordinator.candidate;
    bool candidate_changed = false;

    gesture_id_t id = coordinator.partial_head;
    while (id != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(id);
        gesture_id_t next_id = g->next;

        uint16_t remaining = compute_remaining(g->expiry, event.time);
        gesture_query_t query = g->timeout_outcome ? GS_QUERY_COMPLETE : GS_QUERY_PARTIAL;
        gesture_timeout_t result = g->callback(id, query, &event, remaining, g->timeout_outcome, g->user_data);

        if (result.timeout == 0) {
            if (result.outcome) {
                g->timeout_outcome = result.outcome;
                if (best_candidate == GESTURE_NULL_ID || id > best_candidate) {
                    best_candidate = id;
                    candidate_changed = true;
                }
            } else {
                queue_remove(&coordinator.partial_head, id);
                g->state = GS_INACTIVE;
                queue_push(&coordinator.inactive_head, id);
            }
        } else {
            gesture_set_expiry(g, event.time, result);
        }

        id = next_id;
    }

    if (candidate_changed) {
        propose_candidate(best_candidate);
    } else if (coordinator.candidate != GESTURE_NULL_ID &&
               coordinator.partial_head == GESTURE_NULL_ID) {
        activate_gesture(coordinator.candidate);
        coordinator.candidate = GESTURE_NULL_ID;
    }

    recalculate_next_timeout();
}

/**
 * Propose a gesture as candidate for activation.
 */
static void propose_candidate(gesture_id_t gesture_id) {
    if (coordinator.candidate != GESTURE_NULL_ID &&
        coordinator.candidate < gesture_id) {
        gesture_t *old = gesture_get(coordinator.candidate);
        old->state = GS_INACTIVE;
        queue_push(&coordinator.inactive_head, coordinator.candidate);
    }

    queue_remove(&coordinator.partial_head, gesture_id);

    while (coordinator.partial_head != GESTURE_NULL_ID &&
           coordinator.partial_head < gesture_id) {
        gesture_id_t id = coordinator.partial_head;
        gesture_t *g = gesture_get(id);
        coordinator.partial_head = g->next;
        g->next = GESTURE_NULL_ID;
        g->state = GS_INACTIVE;
        queue_push(&coordinator.inactive_head, id);
    }

    coordinator.candidate = gesture_id;

    if (coordinator.partial_head == GESTURE_NULL_ID) {
        activate_gesture(gesture_id);
        coordinator.candidate = GESTURE_NULL_ID;
    }
}

/**
 * Activate a gesture (emit virtual press, scan buffer for consumption).
 */
static void activate_gesture(gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);
    uint16_t activation_time = buffer_head_event()->event.time;

    // Emit virtual press with packed (gesture_id, outcome)
    gesture_event_t virtual_press = {
        .gesture = {
            .gesture_id = gesture_id,
            .outcome = g->timeout_outcome,
        },
        .time = activation_time,
        .type = EVENT_TYPE_GESTURE,
        .pressed = true,
    };
    emit_press(virtual_press);

    // Move to active queue
    g->state = GS_ACTIVE;
    queue_append(&coordinator.active_head, gesture_id);

    // Activation scan: replay buffer through the gesture callback.
    gesture_timeout_t last_result = {0};
    uint16_t last_event_time = activation_time;
    bool scan_done = false;

    // First call: ACTIVATION_INITIAL with the triggering press at buffer head
    gesture_buffered_event_t *trigger = buffer_head_event();
    last_result = g->callback(gesture_id, GS_QUERY_ACTIVATION_INITIAL,
                              &trigger->event, 0, g->timeout_outcome, g->user_data);
    if (last_result.timeout == 0) {
        scan_done = true;
    }
    if (last_result.outcome &&
        (trigger->event.type == EVENT_TYPE_ENCODER || trigger->event.pressed)) {
        trigger->is_consumed = true;
    }
    last_event_time = trigger->event.time;

    // Scan remaining buffered events (trigger already processed above)
    uint8_t count = buffer_size();
    for (uint8_t i = 1; i < count && !scan_done; i++) {
        gesture_buffered_event_t *be = buffer_get(i);
        if (be->is_consumed) {
            continue;
        }

        // Check if previous timeout expired before this event (rolling model)
        if (last_result.timeout != 0 &&
            last_result.timeout != GESTURE_TIMEOUT_NEVER) {
            uint16_t expiry = last_event_time + last_result.timeout;
            if (timer_expired(be->event.time, expiry)) {
                scan_done = true;
                break;
            }
        }

        last_result = g->callback(gesture_id, GS_QUERY_ACTIVATION_REPLAY,
                                  &be->event, 0, g->timeout_outcome, g->user_data);
        last_event_time = be->event.time;

        if (last_result.outcome &&
            (be->event.type == EVENT_TYPE_ENCODER || be->event.pressed)) {
            be->is_consumed = true;
        }

        if (last_result.timeout == 0) {
            scan_done = true;
        }
    }

    // Set expiry from last result without overwriting timeout_outcome
    // (timeout_outcome was set during resolution and must persist through ACTIVE)
    if (last_result.timeout == GESTURE_TIMEOUT_NEVER) {
        g->expiry = GESTURE_TIMEOUT_NEVER;
    } else {
        g->expiry = last_event_time + last_result.timeout;
        if (g->expiry == GESTURE_TIMEOUT_NEVER) {
            g->expiry = GESTURE_TIMEOUT_NEVER - 1;
        }
    }
    update_next_timeout(g->expiry);

    // Triggering press was at head; don't let other gestures trigger on it
    coordinator.unprocessed_head = buffer_second_position();
}

/**
 * Deactivate a gesture: emit virtual release immediately, move to INACTIVE.
 */
static void deactivate_gesture(gesture_id_t gesture_id) {
    gesture_t *g = gesture_get(gesture_id);

    queue_remove(&coordinator.active_head, gesture_id);

    uint16_t release_time = g->expiry;
    if (release_time == GESTURE_TIMEOUT_NEVER) {
        release_time = timer_read();
    }
    gesture_event_t virtual_release = {
        .gesture = {
            .gesture_id = gesture_id,
            .outcome = g->timeout_outcome,
        },
        .time = release_time,
        .type = EVENT_TYPE_GESTURE,
        .pressed = false,
    };
    emit_release(virtual_release);

    if (g->state != GS_DISABLED) {
        g->state = GS_INACTIVE;
        queue_push(&coordinator.inactive_head, gesture_id);
    }
}

/**
 * Process any expired timeouts.
 */
static void process_pending_timeouts(uint16_t now) {
    // N.B. propose_candidate may remove entries from partial_head, but only
    // those with IDs lower than the candidate.  Since partial_head is sorted
    // ascending and we iterate forward, removed entries have already been
    // visited, so next_id (always higher) remains valid.
    gesture_id_t id = coordinator.partial_head;
    while (id != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(id);
        gesture_id_t next_id = g->next;

        if (g->expiry != GESTURE_TIMEOUT_NEVER &&
            timer_expired(now, g->expiry)) {
            if (g->timeout_outcome) {
                propose_candidate(id);
            } else {
                queue_remove(&coordinator.partial_head, id);
                g->state = GS_INACTIVE;
                queue_push(&coordinator.inactive_head, id);
            }
        }
        id = next_id;
    }

    if (coordinator.candidate != GESTURE_NULL_ID &&
        coordinator.partial_head == GESTURE_NULL_ID) {
        activate_gesture(coordinator.candidate);
        coordinator.candidate = GESTURE_NULL_ID;
    }

    id = coordinator.active_head;
    while (id != GESTURE_NULL_ID) {
        gesture_t *g = gesture_get(id);
        gesture_id_t next_id = g->next;

        if (g->expiry != GESTURE_TIMEOUT_NEVER &&
            timer_expired(now, g->expiry)) {
            deactivate_gesture(id);
        }
        id = next_id;
    }

    recalculate_next_timeout();

    while (try_emit_head() || try_activate_gesture()) {
        // Keep processing
    }
}
