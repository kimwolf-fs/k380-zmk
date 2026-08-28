#ifndef ZMK_KEYBOARD_K380_GHOST_FILTER_H_
#define ZMK_KEYBOARD_K380_GHOST_FILTER_H_

#include <stdint.h>

#define K380_GHOST_FILTER_ROWS 8U
#define K380_GHOST_FILTER_COLS 15U
#define K380_GHOST_FILTER_VALID_KEYS 80U
#define K380_GHOST_FILTER_COL_MASK ((uint16_t)((1U << K380_GHOST_FILTER_COLS) - 1U))

void k380_ghost_filter_apply(const uint16_t raw[K380_GHOST_FILTER_ROWS],
                             const uint16_t accepted[K380_GHOST_FILTER_ROWS],
                             uint16_t filtered[K380_GHOST_FILTER_ROWS],
                             uint16_t ambiguous[K380_GHOST_FILTER_ROWS]);

#endif
