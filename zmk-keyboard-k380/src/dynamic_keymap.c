#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zephyr/kernel.h>
#include <zmk/events/keycode_state_changed.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_keymap.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

/* Task 4 supplies this symbol. A missing executor still consumes macro keys. */
extern int k380_dynamic_macro_trigger(uint8_t preset, uint8_t slot, bool pressed)
    __attribute__((weak));
extern void k380_dynamic_macro_physical_key_state(uint16_t usage, bool pressed)
    __attribute__((weak));

struct dispatch_context {
    uint8_t layer;
    uint8_t key_index;
    uint16_t default_usage;
    bool pressed;
    uint8_t active_preset;
    struct k380_dynamic_binding binding;
};

static bool is_keyboard_keypad_usage(uint16_t usage) {
    return usage >= HID_USAGE_KEY_KEYBOARD_A &&
           usage <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI;
}

static int emit_keyboard_usage(uint16_t usage, bool pressed) {
    if (!is_keyboard_keypad_usage(usage)) {
        return -EINVAL;
    }

    if (k380_dynamic_macro_physical_key_state != NULL) {
        k380_dynamic_macro_physical_key_state(usage, pressed);
    }
    const int err = raise_zmk_keycode_state_changed_from_encoded(
        ZMK_HID_USAGE(HID_USAGE_KEY, usage), pressed, k_uptime_get());
    if (err != 0 && k380_dynamic_macro_physical_key_state != NULL) {
        k380_dynamic_macro_physical_key_state(usage, !pressed);
    }
    return err;
}

static int load_binding(struct k380_dynamic_config *cfg, void *user_data)
{
    struct dispatch_context *ctx = user_data;

    if (ctx->layer >= K380_DYNAMIC_LAYER_COUNT ||
        ctx->key_index >= K380_DYNAMIC_KEY_COUNT) {
        return -EINVAL;
    }

    ctx->active_preset = cfg->active_preset;
    ctx->binding =
        cfg->presets[cfg->active_preset].bindings[ctx->layer][ctx->key_index];

    return 0;
}

static int dispatch(uint8_t layer, uint8_t key_index, uint16_t default_usage, bool pressed) {
    struct dispatch_context ctx = {
        .layer = layer,
        .key_index = key_index,
        .default_usage = default_usage,
        .pressed = pressed,
    };
    int err = k380_dynamic_settings_with_config(load_binding, &ctx);
    if (err != 0) {
        return err;
    }

    switch (ctx.binding.type) {
    case K380_DYNAMIC_BINDING_DEFAULT:
        return emit_keyboard_usage(ctx.default_usage, ctx.pressed);
    case K380_DYNAMIC_BINDING_KEY:
        return emit_keyboard_usage(ctx.binding.value.key_usage, ctx.pressed);
    case K380_DYNAMIC_BINDING_MACRO:
        if (k380_dynamic_macro_trigger == NULL) {
            return 0;
        }
        return k380_dynamic_macro_trigger(ctx.active_preset,
                                          ctx.binding.value.macro_index, ctx.pressed);
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
