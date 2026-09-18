#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(k380_low_power, LOG_LEVEL_INF);

#if DT_NODE_HAS_COMPAT(DT_ROOT, kimwolf_k380)
BUILD_ASSERT(IS_ENABLED(CONFIG_K380_LOW_POWER_COORDINATOR), "K380 requires its coordinator");
BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_PM_SOFT_OFF), "K380 requires soft-off");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_SLEEP), "K380 must not use generic sleep");
BUILD_ASSERT(CONFIG_ZMK_BATTERY_REPORT_INTERVAL == 60, "K380 battery interval must be 60 s");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW), "K380 owns its status pixels");
#endif

#ifndef CONFIG_K380_LOW_POWER_TEST
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/pm.h>
#include <zmk/shutdown_input.h>
#endif

#include <zmk_keyboard_k380/low_power.h>
#include <zmk_keyboard_k380/soft_off.h>
#ifndef CONFIG_K380_LOW_POWER_TEST
#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/kscan.h>
#include <zmk_keyboard_k380/status_indicator.h>
#endif

enum k380_low_power_state {
	K380_LOW_POWER_READY,
	K380_LOW_POWER_WARNING,
	K380_LOW_POWER_RELEASE_WAIT,
	K380_LOW_POWER_SYSTEM_OFF_REQUESTED,
};

static atomic_t state;
static atomic_t cleanup_busy;
static atomic_t cancellation;
static struct k_spinlock lifecycle_lock;
#define CANCEL_USB BIT(0)
#define CANCEL_CONNECTION BIT(1)
#define CANCEL_VOLTAGE_RECOVERY BIT(2)
static atomic_t charging_confirmed;
static bool input_aborted;
static bool request_prepared;
static enum k380_shutdown_reason last_reason;
static bool reason_valid;
static bool ble_start_allowed;
static bool radio_and_led_quiet;
static int64_t warning_deadline;
static void warning_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(warning_work, warning_work_handler);

__weak void k380_low_power_cancel_warning(void)
{
#ifndef CONFIG_K380_LOW_POWER_TEST
	k380_status_indicator_clear(K380_STATUS_Z4_SOFT_OFF_WARNING);
#endif
}

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
#if defined(CONFIG_K380_LOW_POWER_TEST) || !IS_ENABLED(CONFIG_ZMK_BLE)
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
#if defined(CONFIG_K380_LOW_POWER_TEST) || !IS_ENABLED(CONFIG_ZMK_BLE)
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
#if !defined(CONFIG_K380_LOW_POWER_TEST) && IS_ENABLED(CONFIG_ZMK_BLE)
	return zmk_ble_flush_active_profile_if_dirty();
#endif
	return 0;
}
__weak int k380_low_power_clear_hid(void)
{
#ifndef CONFIG_K380_LOW_POWER_TEST
	zmk_hid_keyboard_clear_for_shutdown();
	zmk_endpoint_clear_reports();
#endif
	return 0;
}
__weak int k380_low_power_abort_input(void)
{
#ifdef CONFIG_K380_LOW_POWER_TEST
	return 0;
#else
	return zmk_shutdown_input_abort();
#endif
}
__weak int k380_low_power_disconnect_ble(void)
{
#if defined(CONFIG_K380_LOW_POWER_TEST) || !IS_ENABLED(CONFIG_ZMK_BLE)
	return 0;
#else
	const int profile = zmk_ble_active_profile_index();
	return profile < 0 ? profile : zmk_ble_prof_disconnect(profile);
#endif
}
#ifndef CONFIG_K380_LOW_POWER_TEST
bool k380_low_power_all_keys_released(void)
{
#if IS_ENABLED(CONFIG_K380_KSCAN_NO_DIODE_MATRIX)
	return k380_kscan_all_keys_released();
#else
	return true;
#endif
}
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
	return atomic_get(&charging_confirmed);
}

void k380_low_power_power_state_changed(bool charging)
{
	/* Called under the battery policy mutex: publish only, never drain input. */
	k_spinlock_key_t key = k_spin_lock(&lifecycle_lock);
	atomic_set(&charging_confirmed, charging);
	if (charging && (atomic_get(&state) == K380_LOW_POWER_WARNING ||
			 atomic_get(&state) == K380_LOW_POWER_RELEASE_WAIT)) {
		atomic_or(&cancellation, CANCEL_USB);
	}
	k_spin_unlock(&lifecycle_lock, key);
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

static int abort_input(void)
{
	if (input_aborted) {
		return 0;
	}
	int err = k380_low_power_abort_input();
	if (!err) {
		input_aborted = true;
	}
	return err;
}

static int prepare_request(void)
{
	if (request_prepared) {
		return 0;
	}
	quiet_radio_and_led();
	int err = abort_input();
	if (err) {
		LOG_ERR("Shutdown input abort failed (%d); keeping input closed", err);
		return err;
	}
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
	request_prepared = true;
	return 0;
}

/* Only the cleanup owner changes lifecycle state. Event sources can request cancellation
 * while that owner drains behavior work, without reopening the gate underneath it. */
static bool reconcile_cancellation(void)
{
	atomic_val_t requested = atomic_set(&cancellation, 0);
	bool usb = requested & CANCEL_USB;
	bool connection = requested & CANCEL_CONNECTION;
	bool recovered = (requested & CANCEL_VOLTAGE_RECOVERY) && last_reason == K380_SHUTDOWN_LOW_VOLTAGE;
	if (!usb && !recovered && !(connection && (last_reason == K380_SHUTDOWN_BLE_WAIT_TIMEOUT ||
		last_reason == K380_SHUTDOWN_PAIRING_TIMEOUT))) {
		return false;
	}
	if (abort_input()) {
		atomic_or(&cancellation, requested);
		return false;
	}
	bool may_restore = radio_and_led_quiet && ble_start_allowed;
	bool may_resume_led = radio_and_led_quiet &&
		(ble_start_allowed || (usb && battery_is_charging()));
	if (!request_prepared) {
		log_cleanup_error("HID clear", k380_low_power_clear_hid());
	}
	atomic_set(&state, K380_LOW_POWER_READY);
	(void)k_work_cancel_delayable(&warning_work);
	k380_low_power_cancel_warning();
	reason_valid = false;
	if (may_restore) {
		(void)k380_low_power_restore_radio_and_led();
	}
	if (may_resume_led) {
		(void)k380_low_power_resume_led();
	}
	radio_and_led_quiet = false;
	return true;
}

static int complete_request(void)
{
	k_spinlock_key_t key = k_spin_lock(&lifecycle_lock);
	if (battery_is_charging()) {
		atomic_or(&cancellation, CANCEL_USB);
	}
	if (atomic_get(&cancellation)) {
		k_spin_unlock(&lifecycle_lock, key);
		if (reconcile_cancellation()) {
			return -ECANCELED;
		}
		return -EAGAIN;
	}
	atomic_set(&state, K380_LOW_POWER_SYSTEM_OFF_REQUESTED);
	k_spin_unlock(&lifecycle_lock, key);
#if !defined(CONFIG_K380_LOW_POWER_TEST) && IS_ENABLED(CONFIG_K380_KSCAN_NO_DIODE_MATRIX)
	const int err = k380_kscan_prepare_system_off_wake();
	if (err) {
		atomic_set(&state, K380_LOW_POWER_RELEASE_WAIT);
		return err;
	}
#endif
	return k380_low_power_system_off();
}

static void cancel_pending(atomic_val_t reason);
static void release_cleanup_owner(void)
{
	k_spinlock_key_t key = k_spin_lock(&lifecycle_lock);
	atomic_clear(&cleanup_busy);
	bool pending = input_aborted && atomic_get(&cancellation) &&
		(atomic_get(&state) == K380_LOW_POWER_WARNING ||
		 atomic_get(&state) == K380_LOW_POWER_RELEASE_WAIT);
	k_spin_unlock(&lifecycle_lock, key);
	if (pending) {
		cancel_pending(0);
	}
}

bool k380_low_power_ble_start_allowed(void)
{
	return ble_start_allowed && atomic_get(&state) == K380_LOW_POWER_READY;
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
	/* Qualification can finish after a low-voltage warning was requested. */
	cancel_pending(CANCEL_VOLTAGE_RECOVERY);
#if !defined(CONFIG_K380_LOW_POWER_TEST) && IS_ENABLED(CONFIG_ZMK_BLE)
	(void)zmk_ble_resume_advertising();
#endif
	return 0;
}

static int advance_request(void)
{
	int err = prepare_request();
	if (reconcile_cancellation()) {
		return -ECANCELED;
	}
	if (err || !k380_low_power_all_keys_released()) {
		atomic_set(&state, K380_LOW_POWER_RELEASE_WAIT);
		return err;
	}
	return complete_request();
}

static void warning_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!atomic_cas(&cleanup_busy, 0, 1)) {
		(void)k_work_reschedule(&warning_work, K_MSEC(10));
		return;
	}
	if (atomic_get(&state) == K380_LOW_POWER_WARNING) {
		/* A stale callback must not shorten a newer request's warning. */
		const int64_t remaining = warning_deadline - k_uptime_get();
		if (remaining > 0) {
			(void)k_work_reschedule(&warning_work, K_MSEC(remaining));
		} else if (!reconcile_cancellation()) {
			(void)advance_request();
		}
	}
	release_cleanup_owner();
}

int k380_low_power_request(enum k380_shutdown_reason reason)
{
	/* Until wake is hardware-qualified, BLE timeouts must leave input/radio usable. */
	if (!IS_ENABLED(CONFIG_K380_AUTO_SYSTEM_OFF) &&
	    (reason == K380_SHUTDOWN_BLE_WAIT_TIMEOUT || reason == K380_SHUTDOWN_PAIRING_TIMEOUT)) {
		return -ENOTSUP;
	}
	if (battery_is_charging()) {
		return -ECANCELED;
	}

	if (!atomic_cas(&cleanup_busy, 0, 1)) {
		return 0;
	}
	if (atomic_get(&state) != K380_LOW_POWER_READY) {
		release_cleanup_owner();
		return 0;
	}

	k_spinlock_key_t key = k_spin_lock(&lifecycle_lock);
	atomic_clear(&cancellation);
	input_aborted = false;
	request_prepared = false;
	last_reason = reason;
	reason_valid = true;
	atomic_set(&state, K380_LOW_POWER_WARNING);
	if (battery_is_charging()) {
		atomic_or(&cancellation, CANCEL_USB);
	}
	k_spin_unlock(&lifecycle_lock, key);
	if (reconcile_cancellation()) {
		release_cleanup_owner();
		return -ECANCELED;
	}
	if (reason == K380_SHUTDOWN_LOW_VOLTAGE && k380_low_power_start_warning(reason) != 0) {
		atomic_or(&cancellation, CANCEL_USB);
		if (!reconcile_cancellation()) {
			atomic_set(&state, K380_LOW_POWER_RELEASE_WAIT);
		}
		release_cleanup_owner();
		return -EIO;
	}

	if (reason == K380_SHUTDOWN_LOW_VOLTAGE) {
		warning_deadline = k_uptime_get() + 3000;
		(void)k_work_reschedule(&warning_work, K_MSEC(3000));
		const bool cancelled = reconcile_cancellation();
		release_cleanup_owner();
		return cancelled ? -ECANCELED : 0;
	}

	const int err = advance_request();
	release_cleanup_owner();
	return err;
}

void k380_low_power_notify_all_keys_released(void)
{
	if (!atomic_cas(&cleanup_busy, 0, 1)) {
		return;
	}
	if (atomic_get(&state) == K380_LOW_POWER_RELEASE_WAIT) {
		if (!prepare_request() && !reconcile_cancellation() &&
			k380_low_power_all_keys_released()) {
			(void)complete_request();
		}
	}
	release_cleanup_owner();
}

static void cancel_pending(atomic_val_t reason)
{
	k_spinlock_key_t key = k_spin_lock(&lifecycle_lock);
	if (atomic_get(&state) == K380_LOW_POWER_SYSTEM_OFF_REQUESTED) {
		k_spin_unlock(&lifecycle_lock, key);
		return;
	}
	atomic_or(&cancellation, reason);
	k_spin_unlock(&lifecycle_lock, key);
	if (!atomic_cas(&cleanup_busy, 0, 1)) {
		return;
	}
	if (atomic_get(&state) == K380_LOW_POWER_WARNING ||
		atomic_get(&state) == K380_LOW_POWER_RELEASE_WAIT) {
		(void)reconcile_cancellation();
	} else {
		atomic_clear(&cancellation);
	}
	release_cleanup_owner();
}

void k380_low_power_cancel_pending(void) { cancel_pending(CANCEL_CONNECTION); }
void k380_low_power_cancel_usb_pending(void) { cancel_pending(CANCEL_USB); }

enum k380_shutdown_reason k380_low_power_last_reason(void)
{
	return reason_valid ? last_reason : K380_SHUTDOWN_LOW_VOLTAGE;
}

bool k380_low_power_is_release_waiting(void)
{
	return atomic_get(&state) == K380_LOW_POWER_RELEASE_WAIT;
}

bool k380_low_power_input_events_allowed(void)
{
	return atomic_get(&state) == K380_LOW_POWER_READY;
}

#ifdef CONFIG_K380_LOW_POWER_TEST
static bool test_keys_released = true;
void k380_low_power_test_reset(void)
{
	struct k_work_sync sync;
	(void)k_work_cancel_delayable_sync(&warning_work, &sync);
	atomic_set(&state, K380_LOW_POWER_READY);
	atomic_clear(&cleanup_busy);
	atomic_clear(&cancellation);
	input_aborted = false;
	request_prepared = false;
	reason_valid = false;
	ble_start_allowed = false;
	radio_and_led_quiet = false;
	atomic_clear(&charging_confirmed);
	test_keys_released = true;
}
void k380_low_power_test_set_all_keys_released(bool released) { test_keys_released = released; }
void k380_low_power_test_set_battery_charging(bool charging) { k380_low_power_power_state_changed(charging); }
bool k380_low_power_all_keys_released(void) { return test_keys_released; }
#endif
