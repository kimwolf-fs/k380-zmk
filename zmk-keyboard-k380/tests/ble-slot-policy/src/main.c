#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/ble_slot_policy.h>
#include <zmk_keyboard_k380/status_indicator.h>

static uint8_t selected_profile;
static uint8_t cleared_profile;
static bool open_profiles[3];
static bool connected_profiles[3];

int zmk_ble_prof_select(uint8_t index) {
    selected_profile = index;
    return 0;
}

int zmk_ble_active_profile_index(void) { return selected_profile; }

void zmk_ble_clear_bonds(void) { cleared_profile = selected_profile; }

bool zmk_ble_profile_is_open(uint8_t index) { return open_profiles[index]; }

bool zmk_ble_profile_is_connected(uint8_t index) { return connected_profiles[index]; }

static void reset_fakes(void *fixture) {
    ARG_UNUSED(fixture);

    selected_profile = 0;
    cleared_profile = 0xff;
    memset(open_profiles, 0, sizeof(open_profiles));
    memset(connected_profiles, 0, sizeof(connected_profiles));
    k380_ble_slot_policy_reset_for_test();
    zassert_ok(k380_status_indicator_set(K380_STATUS_B1_BOOTLOADER_WAITING));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
}

ZTEST(k380_ble_slot_policy, test_select_slot_2_waits_on_ws2) {
    zassert_ok(k380_ble_slot_select(2));

    zassert_equal(k380_ble_slot_current(), 2);
    zassert_equal(selected_profile, 1);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z5_BLE_WAITING);
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

ZTEST_SUITE(k380_ble_slot_policy, NULL, NULL, reset_fakes, NULL, NULL);
