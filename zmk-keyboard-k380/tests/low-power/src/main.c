#include <zephyr/ztest.h>

#include <errno.h>

#include <zmk_keyboard_k380/low_power.h>

static int warning_calls;
static int radio_stop_calls;
static int led_stop_calls;
static int flush_calls;
static int hid_clear_calls;
static int disconnect_calls;
static int system_off_calls;

int k380_low_power_start_warning(enum k380_shutdown_reason reason)
{
	ARG_UNUSED(reason);
	warning_calls++;
	return 0;
}
int k380_low_power_stop_radio(void) { return ++radio_stop_calls, 0; }
int k380_low_power_stop_led(void) { return ++led_stop_calls, 0; }
int k380_low_power_flush_dirty_profile(void) { return ++flush_calls, 0; }
int k380_low_power_clear_hid(void) { return ++hid_clear_calls, 0; }
int k380_low_power_disconnect_ble(void) { return ++disconnect_calls, 0; }
int k380_low_power_system_off(void) { return ++system_off_calls, 0; }

static void reset(void)
{
	warning_calls = 0;
	radio_stop_calls = 0;
	led_stop_calls = 0;
	flush_calls = 0;
	hid_clear_calls = 0;
	disconnect_calls = 0;
	system_off_calls = 0;
	k380_low_power_test_reset();
}

ZTEST(k380_low_power, test_ble_wait_timeout_is_ram_only_and_requests_system_off)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_equal(k380_low_power_last_reason(), K380_SHUTDOWN_BLE_WAIT_TIMEOUT);
	zassert_equal(system_off_calls, 1);
	zassert_equal(flush_calls, 1, "only an explicit dirty-profile flush is requested");
}

ZTEST(k380_low_power, test_pairing_timeout_is_ram_only_and_requests_system_off)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT));
	zassert_equal(k380_low_power_last_reason(), K380_SHUTDOWN_PAIRING_TIMEOUT);
	zassert_equal(system_off_calls, 1);
}

ZTEST(k380_low_power, test_low_voltage_reason_sets_latch_path)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
	zassert_equal(k380_low_power_last_reason(), K380_SHUTDOWN_LOW_VOLTAGE);
	zassert_equal(warning_calls, 1);
	zassert_equal(system_off_calls, 1);
}

ZTEST(k380_low_power, test_timeout_is_cancelled_when_usb_or_connection_changes)
{
	reset();
	k380_low_power_test_set_all_keys_released(false);
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_true(k380_low_power_is_release_waiting());
	zassert_equal(k380_low_power_startup_voltage_result(true, true, true), -EACCES);
	zassert_false(k380_low_power_is_release_waiting());
	k380_low_power_cancel_pending();
	k380_low_power_test_set_all_keys_released(true);
	k380_low_power_notify_all_keys_released();
	zassert_equal(system_off_calls, 0);
}

ZTEST(k380_low_power, test_dirty_profile_flushes_once_before_system_off)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_equal(flush_calls, 1);
	zassert_equal(system_off_calls, 1);
}

ZTEST(k380_low_power, test_held_key_enters_release_wait_without_system_off)
{
	reset();
	k380_low_power_test_set_all_keys_released(false);
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT));
	zassert_true(k380_low_power_is_release_waiting());
	zassert_equal(system_off_calls, 0);
	k380_low_power_test_set_all_keys_released(true);
	k380_low_power_notify_all_keys_released();
	zassert_equal(system_off_calls, 1);
}

ZTEST_SUITE(k380_low_power, NULL, NULL, NULL, NULL, NULL);
