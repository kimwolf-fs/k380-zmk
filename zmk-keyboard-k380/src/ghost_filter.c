#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zmk_keyboard_k380/ghost_filter.h>

static bool has_at_least_two_bits(uint16_t value) { return (value & (uint16_t)(value - 1U)) != 0U; }

void k380_ghost_filter_apply(const uint16_t raw[K380_GHOST_FILTER_ROWS],
                             const uint16_t accepted[K380_GHOST_FILTER_ROWS],
                             uint16_t filtered[K380_GHOST_FILTER_ROWS],
                             uint16_t ambiguous[K380_GHOST_FILTER_ROWS]) {
    memset(ambiguous, 0, sizeof(uint16_t) * K380_GHOST_FILTER_ROWS);

    for (size_t first_row = 0; first_row < K380_GHOST_FILTER_ROWS; first_row++) {
        const uint16_t first_active = raw[first_row] & K380_GHOST_FILTER_COL_MASK;

        for (size_t second_row = first_row + 1U; second_row < K380_GHOST_FILTER_ROWS;
             second_row++) {
            const uint16_t common_active =
                first_active & raw[second_row] & K380_GHOST_FILTER_COL_MASK;

            if (has_at_least_two_bits(common_active)) {
                ambiguous[first_row] |= common_active;
                ambiguous[second_row] |= common_active;
            }
        }
    }

    for (size_t row = 0; row < K380_GHOST_FILTER_ROWS; row++) {
        const uint16_t active = raw[row] & K380_GHOST_FILTER_COL_MASK;
        const uint16_t held = accepted[row] & active;
        const uint16_t new_unambiguous =
            active & (uint16_t)~accepted[row] & (uint16_t)~ambiguous[row];

        filtered[row] = (held | new_unambiguous) & K380_GHOST_FILTER_COL_MASK;
    }
}
