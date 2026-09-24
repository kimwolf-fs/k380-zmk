/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <zephyr/drivers/sensor.h>
#include <stdint.h>

struct battery_value {
    uint16_t adc_raw;
    uint16_t millivolts;
    uint8_t state_of_charge;
};

int battery_channel_get(const struct battery_value *value, enum sensor_channel chan,
                        struct sensor_value *val_out);

enum sensor_channel battery_channel_alias(enum sensor_channel chan);
bool battery_channel_is_supported(enum sensor_channel chan);

uint8_t lithium_ion_mv_to_pct(int16_t bat_mv);
