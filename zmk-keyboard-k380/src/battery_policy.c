#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/soft_off.h>
#include <zmk_keyboard_k380/status_indicator.h>

#if IS_ENABLED(CONFIG_K380_BATTERY_POLICY_RUNTIME)
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

#define K380_BATTERY_WINDOW_SIZE 5U
#define K380_BATTERY_DEBOUNCE_SAMPLES 3U
#define K380_USB_POWER_PRESENT_MV 4500U
#define K380_LOW_BATTERY_ENTER_MV 3400U
#define K380_LOW_BATTERY_EXIT_MV 3500U
#define K380_SOFT_OFF_ENTER_MV 3200U
#define K380_SOFT_OFF_EXIT_MV 3300U
#define K380_VDDH_MIN_VALID_MV 1000U
#define K380_VDDH_MAX_VALID_MV 6000U
#define K380_STARTUP_SAMPLE_LIMIT 3U

static uint16_t samples[K380_BATTERY_WINDOW_SIZE];
static uint8_t sample_count;
static uint8_t next_sample;
static uint8_t low_battery_hits;
static uint8_t low_battery_recovery_hits;
static uint8_t soft_off_hits;
static uint8_t soft_off_recovery_hits;
static enum k380_power_state power_state = K380_POWER_NORMAL;
K_MUTEX_DEFINE(battery_policy_lock);
K_MUTEX_DEFINE(battery_sample_lock);
#if IS_ENABLED(CONFIG_K380_BATTERY_POLICY_RUNTIME)
K_MUTEX_DEFINE(qualification_lock);
static bool qualification_pending;
static bool qualification_requires_recovery;
static uint8_t qualification_recovery_hits;
static int64_t last_latch_clear_attempt;
#define K380_LATCH_CLEAR_RETRY_MS 60000
static void qualify_submitted_sample(uint16_t mv);
#else
static void qualify_submitted_sample(uint16_t mv) { ARG_UNUSED(mv); }
#endif

__weak void k380_ble_slot_power_state_changed(void) {}
__weak void k380_low_power_power_state_changed(bool charging) { ARG_UNUSED(charging); }

static uint16_t average_mv(void) {
    uint32_t sum = 0;

    if (sample_count == 0U) {
        return 0;
    }

    for (uint8_t i = 0; i < sample_count; i++) {
        sum += samples[i];
    }

    return (uint16_t)(sum / sample_count);
}

static void set_power_state(enum k380_power_state state) {
    if (state == power_state) {
        return;
    }

    k380_status_indicator_clear(K380_STATUS_Z2_CHARGING);
    k380_status_indicator_clear(K380_STATUS_Z3_LOW_BATTERY);
    k380_status_indicator_clear(K380_STATUS_Z4_SOFT_OFF_WARNING);

    low_battery_hits = 0;
    low_battery_recovery_hits = 0;
    soft_off_hits = 0;
    soft_off_recovery_hits = 0;
    power_state = state;
    k380_low_power_power_state_changed(state == K380_POWER_CHARGING);

    switch (state) {
    case K380_POWER_CHARGING:
        (void)k380_status_indicator_set(K380_STATUS_Z2_CHARGING);
        break;
    case K380_POWER_LOW_BATTERY:
        (void)k380_status_indicator_set(K380_STATUS_Z3_LOW_BATTERY);
        break;
    case K380_POWER_SOFT_OFF_WARNING_REQUESTED:
        (void)k380_status_indicator_set(K380_STATUS_Z4_SOFT_OFF_WARNING);
        break;
    case K380_POWER_NORMAL:
        break;
    }
}

static bool debounced(uint8_t *hits) {
    if (*hits < K380_BATTERY_DEBOUNCE_SAMPLES) {
        (*hits)++;
    }

    return *hits == K380_BATTERY_DEBOUNCE_SAMPLES;
}

int k380_battery_policy_submit_mv(uint16_t vddh_mv) {
    if (vddh_mv < K380_VDDH_MIN_VALID_MV || vddh_mv > K380_VDDH_MAX_VALID_MV) {
        return -EINVAL;
    }

#if IS_ENABLED(CONFIG_K380_SOFT_OFF)
    bool request_soft_off;
    uint32_t request_generation;
#endif
    k_mutex_lock(&battery_policy_lock, K_FOREVER);
    const enum k380_power_state previous_state = power_state;

    samples[next_sample] = vddh_mv;
    next_sample = (next_sample + 1U) % K380_BATTERY_WINDOW_SIZE;
    if (sample_count < K380_BATTERY_WINDOW_SIZE) {
        sample_count++;
    }

    if (vddh_mv > K380_USB_POWER_PRESENT_MV) {
        low_battery_hits = 0;
        low_battery_recovery_hits = 0;
        soft_off_hits = 0;
        soft_off_recovery_hits = 0;
        set_power_state(K380_POWER_CHARGING);
        k_mutex_unlock(&battery_policy_lock);
        if (previous_state != K380_POWER_CHARGING) {
            k380_ble_slot_power_state_changed();
        }
        qualify_submitted_sample(vddh_mv);
        return 0;
    }

    const uint16_t mv = average_mv();

    if (power_state == K380_POWER_CHARGING) {
        set_power_state(K380_POWER_NORMAL);
    }

    switch (power_state) {
    case K380_POWER_NORMAL:
        if (mv < K380_SOFT_OFF_ENTER_MV) {
            if (debounced(&soft_off_hits)) {
                set_power_state(K380_POWER_SOFT_OFF_WARNING_REQUESTED);
            }
        } else {
            soft_off_hits = 0;
        }

        if (power_state == K380_POWER_NORMAL && mv < K380_LOW_BATTERY_ENTER_MV) {
            if (debounced(&low_battery_hits)) {
                set_power_state(K380_POWER_LOW_BATTERY);
            }
        } else {
            low_battery_hits = 0;
        }
        break;
    case K380_POWER_LOW_BATTERY:
        if (mv < K380_SOFT_OFF_ENTER_MV) {
            if (debounced(&soft_off_hits)) {
                set_power_state(K380_POWER_SOFT_OFF_WARNING_REQUESTED);
            }
        } else {
            soft_off_hits = 0;
        }

        if (power_state == K380_POWER_LOW_BATTERY && mv >= K380_LOW_BATTERY_EXIT_MV) {
            if (debounced(&low_battery_recovery_hits)) {
                set_power_state(K380_POWER_NORMAL);
            }
        } else {
            low_battery_recovery_hits = 0;
        }
        break;
    case K380_POWER_SOFT_OFF_WARNING_REQUESTED:
        if (mv >= K380_SOFT_OFF_EXIT_MV) {
            if (debounced(&soft_off_recovery_hits)) {
                set_power_state(K380_POWER_NORMAL);
            }
        } else {
            soft_off_recovery_hits = 0;
        }
        break;
    case K380_POWER_CHARGING:
        break;
    }

#if IS_ENABLED(CONFIG_K380_SOFT_OFF)
    request_soft_off = previous_state != K380_POWER_SOFT_OFF_WARNING_REQUESTED &&
                       power_state == K380_POWER_SOFT_OFF_WARNING_REQUESTED;
    request_generation = request_soft_off ? k380_low_power_voltage_generation() : 0U;
#endif
    const enum k380_power_state submitted_state = power_state;
    k_mutex_unlock(&battery_policy_lock);

    if (previous_state != submitted_state) {
        k380_ble_slot_power_state_changed();
    }

#if IS_ENABLED(CONFIG_K380_SOFT_OFF)
    if (request_soft_off) {
        (void)k380_low_power_request_low_voltage_at_generation(request_generation);
    }
#endif
    qualify_submitted_sample(vddh_mv);
    return 0;
}

enum k380_power_state k380_battery_policy_state(void) {
    k_mutex_lock(&battery_policy_lock, K_FOREVER);
    const enum k380_power_state state = power_state;
    k_mutex_unlock(&battery_policy_lock);

    return state;
}

bool k380_battery_policy_is_battery_powered(void) {
    return k380_battery_policy_state() != K380_POWER_CHARGING;
}

bool k380_battery_policy_voltage_safe_for_startup(void) {
    k_mutex_lock(&battery_policy_lock, K_FOREVER);
    const bool safe = power_state == K380_POWER_CHARGING ||
                      (sample_count > 0U && average_mv() >= K380_SOFT_OFF_EXIT_MV);
    k_mutex_unlock(&battery_policy_lock);
    return safe;
}

static bool startup_recovery_sample(uint16_t mv, uint8_t *recovery_hits) {
    if (mv >= K380_SOFT_OFF_EXIT_MV) {
        if (*recovery_hits < K380_STARTUP_SAMPLE_LIMIT) {
            (*recovery_hits)++;
        }
    } else {
        *recovery_hits = 0U;
    }

    return *recovery_hits >= K380_STARTUP_SAMPLE_LIMIT;
}

#ifdef CONFIG_ZTEST
bool k380_battery_policy_test_startup_recovery_sample(uint16_t mv, uint8_t *recovery_hits) {
    return startup_recovery_sample(mv, recovery_hits);
}
#endif

#if IS_ENABLED(CONFIG_K380_BATTERY_POLICY_RUNTIME)
static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

static void qualify_submitted_sample(uint16_t mv) {
    k_mutex_lock(&qualification_lock, K_FOREVER);
    if (!qualification_pending) {
        k_mutex_unlock(&qualification_lock);
        return;
    }
    const bool recovered = startup_recovery_sample(mv, &qualification_recovery_hits);
    const bool safe = mv > K380_USB_POWER_PRESENT_MV ||
                      (qualification_requires_recovery ? recovered : mv >= K380_SOFT_OFF_ENTER_MV);
    const int64_t now = k_uptime_get();
    if (safe && now - last_latch_clear_attempt >= K380_LATCH_CLEAR_RETRY_MS) {
        last_latch_clear_attempt = now;
        k380_low_power_publish_voltage_recovery();
        if (k380_soft_off_clear_low_voltage_latch_if_safe(true) == 0) {
            qualification_pending = false;
            (void)k380_low_power_startup_voltage_result(true, false, true);
        }
    }
    k_mutex_unlock(&qualification_lock);
}

int k380_battery_policy_sample_now_sync(uint16_t *vddh_mv) {
    if (vddh_mv == NULL || !device_is_ready(battery)) {
        return -ENODEV;
    }

    k_mutex_lock(&battery_sample_lock, K_FOREVER);
    struct sensor_value voltage;
    int rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_VOLTAGE);
    if (rc == 0) {
        rc = sensor_channel_get(battery, SENSOR_CHAN_VOLTAGE, &voltage);
    }
    k_mutex_unlock(&battery_sample_lock);
    if (rc != 0) {
        return rc;
    }

    const int64_t mv = (int64_t)voltage.val1 * 1000 + voltage.val2 / 1000;
    if (mv < K380_VDDH_MIN_VALID_MV || mv > K380_VDDH_MAX_VALID_MV) {
        return -EINVAL;
    }

    *vddh_mv = (uint16_t)mv;
    return 0;
}

static void sample_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    uint16_t mv;
    if (k380_battery_policy_sample_now_sync(&mv) == 0) {
        (void)k380_battery_policy_submit_mv(mv);
    }
}

K_WORK_DEFINE(sample_work, sample_work_handler);

void k380_battery_policy_sample_now(void) {
    if (device_is_ready(battery)) {
        k_work_submit(&sample_work);
    }
}

int k380_battery_policy_startup_qualify(void) {
    const int64_t deadline = k_uptime_get() + CONFIG_K380_STARTUP_QUALIFICATION_BUDGET_MS;
    k_mutex_lock(&qualification_lock, K_FOREVER);
    qualification_pending = true;
    qualification_requires_recovery = k380_soft_off_has_low_voltage_latch();
    qualification_recovery_hits = 0U;
    last_latch_clear_attempt = k_uptime_get() - K380_LATCH_CLEAR_RETRY_MS;
    k_mutex_unlock(&qualification_lock);
    uint8_t valid_samples = 0U;

    while (valid_samples < K380_STARTUP_SAMPLE_LIMIT && k_uptime_get() <= deadline) {
        uint16_t mv;
        if (k380_battery_policy_sample_now_sync(&mv) != 0) {
            break;
        }

        valid_samples++;
        if (k380_battery_policy_submit_mv(mv) != 0) {
            break;
        }
        k_mutex_lock(&qualification_lock, K_FOREVER);
        const bool qualified = !qualification_pending;
        k_mutex_unlock(&qualification_lock);
        if (qualified) {
            return 0;
        }
    }

    /* Later USB/activity/periodic samples continue the same qualification. */
    return -EACCES;
}

static int k380_battery_policy_usb_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    k380_battery_policy_sample_now();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(k380_battery_policy_usb_listener, k380_battery_policy_usb_listener);
ZMK_SUBSCRIPTION(k380_battery_policy_usb_listener, zmk_usb_conn_state_changed);

static int k380_battery_policy_init(void) {
    return 0;
}

SYS_INIT(k380_battery_policy_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#else
void k380_battery_policy_sample_now(void) {}
int k380_battery_policy_sample_now_sync(uint16_t *vddh_mv) {
    ARG_UNUSED(vddh_mv);
    return -ENOTSUP;
}
int k380_battery_policy_startup_qualify(void) { return -ENOTSUP; }
#endif
