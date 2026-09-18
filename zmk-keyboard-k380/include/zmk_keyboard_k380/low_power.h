#pragma once

#include <stdbool.h>
#include <stdint.h>

enum k380_shutdown_reason {
	K380_SHUTDOWN_LOW_VOLTAGE,
	K380_SHUTDOWN_BLE_WAIT_TIMEOUT,
	K380_SHUTDOWN_PAIRING_TIMEOUT,
};

bool k380_low_power_ble_start_allowed(void);
int k380_low_power_startup_voltage_result(bool valid, bool charging, bool safe);
int k380_low_power_request(enum k380_shutdown_reason reason);
void k380_low_power_notify_all_keys_released(void);
enum k380_shutdown_reason k380_low_power_last_reason(void);
bool k380_low_power_is_release_waiting(void);
bool k380_low_power_input_events_allowed(void);
bool k380_low_power_all_keys_released(void);

/* Atomic-only power publication, safe while the battery policy mutex is held.
 * USB confirmation and the final system-off handoff share one lifecycle lock. */
void k380_low_power_power_state_changed(bool charging);
/* Capture with the policy decision; stale low-voltage decisions are rejected. */
uint32_t k380_low_power_voltage_generation(void);
/* Invalidate obsolete LOW requests before waiting for latch deletion; keep BLE gated. */
void k380_low_power_publish_voltage_recovery(void);
int k380_low_power_request_low_voltage_at_generation(uint32_t generation);

/* Event sources use this to cancel a pending BLE timeout request. */
void k380_low_power_cancel_pending(void);
void k380_low_power_cancel_usb_pending(void);

#ifdef CONFIG_K380_LOW_POWER_TEST
void k380_low_power_test_reset(void);
void k380_low_power_test_set_all_keys_released(bool released);
void k380_low_power_test_set_battery_charging(bool charging);
#endif
