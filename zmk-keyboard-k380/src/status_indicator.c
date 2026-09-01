#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <zmk_keyboard_k380/status_indicator.h>
#include <zmk_keyboard_k380/ble_slot_policy.h>

static enum k380_status_id current_status = K380_STATUS_Z1_NORMAL;
static struct led_rgb status_pixels[4];

#if !IS_ENABLED(CONFIG_ZTEST)
#if !DT_HAS_CHOSEN(zmk_underglow)
#error "A zmk,underglow chosen node must be declared"
#endif

static const struct device *status_led_strip;
#endif

#ifdef CONFIG_ZTEST
extern void k380_status_indicator_test_render(enum k380_status_id status,
                                              const struct led_rgb *pixels, size_t pixel_count);
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

static uint8_t slot_led_index(void) {
    switch (k380_ble_slot_current()) {
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

static int render_zmk_status(enum k380_status_id status) {
    const uint8_t index = slot_led_index();

    switch (status) {
    case K380_STATUS_Z1_NORMAL:
        return 0;
    case K380_STATUS_Z2_CHARGING:
        status_pixels[3] = rgb(0, 24, 24);
        return 0;
    case K380_STATUS_Z3_LOW_BATTERY:
        status_pixels[3] = rgb(24, 0, 0);
        return 0;
    case K380_STATUS_Z4_SOFT_OFF_WARNING:
        status_pixels[3] = rgb(24, 0, 0);
        return 0;
    case K380_STATUS_Z5_BLE_WAITING:
        status_pixels[index] = rgb(0, 0, 24);
        return 0;
    case K380_STATUS_Z6_BLE_CONNECTED:
        status_pixels[index] = rgb(0, 24, 0);
        return 0;
    case K380_STATUS_Z7_BLE_PAIRING:
        status_pixels[index] = rgb(12, 0, 24);
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

static int render_status(enum k380_status_id status) {
    clear_pixels();

    if (status >= K380_STATUS_B1_BOOTLOADER_WAITING && status <= K380_STATUS_B6_BOOTLOADER_LOW_POWER) {
        int err = render_bootloader_status(status);
        if (err < 0) {
            return err;
        }
    } else if (status >= K380_STATUS_Z1_NORMAL && status <= K380_STATUS_Z9_MATRIX_FAULT) {
        int err = render_zmk_status(status);
        if (err < 0) {
            return err;
        }
    } else {
        return -EINVAL;
    }

#if IS_ENABLED(CONFIG_ZTEST)
    k380_status_indicator_test_render(status, status_pixels, ARRAY_SIZE(status_pixels));
    return 0;
#else
    if (!device_is_ready(status_led_strip)) {
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

static int status_priority(enum k380_status_id status) {
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
    case K380_STATUS_Z1_NORMAL:
        return 1;
    case K380_STATUS_Z6_BLE_CONNECTED:
        return 2;
    case K380_STATUS_Z5_BLE_WAITING:
        return 3;
    case K380_STATUS_Z7_BLE_PAIRING:
        return 4;
    case K380_STATUS_Z3_LOW_BATTERY:
        return 5;
    case K380_STATUS_Z2_CHARGING:
        return 6;
    case K380_STATUS_Z8_BOOTLOADER_REQUEST:
        return 7;
    case K380_STATUS_Z9_MATRIX_FAULT:
        return 8;
    case K380_STATUS_Z4_SOFT_OFF_WARNING:
        return 9;
    default:
        return -EINVAL;
    }
}

int k380_status_indicator_set(enum k380_status_id status) {
    const bool new_bootloader = is_bootloader_status(status);
    const bool new_zmk = is_zmk_status(status);

    if (!new_bootloader && !new_zmk) {
        return -EINVAL;
    }

    const bool current_bootloader = is_bootloader_status(current_status);
    const bool current_zmk = is_zmk_status(current_status);

    if (current_status == K380_STATUS_Z6_BLE_CONNECTED && status == K380_STATUS_Z1_NORMAL) {
        current_status = status;
        return render_status(current_status);
    }

    if ((new_bootloader && !current_bootloader) || (new_zmk && !current_zmk)) {
        current_status = status;
        return render_status(current_status);
    }

    if (status_priority(status) > status_priority(current_status)) {
        current_status = status;
        return render_status(current_status);
    }

    return 0;
}

void k380_status_indicator_clear(enum k380_status_id status) {
    if (current_status == status) {
        current_status =
            is_bootloader_status(status) ? K380_STATUS_B1_BOOTLOADER_WAITING : K380_STATUS_Z1_NORMAL;
        (void)render_status(current_status);
    }
}

enum k380_status_id k380_status_indicator_current(void) { return current_status; }

#if !IS_ENABLED(CONFIG_ZTEST)
static int k380_status_indicator_init(void) {
    status_led_strip = DEVICE_DT_GET(DT_CHOSEN(zmk_underglow));
    return render_status(current_status);
}

SYS_INIT(k380_status_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif
