#pragma once

#include <stdbool.h>

#define K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS 1000U

int k380_soft_off_request_low_voltage(void);
const char *k380_soft_off_last_reason(void);
void k380_soft_off_clear_last_reason(void);
void k380_soft_off_handle_successful_boot(void);
bool k380_soft_off_has_low_voltage_latch(void);
int k380_soft_off_clear_low_voltage_latch_if_safe(bool safe_or_charging);
