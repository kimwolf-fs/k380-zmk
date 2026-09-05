#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/dynamic_config.h>

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
    cfg.presets[2].bindings[0][0].type = K380_DYNAMIC_BINDING_KEY;
    cfg.presets[2].bindings[0][0].value.key_usage = K380_HID_USAGE_HOME;
    cfg.presets[2].macros[0].step_count = 1;

    k380_dynamic_config_restore_all(&cfg);

    zassert_equal(0, cfg.active_preset);
    zassert_equal(K380_DYNAMIC_BINDING_DEFAULT, cfg.presets[2].bindings[0][0].type);
    zassert_equal(0, cfg.presets[2].macros[0].step_count);
    zassert_equal(0, k380_dynamic_config_validate(&cfg));
}

ZTEST(dynamic_config, test_invalid_macro_limits_are_rejected)
{
    struct k380_dynamic_config cfg;

    k380_dynamic_config_init_defaults(&cfg);

    cfg.presets[0].macros[0].step_count = K380_DYNAMIC_MACRO_MAX_STEPS + 1;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));

    k380_dynamic_config_init_defaults(&cfg);
    cfg.presets[0].macros[0].step_count = 1;
    cfg.presets[0].macros[0].steps[0].type = K380_DYNAMIC_MACRO_WAIT_MS;
    cfg.presets[0].macros[0].steps[0].value.wait_ms = 0;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));

    k380_dynamic_config_init_defaults(&cfg);
    cfg.presets[0].macros[0].step_count = 1;
    cfg.presets[0].macros[0].steps[0].type = K380_DYNAMIC_MACRO_WAIT_RANDOM;
    cfg.presets[0].macros[0].steps[0].value.wait_random.min_ms = 200;
    cfg.presets[0].macros[0].steps[0].value.wait_random.max_ms = 100;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));

    k380_dynamic_config_init_defaults(&cfg);
    cfg.presets[0].macros[0].trigger = K380_DYNAMIC_MACRO_TRIGGER_COUNT;
    cfg.presets[0].macros[0].count = 0;
    zassert_not_equal(0, k380_dynamic_config_validate(&cfg));
}

ZTEST_SUITE(dynamic_config, NULL, NULL, NULL, NULL, NULL);
