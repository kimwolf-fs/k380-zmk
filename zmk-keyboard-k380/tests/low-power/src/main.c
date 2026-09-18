#include <zephyr/ztest.h>

#include <errno.h>
#include <stdint.h>

#include <zmk_keyboard_k380/low_power.h>
#include <zmk_keyboard_k380/soft_off.h>

static int warning_calls;
static int radio_stop_calls;
static int led_stop_calls;
static int restore_calls;
static int latch_calls;
static int flush_calls;
static int hid_clear_calls;
static int disconnect_calls;
static int system_off_calls;
static uint32_t latch_budget;
static uint32_t flush_budget;
static int sequence;
static int flush_sequence;
static int system_off_sequence;
static int calls[16];
static int call_count;
static int cleanup_rc;
enum cleanup_call { RADIO, LED, LATCH, FLUSH, HID, DISCONNECT, OFF };

int k380_low_power_start_warning(enum k380_shutdown_reason reason)
{
	ARG_UNUSED(reason);
	zassert_false(k380_low_power_input_events_allowed(),
		      "input must close before shutdown warning and cleanup");
	warning_calls++;
	return 0;
}
int k380_low_power_stop_radio(void) { calls[call_count++] = RADIO; radio_stop_calls++; return cleanup_rc; }
int k380_low_power_stop_led(void) { calls[call_count++] = LED; led_stop_calls++; return cleanup_rc; }
int k380_low_power_restore_radio_and_led(void) { restore_calls++; return 0; }
int k380_low_power_latch_low_voltage(uint32_t save_budget_ms)
{
	latch_calls++;
	calls[call_count++] = LATCH;
	latch_budget = save_budget_ms;
	return cleanup_rc;
}
int k380_low_power_flush_dirty_profile(uint32_t save_budget_ms)
{
	flush_calls++;
	calls[call_count++] = FLUSH;
	flush_budget = save_budget_ms;
	flush_sequence = ++sequence;
	return cleanup_rc;
}
int k380_low_power_clear_hid(void) { calls[call_count++] = HID; return ++hid_clear_calls, cleanup_rc; }
int k380_low_power_disconnect_ble(void) { calls[call_count++] = DISCONNECT; return ++disconnect_calls, cleanup_rc; }
int k380_low_power_system_off(void) { calls[call_count++] = OFF; system_off_calls++; system_off_sequence = ++sequence; return 0; }

static void reset(void)
{
	warning_calls = 0;
	radio_stop_calls = 0;
	led_stop_calls = 0;
	restore_calls = 0;
	latch_calls = 0;
	flush_calls = 0;
	hid_clear_calls = 0;
	disconnect_calls = 0;
	system_off_calls = 0;
	latch_budget = 0;
	flush_budget = 0;
	sequence = 0;
	flush_sequence = 0;
	system_off_sequence = 0;
	call_count = 0;
	cleanup_rc = 0;
	k380_low_power_test_reset();
}

ZTEST(k380_low_power, test_ble_start_is_closed_until_safe_startup_result)
{
	reset();
	zassert_false(k380_low_power_ble_start_allowed());
	zassert_equal(k380_low_power_startup_voltage_result(false, false, true), -EACCES);
	zassert_false(k380_low_power_ble_start_allowed());
	zassert_equal(k380_low_power_startup_voltage_result(true, false, false), -EACCES);
	zassert_false(k380_low_power_ble_start_allowed());
	zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
	zassert_true(k380_low_power_ble_start_allowed());
}

ZTEST(k380_low_power, test_ble_wait_timeout_is_ram_only_and_requests_system_off)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_equal(k380_low_power_last_reason(), K380_SHUTDOWN_BLE_WAIT_TIMEOUT);
	zassert_equal(system_off_calls, 1);
	zassert_equal(flush_calls, 1, "only an explicit dirty-profile flush is requested");
	zassert_equal(latch_calls, 0, "BLE timeout must not persist a low-voltage latch");
}

ZTEST(k380_low_power, test_pairing_timeout_is_ram_only_and_requests_system_off)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT));
	zassert_equal(k380_low_power_last_reason(), K380_SHUTDOWN_PAIRING_TIMEOUT);
	zassert_equal(system_off_calls, 1);
	zassert_equal(latch_calls, 0, "pairing timeout must not persist a low-voltage latch");
}

ZTEST(k380_low_power, test_low_voltage_reason_sets_latch_path)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
	k_sleep(K_MSEC(3050));
	zassert_equal(k380_low_power_last_reason(), K380_SHUTDOWN_LOW_VOLTAGE);
	zassert_equal(warning_calls, 1);
	zassert_equal(latch_calls, 1);
	zassert_equal(latch_budget, K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS);
	zassert_equal(system_off_calls, 1);
}

ZTEST(k380_low_power, test_timeout_is_cancelled_when_usb_or_connection_changes)
{
	reset();
	zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
	k380_low_power_test_set_all_keys_released(false);
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_true(k380_low_power_is_release_waiting());
	k380_low_power_cancel_pending();
	zassert_false(k380_low_power_is_release_waiting());
	zassert_true(k380_low_power_input_events_allowed());
	zassert_equal(restore_calls, 1, "cancellation must restore quieted radio/LED once");
	k380_low_power_cancel_pending();
	zassert_equal(restore_calls, 1, "repeated cancellation must not restore twice");
	k380_low_power_test_set_all_keys_released(true);
	k380_low_power_notify_all_keys_released();
	zassert_equal(system_off_calls, 0);
}

ZTEST(k380_low_power, test_unsafe_voltage_cancellation_never_restores_radio)
{
	reset();
	zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
	k380_low_power_test_set_all_keys_released(false);
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_equal(k380_low_power_startup_voltage_result(true, false, false), -EACCES);
	k380_low_power_cancel_pending();
	zassert_equal(restore_calls, 0, "unsafe voltage must keep radio/LED stopped");
}

ZTEST(k380_low_power, test_usb_insertion_cancels_low_voltage_shutdown)
{
	reset();
	zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
	k380_low_power_test_set_all_keys_released(false);
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
	k_sleep(K_MSEC(3050));
	zassert_true(k380_low_power_is_release_waiting());
	zassert_equal(k380_low_power_startup_voltage_result(true, true, true), -EACCES);
	zassert_false(k380_low_power_is_release_waiting());
	zassert_equal(restore_calls, 1);
}

ZTEST(k380_low_power, test_dirty_profile_flushes_once_before_system_off)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
	zassert_equal(flush_calls, 1);
	zassert_equal(flush_budget, K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS);
	zassert_true(flush_sequence < system_off_sequence,
		     "bounded explicit flush must finish before system-off");
	zassert_equal(system_off_calls, 1);
}

ZTEST(k380_low_power, test_held_key_enters_release_wait_without_system_off)
{
	reset();
	k380_low_power_test_set_all_keys_released(false);
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT));
	zassert_true(k380_low_power_is_release_waiting());
	zassert_equal(system_off_calls, 0);
	zassert_equal(radio_stop_calls, 1, "radio must be quiet while awaiting release");
	zassert_equal(led_stop_calls, 1, "LED must be quiet while awaiting release");
	k380_low_power_test_set_all_keys_released(true);
	k380_low_power_notify_all_keys_released();
	zassert_equal(system_off_calls, 1);
}

ZTEST_SUITE(k380_low_power, NULL, NULL, NULL, NULL, NULL);

ZTEST(k380_low_power, test_low_voltage_warning_defers_cleanup_for_three_seconds)
{
	reset();
	const int64_t started = k_uptime_get();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
	zassert_true(k_uptime_get() - started < 100, "request must not block the work queue");
	zassert_equal(system_off_calls, 0, "warning must precede system off");
	zassert_equal(led_stop_calls, 0, "Z4 must stay visible during the warning");
	k_sleep(K_MSEC(2900));
	zassert_equal(system_off_calls, 0);
	k_sleep(K_MSEC(150));
	zassert_equal(system_off_calls, 1);
	zassert_equal(latch_calls, 1);
}

ZTEST(k380_low_power, test_usb_cancels_warning_before_any_latch_write)
{
	reset();
	zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
	k_sleep(K_MSEC(100));
	k380_low_power_cancel_usb_pending();
	zassert_true(k380_low_power_input_events_allowed());
	k_sleep(K_MSEC(3050));
	zassert_equal(system_off_calls, 0);
	zassert_equal(latch_calls, 0);
}

ZTEST(k380_low_power, test_safe_qualification_cancels_existing_low_voltage_warning)
{
	reset();
	zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
	zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
	zassert_true(k380_low_power_input_events_allowed(), "recovered startup must cancel its warning");
	zassert_true(k380_low_power_ble_start_allowed());
	k_sleep(K_MSEC(3050));
	zassert_equal(system_off_calls, 0);
	zassert_equal(latch_calls, 0);
}

ZTEST(k380_low_power, test_all_causes_cleanup_before_release_wait_even_on_errors)
{
	for (int reason = K380_SHUTDOWN_LOW_VOLTAGE; reason <= K380_SHUTDOWN_PAIRING_TIMEOUT; reason++) {
		reset();
		cleanup_rc = -EIO;
		k380_low_power_test_set_all_keys_released(false);
		zassert_ok(k380_low_power_request(reason));
		if (reason == K380_SHUTDOWN_LOW_VOLTAGE) { k_sleep(K_MSEC(3050)); }
		const int expected_low[] = { RADIO, LED, LATCH, FLUSH, HID, DISCONNECT, OFF };
		const int expected_ble[] = { RADIO, LED, FLUSH, HID, DISCONNECT, OFF };
		const int *expected = reason == K380_SHUTDOWN_LOW_VOLTAGE ? expected_low : expected_ble;
		const int count = reason == K380_SHUTDOWN_LOW_VOLTAGE ? ARRAY_SIZE(expected_low) : ARRAY_SIZE(expected_ble);
		zassert_true(k380_low_power_is_release_waiting());
		zassert_false(k380_low_power_input_events_allowed(), "quiet wait must suppress key events");
		zassert_false(k380_low_power_ble_start_allowed(), "disconnect callbacks must not restart advertising");
		zassert_equal(call_count, count - 1);
		zassert_mem_equal(calls, expected, (count - 1) * sizeof(int));
		zassert_equal(latch_calls, reason == K380_SHUTDOWN_LOW_VOLTAGE ? 1 : 0);
		k380_low_power_test_set_all_keys_released(true);
		k380_low_power_notify_all_keys_released();
		k380_low_power_notify_all_keys_released();
		zassert_equal(call_count, count);
		zassert_mem_equal(calls, expected, count * sizeof(int));
		zassert_equal(flush_calls, 1);
		zassert_false(k380_low_power_input_events_allowed(), "system-off must keep input closed");
	}
}

ZTEST(k380_low_power, test_all_causes_cleanup_order_when_keys_are_released)
{
	for (int reason = K380_SHUTDOWN_LOW_VOLTAGE; reason <= K380_SHUTDOWN_PAIRING_TIMEOUT; reason++) {
		reset();
		zassert_ok(k380_low_power_request(reason));
		if (reason == K380_SHUTDOWN_LOW_VOLTAGE) { k_sleep(K_MSEC(3050)); }
		const int low[] = { RADIO, LED, LATCH, FLUSH, HID, DISCONNECT, OFF };
		const int ble[] = { RADIO, LED, FLUSH, HID, DISCONNECT, OFF };
		const int *expected = reason == K380_SHUTDOWN_LOW_VOLTAGE ? low : ble;
		const int count = reason == K380_SHUTDOWN_LOW_VOLTAGE ? ARRAY_SIZE(low) : ARRAY_SIZE(ble);
		zassert_equal(call_count, count);
		zassert_mem_equal(calls, expected, count * sizeof(int));
		zassert_false(k380_low_power_is_release_waiting());
	}
}
