#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/ghost_filter.h>

static void assert_rows_equal(const uint16_t actual[K380_GHOST_FILTER_ROWS],
                              const uint16_t expected[K380_GHOST_FILTER_ROWS]) {
    for (size_t row = 0; row < K380_GHOST_FILTER_ROWS; row++) {
        zassert_equal(actual[row], expected[row], "row %u", (unsigned int)row);
    }
}

ZTEST(k380_ghost_filter, test_non_rectangular_presses_are_unchanged) {
    const uint16_t raw[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(3), BIT(1), 0, BIT(7), 0, 0, 0, 0,
    };
    const uint16_t accepted[K380_GHOST_FILTER_ROWS] = {0};
    const uint16_t expected[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(3), BIT(1), 0, BIT(7), 0, 0, 0, 0,
    };
    uint16_t filtered[K380_GHOST_FILTER_ROWS];
    uint16_t ambiguous[K380_GHOST_FILTER_ROWS];

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    assert_rows_equal(filtered, expected);
    zassert_equal(ambiguous[0], 0, "unexpected ambiguity");
    zassert_equal(ambiguous[1], 0, "unexpected ambiguity");
    zassert_equal(ambiguous[3], 0, "unexpected ambiguity");
}

ZTEST(k380_ghost_filter, test_new_rectangle_corners_are_withheld) {
    const uint16_t raw[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(1), BIT(0) | BIT(1), 0, 0, 0, 0, 0, 0,
    };
    const uint16_t accepted[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(1), 0, 0, 0, 0, 0, 0, 0,
    };
    const uint16_t expected[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(1), 0, 0, 0, 0, 0, 0, 0,
    };
    uint16_t filtered[K380_GHOST_FILTER_ROWS];
    uint16_t ambiguous[K380_GHOST_FILTER_ROWS];

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    assert_rows_equal(filtered, expected);
    zassert_equal(ambiguous[0], BIT(0) | BIT(1), "row 0 ambiguity");
    zassert_equal(ambiguous[1], BIT(0) | BIT(1), "row 1 ambiguity");
}

ZTEST(k380_ghost_filter, test_release_is_not_blocked_by_ambiguity) {
    const uint16_t raw[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(1), BIT(0) | BIT(1), 0, 0, 0, 0, 0, 0,
    };
    const uint16_t accepted[K380_GHOST_FILTER_ROWS] = {
        BIT(2), 0, 0, 0, 0, 0, 0, 0,
    };
    const uint16_t expected[K380_GHOST_FILTER_ROWS] = {0};
    uint16_t filtered[K380_GHOST_FILTER_ROWS];
    uint16_t ambiguous[K380_GHOST_FILTER_ROWS];

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    assert_rows_equal(filtered, expected);
    zassert_equal(ambiguous[0], BIT(0) | BIT(1), "row 0 ambiguity");
    zassert_equal(ambiguous[1], BIT(0) | BIT(1), "row 1 ambiguity");
}

ZTEST(k380_ghost_filter, test_bits_outside_the_15_column_matrix_are_ignored) {
    const uint16_t raw[K380_GHOST_FILTER_ROWS] = {
        BIT(15), 0, 0, 0, 0, 0, 0, 0,
    };
    const uint16_t accepted[K380_GHOST_FILTER_ROWS] = {0};
    uint16_t filtered[K380_GHOST_FILTER_ROWS];
    uint16_t ambiguous[K380_GHOST_FILTER_ROWS];

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    zassert_equal(filtered[0], 0, "bit 15 must not enter the matrix");
    zassert_equal(ambiguous[0], 0, "bit 15 must not create ambiguity");
}

ZTEST_SUITE(k380_ghost_filter, NULL, NULL, NULL, NULL, NULL);
