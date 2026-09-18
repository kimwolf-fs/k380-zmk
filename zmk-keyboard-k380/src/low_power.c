#include <errno.h>
#include <stdbool.h>

#include <zephyr/sys/util.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/pm.h>

#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/low_power.h>
#include <zmk_keyboard_k380/status_indicator.h>

enum k380_low_power_state {
	K380_LOW_POWER_READY,
	K380_LOW_POWER_WARNING,
	K380_LOW_POWER_RELEASE_WAIT,
	K380_LOW_POWER_SYSTEM_OFF_REQUESTED,
};

static enum k380_low_power_state state;
static enum k380_shutdown_reason last_reason;
static bool reason_valid;
static bool ble_start_allowed = true;

/* These weak seams keep the coordinator independent of later BLE/LED/PM work. */
__weak int k380_low_power_start_warning(enum k380_shutdown_reason reason)
{
	ARG_UNUSED(reason);
	return k380_status_indicator_set(K380_STATUS_Z4_SOFT_OFF_WARNING);
}
__weak int k380_low_power_stop_radio(void) { return 0; }
__weak int k380_low_power_stop_led(void) { return 0; }
__weak int k380_low_power_flush_dirty_profile(void) { return 0; }
__weak int k380_low_power_clear_hid(void)
{
	zmk_endpoint_clear_reports();
	return 0;
}
__weak int k380_low_power_disconnect_ble(void)
{
	const int profile = zmk_ble_active_profile_index();
	return profile < 0 ? profile : zmk_ble_prof_disconnect(profile);
}
#ifndef CONFIG_K380_LOW_POWER_TEST
__weak bool k380_low_power_all_keys_released(void) { return true; }
#endif
__weak int k380_low_power_system_off(void) { return zmk_pm_soft_off(); }

static bool battery_is_charging(void)
{
#ifdef CONFIG_K380_LOW_POWER_TEST
	extern bool k380_low_power_test_battery_charging;
	return k380_low_power_test_battery_charging;
#else
	return k380_battery_policy_state() == K380_POWER_CHARGING;
#endif
}

static int complete_request(void)
{
	(void)k380_low_power_stop_radio();
	(void)k380_low_power_stop_led();
	(void)k380_low_power_flush_dirty_profile();
	(void)k380_low_power_clear_hid();
	(void)k380_low_power_disconnect_ble();
	state = K380_LOW_POWER_SYSTEM_OFF_REQUESTED;
	return k380_low_power_system_off();
}

bool k380_low_power_ble_start_allowed(void)
{
	return ble_start_allowed;
}

int k380_low_power_startup_voltage_result(bool valid, bool charging, bool safe)
{
	if (charging) {
		k380_low_power_cancel_pending();
	}
	if (!valid || charging || !safe) {
		ble_start_allowed = false;
		return -EACCES;
	}

	ble_start_allowed = true;
	return 0;
}

int k380_low_power_request(enum k380_shutdown_reason reason)
{
	if (reason != K380_SHUTDOWN_LOW_VOLTAGE && battery_is_charging()) {
		return -ECANCELED;
	}

	if (state != K380_LOW_POWER_READY) {
		return 0;
	}

	last_reason = reason;
	reason_valid = true;
	state = K380_LOW_POWER_WARNING;
	if (k380_low_power_start_warning(reason) != 0) {
		state = K380_LOW_POWER_READY;
		reason_valid = false;
		return -EIO;
	}

	if (!k380_low_power_all_keys_released()) {
		state = K380_LOW_POWER_RELEASE_WAIT;
		return 0;
	}

	return complete_request();
}

void k380_low_power_notify_all_keys_released(void)
{
	if (state == K380_LOW_POWER_RELEASE_WAIT && k380_low_power_all_keys_released()) {
		(void)complete_request();
	}
}

void k380_low_power_cancel_pending(void)
{
	if (state == K380_LOW_POWER_WARNING || state == K380_LOW_POWER_RELEASE_WAIT) {
		state = K380_LOW_POWER_READY;
		reason_valid = false;
	}
}

enum k380_shutdown_reason k380_low_power_last_reason(void)
{
	return reason_valid ? last_reason : K380_SHUTDOWN_LOW_VOLTAGE;
}

bool k380_low_power_is_release_waiting(void)
{
	return state == K380_LOW_POWER_RELEASE_WAIT;
}

#ifdef CONFIG_K380_LOW_POWER_TEST
bool k380_low_power_test_battery_charging;
static bool test_keys_released = true;
void k380_low_power_test_reset(void)
{
	state = K380_LOW_POWER_READY;
	reason_valid = false;
	ble_start_allowed = true;
	k380_low_power_test_battery_charging = false;
	test_keys_released = true;
}
void k380_low_power_test_set_all_keys_released(bool released) { test_keys_released = released; }
void k380_low_power_test_set_battery_charging(bool charging) { k380_low_power_test_battery_charging = charging; }
bool k380_low_power_all_keys_released(void) { return test_keys_released; }
#endif
