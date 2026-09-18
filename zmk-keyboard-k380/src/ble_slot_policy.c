#define DT_DRV_COMPAT k380_behavior_ble_slot

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/ble.h>
#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/low_power.h>
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

static void ble_wait_timeout_expired(struct k_work *work);
static void pairing_timeout_expired(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ble_wait_timeout_work, ble_wait_timeout_expired);
static K_WORK_DELAYABLE_DEFINE(pairing_timeout_work, pairing_timeout_expired);

#if !defined(CONFIG_ZTEST) && !IS_ENABLED(CONFIG_K380_BATTERY_POLICY)
static enum k380_power_state k380_battery_policy_state(void) { return K380_POWER_NORMAL; }
#endif

#if !defined(CONFIG_ZTEST) && !IS_ENABLED(CONFIG_K380_LOW_POWER_COORDINATOR)
static int k380_low_power_request(enum k380_shutdown_reason reason) {
    ARG_UNUSED(reason);
    return 0;
}
#endif

static bool on_usb_power(void) { return k380_battery_policy_state() == K380_POWER_CHARGING; }

static void cancel_timeout_work(void) {
    k_work_cancel_delayable(&ble_wait_timeout_work);
    k_work_cancel_delayable(&pairing_timeout_work);
}

static bool timeout_profile_is_current(void) {
    const int profile = zmk_ble_active_profile_index();
    return profile >= 0 && profile < K380_BLE_SLOT_COUNT;
}

static bool is_valid_slot(uint8_t slot) { return slot >= 1 && slot <= K380_BLE_SLOT_COUNT; }

static uint8_t profile_index_for_slot(uint8_t slot) { return slot - 1; }

static void clear_ble_slot_statuses(void) {
    k380_status_indicator_clear(K380_STATUS_Z5_BLE_WAITING);
    k380_status_indicator_clear(K380_STATUS_Z6_BLE_CONNECTED);
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
}

static void schedule_timeout_for_active_profile(int profile) {
    cancel_timeout_work();

    if (zmk_ble_profile_is_connected(profile)) {
        return;
    }

    if (zmk_ble_profile_is_open(profile)) {
        (void)k_work_reschedule(&pairing_timeout_work, K_MSEC(CONFIG_K380_BLE_PAIRING_TIMEOUT_MS));
    } else if (!on_usb_power()) {
        (void)k_work_reschedule(&ble_wait_timeout_work, K_MSEC(CONFIG_K380_BLE_WAIT_TIMEOUT_MS));
    }
}

static void ble_wait_timeout_expired(struct k_work *work) {
    ARG_UNUSED(work);

    if (!timeout_profile_is_current() || on_usb_power()) {
        return;
    }

    const int profile = zmk_ble_active_profile_index();
    if (zmk_ble_profile_is_connected(profile) || zmk_ble_profile_is_open(profile)) {
        return;
    }

    (void)k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT);
}

static void pairing_timeout_expired(struct k_work *work) {
    ARG_UNUSED(work);

    if (!timeout_profile_is_current() || !zmk_ble_profile_is_open(zmk_ble_active_profile_index())) {
        return;
    }

    if (on_usb_power()) {
        (void)zmk_ble_stop_advertising();
        clear_ble_slot_statuses();
        return;
    }

    (void)k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT);
}

static int update_active_slot_status(void) {
    const int profile = zmk_ble_active_profile_index();

    if (profile < 0 || profile >= K380_BLE_SLOT_COUNT) {
        return 0;
    }

    k380_low_power_cancel_pending();
    k_work_cancel_delayable(&connected_prompt_work);
    cancel_timeout_work();
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
        int err = k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING);
        schedule_timeout_for_active_profile(profile);
        return err;
    }

    int err = k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING);
    schedule_timeout_for_active_profile(profile);
    return err;
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
    return update_active_slot_status();
}

uint8_t k380_ble_slot_current(void) {
    const int profile = zmk_ble_active_profile_index();

    if (profile < 0 || profile >= K380_BLE_SLOT_COUNT) {
        return 1;
    }

    return profile + 1;
}

#ifdef CONFIG_ZTEST
void k380_ble_slot_policy_reset_for_test(void) {
    cancel_timeout_work();
    k_work_cancel_delayable(&connected_prompt_work);
}
void k380_ble_slot_active_profile_changed_for_test(void) { update_active_slot_status(); }
void k380_ble_slot_connected_prompt_expire_for_test(void) { connected_prompt_expired(NULL); }
void k380_ble_slot_wait_timeout_expire_for_test(void) { ble_wait_timeout_expired(NULL); }
void k380_ble_slot_pairing_timeout_expire_for_test(void) { pairing_timeout_expired(NULL); }
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
