#pragma once

#define K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS 1000U

int k380_soft_off_request_low_voltage(void);
const char *k380_soft_off_last_reason(void);
void k380_soft_off_clear_last_reason(void);
