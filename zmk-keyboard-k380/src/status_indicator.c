#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk_keyboard_k380/status_indicator.h>
#include <zmk_keyboard_k380/ble_slot_policy.h>
#include <zmk_keyboard_k380/soft_off.h>

struct k380_status_model {
    bool bootloader_active;
    enum k380_status_id bootloader;
    enum k380_status_id power;
    enum k380_status_id ble;
    enum k380_status_id system;
    uint8_t ble_slot;
};

static struct k380_status_model status_model = {
    .bootloader_active = false,
    .bootloader = K380_STATUS_B1_BOOTLOADER_WAITING,
    .power = K380_STATUS_Z1_NORMAL,
    .ble = K380_STATUS_Z1_NORMAL,
    .system = K380_STATUS_Z1_NORMAL,
    .ble_slot = 1U,
};

static struct k_spinlock status_lock;
static struct led_rgb status_pixels[4];
static uint8_t animation_step;
static bool animation_stopped;
static uint32_t render_generation;
static uint32_t rendered_generation;
static bool render_initialized;
K_MUTEX_DEFINE(render_mutex);

#define K380_CHARGING_BREATH_TICKS 80U
#define K380_CHARGING_BREATH_HALF_TICKS (K380_CHARGING_BREATH_TICKS / 2U)
#define K380_CHARGING_BREATH_MIN 2U
#define K380_CHARGING_BREATH_MAX 24U
#define K380_CHARGING_EDGE_MS 50U
#define K380_SLOW_EDGE_MS 1000U
#define K380_FAST_EDGE_MS 250U
#define K380_BLUE_20_PERCENT 51U

static int render_pending_status(void);
static int submit_status_render(void);
static uint32_t model_animation_period_ms(const struct k380_status_model *model);

#if !IS_ENABLED(CONFIG_ZTEST)
#if !DT_HAS_CHOSEN(zmk_underglow)
#error "A zmk,underglow chosen node must be declared"
#endif

static const struct device *status_led_strip;
#endif

#ifdef CONFIG_ZTEST
__weak void k380_status_indicator_test_render(enum k380_status_id status,
                                              const struct led_rgb *pixels, size_t pixel_count) {
    ARG_UNUSED(status);
    ARG_UNUSED(pixels);
    ARG_UNUSED(pixel_count);
}

__weak uint8_t k380_ble_slot_current(void) { return 1U; }
__weak bool k380_status_indicator_test_use_timer(void) { return true; }
#endif

static struct led_rgb rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (struct led_rgb){
        .r = r,
        .g = g,
        .b = b,
    };
}

static void clear_pixels(void) {
    for (size_t i = 0; i < ARRAY_SIZE(status_pixels); i++) {
        status_pixels[i] = rgb(0, 0, 0);
    }
}

static uint8_t charging_breath_brightness(uint8_t animation_step) {
    const uint8_t step = animation_step % K380_CHARGING_BREATH_TICKS;
    const uint8_t position =
        step < K380_CHARGING_BREATH_HALF_TICKS ? step : K380_CHARGING_BREATH_TICKS - step - 1U;

    return K380_CHARGING_BREATH_MIN +
           ((K380_CHARGING_BREATH_MAX - K380_CHARGING_BREATH_MIN) * position) /
               (K380_CHARGING_BREATH_HALF_TICKS - 1U);
}

static uint8_t slot_led_index(uint8_t slot) {
    switch (slot) {
    case 1:
        return 2;
    case 2:
        return 1;
    case 3:
        return 0;
    default:
        return 2;
    }
}

static bool blink_is_on(uint32_t elapsed_ms, uint32_t edge_ms) {
    return ((elapsed_ms / edge_ms) % 2U) == 0U;
}

static int render_bootloader_status(enum k380_status_id status) {
    switch (status) {
    case K380_STATUS_B1_BOOTLOADER_WAITING:
        status_pixels[0] = rgb(0, 24, 0);
        status_pixels[1] = rgb(0, 24, 0);
        status_pixels[2] = rgb(0, 24, 0);
        break;
    case K380_STATUS_B2_BOOTLOADER_CDC_ONLY:
        status_pixels[0] = rgb(24, 0, 24);
        status_pixels[1] = rgb(24, 0, 24);
        status_pixels[2] = rgb(24, 0, 24);
        break;
    case K380_STATUS_B3_BOOTLOADER_WRITING:
        status_pixels[0] = rgb(24, 24, 0);
        status_pixels[1] = rgb(24, 24, 0);
        status_pixels[2] = rgb(24, 24, 0);
        break;
    case K380_STATUS_B4_BOOTLOADER_WRITE_SUCCESS:
        status_pixels[0] = rgb(0, 24, 0);
        status_pixels[1] = rgb(0, 24, 0);
        status_pixels[2] = rgb(0, 24, 0);
        break;
    case K380_STATUS_B5_BOOTLOADER_WRITE_FAILED:
        status_pixels[0] = rgb(24, 0, 0);
        status_pixels[1] = rgb(24, 0, 0);
        status_pixels[2] = rgb(24, 0, 0);
        break;
    case K380_STATUS_B6_BOOTLOADER_LOW_POWER:
        status_pixels[3] = rgb(24, 0, 0);
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int render_power_status(enum k380_status_id status, uint32_t elapsed_ms) {
    switch (status) {
    case K380_STATUS_Z1_NORMAL:
        break;
    case K380_STATUS_Z2_CHARGING: {
        const uint8_t brightness =
            charging_breath_brightness(elapsed_ms / K380_CHARGING_EDGE_MS);
        status_pixels[3] = rgb(0, brightness, brightness);
        break;
    }
    case K380_STATUS_Z3_LOW_BATTERY:
        if (blink_is_on(elapsed_ms, K380_SLOW_EDGE_MS)) {
            status_pixels[3] = rgb(24, 0, 0);
        }
        break;
    case K380_STATUS_Z4_SOFT_OFF_WARNING:
        if (blink_is_on(elapsed_ms, K380_FAST_EDGE_MS)) {
            status_pixels[3] = rgb(24, 0, 0);
        }
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int render_ble_status(enum k380_status_id status, uint8_t slot, uint32_t elapsed_ms) {
    const uint8_t index = slot_led_index(slot);

    switch (status) {
    case K380_STATUS_Z1_NORMAL:
        return 0;
    case K380_STATUS_Z5_BLE_WAITING:
        if (blink_is_on(elapsed_ms, K380_SLOW_EDGE_MS)) {
            status_pixels[index] = rgb(0, 0, K380_BLUE_20_PERCENT);
        }
        return 0;
    case K380_STATUS_Z6_BLE_CONNECTED:
        status_pixels[index] = rgb(0, 24, 0);
        return 0;
    case K380_STATUS_Z7_BLE_PAIRING:
        if (blink_is_on(elapsed_ms, K380_FAST_EDGE_MS)) {
            status_pixels[index] = rgb(0, 0, K380_BLUE_20_PERCENT);
        }
        return 0;
    default:
        return -EINVAL;
    }
}

static int render_system_status(enum k380_status_id status) {
    switch (status) {
    case K380_STATUS_Z1_NORMAL:
        return 0;
    case K380_STATUS_Z8_BOOTLOADER_REQUEST:
        status_pixels[0] = rgb(24, 0, 24);
        status_pixels[1] = rgb(24, 0, 24);
        status_pixels[2] = rgb(24, 0, 24);
        return 0;
    case K380_STATUS_Z9_MATRIX_FAULT:
        status_pixels[0] = rgb(24, 24, 0);
        status_pixels[1] = rgb(24, 24, 0);
        status_pixels[2] = rgb(24, 24, 0);
        return 0;
    default:
        return -EINVAL;
    }
}

static uint32_t model_animation_period_ms(const struct k380_status_model *model) {
    if (model->bootloader_active) {
        return 0U;
    }

    uint32_t period = 0U;
    if (model->power == K380_STATUS_Z2_CHARGING) {
        period = K380_CHARGING_EDGE_MS;
    } else if (model->power == K380_STATUS_Z3_LOW_BATTERY) {
        period = K380_SLOW_EDGE_MS;
    } else if (model->power == K380_STATUS_Z4_SOFT_OFF_WARNING) {
        period = K380_FAST_EDGE_MS;
    }

    /* System indications obscure the BLE LEDs, but not the power LED. */
    if (model->system == K380_STATUS_Z1_NORMAL) {
        uint32_t ble_period = 0U;
        if (model->ble == K380_STATUS_Z5_BLE_WAITING) {
            ble_period = K380_SLOW_EDGE_MS;
        } else if (model->ble == K380_STATUS_Z7_BLE_PAIRING) {
            ble_period = K380_FAST_EDGE_MS;
        }
        if (ble_period != 0U && (period == 0U || ble_period < period)) {
            period = ble_period;
        }
    }
    return period;
}

static bool model_uses_animation(const struct k380_status_model *model) {
    return model_animation_period_ms(model) != 0U;
}

static void render_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    (void)render_pending_status();
}

K_WORK_DEFINE(render_work, render_work_handler);

static void animation_timer_handler(struct k_timer *timer) {
    ARG_UNUSED(timer);

    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool animate = !animation_stopped && model_uses_animation(&status_model);
    if (animate) {
        animation_step = (animation_step + 1U) % K380_CHARGING_BREATH_TICKS;
        render_generation++;
        k_work_submit(&render_work);
    }
    k_spin_unlock(&status_lock, key);
}

K_TIMER_DEFINE(animation_timer, animation_timer_handler, NULL);

static void update_animation_timer(void) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    uint32_t period =
        animation_stopped ? 0U : model_animation_period_ms(&status_model);
#ifdef CONFIG_ZTEST
    if (!k380_status_indicator_test_use_timer()) {
        period = 0U;
    }
#endif
    if (period != 0U) {
        k_timer_start(&animation_timer, K_MSEC(period), K_MSEC(period));
    } else {
        k_timer_stop(&animation_timer);
        (void)k_work_cancel(&render_work);
    }
    k_spin_unlock(&status_lock, key);
}

void k380_status_indicator_animation_step(void) {
    bool animate;
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    animate = !animation_stopped && model_uses_animation(&status_model);
    if (animate) {
        animation_step = (animation_step + 1U) % K380_CHARGING_BREATH_TICKS;
        render_generation++;
    }
    k_spin_unlock(&status_lock, key);

    if (!animate) {
        return;
    }

#if IS_ENABLED(CONFIG_ZTEST)
    (void)render_pending_status();
#else
    (void)submit_status_render();
#endif
}

static int render_model(enum k380_status_id primary, const struct k380_status_model *model,
                        uint8_t step) {
    const uint32_t elapsed_ms = step * model_animation_period_ms(model);
    clear_pixels();

    if (model->bootloader_active) {
        int err = render_bootloader_status(model->bootloader);
        if (err < 0) {
            return err;
        }
    } else {
        int err = render_power_status(model->power, elapsed_ms);
        if (err < 0) {
            return err;
        }

        err = render_ble_status(model->ble, model->ble_slot, elapsed_ms);
        if (err < 0) {
            return err;
        }

        err = render_system_status(model->system);
        if (err < 0) {
            return err;
        }
    }

#if IS_ENABLED(CONFIG_ZTEST)
    k380_status_indicator_test_render(primary, status_pixels, ARRAY_SIZE(status_pixels));
    return 0;
#else
    if (status_led_strip == NULL || !device_is_ready(status_led_strip)) {
        return -ENODEV;
    }

    return led_strip_update_rgb(status_led_strip, status_pixels, ARRAY_SIZE(status_pixels));
#endif
}

static bool is_bootloader_status(enum k380_status_id status) {
    return status >= K380_STATUS_B1_BOOTLOADER_WAITING &&
           status <= K380_STATUS_B6_BOOTLOADER_LOW_POWER;
}

static bool is_zmk_status(enum k380_status_id status) {
    return status >= K380_STATUS_Z1_NORMAL && status <= K380_STATUS_Z9_MATRIX_FAULT;
}

static bool is_power_status(enum k380_status_id status) {
    return status == K380_STATUS_Z2_CHARGING || status == K380_STATUS_Z3_LOW_BATTERY ||
           status == K380_STATUS_Z4_SOFT_OFF_WARNING;
}

static bool is_ble_status(enum k380_status_id status) {
    return status == K380_STATUS_Z5_BLE_WAITING || status == K380_STATUS_Z6_BLE_CONNECTED ||
           status == K380_STATUS_Z7_BLE_PAIRING;
}

static bool is_system_status(enum k380_status_id status) {
    return status == K380_STATUS_Z8_BOOTLOADER_REQUEST || status == K380_STATUS_Z9_MATRIX_FAULT;
}

static int bootloader_status_priority(enum k380_status_id status) {
    switch (status) {
    case K380_STATUS_B1_BOOTLOADER_WAITING:
        return 1;
    case K380_STATUS_B2_BOOTLOADER_CDC_ONLY:
        return 2;
    case K380_STATUS_B4_BOOTLOADER_WRITE_SUCCESS:
        return 3;
    case K380_STATUS_B5_BOOTLOADER_WRITE_FAILED:
        return 4;
    case K380_STATUS_B6_BOOTLOADER_LOW_POWER:
        return 5;
    case K380_STATUS_B3_BOOTLOADER_WRITING:
        return 6;
    default:
        return -EINVAL;
    }
}

static int zmk_status_priority(enum k380_status_id status) {
    switch (status) {
    case K380_STATUS_Z1_NORMAL:
        return 0;
    case K380_STATUS_Z6_BLE_CONNECTED:
        return 20;
    case K380_STATUS_Z5_BLE_WAITING:
        return 30;
    case K380_STATUS_Z7_BLE_PAIRING:
        return 40;
    case K380_STATUS_Z3_LOW_BATTERY:
        return 50;
    case K380_STATUS_Z2_CHARGING:
        return 60;
    case K380_STATUS_Z8_BOOTLOADER_REQUEST:
        return 70;
    case K380_STATUS_Z9_MATRIX_FAULT:
        return 80;
    case K380_STATUS_Z4_SOFT_OFF_WARNING:
        return 90;
    default:
        return -EINVAL;
    }
}

static enum k380_status_id model_primary_status(const struct k380_status_model *model) {
    if (model->bootloader_active) {
        return model->bootloader;
    }

    enum k380_status_id primary = K380_STATUS_Z1_NORMAL;
    int primary_priority = zmk_status_priority(primary);
    const enum k380_status_id candidates[] = {model->power, model->ble, model->system};

    for (size_t i = 0; i < ARRAY_SIZE(candidates); i++) {
        const int priority = zmk_status_priority(candidates[i]);
        if (priority > primary_priority) {
            primary = candidates[i];
            primary_priority = priority;
        }
    }

    return primary;
}

static void reset_zmk_model(struct k380_status_model *model) {
    model->bootloader_active = false;
    model->power = K380_STATUS_Z1_NORMAL;
    model->ble = K380_STATUS_Z1_NORMAL;
    model->system = K380_STATUS_Z1_NORMAL;
    model->ble_slot = 1U;
}

static bool channel_should_replace(enum k380_status_id current, enum k380_status_id next) {
    return zmk_status_priority(next) >= zmk_status_priority(current);
}

static int apply_zmk_status(struct k380_status_model *model, enum k380_status_id status,
                            uint8_t ble_slot) {
    model->bootloader_active = false;

    if (status == K380_STATUS_Z1_NORMAL) {
        reset_zmk_model(model);
        return 0;
    }

    if (is_power_status(status)) {
        if (channel_should_replace(model->power, status)) {
            model->power = status;
        }
        return 0;
    }

    if (is_ble_status(status)) {
        if (channel_should_replace(model->ble, status)) {
            model->ble = status;
            model->ble_slot = ble_slot;
        }
        return 0;
    }

    if (is_system_status(status)) {
        if (channel_should_replace(model->system, status)) {
            model->system = status;
        }
        return 0;
    }

    return -EINVAL;
}

static void mark_render_needed(bool reset_animation) {
    if (reset_animation) {
        animation_step = 0U;
    }
    render_generation++;
}

static int submit_status_render(void) {
#if IS_ENABLED(CONFIG_ZTEST)
    return render_pending_status();
#else
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    if (!animation_stopped) {
        k_work_submit(&render_work);
    }
    k_spin_unlock(&status_lock, key);
    return 0;
#endif
}

int k380_status_indicator_set(enum k380_status_id status) {
    if (!is_bootloader_status(status) && !is_zmk_status(status)) {
        return -EINVAL;
    }

    bool changed = false;
    bool reset_animation = false;
    const uint8_t ble_slot = is_ble_status(status) ? k380_ble_slot_current() : 1U;
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const struct k380_status_model previous = status_model;

    if (is_bootloader_status(status)) {
        if (!status_model.bootloader_active ||
            bootloader_status_priority(status) >
                bootloader_status_priority(status_model.bootloader)) {
            status_model.bootloader_active = true;
            status_model.bootloader = status;
            changed = true;
            reset_animation = true;
        }
    } else {
        const int err = apply_zmk_status(&status_model, status, ble_slot);
        if (err < 0) {
            k_spin_unlock(&status_lock, key);
            return err;
        }

        changed = previous.bootloader_active != status_model.bootloader_active ||
                  previous.power != status_model.power || previous.ble != status_model.ble ||
                  previous.system != status_model.system ||
                  previous.ble_slot != status_model.ble_slot;
        reset_animation = previous.bootloader_active != status_model.bootloader_active ||
                          previous.power != status_model.power ||
                          previous.ble != status_model.ble ||
                          previous.system != status_model.system ||
                          previous.ble_slot != status_model.ble_slot;
    }

    if (changed) {
        mark_render_needed(reset_animation);
    }
    k_spin_unlock(&status_lock, key);

    if (changed) {
        update_animation_timer();
    }
    return changed ? submit_status_render() : 0;
}

void k380_status_indicator_clear(enum k380_status_id status) {
    bool changed = false;
    bool reset_animation = false;
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const struct k380_status_model previous = status_model;

    if (is_bootloader_status(status)) {
        if (status_model.bootloader_active && status_model.bootloader == status) {
            status_model.bootloader_active = false;
            status_model.bootloader = K380_STATUS_B1_BOOTLOADER_WAITING;
            changed = true;
            reset_animation = true;
        }
    } else if (is_power_status(status) && status_model.power == status) {
        status_model.power = K380_STATUS_Z1_NORMAL;
        changed = true;
        reset_animation = true;
    } else if (is_ble_status(status) && status_model.ble == status) {
        status_model.ble = K380_STATUS_Z1_NORMAL;
        changed = true;
        reset_animation = true;
    } else if (is_system_status(status) && status_model.system == status) {
        status_model.system = K380_STATUS_Z1_NORMAL;
        changed = true;
        reset_animation = true;
    } else if (status == K380_STATUS_Z1_NORMAL) {
        reset_zmk_model(&status_model);
        changed = previous.bootloader_active != status_model.bootloader_active ||
                  previous.power != status_model.power || previous.ble != status_model.ble ||
                  previous.system != status_model.system ||
                  previous.ble_slot != status_model.ble_slot;
        reset_animation = true;
    }

    if (changed) {
        mark_render_needed(reset_animation);
    }
    k_spin_unlock(&status_lock, key);

    if (changed) {
        update_animation_timer();
        (void)submit_status_render();
    }
}

enum k380_status_id k380_status_indicator_current(void) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const enum k380_status_id status = model_primary_status(&status_model);
    k_spin_unlock(&status_lock, key);

    return status;
}

static int render_pending_status(void) {
    int err = 0;
    k_mutex_lock(&render_mutex, K_FOREVER);

    for (;;) {
        struct k380_status_model snapshot;
        enum k380_status_id primary;
        uint8_t step;
        uint32_t generation;

        k_spinlock_key_t key = k_spin_lock(&status_lock);
        if (animation_stopped ||
            (render_initialized && rendered_generation == render_generation)) {
            k_spin_unlock(&status_lock, key);
            break;
        }

        snapshot = status_model;
        step = animation_step;
        primary = model_primary_status(&snapshot);
        generation = render_generation;
        k_spin_unlock(&status_lock, key);

        err = render_model(primary, &snapshot, step);

        key = k_spin_lock(&status_lock);
        rendered_generation = generation;
        render_initialized = true;
        const bool done = render_generation == generation;
        k_spin_unlock(&status_lock, key);

        if (done) {
            break;
        }
    }

    k_mutex_unlock(&render_mutex);
    return err;
}

bool k380_status_indicator_animation_active(void) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool active = !animation_stopped && model_uses_animation(&status_model);
    k_spin_unlock(&status_lock, key);
    return active;
}

void k380_status_indicator_stop_animation(void) {
    /* Serialize with an in-flight LED write; queued renders observe the quiet gate. */
    k_mutex_lock(&render_mutex, K_FOREVER);
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool already_stopped = animation_stopped;
    animation_stopped = true;
    animation_step = 0U;
    render_generation++;
    k_timer_stop(&animation_timer);
    (void)k_work_cancel(&render_work);
    k_spin_unlock(&status_lock, key);

    if (!already_stopped) {
        struct k380_status_model quiet_model = {0};
        reset_zmk_model(&quiet_model);
        (void)render_model(K380_STATUS_Z1_NORMAL, &quiet_model, 0U);
    }
    k_mutex_unlock(&render_mutex);
}

int k380_soft_off_stop_animation(void) {
    k380_status_indicator_stop_animation();
    return 0;
}

void k380_status_indicator_resume_animation(void) {
    k_mutex_lock(&render_mutex, K_FOREVER);
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool was_stopped = animation_stopped;
    if (was_stopped) {
        animation_stopped = false;
        mark_render_needed(true);
    }
    k_spin_unlock(&status_lock, key);
    k_mutex_unlock(&render_mutex);

    if (was_stopped) {
        update_animation_timer();
        (void)submit_status_render();
    }
}

#if !IS_ENABLED(CONFIG_ZTEST)
static int k380_status_indicator_init(void) {
    status_led_strip = DEVICE_DT_GET(DT_CHOSEN(zmk_underglow));
    return render_pending_status();
}

SYS_INIT(k380_status_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif
