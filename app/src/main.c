/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_K380_SOFT_OFF)
#include <zmk_keyboard_k380/soft_off.h>
#include <zmk_keyboard_k380/low_power.h>
#if IS_ENABLED(CONFIG_K380_BATTERY_POLICY)
#include <zmk_keyboard_k380/battery_policy.h>
#endif
#endif

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)

#include <zmk/display.h>
#include <lvgl.h>

#endif

int main(void) {
    LOG_INF("Welcome to ZMK!\n");

#if IS_ENABLED(CONFIG_K380_SOFT_OFF) && IS_ENABLED(CONFIG_K380_BATTERY_POLICY)
    /* Keep BLE gated until settings and a valid startup voltage decision exist. */
    (void)k380_low_power_startup_voltage_result(false, false, false);
#endif

#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();
    const int err = settings_load();
    if (err != 0) {
        LOG_ERR("Failed to load settings (%d); keeping startup gate closed until qualification", err);
    }
#endif

#if IS_ENABLED(CONFIG_K380_SOFT_OFF) && IS_ENABLED(CONFIG_K380_BATTERY_POLICY)
    const int qualify_rc = k380_battery_policy_startup_qualify();
    if (qualify_rc == 0) {
        (void)k380_soft_off_clear_low_voltage_latch_if_safe(true);
        /* The coordinator's charging argument denotes an unsafe transition;
         * a qualified USB sample is already a safe startup result. */
        (void)k380_low_power_startup_voltage_result(true, false, true);
    } else {
        (void)k380_low_power_startup_voltage_result(false, false, false);
    }
#elif IS_ENABLED(CONFIG_K380_SOFT_OFF)
    /* Soft-off without the battery policy has no voltage gate to qualify. */
    (void)k380_low_power_startup_voltage_result(true, false, true);
#endif

#ifdef CONFIG_ZMK_DISPLAY
    zmk_display_init();

#if IS_ENABLED(CONFIG_ARCH_POSIX)
    // Workaround for an SDL display issue:
    // https://github.com/zephyrproject-rtos/zephyr/issues/71410
    while (1) {
        lv_task_handler();
        k_sleep(K_MSEC(10));
    }
#endif

#endif /* CONFIG_ZMK_DISPLAY */

    return 0;
}
