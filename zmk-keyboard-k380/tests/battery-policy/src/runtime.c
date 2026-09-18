#include <errno.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/soft_off.h>

static int sensor_rc;
static int sensor_mv;
static bool latch;
static int delete_rc;
static int deletes;
static int off_calls;
static bool recover_before_request;

static int fetch(const struct device *dev, enum sensor_channel chan) {
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    return sensor_rc;
}
static int get(const struct device *dev, enum sensor_channel chan, struct sensor_value *value) {
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    value->val1 = sensor_mv / 1000;
    value->val2 = (sensor_mv % 1000) * 1000;
    return 0;
}
static const struct sensor_driver_api api = {.sample_fetch = fetch, .channel_get = get};
DEVICE_DT_DEFINE(DT_NODELABEL(test_battery), NULL, NULL, NULL, NULL,
                 POST_KERNEL, 50, &api);
const struct zmk_event_type zmk_event_zmk_usb_conn_state_changed = {.name = "usb"};
bool k380_soft_off_has_low_voltage_latch(void) { return latch; }
int k380_soft_off_clear_low_voltage_latch_if_safe(bool safe) {
    zassert_true(safe);
    if (latch) {
        deletes++;
        if (delete_rc) { return delete_rc; }
        latch = false;
    }
    return 0;
}
int k380_low_power_system_off(void) { off_calls++; return 0; }
void k380_ble_slot_power_state_changed(void) {
    const bool charging = k380_battery_policy_state() == K380_POWER_CHARGING;
    if (charging) { k380_low_power_cancel_usb_pending(); }
    if (recover_before_request && k380_battery_policy_state() == K380_POWER_SOFT_OFF_WARNING_REQUESTED) {
        recover_before_request = false;
        /* Reproduce another thread's recovery after the policy unlock but
         * before the older low-voltage decision reaches the coordinator. */
        for (int i = 0; i < 3; i++) { zassert_ok(k380_battery_policy_submit_mv(3300)); }
    }
}

static void reset(void) {
    sensor_rc = 0;
    sensor_mv = 4000;
    recover_before_request = false;
    delete_rc = 0;
    deletes = 0;
    latch = false;
    for (int i = 0; i < 8; i++) { zassert_ok(k380_battery_policy_submit_mv(4000)); }
    k380_low_power_test_reset();
    latch = true;
    off_calls = 0;
}

ZTEST(k380_battery_runtime, test_invalid_high_voltage_cannot_qualify_or_clear_latch) {
    reset();
    sensor_mv = 6100;
    uint16_t mv = 1234;
    zassert_equal(k380_battery_policy_sample_now_sync(&mv), -EINVAL);
    zassert_equal(mv, 1234);
    zassert_equal(k380_battery_policy_startup_qualify(), -EACCES);
    zassert_true(latch);
    zassert_false(k380_low_power_ble_start_allowed());
    zassert_equal(deletes, 0);
}

ZTEST(k380_battery_runtime, test_valid_runtime_usb_recovers_failed_startup) {
    reset();
    sensor_rc = -EIO;
    zassert_equal(k380_battery_policy_startup_qualify(), -EACCES);
    zassert_false(k380_low_power_ble_start_allowed());
    zassert_ok(k380_battery_policy_submit_mv(4600));
    zassert_true(k380_low_power_ble_start_allowed(), "a later valid USB sample must reopen the gate");
    zassert_false(latch);
    zassert_equal(deletes, 1);
}

ZTEST(k380_battery_runtime, test_runtime_battery_recovery_requires_three_safe_samples) {
    reset();
    sensor_rc = -EIO;
    zassert_equal(k380_battery_policy_startup_qualify(), -EACCES);
    zassert_ok(k380_battery_policy_submit_mv(3300));
    zassert_false(k380_low_power_ble_start_allowed());
    zassert_ok(k380_battery_policy_submit_mv(3200));
    zassert_ok(k380_battery_policy_submit_mv(3300));
    zassert_ok(k380_battery_policy_submit_mv(3300));
    zassert_false(k380_low_power_ble_start_allowed());
    zassert_ok(k380_battery_policy_submit_mv(3300));
    zassert_true(k380_low_power_ble_start_allowed());
    zassert_false(latch);
}

ZTEST(k380_battery_runtime, test_delete_failure_keeps_startup_gate_closed_without_write_storm) {
    reset();
    sensor_rc = -EIO;
    zassert_equal(k380_battery_policy_startup_qualify(), -EACCES);
    delete_rc = -EIO;
    for (int i = 0; i < 10; i++) {
        zassert_ok(k380_battery_policy_submit_mv(4600));
    }
    zassert_false(k380_low_power_ble_start_allowed());
    zassert_true(latch);
    zassert_equal(deletes, 1, "failed deletes must be rate limited");
}

ZTEST(k380_battery_runtime, test_qualified_recovery_cancels_real_coordinator_warning) {
    reset();
    sensor_mv = 3100;
    for (int i = 0; i < 8; i++) { zassert_ok(k380_battery_policy_submit_mv(3100)); }
    zassert_equal(k380_battery_policy_startup_qualify(), -EACCES);
    zassert_false(k380_low_power_input_events_allowed());
    for (int i = 0; i < 3; i++) { zassert_ok(k380_battery_policy_submit_mv(3300)); }
    zassert_false(latch);
    zassert_true(k380_low_power_ble_start_allowed());
    zassert_true(k380_low_power_input_events_allowed());
    k_sleep(K_MSEC(3050));
    zassert_equal(off_calls, 0);
}

ZTEST(k380_battery_runtime, test_recovery_before_stale_request_keeps_coordinator_ready) {
    reset();
    sensor_rc = -EIO;
    zassert_equal(k380_battery_policy_startup_qualify(), -EACCES);
    recover_before_request = true;
    for (int i = 0; i < 12 && recover_before_request; i++) {
        zassert_ok(k380_battery_policy_submit_mv(3100));
    }
    zassert_false(recover_before_request, "fixture must reach the unlocked shutdown decision");
    zassert_false(latch);
    zassert_true(k380_low_power_ble_start_allowed(), "stale low-voltage decision must not undo recovery");
    zassert_true(k380_low_power_input_events_allowed());
    k_sleep(K_MSEC(3050));
    zassert_equal(off_calls, 0);
}
ZTEST_SUITE(k380_battery_runtime, NULL, NULL, NULL, NULL, NULL);
