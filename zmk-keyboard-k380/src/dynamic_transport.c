#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zmk_keyboard_k380/dynamic_protocol.h>

LOG_MODULE_REGISTER(k380_dynamic_transport, LOG_LEVEL_INF);

#define UART_DEVICE_NODE DT_CHOSEN(k380_config_uart)

#if DT_NODE_EXISTS(UART_DEVICE_NODE)

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
static struct k380_dynamic_protocol_parser parser;
static uint8_t magic_prefix[K380_DYNAMIC_PROTOCOL_MAGIC_SIZE];
static size_t magic_prefix_len;
static uint8_t request_frame[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
static size_t request_frame_len;
static size_t request_frame_expected_len;
static uint8_t response_frame[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
static bool dynamic_frame_active;
static bool request_pending;

static void dynamic_transport_work_handler(struct k_work *work);
K_WORK_DEFINE(dynamic_transport_work, dynamic_transport_work_handler);

static size_t forward_prefix(k380_dynamic_transport_passthrough_t passthrough, void *user_data)
{
    size_t suffix_len = MIN(magic_prefix_len, K380_DYNAMIC_PROTOCOL_MAGIC_SIZE);
    while (suffix_len > 0U &&
           memcmp(&magic_prefix[magic_prefix_len - suffix_len], K380_DYNAMIC_PROTOCOL_MAGIC,
                  suffix_len) != 0) {
        suffix_len--;
    }
    const size_t forwarded_len = magic_prefix_len - suffix_len;
    if (forwarded_len > 0U && passthrough != NULL) {
        passthrough(magic_prefix, forwarded_len, user_data);
    }
    if (suffix_len > 0U) {
        memmove(magic_prefix, &magic_prefix[forwarded_len], suffix_len);
    }
    magic_prefix_len = suffix_len;
    return forwarded_len;
}

static void submit_request(void)
{
    dynamic_frame_active = false;
    request_pending = true;
    if (k_work_submit(&dynamic_transport_work) < 0) {
        request_pending = false;
        request_frame_len = 0;
        request_frame_expected_len = 0;
    }
}

static void dynamic_transport_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    size_t response_len = 0;
    size_t consumed_len = 0;
    (void)k380_dynamic_protocol_feed(&parser, request_frame, request_frame_len, response_frame,
                                     sizeof(response_frame), &response_len, &consumed_len);
    if (response_len > 0U) {
        for (size_t index = 0; index < response_len; index++) {
            uart_poll_out(uart_dev, response_frame[index]);
        }
    }
    request_frame_len = 0;
    request_frame_expected_len = 0;
    request_pending = false;
}

size_t k380_dynamic_transport_receive(const uint8_t *data, size_t data_len,
                                      k380_dynamic_transport_passthrough_t passthrough,
                                      void *user_data)
{
    size_t forwarded_len = 0;

    for (size_t index = 0; index < data_len; index++) {
        const uint8_t byte = data[index];
        if (!dynamic_frame_active) {
            if (request_pending) {
                if (passthrough != NULL) {
                    passthrough(&byte, 1U, user_data);
                }
                forwarded_len++;
                continue;
            }
            magic_prefix[magic_prefix_len++] = byte;
            forwarded_len += forward_prefix(passthrough, user_data);
            if (magic_prefix_len == K380_DYNAMIC_PROTOCOL_MAGIC_SIZE) {
                memcpy(request_frame, magic_prefix, magic_prefix_len);
                request_frame_len = magic_prefix_len;
                magic_prefix_len = 0;
                dynamic_frame_active = true;
            }
            continue;
        }

        if (request_frame_len == sizeof(request_frame)) {
            dynamic_frame_active = false;
            request_frame_len = 0;
            request_frame_expected_len = 0;
            continue;
        }
        request_frame[request_frame_len++] = byte;
        if (request_frame_len == K380_DYNAMIC_PROTOCOL_HEADER_SIZE) {
            const uint16_t payload_len = sys_get_le16(&request_frame[12]);
            request_frame_expected_len = K380_DYNAMIC_PROTOCOL_FRAME_SIZE(payload_len);
            if (payload_len > K380_DYNAMIC_MAX_PAYLOAD) {
                submit_request();
                continue;
            }
        }
        if (request_frame_expected_len > 0U && request_frame_len == request_frame_expected_len) {
            submit_request();
        }
    }
    return forwarded_len;
}

static void uart_rx_dispatch(const uint8_t *data, size_t data_len)
{
    (void)k380_dynamic_transport_receive(data, data_len, NULL, NULL);
}

static void serial_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    if (uart_irq_rx_ready(dev)) {
        uint8_t byte;
        while (uart_fifo_read(dev, &byte, 1) == 1) {
            uart_rx_dispatch(&byte, 1U);
        }
    }
}

static int dynamic_transport_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("K380 config CDC UART is unavailable");
        return -ENODEV;
    }

    k380_dynamic_protocol_parser_init(&parser);

    int err = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
    if (err < 0) {
        LOG_ERR("Failed to configure K380 config CDC UART callback: %d", err);
        return err;
    }
    uart_irq_rx_enable(uart_dev);
    return 0;
}

SYS_INIT(dynamic_transport_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#else

size_t k380_dynamic_transport_receive(const uint8_t *data, size_t data_len,
                                      k380_dynamic_transport_passthrough_t passthrough,
                                      void *user_data)
{
    if (data_len > 0U && passthrough != NULL) {
        passthrough(data, data_len, user_data);
    }
    return data_len;
}

#endif
