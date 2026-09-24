#define DT_DRV_COMPAT k380_behavior_dynamic_key

#include <errno.h>
#include <stdint.h>

#include <drivers/behavior.h>
#include <zephyr/device.h>

#include <zmk_keyboard_k380/dynamic_keymap.h>

static int get_binding_params(struct zmk_behavior_binding *binding,
                              uint8_t *key_index, uint16_t *default_usage) {
    if (binding->param1 > UINT8_MAX || binding->param2 > UINT16_MAX) {
        return -EINVAL;
    }

    *key_index = (uint8_t)binding->param1;
    *default_usage = (uint16_t)binding->param2;
    return 0;
}

static int dynamic_key_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    uint8_t key_index;
    uint16_t default_usage;
    int err = get_binding_params(binding, &key_index, &default_usage);

    if (err != 0) {
        return err;
    }

    return k380_dynamic_keymap_press(event.layer, key_index, default_usage);
}

static int dynamic_key_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    uint8_t key_index;
    uint16_t default_usage;
    int err = get_binding_params(binding, &key_index, &default_usage);

    if (err != 0) {
        return err;
    }

    return k380_dynamic_keymap_release(event.layer, key_index, default_usage);
}

static const struct behavior_driver_api dynamic_key_driver_api = {
    .binding_pressed = dynamic_key_pressed,
    .binding_released = dynamic_key_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &dynamic_key_driver_api);
