/* Provide a weak default keymaps array.
 *
 * The gesture module bypasses QMK's layer lookup, but keymap_introspection.c
 * uses sizeof(keymaps) to compute the raw layer count. This weak definition
 * satisfies that requirement so users don't need to define keymaps themselves.
 * A user-supplied keymaps definition (strong symbol) will override this.
 */
#include <stdint.h>
#include "progmem.h"

__attribute__((weak)) const uint16_t PROGMEM
    keymaps[][MATRIX_ROWS][MATRIX_COLS] = {[0] = {{0}}};
