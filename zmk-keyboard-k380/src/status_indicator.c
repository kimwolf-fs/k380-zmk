#include <errno.h>
#include <stdbool.h>

#include <zmk_keyboard_k380/status_indicator.h>

static enum k380_status_id current_status = K380_STATUS_Z1_NORMAL;

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
        return 0;
    }

    if ((new_bootloader && !current_bootloader) || (new_zmk && !current_zmk)) {
        current_status = status;
        return 0;
    }

    if (status_priority(status) > status_priority(current_status)) {
        current_status = status;
    }

    return 0;
}

void k380_status_indicator_clear(enum k380_status_id status) {
    if (current_status == status) {
        current_status =
            is_bootloader_status(status) ? K380_STATUS_B1_BOOTLOADER_WAITING : K380_STATUS_Z1_NORMAL;
    }
}

enum k380_status_id k380_status_indicator_current(void) { return current_status; }
