#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

ZTEST(dynamic_config, test_settings_namespace_is_k380_only)
{
    zassert_mem_equal(K380_DYNAMIC_SETTINGS_ROOT, "k380/dynamic_config",
                      sizeof("k380/dynamic_config"));
}

ZTEST(dynamic_config, test_defaults_are_valid)
{
    struct k380_dynamic_config cfg;

    k380_dynamic_config_init_defaults(&cfg);

    zassert_equal(0, k380_dynamic_config_validate(&cfg));
    zassert_equal(0, cfg.active_preset);
    zassert_equal(K380_DYNAMIC_PRESET_COUNT, 4);
    zassert_equal(K380_DYNAMIC_MACRO_SLOT_COUNT, 16);
    zassert_equal(K380_DYNAMIC_MACRO_MAX_STEPS, 64);
}

ZTEST(dynamic_config, test_restore_all_resets_presets_but_not_external_settings)
{
    struct k380_dynamic_config cfg;

    k380_dynamic_config_init_defaults(&cfg);
    cfg.active_preset = 3;
    for (uint8_t preset = 0; preset < K380_DYNAMIC_PRESET_COUNT; preset++) {
        cfg.presets[preset].name[0] = preset + 1;
        cfg.presets[preset].bindings[preset % K380_DYNAMIC_LAYER_COUNT][preset]
            .type = K380_DYNAMIC_BINDING_KEY;
        cfg.presets[preset].bindings[preset % K380_DYNAMIC_LAYER_COUNT][preset]
            .value.key_usage = 0x4A + preset;
        cfg.presets[preset].macros[preset].step_count = preset + 1;
    }

    k380_dynamic_config_restore_all(&cfg);

    zassert_equal(0, cfg.active_preset);
    for (uint8_t preset = 0; preset < K380_DYNAMIC_PRESET_COUNT; preset++) {
        struct k380_dynamic_preset empty_preset = {0};

        zassert_mem_equal(&cfg.presets[preset], &empty_preset,
                          sizeof(empty_preset));
    }
    zassert_equal(0, k380_dynamic_config_validate(&cfg));
}

ZTEST(dynamic_config, test_invalid_macro_limits_are_rejected)
{
    struct k380_dynamic_config cfg;

    k380_dynamic_config_init_defaults(&cfg);
    cfg.active_preset = K380_DYNAMIC_PRESET_COUNT;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));

    k380_dynamic_config_init_defaults(&cfg);
    cfg.presets[0].bindings[0][0].type = K380_DYNAMIC_BINDING_MACRO;
    cfg.presets[0].bindings[0][0].value.macro_index =
        K380_DYNAMIC_MACRO_SLOT_COUNT;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));

    k380_dynamic_config_init_defaults(&cfg);

    cfg.presets[0].macros[0].step_count = K380_DYNAMIC_MACRO_MAX_STEPS + 1;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));

    k380_dynamic_config_init_defaults(&cfg);
    cfg.presets[0].macros[0].step_count = 1;
    cfg.presets[0].macros[0].steps[0].type = K380_DYNAMIC_MACRO_WAIT_MS;
    cfg.presets[0].macros[0].steps[0].value.wait_ms = 1;
    zassert_equal(0, k380_dynamic_config_validate(&cfg));

    cfg.presets[0].macros[0].steps[0].value.wait_ms = 60000;
    zassert_equal(0, k380_dynamic_config_validate(&cfg));

    cfg.presets[0].macros[0].steps[0].value.wait_ms = 60001;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));

    k380_dynamic_config_init_defaults(&cfg);
    cfg.presets[0].macros[0].step_count = 1;
    cfg.presets[0].macros[0].steps[0].type = K380_DYNAMIC_MACRO_WAIT_RANDOM;
    cfg.presets[0].macros[0].steps[0].value.wait_random.min_ms = 1;
    cfg.presets[0].macros[0].steps[0].value.wait_random.max_ms = 60000;
    zassert_equal(0, k380_dynamic_config_validate(&cfg));

    cfg.presets[0].macros[0].steps[0].value.wait_random.min_ms = 60000;
    cfg.presets[0].macros[0].steps[0].value.wait_random.max_ms = 60000;
    zassert_equal(0, k380_dynamic_config_validate(&cfg));

    cfg.presets[0].macros[0].steps[0].value.wait_random.min_ms = 2;
    cfg.presets[0].macros[0].steps[0].value.wait_random.max_ms = 1;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));

    k380_dynamic_config_init_defaults(&cfg);
    cfg.presets[0].macros[0].trigger = K380_DYNAMIC_MACRO_TRIGGER_COUNT;
    cfg.presets[0].macros[0].count = 1;
    zassert_equal(0, k380_dynamic_config_validate(&cfg));

    cfg.presets[0].macros[0].count = 65535;
    zassert_equal(0, k380_dynamic_config_validate(&cfg));

    cfg.presets[0].macros[0].count = 0;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));
}

ZTEST(dynamic_config, test_restore_helpers_ignore_invalid_indexes)
{
    struct k380_dynamic_config cfg;
    struct k380_dynamic_config before;

    k380_dynamic_config_init_defaults(&cfg);
    cfg.presets[0].bindings[0][0].type = K380_DYNAMIC_BINDING_KEY;
    cfg.presets[0].bindings[0][0].value.key_usage = 0x4A;
    cfg.presets[0].macros[0].step_count = 1;
    before = cfg;

    k380_dynamic_config_restore_key(&cfg, K380_DYNAMIC_PRESET_COUNT, 0, 0);
    k380_dynamic_config_restore_key(&cfg, 0, K380_DYNAMIC_LAYER_COUNT, 0);
    k380_dynamic_config_restore_key(&cfg, 0, 0, K380_DYNAMIC_KEY_COUNT);
    k380_dynamic_config_restore_preset(&cfg, K380_DYNAMIC_PRESET_COUNT);

    zassert_mem_equal(&cfg, &before, sizeof(cfg));
}

ZTEST_SUITE(dynamic_config, NULL, NULL, NULL, NULL, NULL);
