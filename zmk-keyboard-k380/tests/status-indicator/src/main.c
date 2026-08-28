#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/status_indicator.h>

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
    for (size_t i = 0; i < ARRAY_SIZE(zmk_low_to_high) - 1; i++) {
        zassert_ok(k380_status_indicator_set(zmk_low_to_high[i]));
        zassert_equal(k380_status_indicator_current(), K380_STATUS_Z4_SOFT_OFF_WARNING,
                      "lower ZMK status unexpectedly replaced Z4 at index %u",
                      (unsigned int)i);
    }

    const enum k380_status_id bootloader_low_to_high[] = {
        K380_STATUS_B1_BOOTLOADER_WAITING,
        K380_STATUS_B2_BOOTLOADER_CDC_ONLY,
        K380_STATUS_B4_BOOTLOADER_WRITE_SUCCESS,
        K380_STATUS_B5_BOOTLOADER_WRITE_FAILED,
        K380_STATUS_B6_BOOTLOADER_LOW_POWER,
        K380_STATUS_B3_BOOTLOADER_WRITING,
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
}

ZTEST_SUITE(k380_status_indicator, NULL, NULL, NULL, NULL, NULL);
