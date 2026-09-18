#include <errno.h>
#include <zephyr/ztest.h>
#include <zmk_keyboard_k380/low_power.h>
#include <zmk_keyboard_k380/soft_off.h>

static int off_calls;
static int warning_calls;
int k380_low_power_system_off(void) { off_calls++; return 0; }
int k380_low_power_start_warning(enum k380_shutdown_reason reason)
{
    ARG_UNUSED(reason);
    warning_calls++;
    return 0;
}

ZTEST(k380_low_power_gate, test_unqualified_ble_timeouts_leave_input_open)
{
    k380_low_power_test_reset();
    off_calls = warning_calls = 0;
    zassert_equal(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT), -ENOTSUP);
    zassert_equal(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT), -ENOTSUP);
    k380_low_power_notify_all_keys_released();
    zassert_equal(off_calls, 0);
    zassert_equal(warning_calls, 0);
    zassert_true(k380_low_power_input_events_allowed());
    zassert_false(k380_low_power_is_release_waiting());
}

ZTEST(k380_low_power_gate, test_low_voltage_protection_remains_enabled)
{
    k380_low_power_test_reset();
    off_calls = warning_calls = 0;
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
    zassert_equal(off_calls, 1);
    zassert_equal(warning_calls, 1);
    zassert_false(k380_low_power_input_events_allowed());
}

ZTEST_SUITE(k380_low_power_gate, NULL, NULL, NULL, NULL, NULL);
