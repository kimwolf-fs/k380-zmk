#pragma once

#include <stdbool.h>
#include <stdint.h>

enum k380_shutdown_reason {
	K380_SHUTDOWN_LOW_VOLTAGE,
	K380_SHUTDOWN_BLE_WAIT_TIMEOUT,
	K380_SHUTDOWN_PAIRING_TIMEOUT,
    K380_SHUTDOWN_IDLE_TIMEOUT,
};

bool k380_low_power_ble_start_allowed(void);
int k380_low_power_startup_voltage_result(bool valid, bool charging, bool safe);
int k380_low_power_request(enum k380_shutdown_reason reason);
void k380_low_power_notify_all_keys_released(void);
enum k380_shutdown_reason k380_low_power_last_reason(void);
bool k380_low_power_is_release_waiting(void);
bool k380_low_power_input_events_allowed(void);
bool k380_low_power_all_keys_released(void);

/* Event sources use this to cancel a pending BLE timeout request. */
void k380_low_power_cancel_pending(void);
void k380_low_power_cancel_usb_pending(void);
void k380_low_power_power_state_changed(bool battery_powered);

#ifdef CONFIG_K380_LOW_POWER_TEST
void k380_low_power_test_reset(void);
void k380_low_power_test_set_all_keys_released(bool released);
void k380_low_power_test_set_battery_charging(bool charging);
void k380_low_power_test_set_battery_powered(bool battery_powered);
void k380_low_power_test_start_idle_timer(void);
void k380_low_power_test_advance_idle_ms(uint32_t elapsed_ms);
void k380_low_power_test_notify_matrix_event(bool pressed);
bool k380_low_power_test_idle_timer_running(void);
#endif
