#pragma once

#include <stdbool.h>

#include <zmk_keyboard_k380/low_power.h>

#define K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS 1000U

int k380_soft_off_request_low_voltage(void);
int k380_soft_off_request_reason(enum k380_shutdown_reason reason);
/* Non-preemptible settings backends must finish one write in the remaining budget.
 * No retries or debounce waits; an overrun skips further writes and is reported. */
int k380_soft_off_flush_required_settings(void);
int k380_soft_off_prepare_radio_and_led_quiet(void);
int k380_soft_off_stop_animation(void);
const char *k380_soft_off_last_reason(void);
int k380_soft_off_clear_last_reason(void);
void k380_soft_off_handle_successful_boot(void);
bool k380_soft_off_has_low_voltage_latch(void);
int k380_soft_off_clear_low_voltage_latch_if_safe(bool safe_or_charging);

/* Internal coordinator hand-off; the public request APIs set this automatically. */
void k380_soft_off_set_pending_reason(enum k380_shutdown_reason reason);
