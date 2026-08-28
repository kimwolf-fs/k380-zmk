#include <errno.h>

#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/status_indicator.h>

static void submit_samples(uint16_t mv, int count) {
    for (int i = 0; i < count; i++) {
        zassert_ok(k380_battery_policy_submit_mv(mv));
    }
}

ZTEST(k380_battery_policy, test_vddh_average_debounce_and_charging_override) {
    /* A VDDH reading above 4.5 V is USB power and immediately wins. */
    zassert_ok(k380_battery_policy_submit_mv(4600));
    zassert_equal(k380_battery_policy_state(), K380_POWER_CHARGING);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z2_CHARGING);

    /* Flush the USB reading, then require three low-average decisions. */
    submit_samples(3390, 4);
    submit_samples(3390, 2);
    zassert_equal(k380_battery_policy_state(), K380_POWER_NORMAL);
    zassert_ok(k380_battery_policy_submit_mv(3390));
    zassert_equal(k380_battery_policy_state(), K380_POWER_LOW_BATTERY);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z3_LOW_BATTERY);

    /* Low battery recovery also needs three high-average decisions. */
    submit_samples(3500, 4);
    submit_samples(3500, 2);
    zassert_equal(k380_battery_policy_state(), K380_POWER_LOW_BATTERY);
    zassert_ok(k380_battery_policy_submit_mv(3500));
    zassert_equal(k380_battery_policy_state(), K380_POWER_NORMAL);

    /* Critical voltage requests only the Task 4 warning, never soft-off itself. */
    submit_samples(3190, 4);
    zassert_equal(k380_battery_policy_state(), K380_POWER_LOW_BATTERY);
    submit_samples(3190, 2);
    zassert_equal(k380_battery_policy_state(), K380_POWER_LOW_BATTERY);
    zassert_ok(k380_battery_policy_submit_mv(3190));
    zassert_equal(k380_battery_policy_state(), K380_POWER_SOFT_OFF_WARNING_REQUESTED);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z4_SOFT_OFF_WARNING);

    submit_samples(3300, 4);
    submit_samples(3300, 2);
    zassert_equal(k380_battery_policy_state(), K380_POWER_SOFT_OFF_WARNING_REQUESTED);
    zassert_ok(k380_battery_policy_submit_mv(3300));
    zassert_equal(k380_battery_policy_state(), K380_POWER_NORMAL);

    submit_samples(3190, 4);
    submit_samples(3190, 3);
    zassert_equal(k380_battery_policy_state(), K380_POWER_SOFT_OFF_WARNING_REQUESTED);
    zassert_ok(k380_battery_policy_submit_mv(4600));
    zassert_equal(k380_battery_policy_state(), K380_POWER_CHARGING);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z2_CHARGING);

    /* Invalid values must leave the window, debounce counters, and state untouched. */
    zassert_equal(k380_battery_policy_submit_mv(0), -EINVAL);
    zassert_equal(k380_battery_policy_state(), K380_POWER_CHARGING);
}

ZTEST_SUITE(k380_battery_policy, NULL, NULL, NULL, NULL, NULL);
