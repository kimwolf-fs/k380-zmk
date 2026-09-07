#define DT_DRV_COMPAT k380_behavior_preset_switch

#include <errno.h>
#include <stdint.h>

#include <drivers/behavior.h>
#include <zephyr/device.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

extern void k380_dynamic_macro_stop_before_preset_switch(void) __attribute__((weak));

struct set_active_preset_context {
    uint8_t preset;
};

static int set_active_preset(struct k380_dynamic_config *cfg, void *user_data)
{
    const struct set_active_preset_context *ctx = user_data;

    cfg->active_preset = ctx->preset;

    return 0;
}

static int get_active_preset(struct k380_dynamic_config *cfg, void *user_data)
{
    uint8_t *preset = user_data;

    *preset = cfg->active_preset;

    return 0;
}

int k380_dynamic_set_active_preset(uint8_t preset) {
    struct set_active_preset_context ctx = {
        .preset = preset,
    };

    if (preset >= K380_DYNAMIC_PRESET_COUNT) {
        return -EINVAL;
    }

    if (k380_dynamic_macro_stop_before_preset_switch != NULL) {
        k380_dynamic_macro_stop_before_preset_switch();
    }

    return k380_dynamic_settings_update(set_active_preset, &ctx);
}

uint8_t k380_dynamic_get_active_preset(void) {
    uint8_t preset = 0;

    if (k380_dynamic_settings_with_config(get_active_preset, &preset) != 0) {
        return 0;
    }

    return preset;
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
