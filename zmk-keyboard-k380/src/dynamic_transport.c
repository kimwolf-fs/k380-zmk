#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk_keyboard_k380/dynamic_protocol.h>

LOG_MODULE_REGISTER(k380_dynamic_transport, LOG_LEVEL_INF);

#define UART_DEVICE_NODE DT_CHOSEN(zmk_studio_rpc_uart)

#if DT_NODE_EXISTS(UART_DEVICE_NODE)

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
static struct k380_dynamic_protocol_parser parser;

static void dynamic_transport_rx(void)
{
    uint8_t request;
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len;

    for (;;) {
        if (uart_poll_in(uart_dev, &request) != 0) {
            k_sleep(K_MSEC(1));
            continue;
        }
        const enum k380_dynamic_result result = k380_dynamic_protocol_feed(
            &parser, &request, 1U, response, sizeof(response), &response_len);
        if (result == K380_DYNAMIC_RESULT_NEED_MORE || response_len == 0U) {
            continue;
        }
        for (size_t index = 0; index < response_len; index++) {
            uart_poll_out(uart_dev, response[index]);
        }
    }
}

K_THREAD_DEFINE(k380_dynamic_transport_rx_thread, 1024, dynamic_transport_rx, NULL, NULL, NULL, 9,
                0, 0);

static int dynamic_transport_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("Studio CDC UART is unavailable");
        return -ENODEV;
    }
    k380_dynamic_protocol_parser_init(&parser);
    return 0;
}

SYS_INIT(dynamic_transport_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#else

static int dynamic_transport_init(void)
{
    LOG_ERR("Studio CDC UART is unavailable");
    return -ENODEV;
}

SYS_INIT(dynamic_transport_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#endif
