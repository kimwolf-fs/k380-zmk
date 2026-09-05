#define DT_DRV_COMPAT k380_behavior_dynamic_key

#include <drivers/behavior.h>
#include <zephyr/device.h>

#include <zmk_keyboard_k380/dynamic_keymap.h>

static int dynamic_key_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    return k380_dynamic_keymap_press(event.layer, (uint8_t)binding->param1,
                                     (uint16_t)binding->param2);
}

static int dynamic_key_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    return k380_dynamic_keymap_release(event.layer, (uint8_t)binding->param1,
                                       (uint16_t)binding->param2);
}

static const struct behavior_driver_api dynamic_key_driver_api = {
    .binding_pressed = dynamic_key_pressed,
    .binding_released = dynamic_key_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &dynamic_key_driver_api);
