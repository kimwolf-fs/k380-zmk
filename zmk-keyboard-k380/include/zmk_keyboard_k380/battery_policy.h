#pragma once

#include <stdint.h>

enum k380_power_state {
    K380_POWER_NORMAL,
    K380_POWER_CHARGING,
    K380_POWER_LOW_BATTERY,
    K380_POWER_SOFT_OFF_WARNING_REQUESTED,
};

void k380_battery_policy_sample_now(void);
int k380_battery_policy_submit_mv(uint16_t vddh_mv);
enum k380_power_state k380_battery_policy_state(void);
