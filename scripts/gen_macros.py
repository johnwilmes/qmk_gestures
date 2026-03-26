#!/usr/bin/env python3
"""Generate gesture module macro helpers.

Outputs a single header with optional sections:

  --layout <info.json> [layout_name]
      Generate GESTURE_LAYOUT_CALL and NUM_KEY_POSITIONS from a QMK
      keyboard's info.json (layout definition).

  --gestures [N]
      Generate _GS_MAP preprocessor foreach macro supporting up to N
      gesture names (default 256). Used by DEFINE_GESTURES to auto-generate
      the gesture ID enum and pointer array from a list of names.

At least one of --layout or --gestures must be specified.

Usage:
    python3 gen_macros.py --layout keyboards/foo/keyboard.json
    python3 gen_macros.py --gestures
    python3 gen_macros.py --layout keyboards/foo/keyboard.json --gestures 512

Example:
    python3 gen_macros.py --layout ~/qmk_firmware/keyboards/splitkb/halcyon/kyria/keyboard.json --gestures
"""

import argparse
import json
import sys
from pathlib import Path


# =============================================================================
# Layout generation (from keyboard info.json)
# =============================================================================

def load_keyboard_info(json_path):
    """Load and return the keyboard's info.json data."""
    path = Path(json_path)
    if not path.exists():
        sys.exit(f"Error: file not found: {path}")
    with open(path) as f:
        return json.load(f)


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


def generate_layout(source, layout_name, keys):
    """Generate GESTURE_LAYOUT_CALL and NUM_KEY_POSITIONS."""
    num_keys = len(keys)
    rows = group_into_visual_rows(keys)

    for key in keys:
        key["param"] = sanitize_label(key.get("label", f"k{keys.index(key)}"))

    lines = []
    lines.append(f"/* Layout: {layout_name} ({num_keys} keys)")
    lines.append(f" *")
    lines.append(f" * Key positions (label: matrix [row, col]):")
    for row in rows:
        parts = [f"{k['param']}:[{k['matrix'][0]},{k['matrix'][1]}]" for k in row]
        lines.append(f" *   {' '.join(parts)}")
    lines.append(f" */")
    lines.append(f"")
    lines.append(f"#define NUM_KEY_POSITIONS {num_keys}")
    lines.append(f"")

    # --- GESTURE_LAYOUT_CALL ---
    lines.append(f"/*")
    lines.append(f" * Higher-order layout macro. Calls _F(name, row, col) for each key.")
    lines.append(f" * Use with DEFINE_KEY_INDICES to generate key index definitions.")
    lines.append(f" */")

    param_rows = []
    for row in rows:
        params = ", ".join(k["param"] for k in row)
        param_rows.append(params)

    lines.append(f"#define GESTURE_LAYOUT_CALL( \\")
    lines.append(f"    _F, \\")
    for i, pr in enumerate(param_rows):
        sep = "," if i < len(param_rows) - 1 else ""
        cont = " \\" if i < len(param_rows) - 1 else " \\"
        lines.append(f"    {pr}{sep}{cont}")
    lines.append(f") \\")

    for i, row in enumerate(rows):
        parts = [f"_F({k['param']}, {k['matrix'][0]}, {k['matrix'][1]})" for k in row]
        cont = " \\" if i < len(rows) - 1 else ""
        lines.append(f"    {' '.join(parts)}{cont}")

    lines.append(f"")
    return "\n".join(lines)


# =============================================================================
# Gesture MAP macro generation
# =============================================================================

def generate_gesture_map(max_args):
    """Generate _GS_MAP preprocessor foreach supporting up to max_args."""
    lines = []
    lines.append(f"/*")
    lines.append(f" * Preprocessor MAP macro for DEFINE_GESTURES.")
    lines.append(f" * Applies _F(name) to each of up to {max_args} arguments.")
    lines.append(f" *")
    lines.append(f" * Usage (internal — called by DEFINE_GESTURES):")
    lines.append(f" *   _GS_MAP(_GS_ENUM, a, b, c)  =>  GS_a, GS_b, GS_c,")
    lines.append(f" */")
    lines.append(f"")

    # Token pasting helpers
    lines.append(f"#define _GS_CAT(a, b) _GS_CAT_(a, b)")
    lines.append(f"#define _GS_CAT_(a, b) a##b")
    lines.append(f"")

    # NARGS: count variadic arguments
    # _GS_NARGS(a, b, c) => 3
    nums_desc = ", ".join(str(i) for i in range(max_args, 0, -1))
    params = ", ".join(f"_{i}" for i in range(1, max_args + 1))
    va = "__VA_ARGS__"
    lines.append(f"#define _GS_NARGS(...) _GS_NARGS_({va}, {nums_desc})")
    lines.append(f"#define _GS_NARGS_({params}, N, ...) N")
    lines.append(f"")

    # MAP dispatch
    lines.append(f"#define _GS_MAP(f, ...) _GS_CAT(_GS_MAP_, _GS_NARGS({va}))(f, {va})")
    lines.append(f"")

    # MAP chain: _GS_MAP_1 through _GS_MAP_N
    lines.append(f"#define _GS_MAP_1(f, a) f(a)")
    for i in range(2, max_args + 1):
        lines.append(f"#define _GS_MAP_{i}(f, a, ...) f(a) _GS_MAP_{i-1}(f, __VA_ARGS__)")

    lines.append(f"")
    return "\n".join(lines)


# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Generate gesture module macro helpers.",
        epilog="At least one of --layout or --gestures must be specified.",
    )
    parser.add_argument(
        "--layout", nargs="+", metavar=("INFO_JSON", "LAYOUT_NAME"),
        help="Generate layout macros from keyboard info.json (optional layout name)",
    )
    parser.add_argument(
        "--gestures", nargs="?", const=256, type=int, metavar="N",
        help="Generate gesture MAP macro supporting up to N names (default 256)",
    )

    args = parser.parse_args()

    if args.layout is None and args.gestures is None:
        parser.error("at least one of --layout or --gestures must be specified")

    sections = []

    # Header
    sources = []
    if args.layout:
        sources.append(Path(args.layout[0]).name)
    if args.gestures is not None:
        sources.append(f"gesture MAP (max {args.gestures})")
    source_desc = ", ".join(sources)

    sections.append(f"/* gesture_macros.h -- generated by gen_macros.py")
    sections.append(f" * Sources: {source_desc}")
    sections.append(f" */")
    sections.append(f"")
    sections.append(f"#pragma once")
    sections.append(f"")

    # Layout section
    if args.layout:
        json_path = args.layout[0]
        layout_name = args.layout[1] if len(args.layout) > 1 else None
        info = load_keyboard_info(json_path)
        name, keys = resolve_layout(info, layout_name)
        sections.append(generate_layout(json_path, name, keys))

    # Gesture MAP section
    if args.gestures is not None:
        sections.append(generate_gesture_map(args.gestures))

    print("\n".join(sections), end="")


if __name__ == "__main__":
    main()
