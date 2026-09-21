#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/logging/log.h>
#include <zmk_keyboard_k380/low_power.h>

static bool input_allowed;
static bool release_waiting;
static int release_notifications;
bool k380_low_power_input_events_allowed(void) { return input_allowed; }
bool k380_low_power_is_release_waiting(void) { return release_waiting; }
void k380_low_power_notify_all_keys_released(void) { release_notifications++; }

/* Exercise the real driver without manufacturing a registered Kscan instance. */
#include "../../../drivers/kscan/kscan_k380_no_diode_matrix.c"

static struct k380_kscan_gpio rows[8];
static struct k380_kscan_gpio cols[15];
static struct zmk_debounce_state states[120];
#if USE_INTERRUPTS
static struct k380_kscan_irq_callback irqs[15];
#endif
static struct k380_kscan_data data;
static struct k380_kscan_config config;
/* k380_kscan_init() registers PM state even for this white-box device. */
static struct pm_device_base matrix_pm;
static const struct device matrix = {.data = &data, .config = &config, .pm_base = &matrix_pm};
static int events;

static void callback(const struct device *dev, uint32_t row, uint32_t col, bool pressed) {
    ARG_UNUSED(dev);
    ARG_UNUSED(row);
    ARG_UNUSED(col);
    ARG_UNUSED(pressed);
    events++;
}

static void reset(void) {
    memset(&data, 0, sizeof(data));
    memset(states, 0, sizeof(states));
    for (int i = 0; i < 8; i++) {
        rows[i] = (struct k380_kscan_gpio){
            .spec = {.port = DEVICE_DT_GET(DT_NODELABEL(test_gpio)), .pin = i}, .index = i};
    }
    for (int i = 0; i < 15; i++) {
        cols[i] = (struct k380_kscan_gpio){
            .spec = {.port = DEVICE_DT_GET(DT_NODELABEL(test_gpio)), .pin = i + 8,
                     .dt_flags = GPIO_PULL_DOWN}, .index = i};
    }
    data.inputs = K380_KSCAN_GPIO_LIST(cols);
    data.matrix_state = states;
#if USE_INTERRUPTS
    data.irqs = irqs;
#endif
    config.outputs = K380_KSCAN_GPIO_LIST(rows);
    config.debounce_scan_period_ms = 1;
    config.poll_period_ms = 10;
    config.debounce_config = (struct zmk_debounce_config){1, 1};
    input_allowed = true;
    release_waiting = false;
    events = 0;
    release_notifications = 0;
    zassert_ok(k380_kscan_init(&matrix));
    zassert_ok(k380_kscan_configure(&matrix, callback));
    zassert_ok(k380_kscan_setup_pins(&matrix));
    for (int i = 0; i < 15; i++) {
        zassert_ok(gpio_emul_input_set(cols[i].spec.port, cols[i].spec.pin, 0));
    }
    data.enabled = true;
}

static int64_t scan_columns(uint16_t active_cols) {
    for (int i = 0; i < 15; i++) {
        zassert_ok(gpio_emul_input_set(cols[i].spec.port, cols[i].spec.pin,
                                       (active_cols & BIT(i)) != 0U));
    }
    k_sched_lock();
    data.scan_time = k_uptime_get();
    const int64_t started = data.scan_time;
    zassert_ok(k380_kscan_read(&matrix));
    const int64_t deadline_delta = data.scan_time - started;
    k_work_cancel_delayable(&data.work);
    k_sched_unlock();
    return deadline_delta;
}

static int64_t scan(bool pressed) { return scan_columns(pressed ? BIT(1) : 0U); }

ZTEST(k380_system_off_wake, test_every_raw_or_debouncing_position_blocks_release) {
    reset();
    scan(false);
    zassert_true(k380_kscan_all_keys_released());
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 15; col++) {
            data.raw_matrix[row] = BIT(col);
            zassert_false(k380_kscan_frame_released(&data), "raw %d,%d", row, col);
            data.raw_matrix[row] = 0;
            states[state_index_rc(row, col)].pressed = true;
            zassert_false(k380_kscan_frame_released(&data), "debounced %d,%d", row, col);
            states[state_index_rc(row, col)].pressed = false;
            states[state_index_rc(row, col)].counter = 1;
            zassert_false(k380_kscan_frame_released(&data), "debouncing %d,%d", row, col);
            states[state_index_rc(row, col)].counter = 0;
        }
    }
    zassert_true(k380_kscan_frame_released(&data));
}

ZTEST(k380_system_off_wake, test_suspend_stops_work_before_disconnecting_gpio) {
    reset();
    k_work_reschedule(&data.work, K_SECONDS(30));
    zassert_ok(k380_kscan_pm_action(&matrix, PM_DEVICE_ACTION_SUSPEND));
    zassert_false(data.enabled);
    zassert_false(k_work_delayable_is_pending(&data.work));
    zassert_ok(k380_kscan_read(&matrix));
    zassert_equal(events, 0);
}

ZTEST(k380_system_off_wake, test_wake_preparation_rejects_held_key_and_does_not_scan) {
    reset();
    scan(true);
    zassert_equal(k380_kscan_prepare_system_off_wake(), -EBUSY);
    zassert_false(data.system_off_requested);
    for (int i = 0; i < 3; i++) { scan(false); }
    zassert_ok(k380_kscan_prepare_system_off_wake());
    zassert_true(data.system_off_requested);
    zassert_false(data.enabled);
    zassert_false(k_work_delayable_is_pending(&data.work));
    zassert_ok(k380_kscan_arm_system_off_wake(&matrix));
    zassert_false(data.enabled, "arming wake must never enable normal scanning");
    zassert_false(k_work_delayable_is_pending(&data.work));
    for (int row = 0; row < 8; row++) {
        zassert_equal(gpio_emul_output_get(rows[row].spec.port, rows[row].spec.pin), 1);
    }
    zassert_equal(events, 0);
}

ZTEST(k380_system_off_wake, test_quiet_release_wait_never_emits_new_events) {
    reset();
    input_allowed = false;
    for (int i = 0; i < 20; i++) { scan(true); }
    zassert_equal(events, 0);
    zassert_false(k380_kscan_all_keys_released());
    zassert_equal(release_notifications, 0);
    for (int i = 0; i < 3; i++) { scan(false); }
    zassert_equal(events, 0);
    zassert_true(k380_kscan_all_keys_released());
    zassert_true(release_notifications > 0);
}

ZTEST(k380_system_off_wake, test_whole_wake_press_is_consumed_until_release) {
    reset();
    data.suppress_until_release = true;
    for (int i = 0; i < 10000; i++) { scan(true); }
    zassert_equal(events, 0, "long wake hold must not escape");
    zassert_true(data.suppress_until_release);
    for (int i = 0; i < 3; i++) { scan(false); }
    zassert_equal(events, 0, "wake release must also be consumed");
    zassert_false(data.suppress_until_release);
    scan(true);
    scan(true);
    scan(true);
    zassert_equal(events, 8, "subsequent ordinary column press reports all eight rows");
}

ZTEST(k380_system_off_wake, test_connected_idle_resume_does_not_consume_first_key) {
    reset();
    zassert_ok(k380_kscan_pm_action(&matrix, PM_DEVICE_ACTION_SUSPEND));
    k_sched_lock();
    zassert_ok(k380_kscan_pm_action(&matrix, PM_DEVICE_ACTION_RESUME));
    k_work_cancel_delayable(&data.work);
    k_sched_unlock();
    scan(true);
    scan(true);
    scan(true);
    zassert_equal(events, 8);
}

ZTEST(k380_system_off_wake, test_release_wait_uses_50_ms_cadence_and_elapsed) {
    reset();
    input_allowed = false;
    release_waiting = true;

    zassert_equal(scan(false), 50, "release-wait must schedule one frame per 50 ms");

    struct zmk_debounce_state *state = &states[state_index_rc(0, 1)];
    state->pressed = true;
    state->counter = 0;
    zassert_equal(scan(false), 50);
    zassert_equal(state->counter, 50,
                  "debounce elapsed time must match the interval that scheduled this frame");
}

ZTEST(k380_system_off_wake, test_release_wait_cancellation_restores_normal_cadence) {
    reset();
    input_allowed = false;
    release_waiting = true;
    zassert_equal(scan(true), 50);
    zassert_equal(scan(true), 50);
    zassert_equal(events, 0);
    zassert_true(data.suppress_until_release);

    input_allowed = true;
    release_waiting = false;
    zassert_equal(scan(true), 1, "cancelled release-wait must restore 1 ms scheduling");
    zassert_equal(events, 0, "the old held key must remain suppressed");
    scan(false);
    scan(false);
    zassert_false(data.suppress_until_release);
    zassert_equal(events, 0, "the consumed release must not leak");
}

ZTEST(k380_system_off_wake, test_release_wait_requires_every_held_column_to_release) {
    reset();
    input_allowed = false;
    release_waiting = true;
    scan_columns(BIT(1) | BIT(2));
    scan_columns(BIT(1) | BIT(2));
    zassert_equal(release_notifications, 0);

    scan_columns(BIT(2));
    scan_columns(BIT(2));
    zassert_equal(release_notifications, 0,
                  "one remaining held column must block system off");

    scan_columns(0);
    scan_columns(0);
    zassert_true(release_notifications > 0,
                 "all positions released must notify the coordinator");
}

ZTEST_SUITE(k380_system_off_wake, NULL, NULL, NULL, NULL, NULL);
