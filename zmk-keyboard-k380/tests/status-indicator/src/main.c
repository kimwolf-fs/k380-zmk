#include <stdbool.h>
#include <string.h>

#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/status_indicator.h>
#include <zmk_keyboard_k380/soft_off.h>

static enum k380_status_id last_rendered_status;
static struct led_rgb last_rendered_pixels[4];
static size_t last_rendered_pixel_count;
static size_t rendered_frame_count;

K_SEM_DEFINE(render_queue_blocked, 0, 1);
K_SEM_DEFINE(release_render_queue, 0, 1);

static void block_render_queue(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&render_queue_blocked);
    (void)k_sem_take(&release_render_queue, K_MSEC(1000));
}

K_WORK_DEFINE(queue_blocker, block_render_queue);

#define K380_TEST_BLUE_20_PERCENT 51U

uint8_t k380_ble_slot_current(void) { return 1; }

void k380_status_indicator_test_render(enum k380_status_id status, const struct led_rgb *pixels,
                                       size_t pixel_count) {
    last_rendered_status = status;
    last_rendered_pixel_count = pixel_count;
    memset(last_rendered_pixels, 0, sizeof(last_rendered_pixels));
    memcpy(last_rendered_pixels, pixels,
           MIN(pixel_count, ARRAY_SIZE(last_rendered_pixels)) * sizeof(*pixels));
    rendered_frame_count++;
}

static void reset_render_capture(void) {
    rendered_frame_count = 0;
    last_rendered_pixel_count = 0;
    memset(last_rendered_pixels, 0, sizeof(last_rendered_pixels));
}

static void assert_only_slot_1_blue(void) {
    zassert_equal(last_rendered_pixel_count, ARRAY_SIZE(last_rendered_pixels));
    zassert_equal(last_rendered_pixels[0].r, 0);
    zassert_equal(last_rendered_pixels[0].g, 0);
    zassert_equal(last_rendered_pixels[0].b, 0);
    zassert_equal(last_rendered_pixels[1].r, 0);
    zassert_equal(last_rendered_pixels[1].g, 0);
    zassert_equal(last_rendered_pixels[1].b, 0);
    zassert_equal(last_rendered_pixels[2].r, 0);
    zassert_equal(last_rendered_pixels[2].g, 0);
    zassert_equal(last_rendered_pixels[2].b, K380_TEST_BLUE_20_PERCENT);
    zassert_equal(last_rendered_pixels[3].r, 0);
    zassert_equal(last_rendered_pixels[3].g, 0);
    zassert_equal(last_rendered_pixels[3].b, 0);
}

static void assert_all_pixels_off(void) {
    zassert_equal(last_rendered_pixel_count, ARRAY_SIZE(last_rendered_pixels));
    for (size_t i = 0; i < ARRAY_SIZE(last_rendered_pixels); i++) {
        zassert_equal(last_rendered_pixels[i].r, 0);
        zassert_equal(last_rendered_pixels[i].g, 0);
        zassert_equal(last_rendered_pixels[i].b, 0);
    }
}

ZTEST(k380_status_indicator, test_status_priority_order) {
    const enum k380_status_id zmk_low_to_high[] = {
        K380_STATUS_Z1_NORMAL,
        K380_STATUS_Z6_BLE_CONNECTED,
        K380_STATUS_Z5_BLE_WAITING,
        K380_STATUS_Z7_BLE_PAIRING,
        K380_STATUS_Z3_LOW_BATTERY,
        K380_STATUS_Z2_CHARGING,
        K380_STATUS_Z8_BOOTLOADER_REQUEST,
        K380_STATUS_Z9_MATRIX_FAULT,
        K380_STATUS_Z4_SOFT_OFF_WARNING,
    };

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    for (size_t i = 0; i < ARRAY_SIZE(zmk_low_to_high); i++) {
        zassert_ok(k380_status_indicator_set(zmk_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), zmk_low_to_high[i],
                      "unexpected ZMK priority winner at index %u", (unsigned int)i);
    }

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z4_SOFT_OFF_WARNING));
    for (size_t i = 1; i < ARRAY_SIZE(zmk_low_to_high) - 1; i++) {
        zassert_ok(k380_status_indicator_set(zmk_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), K380_STATUS_Z4_SOFT_OFF_WARNING,
                      "lower ZMK status unexpectedly replaced Z4 at index %u", (unsigned int)i);
    }

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z9_MATRIX_FAULT));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z8_BOOTLOADER_REQUEST));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z9_MATRIX_FAULT,
                  "lower system status unexpectedly replaced Z9");

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z7_BLE_PAIRING,
                  "lower BLE status unexpectedly replaced Z7");
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z5_BLE_WAITING,
                  "explicit clear should allow a lower BLE status");

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z1_NORMAL);

    const enum k380_status_id bootloader_low_to_high[] = {
        K380_STATUS_B1_BOOTLOADER_WAITING,       K380_STATUS_B2_BOOTLOADER_CDC_ONLY,
        K380_STATUS_B4_BOOTLOADER_WRITE_SUCCESS, K380_STATUS_B5_BOOTLOADER_WRITE_FAILED,
        K380_STATUS_B6_BOOTLOADER_LOW_POWER,     K380_STATUS_B3_BOOTLOADER_WRITING,
    };

    zassert_ok(k380_status_indicator_set(K380_STATUS_B1_BOOTLOADER_WAITING));
    for (size_t i = 0; i < ARRAY_SIZE(bootloader_low_to_high); i++) {
        zassert_ok(k380_status_indicator_set(bootloader_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), bootloader_low_to_high[i],
                      "unexpected bootloader priority winner at index %u", (unsigned int)i);
    }

    zassert_ok(k380_status_indicator_set(K380_STATUS_B3_BOOTLOADER_WRITING));
    for (size_t i = 0; i < ARRAY_SIZE(bootloader_low_to_high) - 1; i++) {
        zassert_ok(k380_status_indicator_set(bootloader_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), K380_STATUS_B3_BOOTLOADER_WRITING,
                      "lower bootloader status unexpectedly replaced B3 at index %u",
                      (unsigned int)i);
    }

    rendered_frame_count = 0;
    memset(last_rendered_pixels, 0, sizeof(last_rendered_pixels));
    last_rendered_pixel_count = 0;

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z2_CHARGING));

    zassert_equal(rendered_frame_count, 1U, "status change should render a WS2812B frame");
    zassert_equal(last_rendered_status, K380_STATUS_Z2_CHARGING);
    zassert_equal(last_rendered_pixel_count, ARRAY_SIZE(last_rendered_pixels));
    zassert_equal(last_rendered_pixels[0].r, 0);
    zassert_equal(last_rendered_pixels[0].g, 0);
    zassert_equal(last_rendered_pixels[0].b, 0);
    zassert_equal(last_rendered_pixels[1].r, 0);
    zassert_equal(last_rendered_pixels[1].g, 0);
    zassert_equal(last_rendered_pixels[1].b, 0);
    zassert_equal(last_rendered_pixels[2].r, 0);
    zassert_equal(last_rendered_pixels[2].g, 0);
    zassert_equal(last_rendered_pixels[2].b, 0);
    zassert_equal(last_rendered_pixels[3].r, 0);
    zassert_not_equal(last_rendered_pixels[3].g, 0);
    zassert_not_equal(last_rendered_pixels[3].b, 0);

    const uint8_t first_green = last_rendered_pixels[3].g;
    for (int i = 0; i < 20; i++) {
        k380_status_indicator_animation_step();
    }

    zassert_true(rendered_frame_count > 1U, "charging status should render animation frames");
    zassert_equal(last_rendered_status, K380_STATUS_Z2_CHARGING);
    zassert_equal(last_rendered_pixels[0].r, 0);
    zassert_equal(last_rendered_pixels[0].g, 0);
    zassert_equal(last_rendered_pixels[0].b, 0);
    zassert_equal(last_rendered_pixels[1].r, 0);
    zassert_equal(last_rendered_pixels[1].g, 0);
    zassert_equal(last_rendered_pixels[1].b, 0);
    zassert_equal(last_rendered_pixels[2].r, 0);
    zassert_equal(last_rendered_pixels[2].g, 0);
    zassert_equal(last_rendered_pixels[2].b, 0);
    zassert_equal(last_rendered_pixels[3].r, 0);
    zassert_equal(last_rendered_pixels[3].g, last_rendered_pixels[3].b);
    zassert_not_equal(last_rendered_pixels[3].g, first_green,
                      "charging status should breathe instead of staying at fixed cyan");
    zassert_true(last_rendered_pixels[3].g < 24U,
                 "charging breathe period should be longer than 2 seconds");
}

ZTEST(k380_status_indicator, test_ble_waiting_status_slow_blinks_blue) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    reset_render_capture();

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));

    zassert_equal(last_rendered_status, K380_STATUS_Z5_BLE_WAITING);
    assert_only_slot_1_blue();

    k380_status_indicator_animation_step();
    assert_all_pixels_off();

    k380_status_indicator_animation_step();
    assert_only_slot_1_blue();
}

ZTEST(k380_status_indicator, test_ble_pairing_status_fast_blinks_blue) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    reset_render_capture();

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));

    zassert_equal(last_rendered_status, K380_STATUS_Z7_BLE_PAIRING);
    assert_only_slot_1_blue();

    k380_status_indicator_animation_step();
    assert_all_pixels_off();

    k380_status_indicator_animation_step();
    assert_only_slot_1_blue();
}

ZTEST(k380_status_indicator, test_charging_and_ble_slot_status_are_composed) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));

    rendered_frame_count = 0;
    memset(last_rendered_pixels, 0, sizeof(last_rendered_pixels));

    zassert_ok(k380_status_indicator_set(K380_STATUS_Z2_CHARGING));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));

    zassert_equal(last_rendered_status, K380_STATUS_Z2_CHARGING);
    zassert_equal(last_rendered_pixel_count, ARRAY_SIZE(last_rendered_pixels));
    zassert_equal(last_rendered_pixels[0].r, 0);
    zassert_equal(last_rendered_pixels[0].g, 0);
    zassert_equal(last_rendered_pixels[0].b, 0);
    zassert_equal(last_rendered_pixels[1].r, 0);
    zassert_equal(last_rendered_pixels[1].g, 0);
    zassert_equal(last_rendered_pixels[1].b, 0);
    zassert_equal(last_rendered_pixels[2].r, 0);
    zassert_equal(last_rendered_pixels[2].g, 0);
    zassert_equal(last_rendered_pixels[2].b, K380_TEST_BLUE_20_PERCENT);
    zassert_equal(last_rendered_pixels[3].r, 0);
    zassert_not_equal(last_rendered_pixels[3].g, 0);
    zassert_equal(last_rendered_pixels[3].g, last_rendered_pixels[3].b);

    const uint8_t first_green = last_rendered_pixels[3].g;
    bool saw_breath_change = false;
    bool saw_ble_on = last_rendered_pixels[2].b == K380_TEST_BLUE_20_PERCENT;
    bool saw_ble_off = false;
    for (int i = 0; i < 20; i++) {
        k380_status_indicator_animation_step();

        zassert_equal(last_rendered_pixels[0].r, 0);
        zassert_equal(last_rendered_pixels[0].g, 0);
        zassert_equal(last_rendered_pixels[0].b, 0);
        zassert_equal(last_rendered_pixels[1].r, 0);
        zassert_equal(last_rendered_pixels[1].g, 0);
        zassert_equal(last_rendered_pixels[1].b, 0);
        zassert_equal(last_rendered_pixels[2].r, 0);
        zassert_equal(last_rendered_pixels[2].g, 0);
        zassert_true(last_rendered_pixels[2].b == 0 ||
                     last_rendered_pixels[2].b == K380_TEST_BLUE_20_PERCENT);
        zassert_equal(last_rendered_pixels[3].r, 0);
        zassert_equal(last_rendered_pixels[3].g, last_rendered_pixels[3].b);

        if (last_rendered_pixels[3].g != first_green) {
            saw_breath_change = true;
        }
        if (last_rendered_pixels[2].b == K380_TEST_BLUE_20_PERCENT) {
            saw_ble_on = true;
        }
        if (last_rendered_pixels[2].b == 0) {
            saw_ble_off = true;
        }
    }

    zassert_true(saw_breath_change,
                 "charging status should breathe while BLE slot remains visible");
    zassert_true(saw_ble_on && saw_ble_off,
                 "BLE waiting status should blink while charging remains visible");
}

ZTEST(k380_status_indicator, test_low_battery_only_renders_one_second_edges) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    reset_render_capture();
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z3_LOW_BATTERY));
    zassert_equal(last_rendered_pixels[3].r, 24U);
    k_sleep(K_MSEC(800));
    zassert_equal(rendered_frame_count, 1U);
    /* Reasserting the same model must not postpone the next edge. */
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z3_LOW_BATTERY));
    k_sleep(K_MSEC(300));
    zassert_equal(rendered_frame_count, 2U);
    assert_all_pixels_off();
    k_sleep(K_MSEC(1000));
    zassert_equal(rendered_frame_count, 3U);
    zassert_equal(last_rendered_pixels[3].r, 24U);
}

ZTEST(k380_status_indicator, test_waiting_only_renders_one_second_edges) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    reset_render_capture();
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    k_sleep(K_MSEC(800));
    zassert_equal(rendered_frame_count, 1U);
    assert_only_slot_1_blue();
    k_sleep(K_MSEC(300));
    zassert_equal(rendered_frame_count, 2U);
    assert_all_pixels_off();
    k_sleep(K_MSEC(1000));
    zassert_equal(rendered_frame_count, 3U);
    assert_only_slot_1_blue();
}

ZTEST(k380_status_indicator, test_pairing_only_renders_quarter_second_edges) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    reset_render_capture();
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    k_sleep(K_MSEC(150));
    zassert_equal(rendered_frame_count, 1U);
    assert_only_slot_1_blue();
    k_sleep(K_MSEC(150));
    zassert_equal(rendered_frame_count, 2U);
    assert_all_pixels_off();
    k_sleep(K_MSEC(250));
    zassert_equal(rendered_frame_count, 3U);
    assert_only_slot_1_blue();
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
    const size_t stopped_frames = rendered_frame_count;
    k_sleep(K_MSEC(350));
    zassert_equal(rendered_frame_count, stopped_frames);
    zassert_false(k380_status_indicator_animation_active());
}

ZTEST(k380_status_indicator, test_soft_off_warning_renders_quarter_second_edges) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    reset_render_capture();
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z4_SOFT_OFF_WARNING));
    zassert_equal(last_rendered_pixels[3].r, 24U);
    k_sleep(K_MSEC(150));
    zassert_equal(rendered_frame_count, 1U);
    k_sleep(K_MSEC(150));
    zassert_equal(rendered_frame_count, 2U);
    assert_all_pixels_off();
}

ZTEST(k380_status_indicator, test_quiet_cancels_pending_animation_and_work) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    k_work_submit(&queue_blocker);
    zassert_ok(k_sem_take(&render_queue_blocked, K_MSEC(100)));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    /* Leave an expired animation render queued behind the blocked worker. */
    k_sleep(K_MSEC(300));
    zassert_ok(k380_soft_off_stop_animation());
    k_sem_give(&release_render_queue);
    zassert_false(k380_status_indicator_animation_active());
    assert_all_pixels_off();
    const size_t quiet_frames = rendered_frame_count;
    k380_status_indicator_animation_step();
    k_sleep(K_MSEC(350));
    zassert_equal(rendered_frame_count, quiet_frames);
    k380_status_indicator_stop_animation();
    zassert_equal(rendered_frame_count, quiet_frames, "quiet must be idempotent");
}

ZTEST(k380_status_indicator, test_static_status_cancels_animation) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z5_BLE_WAITING));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z9_MATRIX_FAULT));
    zassert_false(k380_status_indicator_animation_active());
    const size_t static_frames = rendered_frame_count;
    k_sleep(K_MSEC(1100));
    zassert_equal(rendered_frame_count, static_frames);
}

ZTEST(k380_status_indicator, test_usb_pairing_stop_preserves_charging_channel) {
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z1_NORMAL));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z2_CHARGING));
    zassert_ok(k380_status_indicator_set(K380_STATUS_Z7_BLE_PAIRING));
    k380_status_indicator_clear(K380_STATUS_Z7_BLE_PAIRING);
    zassert_equal(k380_status_indicator_current(), K380_STATUS_Z2_CHARGING);
    zassert_equal(last_rendered_pixels[2].b, 0U);
    zassert_not_equal(last_rendered_pixels[3].g, 0U);
    const size_t stopped_frames = rendered_frame_count;
    k_sleep(K_MSEC(150));
    zassert_true(rendered_frame_count > stopped_frames,
                 "USB breathing must continue after the BLE channel stops");
    zassert_equal(last_rendered_pixels[2].b, 0U);
}

static void after_test(void *fixture) {
    ARG_UNUSED(fixture);
    k380_status_indicator_stop_animation();
}

ZTEST_SUITE(k380_status_indicator, NULL, NULL, NULL, after_test, NULL);
