#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zephyr/kernel.h>
#include <zmk/events/keycode_state_changed.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_keymap.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

/* Task 4 supplies this symbol. A missing executor still consumes macro keys. */
extern int k380_dynamic_macro_trigger(uint8_t preset, uint8_t slot, bool pressed)
    __attribute__((weak));

static int emit_keyboard_usage(uint16_t usage, bool pressed) {
    return raise_zmk_keycode_state_changed_from_encoded(
        ZMK_HID_USAGE(HID_USAGE_KEY, usage), pressed, k_uptime_get());
}

static int dispatch(uint8_t layer, uint8_t key_index, uint16_t default_usage, bool pressed) {
    struct k380_dynamic_config cfg;
    int err = k380_dynamic_settings_load(&cfg);
    if (err != 0) {
        return err;
    }

    if (layer >= K380_DYNAMIC_LAYER_COUNT || key_index >= K380_DYNAMIC_KEY_COUNT) {
        return -EINVAL;
    }

    const struct k380_dynamic_binding *binding =
        &cfg.presets[cfg.active_preset].bindings[layer][key_index];

    switch (binding->type) {
    case K380_DYNAMIC_BINDING_DEFAULT:
        return emit_keyboard_usage(default_usage, pressed);
    case K380_DYNAMIC_BINDING_KEY:
        return emit_keyboard_usage(binding->value.key_usage, pressed);
    case K380_DYNAMIC_BINDING_MACRO:
        if (k380_dynamic_macro_trigger == NULL) {
            return -ENOTSUP;
        }
        return k380_dynamic_macro_trigger(cfg.active_preset,
                                          binding->value.macro_index, pressed);
    default:
        return -EINVAL;
    }
}

int k380_dynamic_keymap_press(uint8_t layer, uint8_t key_index, uint16_t default_usage) {
    return dispatch(layer, key_index, default_usage, true);
}

int k380_dynamic_keymap_release(uint8_t layer, uint8_t key_index, uint16_t default_usage) {
    return dispatch(layer, key_index, default_usage, false);
}
