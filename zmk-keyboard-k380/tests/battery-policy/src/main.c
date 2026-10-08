#include <errno.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>

#include <battery_common.h>
#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/status_indicator.h>

extern bool k380_battery_policy_test_startup_recovery_sample(uint16_t mv,
                                                              uint8_t *recovery_hits);

static void submit_samples(uint16_t mv, int count) {
    for (int i = 0; i < count; i++) {
        zassert_ok(k380_battery_policy_submit_mv(mv));
    }
}

ZTEST(k380_battery_policy, test_battery_voltage_channel_alias_matches_gauge_voltage) {
    const struct battery_value value = {.millivolts = 4123, .state_of_charge = 87};
    struct sensor_value voltage = {0};

    zassert_equal(battery_channel_alias(SENSOR_CHAN_VOLTAGE), SENSOR_CHAN_GAUGE_VOLTAGE);
    zassert_true(battery_channel_is_supported(SENSOR_CHAN_VOLTAGE));
    zassert_true(battery_channel_is_supported(SENSOR_CHAN_GAUGE_VOLTAGE));
    zassert_false(battery_channel_is_supported(SENSOR_CHAN_ACCEL_X));

    zassert_ok(battery_channel_get(&value, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage));
    zassert_equal(voltage.val1, 4);
    zassert_equal(voltage.val2, 123000);

    voltage = (struct sensor_value){0};
    zassert_ok(battery_channel_get(&value, SENSOR_CHAN_VOLTAGE, &voltage));
    zassert_equal(voltage.val1, 4);
    zassert_equal(voltage.val2, 123000);
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

    /* An invalid sample must not displace a valid 3.5 V window entry. */
    submit_samples(3390, 2);
    zassert_equal(k380_battery_policy_submit_mv(0), -EINVAL);
    submit_samples(3390, 3);
    zassert_equal(k380_battery_policy_state(), K380_POWER_NORMAL);

    /* Critical voltage requests only the Task 4 warning, never soft-off itself. */
    submit_samples(3190, 4);
    zassert_equal(k380_battery_policy_state(), K380_POWER_LOW_BATTERY);
    submit_samples(3190, 2);
    zassert_equal(k380_battery_policy_state(), K380_POWER_LOW_BATTERY);
    zassert_equal(k380_battery_policy_submit_mv(0), -EINVAL);
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
}

ZTEST(k380_battery_policy, test_charging_sample_is_startup_safe_and_not_battery_powered) {
    zassert_ok(k380_battery_policy_submit_mv(4600));
    zassert_false(k380_battery_policy_is_battery_powered());
    zassert_true(k380_battery_policy_voltage_safe_for_startup());
}

ZTEST(k380_battery_policy, test_startup_recovery_requires_three_raw_safe_samples) {
    uint8_t hits = 0U;

    zassert_false(k380_battery_policy_test_startup_recovery_sample(3500, &hits));
    zassert_false(k380_battery_policy_test_startup_recovery_sample(3200, &hits));
    zassert_false(k380_battery_policy_test_startup_recovery_sample(3300, &hits));
    zassert_false(k380_battery_policy_test_startup_recovery_sample(3300, &hits));
    zassert_true(k380_battery_policy_test_startup_recovery_sample(3300, &hits));
    zassert_true(k380_battery_policy_test_startup_recovery_sample(3300, &hits));
}

ZTEST_SUITE(k380_battery_policy, NULL, NULL, NULL, NULL, NULL);
