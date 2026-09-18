#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/pm.h>

#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/soft_off.h>
#include <zmk_keyboard_k380/status_indicator.h>

LOG_MODULE_REGISTER(k380_soft_off, LOG_LEVEL_INF);

#if !IS_ENABLED(CONFIG_K380_BATTERY_POLICY)
__weak enum k380_power_state k380_battery_policy_state(void) {
    return K380_POWER_NORMAL;
}
#endif

#define K380_SOFT_OFF_REASON_SETTING "k380/last_shutdown_reason"
#define K380_SOFT_OFF_REASON_LOW_VOLTAGE "low_voltage_protection"

static char last_shutdown_reason[sizeof(K380_SOFT_OFF_REASON_LOW_VOLTAGE)];
static atomic_t pending_reason = K380_SHUTDOWN_LOW_VOLTAGE;
K_MUTEX_DEFINE(latch_lock);

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
__weak int k380_soft_off_test_delete_reason(void) { return 0; }
__weak int64_t k380_soft_off_test_uptime(void) { return k_uptime_get(); }
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

static int confirm_ble_settings(int64_t deadline) {
#ifdef CONFIG_ZTEST
    ARG_UNUSED(deadline);
    return k380_soft_off_test_confirm_ble_settings();
#elif IS_ENABLED(CONFIG_ZMK_BLE)
    return zmk_ble_flush_active_profile_if_dirty_before(deadline);
#else
    ARG_UNUSED(deadline);
    return 0;
#endif
}

static int selected_ble_slot(void) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_active_ble_slot();
#elif IS_ENABLED(CONFIG_ZMK_BLE)
    return zmk_ble_active_profile_index();
#else
    return -ENODEV;
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
#elif IS_ENABLED(CONFIG_ZMK_BLE)
    return zmk_ble_prof_disconnect(index);
#else
    ARG_UNUSED(index);
    return -ENODEV;
#endif
}

static int enter_system_off(void) {
#ifdef CONFIG_ZTEST
    return k380_soft_off_test_system_off();
#else
    return zmk_pm_soft_off();
#endif
}

void k380_soft_off_set_pending_reason(enum k380_shutdown_reason reason) {
    atomic_set(&pending_reason, reason);
}

const char *k380_soft_off_last_reason(void) {
    k_mutex_lock(&latch_lock, K_FOREVER);
    const char *reason = last_shutdown_reason[0] == '\0' ? NULL : K380_SOFT_OFF_REASON_LOW_VOLTAGE;
    k_mutex_unlock(&latch_lock);
    return reason;
}

static int clear_last_reason_locked(void) {
    if (last_shutdown_reason[0] == '\0') {
        return 0;
    }
#if !defined(CONFIG_ZTEST) && IS_ENABLED(CONFIG_SETTINGS)
    const int err = settings_delete(K380_SOFT_OFF_REASON_SETTING);
#elif defined(CONFIG_ZTEST)
    const int err = k380_soft_off_test_delete_reason();
#else
    const int err = 0;
#endif
    if (err < 0) {
        LOG_ERR("Failed to clear shutdown reason (%d)", err);
        return err;
    }
    last_shutdown_reason[0] = '\0';
    return 0;
}

int k380_soft_off_clear_last_reason(void) {
    k_mutex_lock(&latch_lock, K_FOREVER);
    const int err = clear_last_reason_locked();
    k_mutex_unlock(&latch_lock);
    return err;
}

void k380_soft_off_handle_successful_boot(void) {
    /* The low-voltage latch is cleared only after startup voltage qualification. */
}

bool k380_soft_off_has_low_voltage_latch(void) {
    k_mutex_lock(&latch_lock, K_FOREVER);
    const bool latched = strcmp(last_shutdown_reason, K380_SOFT_OFF_REASON_LOW_VOLTAGE) == 0;
    k_mutex_unlock(&latch_lock);
    return latched;
}

int k380_soft_off_clear_low_voltage_latch_if_safe(bool safe_or_charging) {
    k_mutex_lock(&latch_lock, K_FOREVER);
    int err = 0;
    if (strcmp(last_shutdown_reason, K380_SOFT_OFF_REASON_LOW_VOLTAGE) == 0) {
        err = safe_or_charging ? clear_last_reason_locked() : -EACCES;
    }
    k_mutex_unlock(&latch_lock);
    return err;
}

#ifdef CONFIG_ZTEST
char *k380_soft_off_test_last_reason_storage(void) { return last_shutdown_reason; }
#endif

/* Task 5 can override this with its explicit pending-render cancellation API. */
__weak int k380_soft_off_stop_animation(void) {
    return k380_status_indicator_set(K380_STATUS_Z1_NORMAL);
}

int k380_soft_off_prepare_radio_and_led_quiet(void) {
    /* Preserve the charging indication when USB cancels the warning. */
    k380_status_indicator_clear(K380_STATUS_Z4_SOFT_OFF_WARNING);
    if (k380_battery_policy_state() != K380_POWER_CHARGING) {
        (void)k380_soft_off_stop_animation();
    }
#ifdef CONFIG_ZTEST
    k380_soft_off_test_record(2);
#endif
    return 0;
}

int k380_soft_off_flush_required_settings(void) {
    return k380_soft_off_flush_required_settings_at_generation(k380_low_power_voltage_generation());
}

int k380_soft_off_flush_required_settings_at_generation(uint32_t generation) {
    int err;
    const enum k380_shutdown_reason reason = atomic_get(&pending_reason);
#ifdef CONFIG_ZTEST
    const int64_t deadline = k380_soft_off_test_uptime() + K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS;
#else
    const int64_t deadline = k_uptime_get() + K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    zmk_ble_cancel_pending_profile_save();
#endif
#endif

    /* Recovery publishes its generation before taking this lock. A save admitted
     * first finishes before safe deletion; a stale save admitted later is rejected. */
    if (k_mutex_lock(&latch_lock, K_MSEC(K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS)) != 0) {
        return -ETIMEDOUT;
    }
    if (reason == K380_SHUTDOWN_LOW_VOLTAGE &&
        generation != k380_low_power_voltage_generation()) {
        k_mutex_unlock(&latch_lock);
        return -ECANCELED;
    }
    /* BLE timeout causes are deliberately RAM-only. */
    if (reason == K380_SHUTDOWN_LOW_VOLTAGE &&
        strcmp(last_shutdown_reason, K380_SOFT_OFF_REASON_LOW_VOLTAGE) != 0) {
#ifdef CONFIG_ZTEST
        if (k380_soft_off_test_uptime() >= deadline) {
#else
        if (k_uptime_get() >= deadline) {
#endif
            LOG_ERR("Settings deadline expired; skipping low-voltage latch save");
            k_mutex_unlock(&latch_lock);
            return -ETIMEDOUT;
        }
        strcpy(last_shutdown_reason, K380_SOFT_OFF_REASON_LOW_VOLTAGE);
        err = save_shutdown_reason();
        if (err < 0) {
            LOG_ERR("Failed to save shutdown reason (%d)", err);
        }
    }
    k_mutex_unlock(&latch_lock);

#ifdef CONFIG_ZTEST
    if (k380_soft_off_test_uptime() >= deadline) {
#else
    if (k_uptime_get() >= deadline) {
#endif
        LOG_ERR("Settings deadline expired; skipping active profile save");
        return -ETIMEDOUT;
    }
    err = confirm_ble_settings(deadline);
    if (err < 0) {
        LOG_ERR("Failed to flush active BLE profile (%d)", err);
    }

#ifdef CONFIG_ZTEST
    const bool overrun = k380_soft_off_test_uptime() >= deadline;
#else
    const bool overrun = k_uptime_get() >= deadline;
#endif
    if (overrun) {
        LOG_ERR("Synchronous settings backend exceeded shutdown budget");
    }
    return overrun ? -ETIMEDOUT : err;
}

static int complete_low_voltage_soft_off(void) {
    int err;

    (void)k380_soft_off_prepare_radio_and_led_quiet();

    if (atomic_get(&pending_reason) == K380_SHUTDOWN_LOW_VOLTAGE &&
        k380_battery_policy_state() == K380_POWER_CHARGING) {
        return -ECANCELED;
    }

    (void)k380_soft_off_flush_required_settings();

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

int k380_soft_off_request_low_voltage(void) {
    return k380_soft_off_request_reason(K380_SHUTDOWN_LOW_VOLTAGE);
}

int k380_soft_off_request_reason(enum k380_shutdown_reason reason) {
    k380_soft_off_set_pending_reason(reason);

#ifdef CONFIG_ZTEST
    const int err = start_warning();
    if (err < 0) {
        LOG_ERR("Failed to start soft-off warning (%d)", err);
        return err;
    }

    (void)k380_soft_off_test_wait_warning();
    return complete_low_voltage_soft_off();
#else
    return k380_low_power_request(reason);
#endif
}

#if !defined(CONFIG_ZTEST) && IS_ENABLED(CONFIG_SETTINGS)
static int k380_soft_off_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                      void *cb_arg) {
    if (!settings_name_steq(name, "last_shutdown_reason", NULL) ||
        len != sizeof(last_shutdown_reason)) {
        return -EINVAL;
    }

    char loaded_reason[sizeof(last_shutdown_reason)] = {0};
    const int err = read_cb(cb_arg, loaded_reason, sizeof(loaded_reason));
    if (err <= 0) {
        return err;
    }

    loaded_reason[sizeof(loaded_reason) - 1U] = '\0';
    k_mutex_lock(&latch_lock, K_FOREVER);
    memcpy(last_shutdown_reason, loaded_reason, sizeof(last_shutdown_reason));
    k_mutex_unlock(&latch_lock);
    return 0;
}

static int k380_soft_off_settings_commit(void) { return 0; }

SETTINGS_STATIC_HANDLER_DEFINE(k380, "k380", NULL, k380_soft_off_settings_set,
                               k380_soft_off_settings_commit, NULL);
#endif
