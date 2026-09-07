#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_macro.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

static struct k380_dynamic_config config;
static bool physical_keys[256];
static uint16_t emitted_usages[32];
static bool emitted_states[32];
static size_t emitted_count;

int k380_dynamic_settings_load(struct k380_dynamic_config *cfg) {
    *cfg = config;
    return 0;
}

int k380_dynamic_settings_with_config(k380_dynamic_settings_config_cb_t callback,
                                      void *user_data) {
    return callback(&config, user_data);
}

int raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event) {
    zassert_equal(event.usage_page, HID_USAGE_KEY);
    zassert_true(emitted_count < ARRAY_SIZE(emitted_usages));
    emitted_usages[emitted_count] = event.keycode;
    emitted_states[emitted_count++] = event.state;
    return 0;
}

bool zmk_hid_keyboard_is_pressed(zmk_key_t code) {
    return physical_keys[code];
}

static void set_physical_key(uint16_t usage, bool pressed) {
    physical_keys[usage] = pressed;
    k380_dynamic_macro_physical_key_state(usage, pressed);
}

static void reset_fakes(void) {
    k380_dynamic_macro_stop();
    k_sleep(K_MSEC(25));
    k380_dynamic_config_init_defaults(&config);
    memset(physical_keys, 0, sizeof(physical_keys));
    emitted_count = 0;
}

static void wait_until_running(void) {
    for (int i = 0; i < 20 && !k380_dynamic_macro_is_running(); i++) {
        k_sleep(K_MSEC(5));
    }
    zassert_true(k380_dynamic_macro_is_running());
}

static void wait_until_stopped(void) {
    for (int i = 0; i < 40 && k380_dynamic_macro_is_running(); i++) {
        k_sleep(K_MSEC(5));
    }
    zassert_false(k380_dynamic_macro_is_running());
}

static struct k380_dynamic_macro *macro(uint8_t slot) {
    return &config.presets[0].macros[slot];
}

ZTEST(dynamic_macro, test_new_macro_ignored_while_running) {
    reset_fakes();
    macro(0)->step_count = 1;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_WAIT_MS;
    macro(0)->steps[0].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;
    macro(1)->step_count = 1;
    macro(1)->steps[0].type = K380_DYNAMIC_MACRO_PRESS_KEY;
    macro(1)->steps[0].value.key_usage = HID_USAGE_KEY_KEYBOARD_B;

    zassert_ok(k380_dynamic_macro_trigger(0, 0, true));
    wait_until_running();
    zassert_ok(k380_dynamic_macro_trigger(0, 1, true));
    k_sleep(K_MSEC(25));

    zassert_equal(emitted_count, 0U);
    zassert_ok(k380_dynamic_macro_stop());
    wait_until_stopped();
}

ZTEST(dynamic_macro, test_same_toggle_press_stops_running_macro) {
    reset_fakes();
    macro(0)->trigger = K380_DYNAMIC_MACRO_TRIGGER_TOGGLE;
    macro(0)->step_count = 1;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_WAIT_MS;
    macro(0)->steps[0].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;

    zassert_ok(k380_dynamic_macro_trigger(0, 0, true));
    wait_until_running();
    zassert_ok(k380_dynamic_macro_trigger(0, 0, true));
    wait_until_stopped();
}

ZTEST(dynamic_macro, test_hold_release_stops_and_releases_macro_keys) {
    reset_fakes();
    macro(0)->trigger = K380_DYNAMIC_MACRO_TRIGGER_HOLD;
    macro(0)->step_count = 2;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_PRESS_KEY;
    macro(0)->steps[0].value.key_usage = HID_USAGE_KEY_KEYBOARD_B;
    macro(0)->steps[1].type = K380_DYNAMIC_MACRO_WAIT_MS;
    macro(0)->steps[1].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;

    zassert_ok(k380_dynamic_macro_trigger(0, 0, true));
    wait_until_running();
    for (int i = 0; i < 20 && emitted_count == 0; i++) {
        k_sleep(K_MSEC(5));
    }
    zassert_equal(emitted_usages[0], HID_USAGE_KEY_KEYBOARD_B);
    zassert_true(emitted_states[0]);

    zassert_ok(k380_dynamic_macro_trigger(0, 0, false));
    wait_until_stopped();
    zassert_equal(emitted_count, 2U);
    zassert_equal(emitted_usages[1], HID_USAGE_KEY_KEYBOARD_B);
    zassert_false(emitted_states[1]);
}

ZTEST(dynamic_macro, test_stop_does_not_release_physical_key_state) {
    reset_fakes();
    macro(0)->step_count = 2;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_PRESS_KEY;
    macro(0)->steps[0].value.key_usage = HID_USAGE_KEY_KEYBOARD_A;
    macro(0)->steps[1].type = K380_DYNAMIC_MACRO_WAIT_MS;
    macro(0)->steps[1].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;

    zassert_ok(k380_dynamic_macro_trigger(0, 0, true));
    wait_until_running();
    for (int i = 0; i < 20 && emitted_count == 0; i++) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(emitted_count, 1U);
    zassert_equal(emitted_usages[0], HID_USAGE_KEY_KEYBOARD_A);
    zassert_true(emitted_states[0]);

    set_physical_key(HID_USAGE_KEY_KEYBOARD_A, true);
    zassert_ok(k380_dynamic_macro_stop());
    wait_until_stopped();

    zassert_true(physical_keys[HID_USAGE_KEY_KEYBOARD_A]);
    zassert_equal(emitted_count, 1U);
    set_physical_key(HID_USAGE_KEY_KEYBOARD_A, false);
}

ZTEST(dynamic_macro, test_wait_can_be_interrupted) {
    int64_t stop_started_at;
    int64_t stopped_at;

    reset_fakes();
    macro(0)->step_count = 2;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_PRESS_KEY;
    macro(0)->steps[0].value.key_usage = HID_USAGE_KEY_KEYBOARD_B;
    macro(0)->steps[1].type = K380_DYNAMIC_MACRO_WAIT_MS;
    macro(0)->steps[1].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;

    zassert_ok(k380_dynamic_macro_trigger(0, 0, true));
    wait_until_running();
    for (int i = 0; i < 20 && emitted_count == 0; i++) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(emitted_count, 1U);
    stop_started_at = k_uptime_get();
    zassert_ok(k380_dynamic_macro_stop());
    wait_until_stopped();
    stopped_at = k_uptime_get();

    zassert_equal(emitted_count, 2U);
    zassert_equal(emitted_usages[1], HID_USAGE_KEY_KEYBOARD_B);
    zassert_false(emitted_states[1]);
    zassert_true(stopped_at - stop_started_at <= 20,
                 "interrupt took too long: %lld ms", stopped_at - stop_started_at);
}

ZTEST_SUITE(dynamic_macro, NULL, NULL, NULL, NULL, NULL);
