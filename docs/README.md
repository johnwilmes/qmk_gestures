# QMK Gestures Module

A QMK community module that replaces several disjoint QMK features (combos,
tap-hold, tap dance, leader keys) with a unified callback-based gesture system
and a custom layer system.

## Quick start

### 1. Add the module

Add to your `keymap.json`:

```json
{
    "modules": ["johnwilmes/gestures"]
}
```

### 2. Generate helper macros

```bash
# Generate key position layout macro from your keyboard's info.json
python3 scripts/gen_macros.py --layout path/to/info.json > gesture_macros.h

# Optionally generate gesture MAP macro (for DEFINE_GESTURES)
python3 scripts/gen_macros.py --gestures 256 >> gesture_macros.h

# Or both at once
python3 scripts/gen_macros.py --layout path/to/info.json --gestures 256 > gesture_macros.h
```

### 3. Define key positions

```c
#include "gestures.h"
#include "gesture_macros.h"

// DEFINE_KEY_INDICES uses the generated GESTURE_LAYOUT_CALL macro
// to create a dense key index enum, forward/reverse lookup tables,
// and the gesture_key_index function.
DEFINE_KEY_INDICES(
    POS_Q, POS_W, POS_E, POS_R, POS_T,    POS_Y, POS_U, POS_I, POS_O, POS_P,
    POS_A, POS_S, POS_D, POS_F, POS_G,    POS_H, POS_J, POS_K, POS_L, POS_SCLN,
    POS_Z, POS_X, POS_C, POS_V, POS_B,    POS_N, POS_M, POS_COMM,POS_DOT,POS_SLSH,
                   POS_LTHMB, POS_RTHMB
)
```

### 4. Define gestures

```c
// Each DEFINE_* creates a static gesture_t named _gs_##name
DEFINE_HOLD(home_a, POS_A)
DEFINE_HOLD(home_s, POS_S)
DEFINE_COMBO(esc_combo, POS_Q, POS_W)
DEFINE_TAPDANCE(my_td, POS_LTHMB, 3)

// Register gestures (order = priority, last = highest)
// With generated _GS_MAP macro:
DEFINE_GESTURES(home_a, home_s, esc_combo, my_td);

// Or without generated macros (manual fallback):
enum { GS(home_a), GS(home_s), GS(esc_combo), GS(my_td) };
DEFINE_GESTURES_MANUAL(
    GESTURE_ENTRY(home_a),
    GESTURE_ENTRY(home_s),
    GESTURE_ENTRY(esc_combo),
    GESTURE_ENTRY(my_td),
);
```

### 5. Define layers

```c
// Key layer (physical keys)
DEFINE_DENSE_LAYER(base_keys,
    KC_Q, KC_W, KC_E, KC_R, KC_T,    KC_Y, KC_U, KC_I, KC_O, KC_P,
    KC_A, KC_S, KC_D, KC_F, KC_G,    KC_H, KC_J, KC_K, KC_L, KC_SCLN,
    // ...
);

// Gesture layer (gesture outcomes mapped to keycodes)
DEFINE_GESTURE_LAYER(base_gestures,
    GESTURE_MAP(home_a,    KC_LGUI),
    GESTURE_MAP(home_s,    KC_LALT),
    GESTURE_MAP(esc_combo, KC_ESC),
    GESTURE_MAP(my_td,     KC_LSFT, KC_X, KC_LCTL, KC_Y, KC_LALT),
);

// Encoder layer
DEFINE_ENCODER_LAYER(base_encoders,
    ENCODER_MAP(0, KC_VOLD, KC_VOLU),
);

// Sparse key layer for overlays
DEFINE_SPARSE_LAYER(nav_keys,
    {POS_H, KC_LEFT},
    {POS_J, KC_DOWN},
    {POS_K, KC_UP},
    {POS_L, KC_RGHT},
);

// Assemble the layer table
DEFINE_LAYER_TABLE(
    [0] = { .key = &base_keys, .gesture = &base_gestures, .encoder = &base_encoders },
    [1] = { .key = &nav_keys },
);
```

## Documentation

| Document | Contents |
|----------|----------|
| [Gesture System Overview](gestures_overview.md) | Lifecycle, priority, event model |
| [Architecture](architecture.md) | Coordinator internals, callback protocol, timeout model |
| [Layer System Overview](layers_overview.md) | Layer types, resolution, binding table |
| [Case Studies](case_studies.md) | Combo, hold, tap dance, precog worked examples |
| [QMK Integration](discussion.md) | QMK reentry strategy, event pipeline |
| [Feature Audit](features_audit.md) | QMK feature compatibility |

## Source files

| File | Purpose |
|------|---------|
| `gestures.h` | Umbrella header for end users |
| `gesture_api.h` | Core types, callback protocol, gesture registration |
| `gesture_layers.h` | Layer types and definition macros |
| `gesture_internal.h` | Coordinator and layer system internals |
| `coordinator.c` | Gesture coordinator implementation |
| `layer.c` | Layer resolution, binding table, QMK reentry |
| `gestures.c` | QMK community module hooks (pipeline entry point) |
| `types/combo.h`, `types/combo.c` | Combo gesture type |
| `types/tapdance.h`, `types/tapdance.c` | Tap dance and hold gesture types |
| `types/precog.h`, `types/precog.c` | Precognition (home-row mods via combo overrides) |
| `scripts/gen_macros.py` | Generate layout and gesture MAP macros |
