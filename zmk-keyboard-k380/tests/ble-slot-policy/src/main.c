#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/ble_slot_policy.h>
#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/low_power.h>
#include <zmk_keyboard_k380/status_indicator.h>

static uint8_t selected_profile;
static uint8_t cleared_profile;
static bool open_profiles[3];
static bool connected_profiles[3];
static enum k380_power_state power_state;
static int low_power_requests;
static int stop_advertising_calls;
static int cancel_pending_calls;
static int cancel_usb_pending_calls;
static int save_calls;
static int save_failures;
static bool active_profile_dirty;
static bool advertising_gate_open;
static int advertising_update_calls;

int zmk_ble_prof_select(uint8_t index) {
    if (selected_profile != index) {
        active_profile_dirty = true;
    }
    selected_profile = index;
    return 0;
}

int zmk_ble_active_profile_index(void) { return selected_profile; }

void zmk_ble_clear_bonds(void) {
    cleared_profile = selected_profile;
    open_profiles[selected_profile] = true;
    connected_profiles[selected_profile] = false;
}

bool zmk_ble_profile_is_open(uint8_t index) { return open_profiles[index]; }

bool zmk_ble_profile_is_connected(uint8_t index) { return connected_profiles[index]; }

enum k380_power_state k380_battery_policy_state(void) { return power_state; }
int k380_low_power_request(enum k380_shutdown_reason reason) {
    ARG_UNUSED(reason);
    low_power_requests++;
    return 0;
}
int zmk_ble_stop_advertising(void) { stop_advertising_calls++; return 0; }
void k380_low_power_cancel_pending(void) { cancel_pending_calls++; }
void k380_low_power_cancel_usb_pending(void) { cancel_usb_pending_calls++; }
int zmk_ble_flush_active_profile_if_dirty(void) {
    if (!active_profile_dirty) {
        return 0;
    }
    save_calls++;
    if (save_failures > 0) {
        --save_failures;
        return -EIO;
    }
    active_profile_dirty = false;
    return 0;
}
bool zmk_ble_active_profile_is_dirty(void) { return active_profile_dirty; }
int update_advertising(void) {
    advertising_update_calls++;
    return advertising_gate_open ? 1 : 0;
}

static void reset_fakes(void *fixture) {
    ARG_UNUSED(fixture);

    selected_profile = 0;
    cleared_profile = 0xff;
    memset(open_profiles, 0, sizeof(open_profiles));
    memset(connected_profiles, 0, sizeof(connected_profiles));
    power_state = K380_POWER_NORMAL;
    low_power_requests = 0;
    stop_advertising_calls = 0;
    cancel_pending_calls = 0;
    cancel_usb_pending_calls = 0;
    save_calls = 0;
    save_failures = 0;
    active_profile_dirty = false;
    advertising_gate_open = false;
    advertising_update_calls = 0;
    k380_ble_slot_policy_reset_for_test();
    zassert_ok(k380_status_indicator_set(K380_STATUS_B1_BOOTLOADER_WAITING));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
}

ZTEST(k380_ble_slot_policy, test_select_slot_2_waits_on_ws2) {
    zassert_ok(k380_ble_slot_select(2));

    zassert_equal(k380_ble_slot_current(), 2);
    zassert_equal(selected_profile, 1);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z5_BLE_WAITING);
    zassert_true(cancel_pending_calls > 0);
}

ZTEST(k380_ble_slot_policy, test_select_open_slot_2_enters_pairing_on_ws2) {
    open_profiles[1] = true;

    zassert_ok(k380_ble_slot_select(2));

    zassert_equal(k380_ble_slot_current(), 2);
    zassert_equal(selected_profile, 1);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z7_BLE_PAIRING);
}

ZTEST(k380_ble_slot_policy, test_pair_slot_2_enters_pairing_on_ws2) {
    zassert_ok(k380_ble_slot_pair(2));

    zassert_equal(k380_ble_slot_current(), 2);
    zassert_equal(selected_profile, 1);
    zassert_equal(cleared_profile, 1);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z7_BLE_PAIRING);
}

ZTEST(k380_ble_slot_policy, test_connect_slot_2_shows_connected_prompt) {
    connected_profiles[1] = true;

    zassert_ok(k380_ble_slot_select(2));

    zassert_equal(k380_ble_slot_current(), 2);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z6_BLE_CONNECTED);
    zassert_equal(K380_BLE_CONNECTED_PROMPT_MS, 5000);

    k380_ble_slot_connected_prompt_expire_for_test();

    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z1_NORMAL);
}

ZTEST(k380_ble_slot_policy, test_select_slot_3_replaces_slot_2_pairing) {
    zassert_ok(k380_ble_slot_pair(2));
    zassert_ok(k380_ble_slot_select(3));

    zassert_equal(k380_ble_slot_current(), 3);
    zassert_equal(selected_profile, 2);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z5_BLE_WAITING);
}

ZTEST(k380_ble_slot_policy, test_active_slot_connection_shows_connected_prompt) {
    zassert_ok(k380_ble_slot_select(2));
    connected_profiles[1] = true;

    k380_ble_slot_active_profile_changed_for_test();

    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z6_BLE_CONNECTED);

    k380_ble_slot_connected_prompt_expire_for_test();

    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z1_NORMAL);
}

ZTEST(k380_ble_slot_policy, test_current_slot_follows_persisted_zmk_profile) {
    selected_profile = 2;

    zassert_equal(k380_ble_slot_current(), 3);
}

ZTEST(k380_ble_slot_policy, test_select_same_slot_does_not_change_profile) {
    zassert_ok(k380_ble_slot_select(1));
    zassert_equal(selected_profile, 0);
    zassert_false(zmk_ble_active_profile_is_dirty());
}

ZTEST(k380_ble_slot_policy, test_new_slot_flushes_once) {
    zassert_ok(k380_ble_slot_select(2));
    zassert_true(zmk_ble_active_profile_is_dirty());
    zassert_ok(zmk_ble_flush_active_profile_if_dirty());
    zassert_false(zmk_ble_active_profile_is_dirty());
    zassert_ok(zmk_ble_flush_active_profile_if_dirty());
    zassert_equal(save_calls, 1);
}

ZTEST(k380_ble_slot_policy, test_failed_flush_retries_dirty_profile) {
    save_failures = 1;
    zassert_ok(k380_ble_slot_select(2));
    zassert_equal(zmk_ble_flush_active_profile_if_dirty(), -EIO);
    zassert_true(zmk_ble_active_profile_is_dirty());
    zassert_ok(zmk_ble_flush_active_profile_if_dirty());
    zassert_false(zmk_ble_active_profile_is_dirty());
    zassert_equal(save_calls, 2);
}

ZTEST(k380_ble_slot_policy, test_advertising_update_stays_gated) {
    zassert_equal(update_advertising(), 0);
    zassert_equal(advertising_update_calls, 1);
    advertising_gate_open = true;
    zassert_equal(update_advertising(), 1);
}

ZTEST(k380_ble_slot_policy, test_startup_refresh_derives_bonded_wait_state) {
    open_profiles[0] = false;
    connected_profiles[0] = false;
    power_state = K380_POWER_NORMAL;

    k380_low_power_ble_ready();

    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z5_BLE_WAITING);
    k380_ble_slot_wait_timeout_expire_for_test();
    zassert_equal(low_power_requests, 1);
}

ZTEST(k380_ble_slot_policy, test_wait_timeout_requests_low_power_on_battery) {
    open_profiles[0] = false;
    power_state = K380_POWER_NORMAL;
    zassert_ok(k380_ble_slot_select(1));
    k380_ble_slot_wait_timeout_expire_for_test();
    zassert_equal(low_power_requests, 1);
}

ZTEST(k380_ble_slot_policy, test_pairing_timeout_on_usb_stops_advertising) {
    open_profiles[0] = true;
    power_state = K380_POWER_CHARGING;
    zassert_ok(k380_ble_slot_select(1));
    k380_ble_slot_pairing_timeout_expire_for_test();
    zassert_equal(stop_advertising_calls, 1);
    zassert_equal(low_power_requests, 0);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z1_NORMAL);
}

ZTEST(k380_ble_slot_policy, test_stale_wait_timeout_is_ignored_after_slot_switch) {
    power_state = K380_POWER_NORMAL;
    zassert_ok(k380_ble_slot_select(1));
    zassert_ok(k380_ble_slot_select(2));
    k380_ble_slot_wait_timeout_expire_stale_for_test();
    zassert_equal(low_power_requests, 0);
}

ZTEST(k380_ble_slot_policy, test_stale_pairing_timeout_is_ignored_after_slot_switch) {
    open_profiles[0] = true;
    open_profiles[1] = true;
    power_state = K380_POWER_NORMAL;
    zassert_ok(k380_ble_slot_select(1));
    zassert_ok(k380_ble_slot_select(2));
    k380_ble_slot_pairing_timeout_expire_stale_for_test();
    zassert_equal(low_power_requests, 0);
}

ZTEST(k380_ble_slot_policy, test_usb_transition_restarts_wait_timer_on_battery) {
    power_state = K380_POWER_CHARGING;
    zassert_ok(k380_ble_slot_select(1));
    power_state = K380_POWER_NORMAL;
    k380_ble_slot_power_state_changed();
    k380_ble_slot_wait_timeout_expire_for_test();
    zassert_equal(low_power_requests, 1);
}

ZTEST(k380_ble_slot_policy, test_timeout_paths_do_not_save_settings) {
    open_profiles[0] = true;
    power_state = K380_POWER_NORMAL;
    zassert_ok(k380_ble_slot_select(1));
    k380_ble_slot_pairing_timeout_expire_for_test();
    zassert_equal(save_calls, 0);
}

ZTEST(k380_ble_slot_policy, test_pairing_path_does_not_save_settings) {
    zassert_ok(k380_ble_slot_pair(2));
    zassert_equal(save_calls, 0);
}

ZTEST_SUITE(k380_ble_slot_policy, NULL, NULL, reset_fakes, NULL, NULL);
