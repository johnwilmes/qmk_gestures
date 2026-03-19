#!/bin/bash
# Wire gesture module tests into a qmk_firmware checkout.
#
# Copies test files into qmk_firmware/tests/ and symlinks the module source.
# QMK's test discovery uses `find -type f` which doesn't follow symlinks,
# so test files must be real files (or hard links). We use copies for
# portability and re-run this script to update after changes.
#
# The module source directory IS symlinked (SRC paths in test.mk resolve
# through make's vpath, which follows symlinks fine).
#
# Usage: ./setup_tests.sh [path/to/qmk_firmware]
#   Default: ~/qmk_firmware

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QMK_DIR="${1:-$HOME/qmk_firmware}"

if [ ! -f "$QMK_DIR/Makefile" ]; then
    echo "Error: $QMK_DIR does not look like a qmk_firmware checkout" >&2
    exit 1
fi

# 1. Symlink module source into qmk_firmware/modules/johnwilmes/gestures
MODULE_DEST="$QMK_DIR/modules/johnwilmes/gestures"
mkdir -p "$(dirname "$MODULE_DEST")"
if [ -L "$MODULE_DEST" ]; then
    rm "$MODULE_DEST"
elif [ -e "$MODULE_DEST" ]; then
    echo "Error: $MODULE_DEST already exists and is not a symlink" >&2
    exit 1
fi
ln -s "$SCRIPT_DIR/gestures" "$MODULE_DEST"
echo "Linked module: $MODULE_DEST -> $SCRIPT_DIR/gestures"

# 2. Copy test files into qmk_firmware/tests/
for test_dir in "$SCRIPT_DIR"/tests/*/; do
    test_name="$(basename "$test_dir")"
    test_dest="$QMK_DIR/tests/$test_name"

    # Clean up previous setup (remove symlink or directory)
    if [ -L "$test_dest" ]; then
        rm "$test_dest"
    fi
    rm -rf "$test_dest"

    cp -r "$test_dir" "$test_dest"
    echo "Copied test:   $test_dest/ ($(ls "$test_dest" | wc -w) files)"
done

echo ""
echo "Done. Run tests from $QMK_DIR with:"
echo "  make test:gestures_basic"
