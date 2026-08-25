/*
 * Copyright (c) 2020-2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Controlled derivative of:
 * app/module/drivers/kscan/kscan_gpio_matrix.c
 * Baseline commit: 6941abc2afab16502cff9c5149d8dc0fcd5112c9
 * Baseline source blob SHA-1: d68f1593009fe22df8e1d3d70af661fe44f8dbf3
 *
 * K380-specific change: collect a complete 8x15 row2col scan frame and
 * filter no-diode rectangular ambiguity before debounce.
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/__assert.h>
#include <SEGGER_RTT.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zmk/debounce.h>
#include <zmk_keyboard_k380/ghost_filter.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT k380_kscan_no_diode_matrix

#define K380_KSCAN_ROWS K380_GHOST_FILTER_ROWS
#define K380_KSCAN_COLS K380_GHOST_FILTER_COLS
#define K380_KSCAN_MATRIX_LEN (K380_KSCAN_ROWS * K380_KSCAN_COLS)

#if CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS >= 0
#define INST_DEBOUNCE_PRESS_MS(n) CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS
#else
#define INST_DEBOUNCE_PRESS_MS(n)                                                                  \
    DT_INST_PROP_OR(n, debounce_period, DT_INST_PROP(n, debounce_press_ms))
#endif

#if CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS >= 0
#define INST_DEBOUNCE_RELEASE_MS(n) CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS
#else
#define INST_DEBOUNCE_RELEASE_MS(n)                                                                \
    DT_INST_PROP_OR(n, debounce_period, DT_INST_PROP(n, debounce_release_ms))
#endif

#define USE_POLLING IS_ENABLED(CONFIG_ZMK_KSCAN_MATRIX_POLLING)
#define USE_INTERRUPTS (!USE_POLLING)

#define COND_INTERRUPTS(code) COND_CODE_1(CONFIG_ZMK_KSCAN_MATRIX_POLLING, (), code)

#define K380_KSCAN_GPIO_GET_BY_IDX(node_id, prop, idx)                                             \
    ((struct k380_kscan_gpio){.spec = GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx), .index = idx})
#define K380_KSCAN_GPIO_LIST(gpio_array)                                                           \
    ((struct k380_kscan_gpio_list){.gpios = gpio_array, .len = ARRAY_SIZE(gpio_array)})
#define K380_KSCAN_ROW_CFG_INIT(idx, inst_idx)                                                     \
    K380_KSCAN_GPIO_GET_BY_IDX(DT_DRV_INST(inst_idx), row_gpios, idx)
#define K380_KSCAN_COL_CFG_INIT(idx, inst_idx)                                                     \
    K380_KSCAN_GPIO_GET_BY_IDX(DT_DRV_INST(inst_idx), col_gpios, idx)

struct k380_kscan_gpio {
    struct gpio_dt_spec spec;
    size_t index;
};

struct k380_kscan_gpio_list {
    struct k380_kscan_gpio *gpios;
    size_t len;
};

struct k380_kscan_gpio_port_state {
    const struct device *port;
    gpio_port_value_t value;
};

struct k380_kscan_irq_callback {
    const struct device *dev;
    struct gpio_callback callback;
};

struct k380_kscan_data {
    const struct device *dev;
    struct k380_kscan_gpio_list inputs;
    kscan_callback_t callback;
    struct k_work_delayable work;
#if USE_INTERRUPTS
    struct k380_kscan_irq_callback *irqs;
#endif
    int64_t scan_time;
    struct zmk_debounce_state *matrix_state;
};

struct k380_kscan_config {
    struct k380_kscan_gpio_list outputs;
    struct zmk_debounce_config debounce_config;
    int32_t debounce_scan_period_ms;
    int32_t poll_period_ms;
};

static int k380_kscan_compare_ports(const void *left, const void *right) {
    const struct k380_kscan_gpio *left_gpio = left;
    const struct k380_kscan_gpio *right_gpio = right;

    return left_gpio->spec.port - right_gpio->spec.port;
}

static void k380_kscan_sort_inputs(struct k380_kscan_gpio_list *inputs) {
    qsort(inputs->gpios, inputs->len, sizeof(inputs->gpios[0]), k380_kscan_compare_ports);
}

static int k380_kscan_pin_get(const struct k380_kscan_gpio *gpio,
                              struct k380_kscan_gpio_port_state *state) {
    if (gpio->spec.port != state->port) {
        state->port = gpio->spec.port;
        const int err = gpio_port_get(state->port, &state->value);

        if (err) {
            return err;
        }
    }

    return (state->value & BIT(gpio->spec.pin)) != 0;
}

static int state_index_rc(const int row, const int col) {
    __ASSERT(row < K380_KSCAN_ROWS, "Invalid row %i", row);
    __ASSERT(col < K380_KSCAN_COLS, "Invalid column %i", col);

    return (col * K380_KSCAN_ROWS) + row;
}

static void k380_rtt_write(const char *message) {
#if IS_ENABLED(CONFIG_USE_SEGGER_RTT)
    SEGGER_RTT_WriteString(0, message);
#else
    printk("%s", message);
#endif
}

static void k380_kscan_diagnostic_report(uint32_t row, uint32_t col, bool pressed) {
#if IS_ENABLED(CONFIG_K380_MATRIX_DIAGNOSTICS_RTT)
    char line[64];
    const int len = snprintk(line, sizeof(line), "K380_MATRIX row=%u col=%u state=%s\n", row,
                             col, pressed ? "down" : "up");

    if (len > 0) {
        k380_rtt_write(line);
    }
#else
    ARG_UNUSED(row);
    ARG_UNUSED(col);
    ARG_UNUSED(pressed);
#endif
}

static void k380_kscan_rtt_report(const char *message) {
#if IS_ENABLED(CONFIG_K380_MATRIX_DIAGNOSTICS_RTT)
    k380_rtt_write(message);
#endif
}

static int k380_kscan_set_all_outputs(const struct device *dev, const int value) {
    const struct k380_kscan_config *config = dev->config;

    for (int i = 0; i < config->outputs.len; i++) {
        const struct gpio_dt_spec *gpio = &config->outputs.gpios[i].spec;
        const int err = gpio_pin_set_dt(gpio, value);

        if (err) {
            LOG_ERR("Failed to set output %i to %i: %i", i, value, err);
            return err;
        }
    }

    return 0;
}

#if USE_INTERRUPTS
static int k380_kscan_interrupt_configure(const struct device *dev, const gpio_flags_t flags) {
    const struct k380_kscan_data *data = dev->data;

    for (int i = 0; i < data->inputs.len; i++) {
        const struct gpio_dt_spec *gpio = &data->inputs.gpios[i].spec;
        const int err = gpio_pin_interrupt_configure_dt(gpio, flags);

        if (err) {
            LOG_ERR("Unable to configure interrupt for pin %u on %s", gpio->pin, gpio->port->name);
            return err;
        }
    }

    return 0;
}

static int k380_kscan_interrupt_enable(const struct device *dev) {
    const int err = k380_kscan_interrupt_configure(dev, GPIO_INT_LEVEL_ACTIVE);

    return err ? err : k380_kscan_set_all_outputs(dev, 1);
}

static int k380_kscan_interrupt_disable(const struct device *dev) {
    const int err = k380_kscan_interrupt_configure(dev, GPIO_INT_DISABLE);

    return err ? err : k380_kscan_set_all_outputs(dev, 0);
}

static void k380_kscan_irq_callback_handler(const struct device *port, struct gpio_callback *cb,
                                            const gpio_port_pins_t pin) {
    struct k380_kscan_irq_callback *irq_data =
        CONTAINER_OF(cb, struct k380_kscan_irq_callback, callback);
    struct k380_kscan_data *data = irq_data->dev->data;

    ARG_UNUSED(port);
    ARG_UNUSED(pin);
    k380_kscan_interrupt_disable(data->dev);
    data->scan_time = k_uptime_get();
    k_work_reschedule(&data->work, K_NO_WAIT);
}
#endif

static void k380_kscan_read_continue(const struct device *dev) {
    const struct k380_kscan_config *config = dev->config;
    struct k380_kscan_data *data = dev->data;

    data->scan_time += config->debounce_scan_period_ms;
    k_work_reschedule(&data->work, K_TIMEOUT_ABS_MS(data->scan_time));
}

static void k380_kscan_read_end(const struct device *dev) {
#if USE_INTERRUPTS
    k380_kscan_interrupt_enable(dev);
#else
    struct k380_kscan_data *data = dev->data;
    const struct k380_kscan_config *config = dev->config;

    data->scan_time += config->poll_period_ms;
    k_work_reschedule(&data->work, K_TIMEOUT_ABS_MS(data->scan_time));
#endif
}

static int k380_kscan_read(const struct device *dev) {
    struct k380_kscan_data *data = dev->data;
    const struct k380_kscan_config *config = dev->config;
    uint16_t raw[K380_KSCAN_ROWS] = {0};
    uint16_t accepted[K380_KSCAN_ROWS] = {0};
    uint16_t filtered[K380_KSCAN_ROWS];
    uint16_t ambiguous[K380_KSCAN_ROWS];

    for (int i = 0; i < config->outputs.len; i++) {
        const struct k380_kscan_gpio *out_gpio = &config->outputs.gpios[i];
        int err = gpio_pin_set_dt(&out_gpio->spec, 1);

        if (err) {
            LOG_ERR("Failed to set output %i active: %i", out_gpio->index, err);
            return err;
        }

#if CONFIG_ZMK_KSCAN_MATRIX_WAIT_BEFORE_INPUTS > 0
        k_busy_wait(CONFIG_ZMK_KSCAN_MATRIX_WAIT_BEFORE_INPUTS);
#endif
        struct k380_kscan_gpio_port_state port_state = {0};

        for (int j = 0; j < data->inputs.len; j++) {
            const struct k380_kscan_gpio *in_gpio = &data->inputs.gpios[j];
            const int active = k380_kscan_pin_get(in_gpio, &port_state);

            if (active < 0) {
                LOG_ERR("Failed to read port %s: %i", in_gpio->spec.port->name, active);
                return active;
            }

            if (active) {
                raw[out_gpio->index] |= BIT(in_gpio->index);
            }
        }

        err = gpio_pin_set_dt(&out_gpio->spec, 0);
        if (err) {
            LOG_ERR("Failed to set output %i inactive: %i", out_gpio->index, err);
            return err;
        }

#if CONFIG_ZMK_KSCAN_MATRIX_WAIT_BETWEEN_OUTPUTS > 0
        k_busy_wait(CONFIG_ZMK_KSCAN_MATRIX_WAIT_BETWEEN_OUTPUTS);
#endif
    }

    for (int row = 0; row < K380_KSCAN_ROWS; row++) {
        for (int col = 0; col < K380_KSCAN_COLS; col++) {
            const struct zmk_debounce_state *state = &data->matrix_state[state_index_rc(row, col)];

            if (zmk_debounce_is_pressed(state)) {
                accepted[row] |= BIT(col);
            }
        }
    }

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    for (int row = 0; row < K380_KSCAN_ROWS; row++) {
        for (int col = 0; col < K380_KSCAN_COLS; col++) {
            zmk_debounce_update(&data->matrix_state[state_index_rc(row, col)],
                                (filtered[row] & BIT(col)) != 0U, config->debounce_scan_period_ms,
                                &config->debounce_config);
        }
    }

    bool continue_scan = false;

    for (int row = 0; row < K380_KSCAN_ROWS; row++) {
        for (int col = 0; col < K380_KSCAN_COLS; col++) {
            struct zmk_debounce_state *state = &data->matrix_state[state_index_rc(row, col)];

            if (zmk_debounce_get_changed(state)) {
                const bool pressed = zmk_debounce_is_pressed(state);

                LOG_DBG("Sending event at %i,%i state %s", row, col, pressed ? "on" : "off");
                k380_kscan_diagnostic_report(row, col, pressed);
                data->callback(dev, row, col, pressed);
            }

            continue_scan = continue_scan || zmk_debounce_is_active(state);
        }
    }

    if (continue_scan) {
        k380_kscan_read_continue(dev);
    } else {
        k380_kscan_read_end(dev);
    }

    return 0;
}

static void k380_kscan_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct k380_kscan_data *data = CONTAINER_OF(dwork, struct k380_kscan_data, work);

    k380_kscan_read(data->dev);
}

static int k380_kscan_configure(const struct device *dev, const kscan_callback_t callback) {
    struct k380_kscan_data *data = dev->data;

    if (!callback) {
        return -EINVAL;
    }

    data->callback = callback;
    return 0;
}

static int k380_kscan_enable(const struct device *dev) {
    struct k380_kscan_data *data = dev->data;

    k380_kscan_rtt_report("K380_KSCAN_INIT ready\n");
    data->scan_time = k_uptime_get();
    return k380_kscan_read(dev);
}

static int k380_kscan_disable(const struct device *dev) {
    struct k380_kscan_data *data = dev->data;

    k_work_cancel_delayable(&data->work);
#if USE_INTERRUPTS
    return k380_kscan_interrupt_disable(dev);
#else
    return 0;
#endif
}

static int k380_kscan_init_input_inst(const struct device *dev,
                                      const struct k380_kscan_gpio *gpio) {
    if (!device_is_ready(gpio->spec.port)) {
        LOG_ERR("GPIO is not ready: %s", gpio->spec.port->name);
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&gpio->spec, GPIO_INPUT);
    if (err) {
        LOG_ERR("Unable to configure pin %u on %s for input", gpio->spec.pin,
                gpio->spec.port->name);
        return err;
    }

#if USE_INTERRUPTS
    struct k380_kscan_data *data = dev->data;
    struct k380_kscan_irq_callback *irq = &data->irqs[gpio->index];

    irq->dev = dev;
    gpio_init_callback(&irq->callback, k380_kscan_irq_callback_handler, BIT(gpio->spec.pin));
    err = gpio_add_callback(gpio->spec.port, &irq->callback);
    if (err) {
        LOG_ERR("Error adding the callback to the input device: %i", err);
        return err;
    }
#endif

    return 0;
}

static int k380_kscan_init_inputs(const struct device *dev) {
    const struct k380_kscan_data *data = dev->data;

    for (int i = 0; i < data->inputs.len; i++) {
        const int err = k380_kscan_init_input_inst(dev, &data->inputs.gpios[i]);
        if (err) {
            return err;
        }
    }

    return 0;
}

static int k380_kscan_init_outputs(const struct device *dev) {
    const struct k380_kscan_config *config = dev->config;

    for (int i = 0; i < config->outputs.len; i++) {
        const struct gpio_dt_spec *gpio = &config->outputs.gpios[i].spec;

        if (!device_is_ready(gpio->port)) {
            LOG_ERR("GPIO is not ready: %s", gpio->port->name);
            return -ENODEV;
        }

        const int err = gpio_pin_configure_dt(gpio, GPIO_OUTPUT);
        if (err) {
            LOG_ERR("Unable to configure pin %u on %s for output", gpio->pin, gpio->port->name);
            return err;
        }
    }

    return 0;
}

#if IS_ENABLED(CONFIG_PM_DEVICE)
static int k380_kscan_disconnect_inputs(const struct device *dev) {
    const struct k380_kscan_data *data = dev->data;

    for (int i = 0; i < data->inputs.len; i++) {
        const int err = gpio_pin_configure_dt(&data->inputs.gpios[i].spec, GPIO_DISCONNECTED);
        if (err) {
            return err;
        }
    }
    return 0;
}

static int k380_kscan_disconnect_outputs(const struct device *dev) {
    const struct k380_kscan_config *config = dev->config;

    for (int i = 0; i < config->outputs.len; i++) {
        const int err = gpio_pin_configure_dt(&config->outputs.gpios[i].spec, GPIO_DISCONNECTED);
        if (err) {
            return err;
        }
    }
    return 0;
}
#endif

static void k380_kscan_setup_pins(const struct device *dev) {
    k380_kscan_init_inputs(dev);
    k380_kscan_init_outputs(dev);
    k380_kscan_set_all_outputs(dev, 0);
}

static int k380_kscan_init(const struct device *dev) {
    struct k380_kscan_data *data = dev->data;

    data->dev = dev;
    k380_kscan_sort_inputs(&data->inputs);
    k_work_init_delayable(&data->work, k380_kscan_work_handler);
    k380_kscan_rtt_report("K380_KSCAN_BOOT ready\n");

#if IS_ENABLED(CONFIG_PM_DEVICE)
    pm_device_init_suspended(dev);
#if IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
    pm_device_runtime_enable(dev);
#endif
#else
    k380_kscan_setup_pins(dev);
#endif
    return 0;
}

#if IS_ENABLED(CONFIG_PM_DEVICE)
static int k380_kscan_pm_action(const struct device *dev, enum pm_device_action action) {
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        k380_kscan_disconnect_inputs(dev);
        k380_kscan_disconnect_outputs(dev);
        return k380_kscan_disable(dev);
    case PM_DEVICE_ACTION_RESUME:
        k380_kscan_setup_pins(dev);
        return k380_kscan_enable(dev);
    default:
        return -ENOTSUP;
    }
}
#endif

static const struct kscan_driver_api k380_kscan_api = {
    .config = k380_kscan_configure,
    .enable_callback = k380_kscan_enable,
    .disable_callback = k380_kscan_disable,
};

#define K380_KSCAN_INIT(n)                                                                         \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, row_gpios) == K380_KSCAN_ROWS,                                \
                 "K380 kscan requires exactly 8 row-gpios");                                       \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, col_gpios) == K380_KSCAN_COLS,                                \
                 "K380 kscan requires exactly 15 col-gpios");                                      \
    BUILD_ASSERT(INST_DEBOUNCE_PRESS_MS(n) <= DEBOUNCE_COUNTER_MAX,                                \
                 "ZMK_KSCAN_DEBOUNCE_PRESS_MS or debounce-press-ms is too large");                 \
    BUILD_ASSERT(INST_DEBOUNCE_RELEASE_MS(n) <= DEBOUNCE_COUNTER_MAX,                              \
                 "ZMK_KSCAN_DEBOUNCE_RELEASE_MS or debounce-release-ms is too large");             \
    static struct k380_kscan_gpio k380_kscan_rows_##n[] = {                                        \
        LISTIFY(8, K380_KSCAN_ROW_CFG_INIT, (, ), n)};                                             \
    static struct k380_kscan_gpio k380_kscan_cols_##n[] = {                                        \
        LISTIFY(15, K380_KSCAN_COL_CFG_INIT, (, ), n)};                                            \
    static struct zmk_debounce_state k380_kscan_state_##n[K380_KSCAN_MATRIX_LEN];                  \
    COND_INTERRUPTS((static struct k380_kscan_irq_callback k380_kscan_irqs_##n[K380_KSCAN_COLS];)) \
    static struct k380_kscan_data k380_kscan_data_##n = {                                          \
        .inputs = K380_KSCAN_GPIO_LIST(k380_kscan_cols_##n),                                       \
        .matrix_state = k380_kscan_state_##n,                                                      \
        COND_INTERRUPTS((.irqs = k380_kscan_irqs_##n, ))};                                         \
    static const struct k380_kscan_config k380_kscan_config_##n = {                                \
        .outputs = K380_KSCAN_GPIO_LIST(k380_kscan_rows_##n),                                      \
        .debounce_config = {.debounce_press_ms = INST_DEBOUNCE_PRESS_MS(n),                        \
                            .debounce_release_ms = INST_DEBOUNCE_RELEASE_MS(n)},                   \
        .debounce_scan_period_ms = DT_INST_PROP(n, debounce_scan_period_ms),                       \
        .poll_period_ms = DT_INST_PROP(n, poll_period_ms)};                                        \
    PM_DEVICE_DT_INST_DEFINE(n, k380_kscan_pm_action);                                             \
    DEVICE_DT_INST_DEFINE(n, &k380_kscan_init, PM_DEVICE_DT_INST_GET(n), &k380_kscan_data_##n,     \
                          &k380_kscan_config_##n, POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY,         \
                          &k380_kscan_api);

DT_INST_FOREACH_STATUS_OKAY(K380_KSCAN_INIT);
