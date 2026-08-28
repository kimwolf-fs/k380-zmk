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
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
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

static uint16_t samples[K380_BATTERY_WINDOW_SIZE];
static uint8_t sample_count;
static uint8_t next_sample;
static uint8_t low_battery_hits;
static uint8_t low_battery_recovery_hits;
static uint8_t soft_off_hits;
static uint8_t soft_off_recovery_hits;
static enum k380_power_state power_state = K380_POWER_NORMAL;
K_MUTEX_DEFINE(battery_policy_lock);

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
#endif
    k_mutex_lock(&battery_policy_lock, K_FOREVER);
#if IS_ENABLED(CONFIG_K380_SOFT_OFF)
    const enum k380_power_state previous_state = power_state;
#endif

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
#endif
    k_mutex_unlock(&battery_policy_lock);

#if IS_ENABLED(CONFIG_K380_SOFT_OFF)
    if (request_soft_off) {
        (void)k380_soft_off_request_low_voltage();
    }
#endif
    return 0;
}

enum k380_power_state k380_battery_policy_state(void) {
    k_mutex_lock(&battery_policy_lock, K_FOREVER);
    const enum k380_power_state state = power_state;
    k_mutex_unlock(&battery_policy_lock);

    return state;
}

#if IS_ENABLED(CONFIG_K380_BATTERY_POLICY_RUNTIME)
static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

static void sample_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    struct sensor_value voltage;
    int rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_VOLTAGE);
    if (rc == 0) {
        rc = sensor_channel_get(battery, SENSOR_CHAN_VOLTAGE, &voltage);
    }
    if (rc == 0) {
        const int32_t mv = voltage.val1 * 1000 + voltage.val2 / 1000;
        if (mv > 0 && mv <= UINT16_MAX) {
            (void)k380_battery_policy_submit_mv((uint16_t)mv);
        }
    }
}

K_WORK_DEFINE(sample_work, sample_work_handler);

void k380_battery_policy_sample_now(void) {
    if (device_is_ready(battery)) {
        k_work_submit(&sample_work);
    }
}

static int k380_battery_policy_usb_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    k380_battery_policy_sample_now();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(k380_battery_policy_usb_listener, k380_battery_policy_usb_listener);
ZMK_SUBSCRIPTION(k380_battery_policy_usb_listener, zmk_usb_conn_state_changed);

static int k380_battery_policy_activity_listener(const zmk_event_t *eh) {
    if (as_zmk_activity_state_changed(eh)->state == ZMK_ACTIVITY_ACTIVE) {
        k380_battery_policy_sample_now();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(k380_battery_policy_activity_listener, k380_battery_policy_activity_listener);
ZMK_SUBSCRIPTION(k380_battery_policy_activity_listener, zmk_activity_state_changed);

static int k380_battery_policy_init(void) {
    k380_battery_policy_sample_now();
    return 0;
}

SYS_INIT(k380_battery_policy_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#else
void k380_battery_policy_sample_now(void) {}
#endif
