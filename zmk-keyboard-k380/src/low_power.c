#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(k380_low_power, LOG_LEVEL_INF);

#ifndef CONFIG_K380_LOW_POWER_TEST
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/pm.h>
#endif

#include <zmk_keyboard_k380/low_power.h>
#include <zmk_keyboard_k380/soft_off.h>
#ifndef CONFIG_K380_LOW_POWER_TEST
#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/status_indicator.h>
#endif

enum k380_low_power_state {
	K380_LOW_POWER_READY,
	K380_LOW_POWER_WARNING,
	K380_LOW_POWER_RELEASE_WAIT,
	K380_LOW_POWER_SYSTEM_OFF_REQUESTED,
};

static enum k380_low_power_state state;
static enum k380_shutdown_reason last_reason;
static bool reason_valid;
static bool ble_start_allowed;
static bool radio_and_led_quiet;

/* These weak seams keep the coordinator independent of later BLE/LED/PM work. */
__weak int k380_low_power_start_warning(enum k380_shutdown_reason reason)
{
	ARG_UNUSED(reason);
#ifdef CONFIG_K380_LOW_POWER_TEST
	return 0;
#else
	return k380_status_indicator_set(K380_STATUS_Z4_SOFT_OFF_WARNING);
#endif
}
__weak int k380_low_power_stop_radio(void)
{
#ifdef CONFIG_K380_LOW_POWER_TEST
	return 0;
#else
	return zmk_ble_stop_advertising();
#endif
}
__weak int k380_low_power_stop_led(void) { return 0; }
__weak int k380_low_power_resume_led(void)
{
#ifndef CONFIG_K380_LOW_POWER_TEST
	k380_status_indicator_resume_animation();
#endif
	return 0;
}
__weak int k380_low_power_restore_radio_and_led(void)
{
#ifdef CONFIG_K380_LOW_POWER_TEST
	return 0;
#else
	return zmk_ble_resume_advertising();
#endif
}
__weak int k380_low_power_latch_low_voltage(uint32_t save_budget_ms)
{
	ARG_UNUSED(save_budget_ms);
	return 0;
}
__weak int k380_low_power_flush_dirty_profile(uint32_t save_budget_ms)
{
	ARG_UNUSED(save_budget_ms);
#ifndef CONFIG_K380_LOW_POWER_TEST
	return zmk_ble_flush_active_profile_if_dirty();
#endif
	return 0;
}
__weak int k380_low_power_clear_hid(void)
{
#ifndef CONFIG_K380_LOW_POWER_TEST
	zmk_endpoint_clear_reports();
#endif
	return 0;
}
__weak int k380_low_power_disconnect_ble(void)
{
#ifdef CONFIG_K380_LOW_POWER_TEST
	return 0;
#else
	const int profile = zmk_ble_active_profile_index();
	return profile < 0 ? profile : zmk_ble_prof_disconnect(profile);
#endif
}
#ifndef CONFIG_K380_LOW_POWER_TEST
__weak bool k380_low_power_all_keys_released(void) { return true; }
#endif
__weak int k380_low_power_system_off(void)
{
#ifdef CONFIG_K380_LOW_POWER_TEST
	return 0;
#else
	return zmk_pm_soft_off();
#endif
}

static bool battery_is_charging(void)
{
#ifdef CONFIG_K380_LOW_POWER_TEST
	extern bool k380_low_power_test_battery_charging;
	return k380_low_power_test_battery_charging;
#else
	return k380_battery_policy_state() == K380_POWER_CHARGING;
#endif
}

static void log_cleanup_error(const char *operation, int err);

static void quiet_radio_and_led(void)
{
	if (!radio_and_led_quiet) {
		log_cleanup_error("radio stop", k380_low_power_stop_radio());
		log_cleanup_error("LED stop", k380_low_power_stop_led());
#ifndef CONFIG_K380_LOW_POWER_TEST
		log_cleanup_error("indicator quiet", k380_soft_off_prepare_radio_and_led_quiet());
#endif
		radio_and_led_quiet = true;
	}
}

static void log_cleanup_error(const char *operation, int err)
{
	if (err < 0 && err != -ENODEV) {
		LOG_ERR("Shutdown %s failed (%d); continuing", operation, err);
	}
}

static void prepare_request(void)
{
	quiet_radio_and_led();
#ifndef CONFIG_K380_LOW_POWER_TEST
	k380_soft_off_set_pending_reason(last_reason);
	/* This is an explicit bounded flush, never a deferred debounce wait. */
	log_cleanup_error("settings flush", k380_soft_off_flush_required_settings());
#else
	if (last_reason == K380_SHUTDOWN_LOW_VOLTAGE) {
		log_cleanup_error("latch save", k380_low_power_latch_low_voltage(K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS));
	}
	log_cleanup_error("profile flush", k380_low_power_flush_dirty_profile(K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS));
#endif
	log_cleanup_error("HID clear", k380_low_power_clear_hid());
	log_cleanup_error("BLE disconnect", k380_low_power_disconnect_ble());
}

static int complete_request(void)
{
	state = K380_LOW_POWER_SYSTEM_OFF_REQUESTED;
	return k380_low_power_system_off();
}

bool k380_low_power_ble_start_allowed(void)
{
	return ble_start_allowed && state == K380_LOW_POWER_READY;
}

int k380_low_power_startup_voltage_result(bool valid, bool charging, bool safe)
{
	if (!valid) {
		ble_start_allowed = false;
		return -EACCES;
	}
	if (charging) {
		k380_low_power_cancel_usb_pending();
	}
	if (charging || !safe) {
		ble_start_allowed = false;
		return -EACCES;
	}

	ble_start_allowed = true;
#ifndef CONFIG_K380_LOW_POWER_TEST
	(void)zmk_ble_resume_advertising();
#endif
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

	prepare_request();
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
		if (last_reason != K380_SHUTDOWN_BLE_WAIT_TIMEOUT &&
			last_reason != K380_SHUTDOWN_PAIRING_TIMEOUT) {
			return;
		}
		const bool may_restore = radio_and_led_quiet && ble_start_allowed &&
			last_reason != K380_SHUTDOWN_LOW_VOLTAGE;

		state = K380_LOW_POWER_READY;
		reason_valid = false;
		if (may_restore) {
			(void)k380_low_power_restore_radio_and_led();
			(void)k380_low_power_resume_led();
		}
		radio_and_led_quiet = false;
	}
}

void k380_low_power_cancel_usb_pending(void)
{
	if (state == K380_LOW_POWER_WARNING || state == K380_LOW_POWER_RELEASE_WAIT) {
		const bool may_restore = radio_and_led_quiet && ble_start_allowed;
		const bool may_resume_led = radio_and_led_quiet &&
			(ble_start_allowed || battery_is_charging());

		state = K380_LOW_POWER_READY;
		reason_valid = false;
		if (may_restore) {
			(void)k380_low_power_restore_radio_and_led();
		}
		if (may_resume_led) {
			(void)k380_low_power_resume_led();
		}
		radio_and_led_quiet = false;
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
	ble_start_allowed = false;
	radio_and_led_quiet = false;
	k380_low_power_test_battery_charging = false;
	test_keys_released = true;
}
void k380_low_power_test_set_all_keys_released(bool released) { test_keys_released = released; }
void k380_low_power_test_set_battery_charging(bool charging) { k380_low_power_test_battery_charging = charging; }
bool k380_low_power_all_keys_released(void) { return test_keys_released; }
#endif
