#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/battery_policy.h>
#include <zmk_keyboard_k380/soft_off.h>
#include <zmk_keyboard_k380/status_indicator.h>

enum call {
    CALL_WARNING,
    CALL_WAIT_3S,
    CALL_STOP_INDICATOR,
    CALL_SAVE_REASON,
    CALL_CONFIRM_BLE_SETTINGS,
    CALL_CLEAR_HID,
    CALL_DISCONNECT_BLE,
    CALL_SYSTEM_OFF,
};

static enum call calls[16];
static size_t call_count;
static bool charge_during_warning;
static int warning_rc;
static int save_rc;
static int confirm_ble_settings_rc;
static int hid_rc;
static int disconnect_rc;
static size_t save_call_count;
static size_t delete_call_count;
static int delete_rc;
static bool persisted_latch;
static bool hold_save;
static uint32_t voltage_generation;
uint32_t k380_low_power_voltage_generation(void) { return voltage_generation; }
K_SEM_DEFINE(save_entered, 0, 1);
K_SEM_DEFINE(release_save, 0, 1);
K_SEM_DEFINE(clear_done, 0, 1);
K_THREAD_STACK_DEFINE(save_stack, 2048);
K_THREAD_STACK_DEFINE(clear_stack, 2048);
static struct k_thread save_thread;
static struct k_thread clear_thread;
static int save_result;
static int clear_result;
static int64_t fake_now;
static int64_t save_duration;
static int64_t clock_step;
static struct led_rgb rendered_pixels[4];
static size_t render_count;
static bool use_real_timer;
K_MUTEX_DEFINE(capture_lock);

bool k380_status_indicator_test_use_timer(void) { return use_real_timer; }

void k380_status_indicator_test_render(enum k380_status_id status,
                                      const struct led_rgb *pixels, size_t pixel_count) {
    ARG_UNUSED(status);
    zassert_equal(pixel_count, ARRAY_SIZE(rendered_pixels));
    k_mutex_lock(&capture_lock, K_FOREVER);
    memcpy(rendered_pixels, pixels, sizeof(rendered_pixels));
    render_count++;
    k_mutex_unlock(&capture_lock);
}

static size_t captured_render_count(void) {
    k_mutex_lock(&capture_lock, K_FOREVER);
    const size_t count = render_count;
    k_mutex_unlock(&capture_lock);
    return count;
}

static void assert_pixels_quiet(void) {
    struct led_rgb snapshot[4];
    k_mutex_lock(&capture_lock, K_FOREVER);
    memcpy(snapshot, rendered_pixels, sizeof(snapshot));
    k_mutex_unlock(&capture_lock);
    for (size_t i = 0; i < ARRAY_SIZE(rendered_pixels); i++) {
        zassert_equal(snapshot[i].r, 0);
        zassert_equal(snapshot[i].g, 0);
        zassert_equal(snapshot[i].b, 0);
    }
    zassert_false(k380_status_indicator_animation_active());
}
int64_t k380_soft_off_test_uptime(void) {
    const int64_t now = fake_now;
    fake_now += clock_step;
    return now;
}

extern char *k380_soft_off_test_last_reason_storage(void);

void k380_soft_off_test_record(int call) { calls[call_count++] = (enum call)call; }

int k380_soft_off_test_start_warning(void) {
    k380_soft_off_test_record(CALL_WARNING);
    if (warning_rc == 0) {
        return k380_status_indicator_set(K380_STATUS_Z4_SOFT_OFF_WARNING);
    }
    return warning_rc;
}

int k380_soft_off_test_wait_warning(void) {
    k380_soft_off_test_record(CALL_WAIT_3S);
    if (charge_during_warning) {
        return k380_battery_policy_submit_mv(4600);
    }
    return 0;
}

int k380_soft_off_test_save_reason(const char *name, const char *value, size_t len) {
    zassert_equal(strcmp(name, "k380/last_shutdown_reason"), 0);
    zassert_equal(strcmp(value, "low_voltage_protection"), 0);
    zassert_equal(len, strlen("low_voltage_protection") + 1U);
    k380_soft_off_test_record(CALL_SAVE_REASON);
    save_call_count++;
    if (hold_save) {
        k_sem_give(&save_entered);
        if (k_sem_take(&release_save, K_SECONDS(1)) != 0) { return -ETIMEDOUT; }
    }
    if (save_rc == 0) { persisted_latch = true; }
    fake_now += save_duration;
    return save_rc;
}

int k380_soft_off_test_confirm_ble_settings(void) {
    k380_soft_off_test_record(CALL_CONFIRM_BLE_SETTINGS);
    return confirm_ble_settings_rc;
}

int k380_soft_off_test_active_ble_slot(void) { return 2; }

void k380_soft_off_test_restore_reason(const char *reason) {
    strcpy(k380_soft_off_test_last_reason_storage(), reason);
}

int k380_soft_off_test_clear_hid(void) {
    k380_soft_off_test_record(CALL_CLEAR_HID);
    return hid_rc;
}

int k380_soft_off_test_disconnect_ble(int index) {
    zassert_equal(index, 2);
    k380_soft_off_test_record(CALL_DISCONNECT_BLE);
    return disconnect_rc;
}

int k380_soft_off_test_system_off(void) {
    k380_soft_off_test_record(CALL_SYSTEM_OFF);
    return 0;
}

int k380_soft_off_test_delete_reason(void) {
    delete_call_count++;
    if (delete_rc == 0) { persisted_latch = false; }
    return delete_rc;
}

static void reset_fakes(void) {
    use_real_timer = false;
    call_count = 0;
    charge_during_warning = false;
    warning_rc = 0;
    save_rc = 0;
    confirm_ble_settings_rc = 0;
    hid_rc = 0;
    disconnect_rc = 0;
    save_call_count = 0;
    delete_call_count = 0;
    delete_rc = 0;
    hold_save = false;
    voltage_generation = 0U;
    k_sem_reset(&save_entered);
    k_sem_reset(&release_save);
    k_sem_reset(&clear_done);
    fake_now = 0;
    save_duration = 0;
    clock_step = 0;
    k380_soft_off_clear_last_reason();
    delete_call_count = 0;
    persisted_latch = false;
    for (int i = 0; i < 8; i++) {
        zassert_ok(k380_battery_policy_submit_mv(4000));
    }
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    k380_status_indicator_resume_animation();
}

ZTEST(k380_soft_off, test_low_voltage_soft_off_orders_cleanup_after_warning) {
    reset_fakes();

    zassert_ok(k380_soft_off_request_low_voltage());
    const enum call expected[] = {
        CALL_WARNING,
        CALL_WAIT_3S,
        CALL_STOP_INDICATOR,
        CALL_SAVE_REASON,
        CALL_CONFIRM_BLE_SETTINGS,
        CALL_CLEAR_HID,
        CALL_DISCONNECT_BLE,
        CALL_SYSTEM_OFF,
    };

    zassert_equal(call_count, ARRAY_SIZE(expected));
    zassert_mem_equal(calls, expected, sizeof(expected));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z1_NORMAL);
    zassert_equal(strcmp(k380_soft_off_last_reason(), "low_voltage_protection"), 0);
    zassert_equal(save_call_count, 1U);
    zassert_equal(K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS, 1000U);
}

ZTEST(k380_soft_off, test_usb_charging_during_warning_cancels_soft_off) {
    reset_fakes();
    charge_during_warning = true;

    zassert_equal(k380_soft_off_request_low_voltage(), -ECANCELED);
    const enum call expected[] = {CALL_WARNING, CALL_WAIT_3S, CALL_STOP_INDICATOR};

    zassert_equal(call_count, ARRAY_SIZE(expected));
    zassert_mem_equal(calls, expected, sizeof(expected));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z2_CHARGING);
    zassert_equal(k380_soft_off_last_reason(), NULL);
}

ZTEST(k380_soft_off, test_cleanup_failures_do_not_prevent_system_off) {
    reset_fakes();
    save_rc = -EIO;
    hid_rc = -EIO;
    disconnect_rc = -EIO;

    zassert_ok(k380_soft_off_request_low_voltage());
    zassert_equal(calls[call_count - 1U], CALL_SYSTEM_OFF);
}

ZTEST(k380_soft_off, test_ble_settings_failure_still_disconnects_active_slot) {
    reset_fakes();
    confirm_ble_settings_rc = -EIO;

    zassert_ok(k380_soft_off_request_low_voltage());
    const enum call expected[] = {
        CALL_WARNING,
        CALL_WAIT_3S,
        CALL_STOP_INDICATOR,
        CALL_SAVE_REASON,
        CALL_CONFIRM_BLE_SETTINGS,
        CALL_CLEAR_HID,
        CALL_DISCONNECT_BLE,
        CALL_SYSTEM_OFF,
    };

    zassert_equal(call_count, ARRAY_SIZE(expected));
    zassert_mem_equal(calls, expected, sizeof(expected));
}

ZTEST(k380_soft_off, test_successful_boot_keeps_loaded_low_voltage_latch) {
    reset_fakes();
    k380_soft_off_test_restore_reason("low_voltage_protection");

    zassert_equal(strcmp(k380_soft_off_last_reason(), "low_voltage_protection"), 0);
    k380_soft_off_handle_successful_boot();
    zassert_true(k380_soft_off_has_low_voltage_latch());
    zassert_equal(strcmp(k380_soft_off_last_reason(), "low_voltage_protection"), 0);
}

ZTEST(k380_soft_off, test_low_voltage_latch_clears_only_after_safe_qualification) {
    reset_fakes();
    k380_soft_off_test_restore_reason("low_voltage_protection");

    zassert_true(k380_soft_off_has_low_voltage_latch());
    zassert_equal(k380_soft_off_clear_low_voltage_latch_if_safe(false), -EACCES);
    zassert_true(k380_soft_off_has_low_voltage_latch());
    zassert_ok(k380_soft_off_clear_low_voltage_latch_if_safe(true));
    zassert_false(k380_soft_off_has_low_voltage_latch());
    zassert_ok(k380_soft_off_clear_low_voltage_latch_if_safe(true));
    zassert_equal(delete_call_count, 1U, "safe qualification deletes the latch once");
}

ZTEST(k380_soft_off, test_existing_latch_skips_repeat_reason_write) {
    reset_fakes();
    k380_soft_off_test_restore_reason("low_voltage_protection");

    zassert_ok(k380_soft_off_request_low_voltage());
    zassert_equal(save_call_count, 0U);
}

ZTEST(k380_soft_off, test_failed_delete_retains_latch_and_reports_error) {
    reset_fakes();
    k380_soft_off_test_restore_reason("low_voltage_protection");
    delete_rc = -EIO;
    zassert_equal(k380_soft_off_clear_low_voltage_latch_if_safe(true), -EIO);
    zassert_true(k380_soft_off_has_low_voltage_latch());
    k380_soft_off_set_pending_reason(K380_SHUTDOWN_LOW_VOLTAGE);
    zassert_ok(k380_soft_off_flush_required_settings());
    zassert_equal(save_call_count, 0U, "failed deletion must not trigger a duplicate write");
    delete_rc = 0;
    zassert_ok(k380_soft_off_clear_low_voltage_latch_if_safe(true));
    zassert_false(k380_soft_off_has_low_voltage_latch());
}

static void save_worker(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    save_result = k380_soft_off_flush_required_settings();
}
static void clear_worker(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    clear_result = k380_soft_off_clear_low_voltage_latch_if_safe(true);
    k_sem_give(&clear_done);
}
static void reason_worker(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    k380_soft_off_set_pending_reason(K380_SHUTDOWN_PAIRING_TIMEOUT);
    k_sem_give(&clear_done);
}

ZTEST(k380_soft_off, test_ram_reason_publication_does_not_wait_for_flash) {
    reset_fakes();
    hold_save = true;
    k380_soft_off_set_pending_reason(K380_SHUTDOWN_LOW_VOLTAGE);
    k_thread_create(&save_thread, save_stack, K_THREAD_STACK_SIZEOF(save_stack),
                    save_worker, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_ok(k_sem_take(&save_entered, K_MSEC(500)));
    k_thread_create(&clear_thread, clear_stack, K_THREAD_STACK_SIZEOF(clear_stack),
                    reason_worker, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    const int publication_result = k_sem_take(&clear_done, K_MSEC(50));
    k_sem_give(&release_save);
    zassert_ok(k_thread_join(&save_thread, K_SECONDS(2)));
    zassert_ok(k_thread_join(&clear_thread, K_SECONDS(2)));
    hold_save = false;
    zassert_ok(save_result);
    zassert_ok(publication_result, "RAM reason publication must not wait for a Flash transaction");
}

ZTEST(k380_soft_off, test_safe_clear_wins_over_an_inflight_latch_save) {
    reset_fakes();
    hold_save = true;
    k380_soft_off_set_pending_reason(K380_SHUTDOWN_LOW_VOLTAGE);
    k_thread_create(&save_thread, save_stack, K_THREAD_STACK_SIZEOF(save_stack),
                    save_worker, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_ok(k_sem_take(&save_entered, K_MSEC(500)));
    k_thread_create(&clear_thread, clear_stack, K_THREAD_STACK_SIZEOF(clear_stack),
                    clear_worker, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    /* An unordered clear completes before the admitted Flash write; an
     * ordered clear waits until that write releases the latch transaction. */
    (void)k_sem_take(&clear_done, K_MSEC(50));
    k_sem_give(&release_save);
    zassert_ok(k_thread_join(&save_thread, K_SECONDS(2)));
    zassert_ok(k_thread_join(&clear_thread, K_SECONDS(2)));
    hold_save = false;
    zassert_ok(save_result);
    zassert_ok(clear_result);
    zassert_false(persisted_latch, "safe clear must not be undone by an older admitted write");
    zassert_false(k380_soft_off_has_low_voltage_latch());
}

ZTEST(k380_soft_off, test_recovery_before_save_admission_rejects_stale_latch) {
    reset_fakes();
    k380_soft_off_set_pending_reason(K380_SHUTDOWN_LOW_VOLTAGE);
    const uint32_t decision_generation = voltage_generation;
    voltage_generation++;
    zassert_ok(k380_soft_off_clear_low_voltage_latch_if_safe(true));
    zassert_equal(k380_soft_off_flush_required_settings_at_generation(decision_generation), -ECANCELED);
    zassert_equal(save_call_count, 0U);
    zassert_false(persisted_latch);
    zassert_false(k380_soft_off_has_low_voltage_latch());
}

ZTEST(k380_soft_off, test_ble_timeout_reasons_are_ram_only) {
    reset_fakes();

    zassert_ok(k380_soft_off_request_reason(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
    zassert_equal(save_call_count, 0U);
    reset_fakes();
    zassert_ok(k380_soft_off_request_reason(K380_SHUTDOWN_PAIRING_TIMEOUT));
    zassert_equal(save_call_count, 0U);
}

ZTEST(k380_soft_off, test_warning_start_failure_cancels_soft_off) {
    reset_fakes();
    warning_rc = -EIO;

    zassert_equal(k380_soft_off_request_low_voltage(), -EIO);
    zassert_equal(call_count, 1U);
    zassert_equal(calls[0], CALL_WARNING);
    zassert_is_null(k380_soft_off_last_reason());
}

ZTEST_SUITE(k380_soft_off, NULL, NULL, NULL, NULL, NULL);

ZTEST(k380_soft_off, test_save_deadline_skips_second_write_without_retry) {
    reset_fakes();
    save_duration = K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS;
    k380_soft_off_set_pending_reason(K380_SHUTDOWN_LOW_VOLTAGE);
    zassert_equal(k380_soft_off_flush_required_settings(), -ETIMEDOUT);
    zassert_equal(save_call_count, 1U);
    zassert_equal(call_count, 1U);
}

ZTEST(k380_soft_off, test_quiet_stops_ble_animation_for_both_timeout_states) {
    reset_fakes();
    use_real_timer = true;
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    zassert_ok(k380_soft_off_prepare_radio_and_led_quiet());
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z5_BLE_WAITING);
    assert_pixels_quiet();
    const size_t quiet_count = captured_render_count();
    k380_status_indicator_animation_step();
    k_sleep(K_MSEC(1100));
    zassert_equal(captured_render_count(), quiet_count);
    assert_pixels_quiet();
    k380_status_indicator_resume_animation();
    zassert_true(k380_status_indicator_animation_active());
    zassert_true(captured_render_count() > quiet_count);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    zassert_ok(k380_soft_off_prepare_radio_and_led_quiet());
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z7_BLE_PAIRING);
    assert_pixels_quiet();
    const size_t pairing_quiet_count = captured_render_count();
    k380_status_indicator_animation_step();
    k_sleep(K_MSEC(350));
    zassert_equal(captured_render_count(), pairing_quiet_count);
    assert_pixels_quiet();
    k380_status_indicator_resume_animation();
    zassert_true(k380_status_indicator_animation_active());
    zassert_true(captured_render_count() > pairing_quiet_count);
    k380_status_indicator_stop_animation();
    use_real_timer = false;
}

ZTEST(k380_soft_off, test_expired_admission_skips_first_latch_write_and_keeps_latch_absent) {
    reset_fakes();
    clock_step = K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS;
    k380_soft_off_set_pending_reason(K380_SHUTDOWN_LOW_VOLTAGE);
    zassert_equal(k380_soft_off_flush_required_settings(), -ETIMEDOUT);
    zassert_equal(save_call_count, 0U);
    zassert_equal(call_count, 0U, "neither settings write is admitted");
    zassert_false(k380_soft_off_has_low_voltage_latch());

    reset_fakes();
    clock_step = K380_SOFT_OFF_SAVE_WAIT_BUDGET_MS;
    zassert_ok(k380_soft_off_request_low_voltage());
    const enum call expected[] = { CALL_WARNING, CALL_WAIT_3S, CALL_STOP_INDICATOR,
                                  CALL_CLEAR_HID, CALL_DISCONNECT_BLE, CALL_SYSTEM_OFF };
    zassert_equal(call_count, ARRAY_SIZE(expected));
    zassert_mem_equal(calls, expected, sizeof(expected));
    zassert_equal(save_call_count, 0U);
    zassert_false(k380_soft_off_has_low_voltage_latch());
}
