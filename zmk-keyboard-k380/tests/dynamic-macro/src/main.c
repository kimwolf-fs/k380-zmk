#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_macro.h>
#include <zmk_keyboard_k380/dynamic_macro_record.h>
#include <zmk_keyboard_k380/dynamic_macro_store.h>
#include <zmk_keyboard_k380/dynamic_macro_vm.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

#include "fake_settings.h"

static struct k380_dynamic_config config;
static bool physical_keys[256];
static uint16_t emitted_usages[32];
static bool emitted_states[32];
static size_t emitted_count;
static int fail_emit_at = -1;
static struct k380_dynamic_macro_record trace_test_record;
static struct k380_dynamic_macro_trace_event
    trace_test_events[K380_MACRO_VM_MAX_TRACE_EVENTS];

int k380_dynamic_settings_load(struct k380_dynamic_config *cfg) {
    *cfg = config;
    return 0;
}

int k380_dynamic_settings_with_config(k380_dynamic_settings_config_cb_t callback,
                                      void *user_data) {
    return callback(&config, user_data);
}

int raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event) {
    if ((int)emitted_count == fail_emit_at) {
        return -EIO;
    }
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
    fail_emit_at = -1;
    k380_dynamic_macro_stop();
    k_sleep(K_MSEC(25));
    k380_dynamic_config_init_defaults(&config);
    k380_dynamic_macro_test_settings_reset();
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

static void sync_macro(uint8_t slot) {
    struct k380_dynamic_macro_record record;
    uint8_t wire[K380_DYNAMIC_MACRO_RECORD_MAX_BYTES];
    size_t wire_len;
    char key[64];

    zassert_ok(k380_dynamic_macro_record_from_legacy(macro(slot), &record));
    zassert_ok(k380_dynamic_macro_record_encode(&record, wire, sizeof(wire),
                                                &wire_len));
    zassert_true(snprintf(key, sizeof(key),
                          "k380/dynamic_config/v2/p/0/m/%u", slot) <
                 (int)sizeof(key));
    k380_dynamic_macro_test_settings_put(key, wire, wire_len);
}

static uint32_t package_crc32_without_field(const uint8_t *package,
                                            size_t length) {
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t index = 0U; index < length; index++) {
        uint8_t value = index >= 12U && index < 16U ? 0U : package[index];
        crc ^= value;
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

static void make_wait_record(struct k380_dynamic_macro_record *record,
                             uint32_t duration_ms) {
    memset(record, 0, sizeof(*record));
    record->record_version = K380_DYNAMIC_MACRO_RECORD_VERSION;
    record->trigger = K380_DYNAMIC_MACRO_TRIGGER_ONCE;
    record->repeat_count = 1U;
    record->package_len = 22U;
    memcpy(record->package, K380_MACRO_VM_PACKAGE_MAGIC,
           K380_MACRO_VM_PACKAGE_MAGIC_SIZE);
    record->package[4] = K380_MACRO_VM_PACKAGE_VERSION;
    sys_put_le16(6U, &record->package[8]);
    record->package[16] = K380_MACRO_VM_OP_WAIT;
    sys_put_le32(duration_ms, &record->package[17]);
    record->package[21] = K380_MACRO_VM_OP_END;
    record->package_crc32 = package_crc32_without_field(
        record->package, record->package_len);
    sys_put_le32(record->package_crc32, &record->package[12]);
}

static void make_trace_record(struct k380_dynamic_macro_record *record,
                              uint8_t trace_count) {
    memset(record, 0, sizeof(*record));
    record->record_version = K380_DYNAMIC_MACRO_RECORD_VERSION;
    record->trigger = K380_DYNAMIC_MACRO_TRIGGER_ONCE;
    record->repeat_count = 1U;
    record->package_len = (uint16_t)(17U + trace_count);
    memcpy(record->package, K380_MACRO_VM_PACKAGE_MAGIC,
           K380_MACRO_VM_PACKAGE_MAGIC_SIZE);
    record->package[4] = K380_MACRO_VM_PACKAGE_VERSION;
    sys_put_le16((uint16_t)(trace_count + 1U), &record->package[8]);
    memset(&record->package[16], K380_MACRO_VM_OP_RELEASE_ALL, trace_count);
    record->package[16U + trace_count] = K380_MACRO_VM_OP_END;
    record->package_crc32 = package_crc32_without_field(
        record->package, record->package_len);
    sys_put_le32(record->package_crc32, &record->package[12]);
}

ZTEST(dynamic_macro, test_new_macro_ignored_while_running) {
    reset_fakes();
    macro(0)->step_count = 1;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_WAIT_MS;
    macro(0)->steps[0].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;
    macro(1)->step_count = 1;
    macro(1)->steps[0].type = K380_DYNAMIC_MACRO_PRESS_KEY;
    macro(1)->steps[0].value.key_usage = HID_USAGE_KEY_KEYBOARD_B;
    sync_macro(0);
    sync_macro(1);

    zassert_ok(k380_dynamic_macro_trigger(0, 0, true));
    wait_until_running();
    zassert_ok(k380_dynamic_macro_trigger(0, 1, true));
    k_sleep(K_MSEC(25));

    zassert_equal(emitted_count, 0U);
    zassert_ok(k380_dynamic_macro_stop());
    wait_until_stopped();
}

ZTEST(dynamic_macro, test_trace_cursor_advances_only_past_returned_events) {
    struct k380_dynamic_macro_trace_event events[10];
    struct k380_dynamic_macro_run_state state;

    reset_fakes();
    macro(0)->step_count = 20U;
    for (uint8_t index = 0U; index < macro(0)->step_count; index++) {
        macro(0)->steps[index].type = K380_DYNAMIC_MACRO_WAIT_MS;
        macro(0)->steps[index].value.wait_ms = 1U;
    }
    sync_macro(0);

    zassert_ok(k380_dynamic_macro_trigger(0U, 0U, true));
    wait_until_stopped();
    zassert_equal(10, k380_dynamic_macro_trace_read(
                          0U, events, ARRAY_SIZE(events), &state));
    zassert_equal(10U, state.next_cursor);
    zassert_equal(1U, events[0].sequence);
    zassert_equal(10U, events[9].sequence);
    zassert_equal(K380_MACRO_VM_TRACE_WAIT, events[9].event);
}

ZTEST(dynamic_macro, test_same_toggle_press_stops_running_macro) {
    reset_fakes();
    macro(0)->trigger = K380_DYNAMIC_MACRO_TRIGGER_TOGGLE;
    macro(0)->step_count = 1;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_WAIT_MS;
    macro(0)->steps[0].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;
    sync_macro(0);

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
    sync_macro(0);

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
    sync_macro(0);

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
    sync_macro(0);

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

ZTEST(dynamic_macro, test_saved_macro_load_and_v1_migration_run_on_macro_thread) {
    const k_tid_t caller = k_current_get();

    reset_fakes();
    macro(0)->step_count = 1U;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_TAP_KEY;
    macro(0)->steps[0].value.key_usage = HID_USAGE_KEY_KEYBOARD_A;
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", macro(0), sizeof(*macro(0)));

    zassert_ok(k380_dynamic_macro_trigger(0U, 0U, true));
    wait_until_stopped();

    zassert_not_null(k380_dynamic_macro_test_settings_last_load_thread());
    zassert_not_equal(caller,
                      k380_dynamic_macro_test_settings_last_load_thread());
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
}

ZTEST(dynamic_macro, test_all_32_bit_waits_are_interruptible) {
    const uint32_t durations[] = {200U, 600000U, UINT32_MAX};
    struct k380_dynamic_macro_record record;

    for (size_t index = 0U; index < ARRAY_SIZE(durations); index++) {
        uint32_t run_id = 0U;
        reset_fakes();
        make_wait_record(&record, durations[index]);

        zassert_ok(k380_dynamic_macro_test_record_start(0U, 0U, &record,
                                                        &run_id));
        zassert_not_equal(0U, run_id);
        wait_until_running();
        k_sleep(K_MSEC(5));
        zassert_true(k380_dynamic_macro_is_running());

        const int64_t stop_started_at = k_uptime_get();
        zassert_ok(k380_dynamic_macro_stop_if_run_id(run_id));
        wait_until_stopped();
        zassert_true(k_uptime_get() - stop_started_at <= 20,
                     "duration %u stop took too long", durations[index]);
    }
}

ZTEST(dynamic_macro, test_run_id_match_and_stop_are_atomic) {
    struct k380_dynamic_macro_record record;
    uint32_t run_id = 0U;

    reset_fakes();
    make_wait_record(&record, 600000U);

    zassert_ok(k380_dynamic_macro_test_record_start(0U, 0U, &record,
                                                    &run_id));
    wait_until_running();
    zassert_equal(-ESTALE, k380_dynamic_macro_stop_if_run_id(run_id + 1U));
    zassert_true(k380_dynamic_macro_is_running());
    zassert_ok(k380_dynamic_macro_stop_if_run_id(run_id));
    wait_until_stopped();
}

ZTEST(dynamic_macro, test_stop_cleanup_failure_reports_host_failure) {
    struct k380_dynamic_macro_run_state state;

    reset_fakes();
    macro(0)->step_count = 2U;
    macro(0)->steps[0].type = K380_DYNAMIC_MACRO_PRESS_KEY;
    macro(0)->steps[0].value.key_usage = HID_USAGE_KEY_KEYBOARD_A;
    macro(0)->steps[1].type = K380_DYNAMIC_MACRO_WAIT_MS;
    macro(0)->steps[1].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;
    sync_macro(0U);

    zassert_ok(k380_dynamic_macro_trigger(0U, 0U, true));
    wait_until_running();
    for (int attempt = 0; attempt < 20 && emitted_count == 0U; attempt++) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(1U, emitted_count);
    fail_emit_at = 1;

    zassert_equal(-EIO, k380_dynamic_macro_stop());
    wait_until_stopped();
    k380_dynamic_macro_get_run_state(&state);
    zassert_equal(K380_DYNAMIC_MACRO_RUN_ERROR, state.state);
    zassert_equal(K380_MACRO_VM_HOST_FAILURE, state.vm_error);
}

ZTEST(dynamic_macro, test_empty_record_cannot_be_tested_or_executed) {
    struct k380_dynamic_macro_record empty = {
        .record_version = K380_DYNAMIC_MACRO_RECORD_VERSION,
        .trigger = K380_DYNAMIC_MACRO_TRIGGER_ONCE,
        .repeat_count = 1U,
    };
    uint32_t run_id = 99U;

    reset_fakes();
    zassert_ok(k380_dynamic_macro_store_save(0U, 0U, &empty));
    zassert_equal(-EINVAL,
                  k380_dynamic_macro_test_record_start(0U, 0U, &empty,
                                                       &run_id));
    zassert_equal(0U, run_id);
    zassert_ok(k380_dynamic_macro_trigger(0U, 0U, true));
    k_sleep(K_MSEC(25));
    zassert_false(k380_dynamic_macro_is_running());
    zassert_equal(0U, emitted_count);
}

ZTEST(dynamic_macro, test_trace_dropped_is_relative_to_requested_cursor) {
    struct k380_dynamic_macro_run_state state;
    uint32_t run_id;

    reset_fakes();
    make_trace_record(&trace_test_record, 70U);
    zassert_ok(k380_dynamic_macro_test_record_start(
        0U, 0U, &trace_test_record, &run_id));
    wait_until_stopped();

    zassert_equal(-EOVERFLOW,
                  k380_dynamic_macro_trace_read(
                      0U, trace_test_events, ARRAY_SIZE(trace_test_events),
                      &state));
    zassert_equal(6U, state.next_cursor);
    zassert_equal(6U, state.dropped);

    zassert_equal(64, k380_dynamic_macro_trace_read(
                          state.next_cursor, trace_test_events,
                          ARRAY_SIZE(trace_test_events), &state));
    zassert_equal(70U, state.next_cursor);
    zassert_equal(0U, state.dropped);
    zassert_equal(7U, trace_test_events[0].sequence);

    zassert_equal(10, k380_dynamic_macro_trace_read(
                          60U, trace_test_events,
                          ARRAY_SIZE(trace_test_events), &state));
    zassert_equal(0U, state.dropped);
}

ZTEST_SUITE(dynamic_macro, NULL, NULL, NULL, NULL, NULL);
