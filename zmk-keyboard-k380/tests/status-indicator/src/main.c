#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/low_power.h>
#include <zmk_keyboard_k380/status_indicator.h>
#include <zmk_keyboard_k380/soft_off.h>

struct captured_frame {
    enum k380_status_id status;
    struct led_rgb pixels[4];
    size_t pixel_count;
    size_t frame_count;
};

static struct captured_frame captured;
static bool use_real_timer;
K_MUTEX_DEFINE(capture_mutex);
K_SEM_DEFINE(frame_completed, 0, 1);
K_SEM_DEFINE(render_queue_blocked, 0, 1);
K_SEM_DEFINE(release_render_queue, 0, 1);
K_SEM_DEFINE(status_mutation_completed, 0, 1);

#define K380_TEST_BLUE_20_PERCENT 51U

uint8_t k380_ble_slot_current(void) { return 1; }
bool k380_status_indicator_test_use_timer(void) { return use_real_timer; }
int k380_low_power_stop_led(void) {
    k380_status_indicator_stop_animation();
    return 0;
}
int k380_low_power_resume_led(void) {
    k380_status_indicator_resume_animation();
    return 0;
}

void k380_status_indicator_test_render(enum k380_status_id status, const struct led_rgb *pixels,
                                       size_t pixel_count) {
    k_mutex_lock(&capture_mutex, K_FOREVER);
    captured.status = status;
    captured.pixel_count = pixel_count;
    memset(captured.pixels, 0, sizeof(captured.pixels));
    memcpy(captured.pixels, pixels,
           MIN(pixel_count, ARRAY_SIZE(captured.pixels)) * sizeof(*pixels));
    captured.frame_count++;
    k_mutex_unlock(&capture_mutex);
    k_sem_give(&frame_completed);
}

static struct captured_frame capture_snapshot(void) {
    k_mutex_lock(&capture_mutex, K_FOREVER);
    const struct captured_frame snapshot = captured;
    k_mutex_unlock(&capture_mutex);
    return snapshot;
}

static void reset_render_capture(void) {
    k_mutex_lock(&capture_mutex, K_FOREVER);
    memset(&captured, 0, sizeof(captured));
    k_sem_reset(&frame_completed);
    k_mutex_unlock(&capture_mutex);
}

static struct captured_frame wait_for_frames(size_t count, int32_t timeout_ms) {
    const int64_t deadline = k_uptime_get() + timeout_ms;
    struct captured_frame frame = capture_snapshot();
    while (frame.frame_count < count) {
        const int64_t remaining = deadline - k_uptime_get();
        if (remaining <= 0 || k_sem_take(&frame_completed, K_MSEC(remaining)) != 0) {
            break;
        }
        frame = capture_snapshot();
    }
    zassert_equal(frame.frame_count, count, "missing or extra completed frames");
    return frame;
}

static void assert_all_pixels_off(struct captured_frame frame) {
    zassert_equal(frame.pixel_count, ARRAY_SIZE(frame.pixels));
    for (size_t i = 0; i < ARRAY_SIZE(frame.pixels); i++) {
        zassert_equal(frame.pixels[i].r, 0);
        zassert_equal(frame.pixels[i].g, 0);
        zassert_equal(frame.pixels[i].b, 0);
    }
}

static void assert_only_slot_1_blue(struct captured_frame frame) {
    zassert_equal(frame.pixel_count, ARRAY_SIZE(frame.pixels));
    for (size_t i = 0; i < ARRAY_SIZE(frame.pixels); i++) {
        zassert_equal(frame.pixels[i].r, 0);
        zassert_equal(frame.pixels[i].g, 0);
        zassert_equal(frame.pixels[i].b, i == 2U ? K380_TEST_BLUE_20_PERCENT : 0U);
    }
}

static void assert_power_red(struct captured_frame frame) {
    zassert_equal(frame.pixel_count, ARRAY_SIZE(frame.pixels));
    for (size_t i = 0; i < ARRAY_SIZE(frame.pixels); i++) {
        zassert_equal(frame.pixels[i].r, i == 3U ? 24U : 0U);
        zassert_equal(frame.pixels[i].g, 0U);
        zassert_equal(frame.pixels[i].b, 0U);
    }
}

static void assert_charging_frame(struct captured_frame frame, bool with_ble) {
    zassert_equal(frame.status, K380_STATUS_Z2_CHARGING);
    zassert_equal(frame.pixel_count, ARRAY_SIZE(frame.pixels));
    for (size_t i = 0; i < 3; i++) {
        zassert_equal(frame.pixels[i].r, 0U);
        zassert_equal(frame.pixels[i].g, 0U);
        if (with_ble && i == 2U) {
            zassert_true(frame.pixels[i].b == 0U ||
                         frame.pixels[i].b == K380_TEST_BLUE_20_PERCENT);
        } else {
            zassert_equal(frame.pixels[i].b, 0U);
        }
    }
    zassert_equal(frame.pixels[3].r, 0U);
    zassert_not_equal(frame.pixels[3].g, 0U);
    zassert_equal(frame.pixels[3].g, frame.pixels[3].b);
}

static void block_render_queue(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&render_queue_blocked);
    (void)k_sem_take(&release_render_queue, K_MSEC(2000));
}
K_WORK_DEFINE(queue_blocker, block_render_queue);

static void disconnected_profile_notification(struct k_work *work) {
    ARG_UNUSED(work);
    /* Mirror update_active_slot_status after a queued disconnect notification. */
    k380_low_power_cancel_pending();
    k380_status_indicator_clear(K380_STATUS_Z6_BLE_CONNECTED);
    (void)k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING);
    k_sem_give(&status_mutation_completed);
}
K_WORK_DEFINE(status_mutation, disconnected_profile_notification);

static void start_real_animation(enum k380_status_id status) {
    use_real_timer = true;
    reset_render_capture();
    zassert_ok(k380_status_indicator_set(status));
}

ZTEST(k380_status_indicator, test_status_priority_order) {
    const enum k380_status_id zmk_low_to_high[] = {
        K380_STATUS_Z1_NORMAL, K380_STATUS_Z6_BLE_CONNECTED, K380_STATUS_Z5_BLE_WAITING,
        K380_STATUS_Z7_BLE_PAIRING, K380_STATUS_Z3_LOW_BATTERY, K380_STATUS_Z2_CHARGING,
        K380_STATUS_Z8_BOOTLOADER_REQUEST, K380_STATUS_Z9_MATRIX_FAULT,
        K380_STATUS_Z4_SOFT_OFF_WARNING,
    };
    for (size_t i = 0; i < ARRAY_SIZE(zmk_low_to_high); i++) {
        zassert_ok(k380_status_indicator_set(zmk_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), zmk_low_to_high[i]);
    }
    for (size_t i = 1; i < ARRAY_SIZE(zmk_low_to_high) - 1; i++) {
        zassert_ok(k380_status_indicator_set(zmk_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), K380_STATUS_Z4_SOFT_OFF_WARNING);
    }
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z9_MATRIX_FAULT));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z8_BOOTLOADER_REQUEST));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z9_MATRIX_FAULT);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z7_BLE_PAIRING);
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z5_BLE_WAITING);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));

    const enum k380_status_id bootloader_low_to_high[] = {
        K380_STATUS_B1_BOOTLOADER_WAITING, K380_STATUS_B2_BOOTLOADER_CDC_ONLY,
        K380_STATUS_B4_BOOTLOADER_WRITE_SUCCESS, K380_STATUS_B5_BOOTLOADER_WRITE_FAILED,
        K380_STATUS_B6_BOOTLOADER_LOW_POWER, K380_STATUS_B3_BOOTLOADER_WRITING,
    };
    for (size_t i = 0; i < ARRAY_SIZE(bootloader_low_to_high); i++) {
        zassert_ok(k380_status_indicator_set(bootloader_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), bootloader_low_to_high[i]);
    }
    for (size_t i = 0; i < ARRAY_SIZE(bootloader_low_to_high) - 1; i++) {
        zassert_ok(k380_status_indicator_set(bootloader_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), K380_STATUS_B3_BOOTLOADER_WRITING);
    }
    reset_render_capture();
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z2_CHARGING));
    struct captured_frame frame = capture_snapshot();
    zassert_equal(frame.frame_count, 1U);
    assert_charging_frame(frame, false);
    const uint8_t first_green = frame.pixels[3].g;
    for (int i = 0; i < 20; i++) {
        k380_status_indicator_animation_step();
    }
    frame = capture_snapshot();
    assert_charging_frame(frame, false);
    zassert_not_equal(frame.pixels[3].g, first_green);
    zassert_true(frame.pixels[3].g < 24U, "charging period must exceed two seconds");
}

ZTEST(k380_status_indicator, test_ble_waiting_status_slow_blinks_blue) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    assert_only_slot_1_blue(capture_snapshot());
    k380_status_indicator_animation_step();
    assert_all_pixels_off(capture_snapshot());
    k380_status_indicator_animation_step();
    assert_only_slot_1_blue(capture_snapshot());
}

ZTEST(k380_status_indicator, test_ble_pairing_status_fast_blinks_blue) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    assert_only_slot_1_blue(capture_snapshot());
    k380_status_indicator_animation_step();
    assert_all_pixels_off(capture_snapshot());
    k380_status_indicator_animation_step();
    assert_only_slot_1_blue(capture_snapshot());
}

ZTEST(k380_status_indicator, test_charging_and_ble_slot_status_are_composed) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z2_CHARGING));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    struct captured_frame frame = capture_snapshot();
    assert_charging_frame(frame, true);
    zassert_equal(frame.pixels[2].b, K380_TEST_BLUE_20_PERCENT);
    const uint8_t first_green = frame.pixels[3].g;
    bool saw_breath_change = false;
    bool saw_ble_off = false;
    for (int i = 0; i < 20; i++) {
        k380_status_indicator_animation_step();
        frame = capture_snapshot();
        assert_charging_frame(frame, true);
        saw_breath_change |= frame.pixels[3].g != first_green;
        saw_ble_off |= frame.pixels[2].b == 0U;
    }
    zassert_true(saw_breath_change && saw_ble_off);
}

ZTEST(k380_status_indicator, test_low_battery_only_renders_one_second_edges) {
    start_real_animation(K380_STATUS_Z3_LOW_BATTERY);
    assert_power_red(capture_snapshot());
    k_sleep(K_MSEC(800));
    zassert_equal(capture_snapshot().frame_count, 1U);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z3_LOW_BATTERY));
    assert_all_pixels_off(wait_for_frames(2U, 300));
    assert_power_red(wait_for_frames(3U, 1100));
}

ZTEST(k380_status_indicator, test_waiting_only_renders_one_second_edges) {
    start_real_animation(K380_STATUS_Z5_BLE_WAITING);
    k_sleep(K_MSEC(800));
    struct captured_frame frame = capture_snapshot();
    zassert_equal(frame.frame_count, 1U);
    assert_only_slot_1_blue(frame);
    assert_all_pixels_off(wait_for_frames(2U, 300));
    assert_only_slot_1_blue(wait_for_frames(3U, 1100));
}

ZTEST(k380_status_indicator, test_pairing_only_renders_quarter_second_edges) {
    start_real_animation(K380_STATUS_Z7_BLE_PAIRING);
    k_sleep(K_MSEC(150));
    struct captured_frame frame = capture_snapshot();
    zassert_equal(frame.frame_count, 1U);
    assert_only_slot_1_blue(frame);
    assert_all_pixels_off(wait_for_frames(2U, 200));
    assert_only_slot_1_blue(wait_for_frames(3U, 350));
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
    const size_t stopped_frames = capture_snapshot().frame_count;
    k_sleep(K_MSEC(350));
    zassert_equal(capture_snapshot().frame_count, stopped_frames);
    zassert_false(k380_status_indicator_animation_active());
}

ZTEST(k380_status_indicator, test_soft_off_warning_renders_quarter_second_edges) {
    start_real_animation(K380_STATUS_Z4_SOFT_OFF_WARNING);
    assert_power_red(capture_snapshot());
    k_sleep(K_MSEC(150));
    zassert_equal(capture_snapshot().frame_count, 1U);
    assert_all_pixels_off(wait_for_frames(2U, 200));
}

ZTEST(k380_status_indicator, test_quiet_cancels_pending_animation_and_work) {
    k_work_submit(&queue_blocker);
    zassert_ok(k_sem_take(&render_queue_blocked, K_MSEC(100)));
    start_real_animation(K380_STATUS_Z7_BLE_PAIRING);
    k_sleep(K_MSEC(300));
    zassert_ok(k380_soft_off_stop_animation());
    k_sem_give(&release_render_queue);
    zassert_false(k380_status_indicator_animation_active());
    assert_all_pixels_off(capture_snapshot());
    const size_t quiet_frames = capture_snapshot().frame_count;
    k380_status_indicator_animation_step();
    k_sleep(K_MSEC(350));
    zassert_equal(capture_snapshot().frame_count, quiet_frames);
    k380_status_indicator_stop_animation();
    zassert_equal(capture_snapshot().frame_count, quiet_frames);
}

ZTEST(k380_status_indicator, test_low_voltage_release_wait_stays_quiet_after_disconnect) {
    use_real_timer = true;
    zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z6_BLE_CONNECTED));
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
    zassert_true(k380_low_power_is_release_waiting());
    const size_t quiet_frames = capture_snapshot().frame_count;
    k_work_submit(&status_mutation);
    zassert_ok(k_sem_take(&status_mutation_completed, K_MSEC(100)));
    zassert_true(k380_low_power_is_release_waiting());
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z5_BLE_WAITING);
    zassert_false(k380_status_indicator_animation_active());
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    k_sleep(K_MSEC(1100));
    struct captured_frame frame = capture_snapshot();
    zassert_equal(frame.frame_count, quiet_frames);
    assert_all_pixels_off(frame);
}

ZTEST(k380_status_indicator, test_safe_timeout_cancellation_explicitly_resumes_animation) {
    use_real_timer = true;
    zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT));
    zassert_false(k380_status_indicator_animation_active());
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    const size_t quiet_frames = capture_snapshot().frame_count;
    k380_low_power_cancel_pending();
    zassert_false(k380_low_power_is_release_waiting());
    zassert_true(k380_status_indicator_animation_active());
    struct captured_frame frame = capture_snapshot();
    zassert_equal(frame.frame_count, quiet_frames + 1U);
    assert_only_slot_1_blue(frame);
    k380_status_indicator_resume_animation();
    zassert_equal(capture_snapshot().frame_count, frame.frame_count);
    assert_all_pixels_off(wait_for_frames(frame.frame_count + 1U, 1100));
}

ZTEST(k380_status_indicator, test_usb_cancellation_resumes_led_without_battery_ble_permission) {
    use_real_timer = true;
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z2_CHARGING));
    const size_t quiet_frames = capture_snapshot().frame_count;
    zassert_false(k380_status_indicator_animation_active());
    k380_low_power_test_set_battery_charging(true);
    zassert_equal(k380_low_power_startup_voltage_result(true, true, true), -EACCES);
    zassert_false(k380_low_power_is_release_waiting());
    zassert_false(k380_low_power_ble_start_allowed());
    zassert_true(k380_status_indicator_animation_active());
    assert_charging_frame(wait_for_frames(quiet_frames + 2U, 150), false);
}

ZTEST(k380_status_indicator, test_unsafe_voltage_cancellation_keeps_led_quiet) {
    zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_BLE_WAIT_TIMEOUT));
    const size_t quiet_frames = capture_snapshot().frame_count;
    zassert_equal(k380_low_power_startup_voltage_result(false, false, false), -EACCES);
    k380_low_power_cancel_pending();
    zassert_false(k380_status_indicator_animation_active());
    zassert_equal(capture_snapshot().frame_count, quiet_frames);
}

ZTEST(k380_status_indicator, test_unconfirmed_usb_cancellation_keeps_led_quiet) {
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
    const size_t quiet_frames = capture_snapshot().frame_count;
    zassert_equal(k380_low_power_startup_voltage_result(false, false, false), -EACCES);
    k380_low_power_cancel_usb_pending();
    zassert_false(k380_status_indicator_animation_active());
    zassert_equal(capture_snapshot().frame_count, quiet_frames);
}

ZTEST(k380_status_indicator, test_invalid_charging_qualification_cannot_resume_quiet_leds) {
    zassert_ok(k380_low_power_startup_voltage_result(true, false, true));
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z2_CHARGING));
    const size_t quiet_frames = capture_snapshot().frame_count;
    zassert_equal(k380_low_power_startup_voltage_result(false, true, true), -EACCES);
    zassert_true(k380_low_power_is_release_waiting());
    zassert_false(k380_status_indicator_animation_active());
    zassert_equal(capture_snapshot().frame_count, quiet_frames);
}

ZTEST(k380_status_indicator, test_static_status_cancels_animation) {
    start_real_animation(K380_STATUS_Z5_BLE_WAITING);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z9_MATRIX_FAULT));
    zassert_false(k380_status_indicator_animation_active());
    const size_t static_frames = capture_snapshot().frame_count;
    k_sleep(K_MSEC(1100));
    zassert_equal(capture_snapshot().frame_count, static_frames);
}

ZTEST(k380_status_indicator, test_usb_pairing_stop_preserves_charging_channel) {
    start_real_animation(K380_STATUS_Z2_CHARGING);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
    struct captured_frame frame = capture_snapshot();
    assert_charging_frame(frame, false);
    assert_charging_frame(wait_for_frames(frame.frame_count + 1U, 150), false);
}

static void before_test(void *fixture) {
    ARG_UNUSED(fixture);
    use_real_timer = false;
    k_sem_reset(&render_queue_blocked);
    k_sem_reset(&release_render_queue);
    k_sem_reset(&status_mutation_completed);
    k380_low_power_test_reset();
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    k380_status_indicator_resume_animation();
    reset_render_capture();
}

static void after_test(void *fixture) {
    ARG_UNUSED(fixture);
    /* Drain test-owned work even when an assertion aborted a blocked-queue test. */
    struct k_work_sync sync;
    k_sem_give(&release_render_queue);
    (void)k_work_flush(&queue_blocker, &sync);
    (void)k_work_flush(&status_mutation, &sync);
    k380_status_indicator_stop_animation();
}

ZTEST_SUITE(k380_status_indicator, NULL, NULL, before_test, after_test, NULL);
