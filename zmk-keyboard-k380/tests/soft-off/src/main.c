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
static int warning_rc;
static int save_rc;
static int confirm_ble_settings_rc;
static int hid_rc;
static int disconnect_rc;
static size_t save_call_count;

extern char *k380_soft_off_test_last_reason_storage(void);

void k380_soft_off_test_record(int call) { calls[call_count++] = (enum call)call; }

int k380_soft_off_test_start_warning(void) {
    k380_soft_off_test_record(CALL_WARNING);
    if (warning_rc == 0) {
        return k380_status_indicator_set(K380_STATUS_Z4_SOFT_OFF_WARNING);
    }
    return warning_rc;
}

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
    save_call_count++;
    return save_rc;
}

int k380_soft_off_test_confirm_ble_settings(void) {
    k380_soft_off_test_record(CALL_CONFIRM_BLE_SETTINGS);
    return confirm_ble_settings_rc;
}

int k380_soft_off_test_active_ble_slot(void) { return 2; }

void k380_soft_off_test_restore_reason(const char *reason) {
    strcpy(k380_soft_off_test_last_reason_storage(), reason);
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
    warning_rc = 0;
    save_rc = 0;
    confirm_ble_settings_rc = 0;
    hid_rc = 0;
    disconnect_rc = 0;
    save_call_count = 0;
    k380_soft_off_clear_last_reason();
}

ZTEST(k380_soft_off, test_low_voltage_soft_off_orders_cleanup_after_warning) {
    reset_fakes();

    zassert_ok(k380_soft_off_request_low_voltage());
    const enum call expected[] = {
        CALL_WARNING,
        CALL_WAIT_3S,
        CALL_STOP_INDICATOR,
        CALL_SAVE_REASON,
        CALL_CONFIRM_BLE_SETTINGS,
        CALL_CLEAR_HID,
        CALL_DISCONNECT_BLE,
        CALL_SYSTEM_OFF,
    };

    zassert_equal(call_count, ARRAY_SIZE(expected));
    zassert_mem_equal(calls, expected, sizeof(expected));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z1_NORMAL);
    zassert_equal(strcmp(k380_soft_off_last_reason(), "low_voltage_protection"), 0);
    zassert_equal(save_call_count, 1U);
    zassert_equal(K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS, 1000U);
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

ZTEST(k380_soft_off, test_ble_settings_failure_still_disconnects_active_slot) {
    reset_fakes();
    confirm_ble_settings_rc = -EIO;

    zassert_ok(k380_soft_off_request_low_voltage());
    const enum call expected[] = {
        CALL_WARNING,
        CALL_WAIT_3S,
        CALL_STOP_INDICATOR,
        CALL_SAVE_REASON,
        CALL_CONFIRM_BLE_SETTINGS,
        CALL_CLEAR_HID,
        CALL_DISCONNECT_BLE,
        CALL_SYSTEM_OFF,
    };

    zassert_equal(call_count, ARRAY_SIZE(expected));
    zassert_mem_equal(calls, expected, sizeof(expected));
}

ZTEST(k380_soft_off, test_successful_boot_consumes_loaded_last_reason) {
    reset_fakes();
    k380_soft_off_test_restore_reason("low_voltage_protection");

    zassert_equal(strcmp(k380_soft_off_last_reason(), "low_voltage_protection"), 0);
    zassert_equal(strcmp(k380_soft_off_last_reason(), "low_voltage_protection"), 0);
    k380_soft_off_handle_successful_boot();
    zassert_is_null(k380_soft_off_last_reason());
}

ZTEST(k380_soft_off, test_warning_start_failure_cancels_soft_off) {
    reset_fakes();
    warning_rc = -EIO;

    zassert_equal(k380_soft_off_request_low_voltage(), -EIO);
    zassert_equal(call_count, 1U);
    zassert_equal(calls[0], CALL_WARNING);
    zassert_is_null(k380_soft_off_last_reason());
}

ZTEST_SUITE(k380_soft_off, NULL, NULL, NULL, NULL, NULL);
