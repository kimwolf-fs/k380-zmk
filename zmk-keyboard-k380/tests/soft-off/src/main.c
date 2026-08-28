#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/soft_off.h>
#include <zmk_keyboard_k380/status_indicator.h>

enum call {
    CALL_WARNING,
    CALL_WAIT_3S,
    CALL_STOP_INDICATOR,
    CALL_SAVE_REASON,
    CALL_CONFIRM_BLE_SETTINGS,
    CALL_CLEAR_HID,
    CALL_DISCONNECT_BLE,
    CALL_SYSTEM_OFF,
};

static enum call calls[16];
static size_t call_count;
static bool charge_during_warning;
static int save_rc;
static int hid_rc;
static int disconnect_rc;

void k380_soft_off_test_record(int call) { calls[call_count++] = (enum call)call; }

int k380_soft_off_test_wait_warning(void) {
    k380_soft_off_test_record(CALL_WAIT_3S);
    if (charge_during_warning) {
        return k380_battery_policy_submit_mv(4600);
    }
    return 0;
}

int k380_soft_off_test_save_reason(const char *name, const char *value, size_t len) {
    zassert_equal(strcmp(name, "k380/last_shutdown_reason"), 0);
    zassert_equal(strcmp(value, "low_voltage_protection"), 0);
    zassert_equal(len, strlen("low_voltage_protection") + 1U);
    k380_soft_off_test_record(CALL_SAVE_REASON);
    return save_rc;
}

int k380_soft_off_test_confirm_ble_settings(void) {
    k380_soft_off_test_record(CALL_CONFIRM_BLE_SETTINGS);
    return 2;
}

int k380_soft_off_test_clear_hid(void) {
    k380_soft_off_test_record(CALL_CLEAR_HID);
    return hid_rc;
}

int k380_soft_off_test_disconnect_ble(int index) {
    zassert_equal(index, 2);
    k380_soft_off_test_record(CALL_DISCONNECT_BLE);
    return disconnect_rc;
}

int k380_soft_off_test_system_off(void) {
    k380_soft_off_test_record(CALL_SYSTEM_OFF);
    return 0;
}

static void reset_fakes(void) {
    call_count = 0;
    charge_during_warning = false;
    save_rc = 0;
    hid_rc = 0;
    disconnect_rc = 0;
    k380_soft_off_clear_last_reason();
}

ZTEST(k380_soft_off, test_low_voltage_soft_off_orders_cleanup_after_warning) {
    reset_fakes();

    zassert_ok(k380_soft_off_request_low_voltage());
    const enum call expected[] = {
        CALL_WARNING, CALL_WAIT_3S, CALL_STOP_INDICATOR, CALL_SAVE_REASON,
        CALL_CONFIRM_BLE_SETTINGS, CALL_CLEAR_HID, CALL_DISCONNECT_BLE, CALL_SYSTEM_OFF,
    };

    zassert_equal(call_count, ARRAY_SIZE(expected));
    zassert_mem_equal(calls, expected, sizeof(expected));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z1_NORMAL);
    zassert_equal(strcmp(k380_soft_off_last_reason(), "low_voltage_protection"), 0);
}

ZTEST(k380_soft_off, test_usb_charging_during_warning_cancels_soft_off) {
    reset_fakes();
    charge_during_warning = true;

    zassert_equal(k380_soft_off_request_low_voltage(), -ECANCELED);
    const enum call expected[] = {CALL_WARNING, CALL_WAIT_3S, CALL_STOP_INDICATOR};

    zassert_equal(call_count, ARRAY_SIZE(expected));
    zassert_mem_equal(calls, expected, sizeof(expected));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z2_CHARGING);
    zassert_equal(k380_soft_off_last_reason(), NULL);
}

ZTEST(k380_soft_off, test_cleanup_failures_do_not_prevent_system_off) {
    reset_fakes();
    save_rc = -EIO;
    hid_rc = -EIO;
    disconnect_rc = -EIO;

    zassert_ok(k380_soft_off_request_low_voltage());
    zassert_equal(calls[call_count - 1U], CALL_SYSTEM_OFF);
}

ZTEST_SUITE(k380_soft_off, NULL, NULL, NULL, NULL, NULL);
