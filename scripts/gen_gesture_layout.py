#!/usr/bin/env python3
"""Generate gesture_layout.h from a QMK keyboard's info.json.

Reads the keyboard layout definition and produces:
  - GESTURE_LAYOUT_CALL(_F, ...) -- higher-order macro pairing each position
    with its (row, col), calling _F(name, row, col) for each key
  - GESTURE_LAYOUT(...) -- identity macro with arity check, same visual
    arrangement as GESTURE_LAYOUT_CALL
  - NUM_KEY_POSITIONS -- total number of physical key positions

Usage:
    python3 gen_gesture_layout.py <qmk_firmware_dir> <keyboard> [layout_name]

Example:
    python3 gen_gesture_layout.py ~/qmk_firmware splitkb/halcyon/kyria
"""

import json
import sys
from pathlib import Path


def load_keyboard_info(qmk_dir, keyboard):
    """Load and return the keyboard's info.json data."""
    for name in ("info.json", "keyboard.json"):
        path = Path(qmk_dir) / "keyboards" / keyboard / name
        if path.exists():
            with open(path) as f:
                return json.load(f)
    sys.exit(f"Error: no info.json found under {Path(qmk_dir) / 'keyboards' / keyboard}")


def resolve_layout(info, layout_name=None):
    """Resolve the layout to use, returning (name, keys)."""
    layouts = info.get("layouts", {})
    aliases = info.get("layout_aliases", {})

    if layout_name:
        resolved = aliases.get(layout_name, layout_name)
        if resolved not in layouts:
            available = list(layouts.keys()) + list(aliases.keys())
            sys.exit(f"Error: layout '{layout_name}' not found. Available: {available}")
        return resolved, layouts[resolved]["layout"]

    if "LAYOUT" in aliases:
        name = aliases["LAYOUT"]
        return name, layouts[name]["layout"]
    name = next(iter(layouts))
    return name, layouts[name]["layout"]


def group_into_visual_rows(keys):
    """Group keys into visual rows by detecting x-coordinate decreases.

    QMK info.json lists keys in visual reading order (left-to-right,
    top-to-bottom). A new row starts when x decreases from the previous key.
    """
    rows = [[keys[0]]]
    for key in keys[1:]:
        if key["x"] < rows[-1][-1]["x"]:
            rows.append([])
        rows[-1].append(key)
    return rows


def sanitize_label(label):
    """Sanitize a label for use as a C identifier."""
    return label.replace(" ", "_").replace("-", "_")


def generate_header(keyboard, layout_name, keys):
    """Generate the gesture_layout.h content."""
    num_keys = len(keys)
    rows = group_into_visual_rows(keys)

    # Use sanitized labels from info.json as parameter names
    for key in keys:
        key["param"] = sanitize_label(key.get("label", f"k{keys.index(key)}"))

    lines = []
    lines.append(f"/* gesture_layout.h -- generated from {keyboard}")
    lines.append(f" * Layout: {layout_name} ({num_keys} keys)")
    lines.append(f" *")
    lines.append(f" * Key positions (label: matrix [row, col]):")
    for row in rows:
        parts = [f"{k['param']}:[{k['matrix'][0]},{k['matrix'][1]}]" for k in row]
        lines.append(f" *   {' '.join(parts)}")
    lines.append(f" */")
    lines.append(f"")
    lines.append(f"#pragma once")
    lines.append(f"")
    lines.append(f"#define NUM_KEY_POSITIONS {num_keys}")
    lines.append(f"")

    # --- GESTURE_LAYOUT_CALL ---
    lines.append(f"/*")
    lines.append(f" * Higher-order layout macro. Calls _F(name, row, col) for each key.")
    lines.append(f" * Use with DEFINE_KEY_INDICES to generate key index definitions.")
    lines.append(f" */")

    # Build parameter list with visual row grouping
    param_rows = []
    for i, row in enumerate(rows):
        params = ", ".join(k["param"] for k in row)
        param_rows.append(params)

    lines.append(f"#define GESTURE_LAYOUT_CALL( \\")
    lines.append(f"    _F, \\")
    for i, pr in enumerate(param_rows):
        sep = "," if i < len(param_rows) - 1 else ""
        cont = " \\" if i < len(param_rows) - 1 else " \\"
        lines.append(f"    {pr}{sep}{cont}")
    lines.append(f") \\")

    # Body: _F(name, row, col) calls
    for i, row in enumerate(rows):
        parts = [f"_F({k['param']}, {k['matrix'][0]}, {k['matrix'][1]})" for k in row]
        cont = " \\" if i < len(rows) - 1 else ""
        lines.append(f"    {' '.join(parts)}{cont}")

    lines.append(f"")

    # --- GESTURE_LAYOUT ---
    lines.append(f"/*")
    lines.append(f" * Identity layout macro with arity check.")
    lines.append(f" * Use with DEFINE_DENSE_LAYER for visually arranged keycodes.")
    lines.append(f" */")

    lines.append(f"#define GESTURE_LAYOUT( \\")
    for i, pr in enumerate(param_rows):
        sep = "," if i < len(param_rows) - 1 else ""
        cont = " \\" if i < len(param_rows) - 1 else " \\"
        lines.append(f"    {pr}{sep}{cont}")
    lines.append(f") \\")

    # Body: flat list in order
    for i, pr in enumerate(param_rows):
        sep = "," if i < len(param_rows) - 1 else ""
        cont = " \\" if i < len(param_rows) - 1 else ""
        lines.append(f"    {pr}{sep}{cont}")

    lines.append(f"")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    qmk_dir = sys.argv[1]
    keyboard = sys.argv[2]
    layout_name = sys.argv[3] if len(sys.argv) > 3 else None

    info = load_keyboard_info(qmk_dir, keyboard)
    name, keys = resolve_layout(info, layout_name)
    output = generate_header(keyboard, name, keys)
    print(output, end="")


if __name__ == "__main__":
    main()
