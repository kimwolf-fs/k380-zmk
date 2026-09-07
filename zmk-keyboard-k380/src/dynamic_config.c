#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/sys/crc.h>

#include <zmk_keyboard_k380/dynamic_config.h>

static int validate_step(const struct k380_dynamic_macro_step *step)
{
    switch (step->type) {
    case K380_DYNAMIC_MACRO_PRESS_KEY:
    case K380_DYNAMIC_MACRO_RELEASE_KEY:
    case K380_DYNAMIC_MACRO_TAP_KEY:
        return 0;
    case K380_DYNAMIC_MACRO_WAIT_MS:
        return step->value.wait_ms >= K380_DYNAMIC_WAIT_MIN_MS &&
                       step->value.wait_ms <= K380_DYNAMIC_WAIT_MAX_MS
                   ? 0
                   : -EINVAL;
    case K380_DYNAMIC_MACRO_WAIT_RANDOM:
        return step->value.wait_random.min_ms >= K380_DYNAMIC_WAIT_MIN_MS &&
                       step->value.wait_random.min_ms <= K380_DYNAMIC_WAIT_MAX_MS &&
                       step->value.wait_random.max_ms >= K380_DYNAMIC_WAIT_MIN_MS &&
                       step->value.wait_random.max_ms <= K380_DYNAMIC_WAIT_MAX_MS &&
                       step->value.wait_random.min_ms <= step->value.wait_random.max_ms
                   ? 0
                   : -EINVAL;
    case K380_DYNAMIC_MACRO_RELEASE_ALL:
        return 0;
    default:
        return -EINVAL;
    }
}

static int validate_macro(const struct k380_dynamic_macro *macro)
{
    if (macro->step_count > K380_DYNAMIC_MACRO_MAX_STEPS) {
        return -EINVAL;
    }

    if (macro->trigger > K380_DYNAMIC_MACRO_TRIGGER_COUNT) {
        return -EINVAL;
    }

    if (macro->trigger == K380_DYNAMIC_MACRO_TRIGGER_COUNT && macro->count == 0U) {
        return -EINVAL;
    }

    for (uint8_t i = 0; i < macro->step_count; i++) {
        if (validate_step(&macro->steps[i]) != 0) {
            return -EINVAL;
        }
    }

    return 0;
}

void k380_dynamic_config_init_defaults(struct k380_dynamic_config *cfg)
{
    if (cfg != NULL) {
        memset(cfg, 0, sizeof(*cfg));
    }
}

int k380_dynamic_config_validate(const struct k380_dynamic_config *cfg)
{
    if (cfg == NULL || cfg->active_preset >= K380_DYNAMIC_PRESET_COUNT) {
        return -EINVAL;
    }

    for (uint8_t preset = 0; preset < K380_DYNAMIC_PRESET_COUNT; preset++) {
        const struct k380_dynamic_preset *current = &cfg->presets[preset];

        for (uint8_t layer = 0; layer < K380_DYNAMIC_LAYER_COUNT; layer++) {
            for (uint8_t key = 0; key < K380_DYNAMIC_KEY_COUNT; key++) {
                const struct k380_dynamic_binding *binding =
                    &current->bindings[layer][key];

                if (binding->type > K380_DYNAMIC_BINDING_MACRO) {
                    return -EINVAL;
                }
                if (binding->type == K380_DYNAMIC_BINDING_MACRO &&
                    binding->value.macro_index >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
                    return -EINVAL;
                }
            }
        }

        for (uint8_t macro = 0; macro < K380_DYNAMIC_MACRO_SLOT_COUNT; macro++) {
            if (validate_macro(&current->macros[macro]) != 0) {
                return -EINVAL;
            }
        }
    }

    return 0;
}

void k380_dynamic_config_restore_key(struct k380_dynamic_config *cfg,
                                     uint8_t preset, uint8_t layer,
                                     uint8_t key_index)
{
    if (cfg == NULL || preset >= K380_DYNAMIC_PRESET_COUNT ||
        layer >= K380_DYNAMIC_LAYER_COUNT || key_index >= K380_DYNAMIC_KEY_COUNT) {
        return;
    }

    memset(&cfg->presets[preset].bindings[layer][key_index], 0,
           sizeof(cfg->presets[preset].bindings[layer][key_index]));
}

void k380_dynamic_config_restore_preset(struct k380_dynamic_config *cfg,
                                        uint8_t preset)
{
    if (cfg == NULL || preset >= K380_DYNAMIC_PRESET_COUNT) {
        return;
    }

    memset(&cfg->presets[preset], 0, sizeof(cfg->presets[preset]));
}

void k380_dynamic_config_restore_all(struct k380_dynamic_config *cfg)
{
    if (cfg == NULL) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
}

uint32_t k380_dynamic_config_crc32(const struct k380_dynamic_config *cfg)
{
    if (cfg == NULL) {
        return 0U;
    }

    return crc32_ieee((const uint8_t *)cfg, sizeof(*cfg));
}
