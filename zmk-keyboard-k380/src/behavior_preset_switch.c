#define DT_DRV_COMPAT k380_behavior_preset_switch

#include <errno.h>
#include <stdint.h>

#include <drivers/behavior.h>
#include <zephyr/device.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

extern void k380_dynamic_macro_stop_before_preset_switch(void) __attribute__((weak));

int k380_dynamic_set_active_preset(uint8_t preset) {
    struct k380_dynamic_config cfg;
    uint8_t old_preset;
    int err;

    if (preset >= K380_DYNAMIC_PRESET_COUNT) {
        return -EINVAL;
    }

    if (k380_dynamic_macro_stop_before_preset_switch != NULL) {
        k380_dynamic_macro_stop_before_preset_switch();
    }

    err = k380_dynamic_settings_load(&cfg);
    if (err != 0) {
        return err;
    }

    old_preset = cfg.active_preset;
    cfg.active_preset = preset;
    err = k380_dynamic_settings_save(&cfg);
    if (err != 0) {
        cfg.active_preset = old_preset;
        (void)k380_dynamic_settings_save(&cfg);
        return err;
    }

    return 0;
}

uint8_t k380_dynamic_get_active_preset(void) {
    struct k380_dynamic_config cfg;

    if (k380_dynamic_settings_load(&cfg) != 0) {
        return 0;
    }

    return cfg.active_preset;
}

static int preset_switch_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    if (binding->param1 > UINT8_MAX) {
        return -EINVAL;
    }

    return k380_dynamic_set_active_preset((uint8_t)binding->param1);
}

static int preset_switch_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    return 0;
}

static const struct behavior_driver_api preset_switch_driver_api = {
    .binding_pressed = preset_switch_pressed,
    .binding_released = preset_switch_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &preset_switch_driver_api);
