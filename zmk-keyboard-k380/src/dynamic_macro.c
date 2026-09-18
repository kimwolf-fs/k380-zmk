#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>
#include <zmk/shutdown_input.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_macro.h>
#include <zmk_keyboard_k380/dynamic_macro_record.h>
#include <zmk_keyboard_k380/dynamic_macro_store.h>
#include <zmk_keyboard_k380/dynamic_macro_vm.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

bool zmk_hid_keyboard_is_pressed(zmk_key_t code);

struct k380_dynamic_macro_runner {
    bool running;
    bool load_saved_record;
    bool pressed_while_loading;
    bool cleanup_failed;
    uint8_t preset;
    uint8_t slot;
    uint8_t trigger;
    uint8_t state;
    uint8_t vm_error;
    uint16_t reserved;
    uint32_t run_id;
    uint32_t repeat_count;
    struct k380_dynamic_macro_record record;
    uint32_t macro_held_usage_bitmap[8];
    atomic_t stop_requested;
};

static struct k380_dynamic_macro_runner runner;
static bool hold_key_pressed;
static uint8_t physical_usage_counts[256];
static uint32_t next_run_id = 1U;
static struct k380_dynamic_macro_trace_event trace_ring[K380_MACRO_VM_MAX_TRACE_EVENTS];
static size_t trace_head;
static size_t trace_count;
static uint32_t trace_next_sequence;
static struct k_spinlock trace_lock;

K_MUTEX_DEFINE(runner_lock);
K_SEM_DEFINE(macro_start_signal, 0, 1);
K_SEM_DEFINE(macro_stop_signal, 0, 1);
K_SEM_DEFINE(macro_finished_signal, 0, 1);

/* All emission paths take dispatch before runner_lock, including recursive listeners. */
static void lock_runner(void) {
    zmk_shutdown_input_lock();
    k_mutex_lock(&runner_lock, K_FOREVER);
}
static void unlock_runner(void) {
    k_mutex_unlock(&runner_lock);
    zmk_shutdown_input_unlock();
}

BUILD_ASSERT(CONFIG_K380_DYNAMIC_MACRO_THREAD_PRIORITY == 8,
             "macro runner priority is part of the runtime contract");

static bool is_keyboard_keypad_usage(uint16_t usage)
{
    return ((usage >= 4U && usage <= 126U) && usage != 102U) ||
           (usage >= 130U && usage <= 164U) ||
           (usage >= 176U && usage <= 221U) ||
           (usage >= 224U && usage <= 231U);
}

static bool is_macro_held(uint16_t usage)
{
    return (runner.macro_held_usage_bitmap[usage / 32U] &
            BIT(usage % 32U)) != 0U;
}

static void set_macro_held(uint16_t usage, bool held)
{
    WRITE_BIT(runner.macro_held_usage_bitmap[usage / 32U], usage % 32U,
              held);
}

static bool stop_requested(void)
{
    return atomic_get(&runner.stop_requested) != 0;
}

static int emit_keyboard_usage(uint16_t usage, bool pressed)
{
    return raise_zmk_keycode_state_changed_from_encoded(
        ZMK_HID_USAGE(HID_USAGE_KEY, usage), pressed, k_uptime_get());
}

static bool is_physically_held(uint16_t usage)
{
    return physical_usage_counts[usage] != 0U ||
           (!is_macro_held(usage) && zmk_hid_keyboard_is_pressed(usage));
}

static int press_usage(uint16_t usage)
{
    if (!is_keyboard_keypad_usage(usage)) {
        return -EINVAL;
    }

    lock_runner();
    if (stop_requested()) {
        unlock_runner();
        return -ECANCELED;
    }
    if (is_macro_held(usage) || is_physically_held(usage)) {
        unlock_runner();
        return 0;
    }

    const int err = emit_keyboard_usage(usage, true);
    if (err == 0) {
        set_macro_held(usage, true);
    }
    unlock_runner();
    return err;
}

static int release_usage_locked(uint16_t usage)
{
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

static int release_usage(uint16_t usage)
{
    int err;

    if (!is_keyboard_keypad_usage(usage)) {
        return -EINVAL;
    }

    lock_runner();
    err = release_usage_locked(usage);
    unlock_runner();
    return err;
}

static int release_all_macro_held_locked(void)
{
    int first_error = 0;

    for (uint16_t usage = 0U; usage <= UINT8_MAX; usage++) {
        if (!is_macro_held(usage)) {
            continue;
        }
        const int err = release_usage_locked(usage);
        if (first_error == 0 && err != 0) {
            first_error = err;
        }
    }
    return first_error;
}

static int release_all_macro_held(void)
{
    lock_runner();
    const int err = release_all_macro_held_locked();
    unlock_runner();
    return err;
}

static int vm_key_event(uint16_t usage, bool pressed, void *user_data)
{
    ARG_UNUSED(user_data);

    return pressed ? press_usage(usage) : release_usage(usage);
}

static int vm_release_all(void *user_data)
{
    ARG_UNUSED(user_data);

    return release_all_macro_held();
}

static int vm_wait_ms(uint32_t duration_ms, void *user_data)
{
    ARG_UNUSED(user_data);

    const uint64_t deadline_ms = (uint64_t)k_uptime_get() + duration_ms;
    while (!stop_requested()) {
        const uint64_t current_ms = (uint64_t)k_uptime_get();
        if (current_ms >= deadline_ms) {
            return 0;
        }
        const uint64_t remaining_ms = deadline_ms - current_ms;
        const int64_t chunk_ms = (int64_t)MIN(remaining_ms,
                                               (uint64_t)INT32_MAX);
        const int err = k_sem_take(&macro_stop_signal, K_MSEC(chunk_ms));
        if (err == 0 || stop_requested()) {
            return -ECANCELED;
        }
        if (err != -EAGAIN) {
            return err;
        }
    }
    return -ECANCELED;
}

static uint64_t vm_now_ms(void *user_data)
{
    ARG_UNUSED(user_data);

    return (uint64_t)k_uptime_get();
}

static uint32_t vm_random_u32(void *user_data)
{
    ARG_UNUSED(user_data);

    return sys_rand32_get();
}

static bool vm_stop_requested(void *user_data)
{
    ARG_UNUSED(user_data);

    return stop_requested();
}

static void vm_yield_cpu(void *user_data)
{
    ARG_UNUSED(user_data);

    k_yield();
}

static void trace_reset_locked(void)
{
    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    trace_head = 0U;
    trace_count = 0U;
    trace_next_sequence = 1U;
    k_spin_unlock(&trace_lock, key);
}

static void vm_trace(uint16_t pc, uint8_t event, uint8_t result,
                     uint32_t value, uint32_t elapsed_ms, void *user_data)
{
    ARG_UNUSED(user_data);

    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    struct k380_dynamic_macro_trace_event *trace = &trace_ring[trace_head];
    trace->sequence = trace_next_sequence;
    trace->pc = pc;
    trace->event = event;
    trace->result = result;
    trace->value = value;
    trace->elapsed_ms = elapsed_ms;
    trace_head = (trace_head + 1U) % ARRAY_SIZE(trace_ring);
    if (trace_count < ARRAY_SIZE(trace_ring)) {
        trace_count++;
    }
    if (trace_next_sequence != UINT32_MAX) {
        trace_next_sequence++;
    }
    k_spin_unlock(&trace_lock, key);
}

static struct k380_macro_vm_host vm_host(void)
{
    return (struct k380_macro_vm_host){
        .key_event = vm_key_event,
        .release_all = vm_release_all,
        .wait_ms = vm_wait_ms,
        .now_ms = vm_now_ms,
        .random_u32 = vm_random_u32,
        .stop_requested = vm_stop_requested,
        .yield_cpu = vm_yield_cpu,
        .trace = vm_trace,
    };
}

static int start_record_locked(uint8_t preset, uint8_t slot,
                               const struct k380_dynamic_macro_record *record,
                               uint8_t trigger, bool load_saved_record,
                               uint32_t *run_id)
{
    if (!zmk_shutdown_input_dispatch_allowed(true)) {
        return -ECANCELED;
    }
    if (runner.running) {
        return -EBUSY;
    }

    const int cleanup_err = release_all_macro_held_locked();
    if (cleanup_err != 0) {
        return cleanup_err;
    }

    runner.running = true;
    k_sem_reset(&macro_finished_signal);
    runner.load_saved_record = load_saved_record;
    runner.pressed_while_loading = false;
    runner.cleanup_failed = false;
    runner.preset = preset;
    runner.slot = slot;
    runner.trigger = trigger;
    runner.state = K380_DYNAMIC_MACRO_RUN_RUNNING;
    runner.vm_error = 0U;
    runner.run_id = next_run_id;
    next_run_id++;
    if (next_run_id == 0U) {
        next_run_id = 1U;
    }
    runner.repeat_count = record == NULL || record->repeat_count == 0U
                              ? 1U
                              : record->repeat_count;
    if (record != NULL) {
        runner.record = *record;
    } else {
        memset(&runner.record, 0, sizeof(runner.record));
    }
    memset(runner.macro_held_usage_bitmap, 0,
           sizeof(runner.macro_held_usage_bitmap));
    atomic_clear(&runner.stop_requested);
    k_sem_reset(&macro_stop_signal);
    trace_reset_locked();
    hold_key_pressed = trigger == K380_DYNAMIC_MACRO_TRIGGER_HOLD;
    if (load_saved_record) {
        hold_key_pressed = true;
    }
    if (run_id != NULL) {
        *run_id = runner.run_id;
    }
    k_sem_give(&macro_start_signal);
    return 0;
}

static int start_saved_record(uint8_t preset, uint8_t slot)
{
    int err;

    lock_runner();
    if (runner.running) {
        unlock_runner();
        return 0;
    }

    err = start_record_locked(preset, slot, NULL,
                              K380_DYNAMIC_MACRO_TRIGGER_ONCE, true, NULL);
    unlock_runner();
    return err;
}

static int load_active_preset(const struct k380_dynamic_config *config,
                              void *user_data)
{
    uint8_t *preset = user_data;

    *preset = config->active_preset;
    return 0;
}

static enum k380_macro_vm_error run_record(void)
{
    struct k380_macro_vm_package_view view;
    struct k380_macro_vm_context context;
    const struct k380_macro_vm_host host = vm_host();
    uint8_t trigger;
    uint32_t passes_remaining;
    enum k380_macro_vm_error error;
    bool first_pass = true;

    if (runner.load_saved_record) {
        bool wake_after_load = false;
        const int load_error = k380_dynamic_macro_store_load(
            runner.preset, runner.slot, &runner.record);
        if (load_error != 0) {
            return K380_MACRO_VM_HOST_FAILURE;
        }
        if (runner.record.package_len == 0U) {
            return K380_MACRO_VM_OK;
        }
        if (k380_dynamic_macro_record_validate(&runner.record) != 0) {
            return K380_MACRO_VM_INVALID_PACKAGE;
        }

        lock_runner();
        runner.trigger = runner.record.trigger;
        runner.load_saved_record = false;
        runner.repeat_count = runner.record.repeat_count == 0U
                                  ? 1U
                                  : runner.record.repeat_count;
        if (runner.trigger == K380_DYNAMIC_MACRO_TRIGGER_TOGGLE &&
            runner.pressed_while_loading) {
            atomic_set(&runner.stop_requested, 1);
            wake_after_load = true;
        }
        runner.pressed_while_loading = false;
        if (runner.trigger == K380_DYNAMIC_MACRO_TRIGGER_HOLD &&
            !hold_key_pressed) {
            atomic_set(&runner.stop_requested, 1);
        }
        unlock_runner();
        if (wake_after_load) {
            k_sem_give(&macro_stop_signal);
        }
    }

    trigger = runner.trigger;
    passes_remaining = runner.repeat_count;

    error = k380_macro_vm_validate(runner.record.package,
                                   runner.record.package_len, &view);
    if (error != K380_MACRO_VM_OK) {
        return error;
    }

    do {
        error = first_pass ? k380_macro_vm_run(&view, &host, &context)
                           : k380_macro_vm_run_pass(&view, &host, &context);
        if (error != K380_MACRO_VM_OK) {
            break;
        }
        first_pass = false;

        if (trigger == K380_DYNAMIC_MACRO_TRIGGER_ONCE) {
            break;
        }
        if (trigger == K380_DYNAMIC_MACRO_TRIGGER_COUNT &&
            --passes_remaining == 0U) {
            break;
        }
        if (stop_requested()) {
            break;
        }

        if (trigger == K380_DYNAMIC_MACRO_TRIGGER_HOLD) {
            lock_runner();
            const bool still_held = hold_key_pressed;
            unlock_runner();
            if (!still_held) {
                break;
            }
        }

        if (view.code_len == 1U && view.code[0] == K380_MACRO_VM_OP_END) {
            k_sleep(K_MSEC(1));
        }
    } while (trigger == K380_DYNAMIC_MACRO_TRIGGER_HOLD ||
             trigger == K380_DYNAMIC_MACRO_TRIGGER_TOGGLE ||
             trigger == K380_DYNAMIC_MACRO_TRIGGER_COUNT);

    return error;
}

static void dynamic_macro_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (true) {
        k_sem_take(&macro_start_signal, K_FOREVER);
        enum k380_macro_vm_error result = run_record();

        lock_runner();
        const int cleanup_error = release_all_macro_held_locked();
        if (cleanup_error != 0 || runner.cleanup_failed) {
            result = K380_MACRO_VM_HOST_FAILURE;
        }
        runner.running = false;
        k_sem_give(&macro_finished_signal);
        if (result == K380_MACRO_VM_OK) {
            runner.state = stop_requested()
                               ? K380_DYNAMIC_MACRO_RUN_STOPPED
                               : K380_DYNAMIC_MACRO_RUN_COMPLETED;
            runner.vm_error = 0U;
        } else {
            runner.vm_error = (uint8_t)result;
            runner.state = result == K380_MACRO_VM_STOPPED
                               ? K380_DYNAMIC_MACRO_RUN_STOPPED
                               : K380_DYNAMIC_MACRO_RUN_ERROR;
        }
        unlock_runner();
    }
}

K_THREAD_DEFINE(k380_dynamic_macro_thread,
                CONFIG_K380_DYNAMIC_MACRO_THREAD_STACK_SIZE,
                dynamic_macro_thread, NULL, NULL, NULL,
                CONFIG_K380_DYNAMIC_MACRO_THREAD_PRIORITY, 0, 0);

int k380_dynamic_macro_trigger(uint8_t preset, uint8_t slot, bool pressed)
{
    if (preset >= K380_DYNAMIC_PRESET_COUNT ||
        slot >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
        return -EINVAL;
    }

    lock_runner();
    const bool same_macro = runner.running && runner.preset == preset &&
                            runner.slot == slot;
    if (runner.running) {
        bool should_wake = false;
        if (same_macro && runner.load_saved_record) {
            if (pressed) {
                runner.pressed_while_loading = true;
            } else {
                hold_key_pressed = false;
            }
        }
        if (same_macro &&
            runner.trigger == K380_DYNAMIC_MACRO_TRIGGER_TOGGLE && pressed) {
            hold_key_pressed = false;
            atomic_set(&runner.stop_requested, 1);
            should_wake = true;
        } else if (same_macro &&
                   runner.trigger == K380_DYNAMIC_MACRO_TRIGGER_HOLD &&
                   !pressed) {
            hold_key_pressed = false;
            atomic_set(&runner.stop_requested, 1);
            should_wake = true;
        }
        unlock_runner();
        if (should_wake) {
            k_sem_give(&macro_stop_signal);
        }
        return 0;
    }
    unlock_runner();

    if (!pressed) {
        return 0;
    }

    return start_saved_record(preset, slot);
}

int k380_dynamic_macro_test(uint8_t slot)
{
    uint8_t preset;
    int err;

    if (slot >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
        return -EINVAL;
    }

    err = k380_dynamic_settings_with_config(load_active_preset, &preset);
    if (err != 0) {
        return err;
    }

    return start_saved_record(preset, slot);
}

int k380_dynamic_macro_test_temporary(
    uint8_t preset, uint8_t slot, const struct k380_dynamic_macro *macro)
{
    int err;

    if (preset >= K380_DYNAMIC_PRESET_COUNT ||
        slot >= K380_DYNAMIC_MACRO_SLOT_COUNT || macro == NULL) {
        return -EINVAL;
    }

    lock_runner();
    if (runner.running) {
        err = -EBUSY;
    } else {
        err = k380_dynamic_macro_record_from_legacy(macro, &runner.record);
        if (err == 0 && runner.record.package_len == 0U) {
            err = -EINVAL;
        }
        if (err == 0) {
            err = start_record_locked(preset, slot, &runner.record,
                                      K380_DYNAMIC_MACRO_TRIGGER_ONCE, false,
                                      NULL);
        }
    }
    unlock_runner();
    return err;
}

int k380_dynamic_macro_test_record_start(
    uint8_t preset, uint8_t slot,
    const struct k380_dynamic_macro_record *record, uint32_t *run_id)
{
    if (run_id != NULL) {
        *run_id = 0U;
    }
    if (preset >= K380_DYNAMIC_PRESET_COUNT ||
        slot >= K380_DYNAMIC_MACRO_SLOT_COUNT || record == NULL ||
        run_id == NULL) {
        return -EINVAL;
    }

    if (record->package_len == 0U ||
        k380_dynamic_macro_record_validate(record) != 0) {
        return -EINVAL;
    }

    lock_runner();
    const int err = start_record_locked(
        preset, slot, record, K380_DYNAMIC_MACRO_TRIGGER_ONCE, false, run_id);
    unlock_runner();
    return err;
}

int k380_dynamic_macro_test_record(
    uint8_t preset, uint8_t slot,
    const struct k380_dynamic_macro_record *record)
{
    uint32_t run_id;

    return k380_dynamic_macro_test_record_start(preset, slot, record,
                                                &run_id);
}

static int stop_locked(uint32_t run_id, bool *wake)
{
    if (run_id != 0U && run_id != runner.run_id) {
        return -ESTALE;
    }

    const int err = release_all_macro_held_locked();
    if (err != 0 && runner.running) {
        runner.cleanup_failed = true;
    }
    if (runner.running) {
        hold_key_pressed = false;
        atomic_set(&runner.stop_requested, 1);
        *wake = true;
    }
    return err;
}

int k380_dynamic_macro_stop_if_run_id(uint32_t run_id)
{
    bool wake = false;

    lock_runner();
    const int err = stop_locked(run_id, &wake);
    unlock_runner();

    if (wake) {
        k_sem_give(&macro_stop_signal);
    }
    return err;
}

int k380_dynamic_macro_stop(void)
{
    return k380_dynamic_macro_stop_if_run_id(0U);
}

int k380_dynamic_macro_abort_for_shutdown(void)
{
    if (k_current_get() == k380_dynamic_macro_thread) {
        return -EDEADLK;
    }
    int err = k380_dynamic_macro_stop();
    if (err) {
        return err;
    }
    if (k380_dynamic_macro_is_running() &&
        k_sem_take(&macro_finished_signal, K_MSEC(250)) != 0) {
        return -ETIMEDOUT;
    }
    lock_runner();
    memset(physical_usage_counts, 0, sizeof(physical_usage_counts));
    unlock_runner();
    return 0;
}

bool k380_dynamic_macro_is_running(void)
{
    lock_runner();
    const bool running = runner.running;
    unlock_runner();
    return running;
}

void k380_dynamic_macro_get_run_state(
    struct k380_dynamic_macro_run_state *state)
{
    if (state == NULL) {
        return;
    }

    lock_runner();
    *state = (struct k380_dynamic_macro_run_state){
        .run_id = runner.run_id,
        .state = runner.run_id == 0U ? K380_DYNAMIC_MACRO_RUN_IDLE : runner.state,
        .vm_error = runner.vm_error,
        .next_cursor = 0U,
    };
    unlock_runner();

    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    state->next_cursor = trace_next_sequence == 0U ? 0U : trace_next_sequence - 1U;
    state->dropped = 0U;
    k_spin_unlock(&trace_lock, key);
}

int k380_dynamic_macro_trace_read(
    uint32_t cursor, struct k380_dynamic_macro_trace_event *events,
    size_t capacity, struct k380_dynamic_macro_run_state *state)
{
    if (state == NULL || (capacity > 0U && events == NULL) ||
        capacity > K380_MACRO_VM_MAX_TRACE_EVENTS) {
        return -EINVAL;
    }

    lock_runner();
    *state = (struct k380_dynamic_macro_run_state){
        .run_id = runner.run_id,
        .state = runner.run_id == 0U ? K380_DYNAMIC_MACRO_RUN_IDLE : runner.state,
        .vm_error = runner.vm_error,
    };

    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    const uint32_t latest = trace_next_sequence == 0U
                                ? 0U
                                : trace_next_sequence - 1U;
    state->next_cursor = cursor;
    state->dropped = 0U;
    if (cursor > latest) {
        state->next_cursor = latest;
        k_spin_unlock(&trace_lock, key);
        unlock_runner();
        return -EOVERFLOW;
    }
    const uint32_t oldest = trace_count == 0U ? latest + 1U
                                              : latest - (uint32_t)trace_count + 1U;
    if (trace_count > 0U && cursor < oldest - 1U) {
        state->next_cursor = oldest - 1U;
        state->dropped = state->next_cursor - cursor;
        k_spin_unlock(&trace_lock, key);
        unlock_runner();
        return -EOVERFLOW;
    }

    const uint32_t available = latest - cursor;
    const size_t copied = MIN((size_t)available, capacity);
    for (size_t index = 0U; index < copied; index++) {
        const uint32_t sequence = cursor + (uint32_t)index + 1U;
        const uint32_t offset = sequence - oldest;
        const size_t ring_index =
            (trace_head + ARRAY_SIZE(trace_ring) - trace_count + offset) %
            ARRAY_SIZE(trace_ring);
        events[index] = trace_ring[ring_index];
    }
    if (copied > 0U) {
        state->next_cursor = events[copied - 1U].sequence;
    }
    k_spin_unlock(&trace_lock, key);
    unlock_runner();
    return (int)copied;
}

void k380_dynamic_macro_stop_before_preset_switch(void)
{
    (void)k380_dynamic_macro_stop();
}

void k380_dynamic_macro_physical_key_state(uint16_t usage, bool pressed)
{
    if (!zmk_shutdown_input_dispatch_allowed(true)) {
        return;
    }
    if (!is_keyboard_keypad_usage(usage)) {
        return;
    }

    lock_runner();
    if (pressed) {
        if (physical_usage_counts[usage] < UINT8_MAX) {
            physical_usage_counts[usage]++;
        }
    } else if (physical_usage_counts[usage] > 0U) {
        physical_usage_counts[usage]--;
    }
    unlock_runner();
}
