#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/pm.h>

#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/soft_off.h>
#include <zmk_keyboard_k380/status_indicator.h>

LOG_MODULE_REGISTER(k380_soft_off, LOG_LEVEL_INF);

#define K380_SOFT_OFF_REASON_SETTING "k380/last_shutdown_reason"
#define K380_SOFT_OFF_REASON_LOW_VOLTAGE "low_voltage_protection"
#define K380_SOFT_OFF_WARNING_DURATION K_SECONDS(3)

static char last_shutdown_reason[sizeof(K380_SOFT_OFF_REASON_LOW_VOLTAGE)];

#ifdef CONFIG_ZTEST
extern void k380_soft_off_test_record(int call);
extern int k380_soft_off_test_start_warning(void);
extern int k380_soft_off_test_wait_warning(void);
extern int k380_soft_off_test_save_reason(const char *name, const char *value, size_t len);
extern int k380_soft_off_test_confirm_ble_settings(void);
extern int k380_soft_off_test_active_ble_slot(void);
extern int k380_soft_off_test_clear_hid(void);
extern int k380_soft_off_test_disconnect_ble(int index);
extern int k380_soft_off_test_system_off(void);
#endif

static int save_shutdown_reason(void) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_save_reason(K380_SOFT_OFF_REASON_SETTING, last_shutdown_reason,
                                          sizeof(last_shutdown_reason));
#else
    return settings_save_one(K380_SOFT_OFF_REASON_SETTING, last_shutdown_reason,
                             sizeof(last_shutdown_reason));
#endif
}

static int start_warning(void) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_start_warning();
#else
    return k380_status_indicator_set(K380_STATUS_Z4_SOFT_OFF_WARNING);
#endif
}

static int confirm_ble_settings(void) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_confirm_ble_settings();
#else
    return zmk_ble_save_active_profile();
#endif
}

static int selected_ble_slot(void) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_active_ble_slot();
#else
    return zmk_ble_active_profile_index();
#endif
}

static int clear_hid_reports(void) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_clear_hid();
#else
    zmk_endpoint_clear_reports();
    return 0;
#endif
}

static int disconnect_ble(int index) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_disconnect_ble(index);
#else
    return zmk_ble_prof_disconnect(index);
#endif
}

static int enter_system_off(void) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_system_off();
#else
    return zmk_pm_soft_off();
#endif
}

const char *k380_soft_off_last_reason(void) {
    return last_shutdown_reason[0] == '\0' ? NULL : last_shutdown_reason;
}

void k380_soft_off_clear_last_reason(void) {
    last_shutdown_reason[0] = '\0';
#if !defined(CONFIG_ZTEST) && IS_ENABLED(CONFIG_SETTINGS)
    const int err = settings_delete(K380_SOFT_OFF_REASON_SETTING);
    if (err < 0) {
        LOG_ERR("Failed to clear shutdown reason (%d)", err);
    }
#endif
}

#ifdef CONFIG_ZTEST
char *k380_soft_off_test_last_reason_storage(void) { return last_shutdown_reason; }
#endif

static int complete_low_voltage_soft_off(void) {
    int err;

    k380_status_indicator_clear(K380_STATUS_Z4_SOFT_OFF_WARNING);
#ifdef CONFIG_ZTEST
    k380_soft_off_test_record(2);
#endif

    if (k380_battery_policy_state() == K380_POWER_CHARGING) {
        return -ECANCELED;
    }

    strcpy(last_shutdown_reason, K380_SOFT_OFF_REASON_LOW_VOLTAGE);
    err = save_shutdown_reason();
    if (err < 0) {
        LOG_ERR("Failed to save shutdown reason (%d)", err);
    }

    err = confirm_ble_settings();
    if (err < 0) {
        LOG_ERR("Failed to confirm BLE settings (%d)", err);
    }

    const int slot = selected_ble_slot();
    if (slot < 0) {
        LOG_ERR("Failed to get active BLE slot (%d)", slot);
    }

    err = clear_hid_reports();
    if (err < 0) {
        LOG_ERR("Failed to clear HID reports (%d)", err);
    }

    if (slot >= 0) {
        err = disconnect_ble(slot);
        if (err < 0 && err != -ENODEV) {
            LOG_ERR("Failed to disconnect BLE (%d)", err);
        }
    }

    return enter_system_off();
}

#ifndef CONFIG_ZTEST
static void k380_soft_off_warning_expired(struct k_work *work) {
    ARG_UNUSED(work);
    (void)complete_low_voltage_soft_off();
}

K_WORK_DELAYABLE_DEFINE(k380_soft_off_warning_work, k380_soft_off_warning_expired);
#endif

int k380_soft_off_request_low_voltage(void) {
    int err;

#ifdef CONFIG_ZTEST
    k380_soft_off_test_record(0);
#endif
    err = start_warning();
    if (err < 0) {
        LOG_ERR("Failed to start soft-off warning (%d)", err);
        return err;
    }

#ifdef CONFIG_ZTEST
    (void)k380_soft_off_test_wait_warning();
    return complete_low_voltage_soft_off();
#else
    err = k_work_reschedule(&k380_soft_off_warning_work, K380_SOFT_OFF_WARNING_DURATION);
    return err < 0 ? err : 0;
#endif
}

#if !defined(CONFIG_ZTEST) && IS_ENABLED(CONFIG_SETTINGS)
static int k380_soft_off_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                      void *cb_arg) {
    if (!settings_name_steq(name, "last_shutdown_reason", NULL) ||
        len != sizeof(last_shutdown_reason)) {
        return -EINVAL;
    }

    const int err = read_cb(cb_arg, last_shutdown_reason, sizeof(last_shutdown_reason));
    if (err <= 0) {
        return err;
    }

    last_shutdown_reason[sizeof(last_shutdown_reason) - 1U] = '\0';
    return 0;
}

static int k380_soft_off_settings_commit(void) {
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(k380, "k380", NULL, k380_soft_off_settings_set,
                               k380_soft_off_settings_commit, NULL);
#endif
