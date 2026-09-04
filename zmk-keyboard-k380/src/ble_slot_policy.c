#define DT_DRV_COMPAT k380_behavior_ble_slot

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/ble.h>
#ifndef CONFIG_ZTEST
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#endif

#include <zmk_keyboard_k380/ble_slot_policy.h>
#include <zmk_keyboard_k380/status_indicator.h>

#define K380_BLE_SLOT_CMD_SELECT 0
#define K380_BLE_SLOT_CMD_PAIR 1

static void connected_prompt_expired(struct k_work *work) {
    ARG_UNUSED(work);

    k380_status_indicator_clear(K380_STATUS_Z6_BLE_CONNECTED);
}

static K_WORK_DELAYABLE_DEFINE(connected_prompt_work, connected_prompt_expired);

static bool is_valid_slot(uint8_t slot) { return slot >= 1 && slot <= K380_BLE_SLOT_COUNT; }

static uint8_t profile_index_for_slot(uint8_t slot) { return slot - 1; }

static void clear_ble_slot_statuses(void) {
    k380_status_indicator_clear(K380_STATUS_Z5_BLE_WAITING);
    k380_status_indicator_clear(K380_STATUS_Z6_BLE_CONNECTED);
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
}

static int update_active_slot_status(void) {
    const int profile = zmk_ble_active_profile_index();

    if (profile < 0 || profile >= K380_BLE_SLOT_COUNT) {
        return 0;
    }

    k_work_cancel_delayable(&connected_prompt_work);
    clear_ble_slot_statuses();

    if (zmk_ble_profile_is_connected(profile)) {
        int err = k380_status_indicator_set(K380_STATUS_Z6_BLE_CONNECTED);
        if (err < 0) {
            return err;
        }

        int schedule_result =
            k_work_reschedule(&connected_prompt_work, K_MSEC(K380_BLE_CONNECTED_PROMPT_MS));
        return MIN(schedule_result, 0);
    }

    if (zmk_ble_profile_is_open(profile)) {
        return k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING);
    }

    return k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING);
}

int k380_ble_slot_select(uint8_t slot) {
    if (!is_valid_slot(slot)) {
        return -ERANGE;
    }

    const int err = zmk_ble_prof_select(profile_index_for_slot(slot));
    if (err < 0) {
        return err;
    }

    return update_active_slot_status();
}

int k380_ble_slot_pair(uint8_t slot) {
    int err = k380_ble_slot_select(slot);
    if (err < 0) {
        return err;
    }

    zmk_ble_clear_bonds();
    return k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING);
}

uint8_t k380_ble_slot_current(void) {
    const int profile = zmk_ble_active_profile_index();

    if (profile < 0 || profile >= K380_BLE_SLOT_COUNT) {
        return 1;
    }

    return profile + 1;
}

#ifdef CONFIG_ZTEST
void k380_ble_slot_policy_reset_for_test(void) {}
void k380_ble_slot_active_profile_changed_for_test(void) { update_active_slot_status(); }
void k380_ble_slot_connected_prompt_expire_for_test(void) { connected_prompt_expired(NULL); }
#endif

#ifndef CONFIG_ZTEST
static int k380_ble_slot_event_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    return update_active_slot_status();
}

ZMK_LISTENER(k380_ble_slot_event_listener, k380_ble_slot_event_listener);
ZMK_SUBSCRIPTION(k380_ble_slot_event_listener, zmk_ble_active_profile_changed);
#endif

#if !defined(CONFIG_ZTEST) && DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int k380_ble_slot_behavior_pressed(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    switch (binding->param1) {
    case K380_BLE_SLOT_CMD_SELECT:
        return k380_ble_slot_select(binding->param2);
    case K380_BLE_SLOT_CMD_PAIR:
        return k380_ble_slot_pair(binding->param2);
    default:
        return -ENOTSUP;
    }
}

static int k380_ble_slot_behavior_released(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api k380_ble_slot_behavior_driver_api = {
    .binding_pressed = k380_ble_slot_behavior_pressed,
    .binding_released = k380_ble_slot_behavior_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &k380_ble_slot_behavior_driver_api);

#endif
