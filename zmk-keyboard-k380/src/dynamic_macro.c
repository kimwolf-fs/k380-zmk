#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_macro.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

bool zmk_hid_keyboard_is_pressed(zmk_key_t code);

#define K380_DYNAMIC_MACRO_WAIT_SLICE_MS 10U

struct k380_dynamic_macro_runner {
    bool running;
    uint8_t preset;
    uint8_t slot;
    uint32_t macro_held_usage_bitmap[8];
    atomic_t stop_requested;
};

static struct k380_dynamic_macro_runner runner;
static struct k380_dynamic_macro queued_macro;
static uint8_t queued_trigger;
static bool hold_key_pressed;
static uint8_t physical_usage_counts[256];
K_MUTEX_DEFINE(runner_lock);
K_SEM_DEFINE(macro_stop_signal, 0, 1);

struct macro_lookup_context {
    uint8_t preset;
    uint8_t slot;
    uint8_t trigger;
    struct k380_dynamic_macro macro;
};

static void dynamic_macro_work_handler(struct k_work *work);
K_WORK_DEFINE(dynamic_macro_work, dynamic_macro_work_handler);

static bool stop_requested(void);

static bool is_keyboard_keypad_usage(uint16_t usage) {
    return usage >= HID_USAGE_KEY_KEYBOARD_A &&
           usage <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI;
}

static bool is_macro_held(uint16_t usage) {
    return (runner.macro_held_usage_bitmap[usage / 32U] & BIT(usage % 32U)) != 0U;
}

static void set_macro_held(uint16_t usage, bool held) {
    WRITE_BIT(runner.macro_held_usage_bitmap[usage / 32U], usage % 32U, held);
}

static int emit_keyboard_usage(uint16_t usage, bool pressed) {
    return raise_zmk_keycode_state_changed_from_encoded(
        ZMK_HID_USAGE(HID_USAGE_KEY, usage), pressed, k_uptime_get());
}

static bool is_physically_held(uint16_t usage) {
    return physical_usage_counts[usage] != 0U ||
           (!is_macro_held(usage) && zmk_hid_keyboard_is_pressed(usage));
}

static int press_usage(uint16_t usage) {
    if (!is_keyboard_keypad_usage(usage)) {
        return -EINVAL;
    }

    k_mutex_lock(&runner_lock, K_FOREVER);
    if (stop_requested()) {
        k_mutex_unlock(&runner_lock);
        return -ECANCELED;
    }
    if (is_macro_held(usage) || is_physically_held(usage)) {
        k_mutex_unlock(&runner_lock);
        return 0;
    }

    const int err = emit_keyboard_usage(usage, true);
    if (err == 0) {
        set_macro_held(usage, true);
    }
    k_mutex_unlock(&runner_lock);
    return err;
}

static int release_usage_locked(uint16_t usage) {
    if (!is_keyboard_keypad_usage(usage)) {
        return -EINVAL;
    }

    if (!is_macro_held(usage)) {
        return 0;
    }

    if (physical_usage_counts[usage] != 0U) {
        set_macro_held(usage, false);
        return 0;
    }

    const int err = emit_keyboard_usage(usage, false);
    if (err == 0) {
        set_macro_held(usage, false);
    }
    return err;
}

static int release_usage(uint16_t usage) {
    int err;

    k_mutex_lock(&runner_lock, K_FOREVER);
    err = release_usage_locked(usage);
    k_mutex_unlock(&runner_lock);
    return err;
}

static int release_all_macro_held_locked(void) {
    int first_error = 0;

    for (uint16_t usage = HID_USAGE_KEY_KEYBOARD_A;
         usage <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI; usage++) {
        const int err = release_usage_locked(usage);
        if (first_error == 0 && err != 0) {
            first_error = err;
        }
    }
    return first_error;
}

static int release_all_macro_held(void) {
    k_mutex_lock(&runner_lock, K_FOREVER);
    const int err = release_all_macro_held_locked();
    k_mutex_unlock(&runner_lock);
    return err;
}

static bool stop_requested(void) { return atomic_get(&runner.stop_requested) != 0; }

static int interruptible_wait(uint32_t duration_ms) {
    while (duration_ms > 0U) {
        if (stop_requested()) {
            return -ECANCELED;
        }

        const uint32_t slice_ms = MIN(duration_ms, K380_DYNAMIC_MACRO_WAIT_SLICE_MS);
        const int err = k_sem_take(&macro_stop_signal, K_MSEC(slice_ms));
        if (err == 0) {
            return -ECANCELED;
        }
        if (err != -EAGAIN) {
            return err;
        }
        duration_ms -= slice_ms;
    }

    return stop_requested() ? -ECANCELED : 0;
}

static uint32_t random_wait_ms(const struct k380_dynamic_macro_step *step) {
    const uint32_t min_ms = step->value.wait_random.min_ms;
    const uint32_t max_ms = step->value.wait_random.max_ms;

    return min_ms + (sys_rand32_get() % (max_ms - min_ms + 1U));
}

static int execute_pass(const struct k380_dynamic_macro *macro) {
    for (uint8_t step_index = 0; step_index < macro->step_count; step_index++) {
        if (stop_requested()) {
            return -ECANCELED;
        }

        const struct k380_dynamic_macro_step *step = &macro->steps[step_index];
        int err = 0;

        switch (step->type) {
        case K380_DYNAMIC_MACRO_PRESS_KEY:
            err = press_usage(step->value.key_usage);
            break;
        case K380_DYNAMIC_MACRO_RELEASE_KEY:
            err = release_usage(step->value.key_usage);
            break;
        case K380_DYNAMIC_MACRO_TAP_KEY:
            err = press_usage(step->value.key_usage);
            if (err == 0) {
                err = release_usage(step->value.key_usage);
            }
            break;
        case K380_DYNAMIC_MACRO_WAIT_MS:
            err = interruptible_wait(step->value.wait_ms);
            break;
        case K380_DYNAMIC_MACRO_WAIT_RANDOM:
            err = interruptible_wait(random_wait_ms(step));
            break;
        case K380_DYNAMIC_MACRO_RELEASE_ALL:
            err = release_all_macro_held();
            break;
        default:
            return -EINVAL;
        }

        if (err != 0) {
            return err;
        }
    }

    return 0;
}

static int run_queued_macro(void) {
    uint32_t passes_remaining = queued_trigger == K380_DYNAMIC_MACRO_TRIGGER_COUNT
                                    ? queued_macro.count
                                    : 1U;

    do {
        if (execute_pass(&queued_macro) != 0) {
            break;
        }

        if (queued_trigger == K380_DYNAMIC_MACRO_TRIGGER_ONCE ||
            queued_trigger == K380_DYNAMIC_MACRO_TRIGGER_COUNT) {
            if (--passes_remaining == 0U) {
                break;
            }
        }

        if (queued_macro.step_count == 0U) {
            k_sleep(K_MSEC(1));
        }
    } while (!stop_requested() &&
             (queued_trigger != K380_DYNAMIC_MACRO_TRIGGER_HOLD || hold_key_pressed));

    return release_all_macro_held();
}

static void dynamic_macro_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    (void)run_queued_macro();

    k_mutex_lock(&runner_lock, K_FOREVER);
    runner.running = false;
    k_mutex_unlock(&runner_lock);
}

static int start_macro(uint8_t preset, uint8_t slot,
                       const struct k380_dynamic_macro *macro, uint8_t trigger) {
    k_mutex_lock(&runner_lock, K_FOREVER);
    if (runner.running) {
        k_mutex_unlock(&runner_lock);
        return 0;
    }

    const int cleanup_err = release_all_macro_held_locked();
    if (cleanup_err != 0) {
        k_mutex_unlock(&runner_lock);
        return cleanup_err;
    }
    runner.running = true;
    runner.preset = preset;
    runner.slot = slot;
    memset(runner.macro_held_usage_bitmap, 0, sizeof(runner.macro_held_usage_bitmap));
    atomic_clear(&runner.stop_requested);
    k_sem_reset(&macro_stop_signal);
    queued_macro = *macro;
    queued_trigger = trigger;
    hold_key_pressed = trigger == K380_DYNAMIC_MACRO_TRIGGER_HOLD;
    k_mutex_unlock(&runner_lock);

    const int err = k_work_submit(&dynamic_macro_work);
    if (err < 0) {
        k_mutex_lock(&runner_lock, K_FOREVER);
        runner.running = false;
        k_mutex_unlock(&runner_lock);
    }
    return err < 0 ? err : 0;
}

static int load_macro(struct k380_dynamic_config *config, void *user_data)
{
    struct macro_lookup_context *ctx = user_data;

    ctx->macro = config->presets[ctx->preset].macros[ctx->slot];
    ctx->trigger = ctx->macro.trigger;

    return 0;
}

static int load_active_macro(struct k380_dynamic_config *config, void *user_data)
{
    struct macro_lookup_context *ctx = user_data;

    ctx->preset = config->active_preset;
    return load_macro(config, user_data);
}

int k380_dynamic_macro_trigger(uint8_t preset, uint8_t slot, bool pressed) {
    struct macro_lookup_context macro_context = {
        .preset = preset,
        .slot = slot,
    };
    int err;

    if (preset >= K380_DYNAMIC_PRESET_COUNT || slot >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
        return -EINVAL;
    }

    k_mutex_lock(&runner_lock, K_FOREVER);
    const bool same_macro = runner.running && runner.preset == preset && runner.slot == slot;
    if (runner.running) {
        int stop_err = 0;
        bool should_wake = false;
        if (same_macro && queued_trigger == K380_DYNAMIC_MACRO_TRIGGER_TOGGLE && pressed) {
            hold_key_pressed = false;
            atomic_set(&runner.stop_requested, 1);
            stop_err = release_all_macro_held_locked();
            should_wake = true;
        } else if (same_macro && queued_trigger == K380_DYNAMIC_MACRO_TRIGGER_HOLD && !pressed) {
            hold_key_pressed = false;
            atomic_set(&runner.stop_requested, 1);
            stop_err = release_all_macro_held_locked();
            should_wake = true;
        }
        k_mutex_unlock(&runner_lock);
        if (should_wake) {
            k_sem_give(&macro_stop_signal);
        }
        if (stop_err != 0) {
            return stop_err;
        }
        return 0;
    }
    k_mutex_unlock(&runner_lock);

    if (!pressed) {
        return 0;
    }

    err = k380_dynamic_settings_with_config(load_macro, &macro_context);
    if (err != 0) {
        return err;
    }

    return start_macro(preset, slot, &macro_context.macro,
                       macro_context.trigger);
}

int k380_dynamic_macro_test(uint8_t slot) {
    struct macro_lookup_context macro_context = {
        .slot = slot,
    };
    int err;

    if (slot >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
        return -EINVAL;
    }

    err = k380_dynamic_settings_with_config(load_active_macro, &macro_context);
    if (err != 0) {
        return err;
    }

    return start_macro(macro_context.preset, slot, &macro_context.macro,
                       K380_DYNAMIC_MACRO_TRIGGER_ONCE);
}

int k380_dynamic_macro_test_temporary(uint8_t preset, uint8_t slot,
                                      const struct k380_dynamic_macro *macro) {
    if (preset >= K380_DYNAMIC_PRESET_COUNT || slot >= K380_DYNAMIC_MACRO_SLOT_COUNT ||
        macro == NULL) {
        return -EINVAL;
    }

    return start_macro(preset, slot, macro, K380_DYNAMIC_MACRO_TRIGGER_ONCE);
}

int k380_dynamic_macro_stop(void) {
    k_mutex_lock(&runner_lock, K_FOREVER);
    int err = release_all_macro_held_locked();
    if (runner.running) {
        hold_key_pressed = false;
        atomic_set(&runner.stop_requested, 1);
    }
    k_mutex_unlock(&runner_lock);
    k_sem_give(&macro_stop_signal);
    return err;
}

bool k380_dynamic_macro_is_running(void) {
    k_mutex_lock(&runner_lock, K_FOREVER);
    const bool running = runner.running;
    k_mutex_unlock(&runner_lock);
    return running;
}

void k380_dynamic_macro_stop_before_preset_switch(void) {
    (void)k380_dynamic_macro_stop();
}

void k380_dynamic_macro_physical_key_state(uint16_t usage, bool pressed) {
    if (!is_keyboard_keypad_usage(usage)) {
        return;
    }

    k_mutex_lock(&runner_lock, K_FOREVER);
    if (pressed) {
        if (physical_usage_counts[usage] < UINT8_MAX) {
            physical_usage_counts[usage]++;
        }
    } else if (physical_usage_counts[usage] > 0U) {
        physical_usage_counts[usage]--;
    }
    k_mutex_unlock(&runner_lock);
}
