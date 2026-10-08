/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zmk/behavior_queue.h>
#include <zmk/behavior.h>
#include <zmk/shutdown_input.h>
#include <string.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct q_item {
    uint32_t position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
    struct zmk_behavior_binding binding;
    bool press : 1;
    uint32_t wait : 31;
};

K_MSGQ_DEFINE(zmk_behavior_queue_msgq, sizeof(struct q_item), CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE, 4);

static void behavior_queue_process_next(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(queue_work, behavior_queue_process_next);

#if IS_ENABLED(CONFIG_K380_LOW_POWER_COORDINATOR)
static atomic_t queue_generation;
static struct {
    bool held;
    struct q_item item;
} delivered[CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE];

static bool same_binding(const struct q_item *a, const struct q_item *b) {
    return a->position == b->position &&
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
           a->source == b->source &&
#endif
           a->binding.param1 == b->binding.param1 && a->binding.param2 == b->binding.param2 &&
           strcmp(a->binding.behavior_dev, b->binding.behavior_dev) == 0;
}

void zmk_behavior_queue_abort(void) {
    atomic_inc(&queue_generation);
    k_work_cancel_delayable(&queue_work);
    k_msgq_purge(&zmk_behavior_queue_msgq);
    for (int i = 0; i < ARRAY_SIZE(delivered); i++) {
        if (!delivered[i].held) {
            continue;
        }
        struct q_item *item = &delivered[i].item;
        struct zmk_behavior_binding_event event = {
            .position = item->position, .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = item->source,
#endif
        };
        delivered[i].held = false;
        zmk_behavior_invoke_binding(&item->binding, event, false);
    }
    k_msgq_purge(&zmk_behavior_queue_msgq);
}
#endif

static void behavior_queue_process_next(struct k_work *work) {
#if IS_ENABLED(CONFIG_K380_LOW_POWER_COORDINATOR)
    atomic_val_t generation = atomic_get(&queue_generation);
#endif
    zmk_shutdown_input_lock();
#if IS_ENABLED(CONFIG_K380_LOW_POWER_COORDINATOR)
    if (generation != atomic_get(&queue_generation)) {
        zmk_shutdown_input_unlock();
        return;
    }
#endif
    if (!zmk_shutdown_input_dispatch_allowed(true)) {
        k_msgq_purge(&zmk_behavior_queue_msgq);
        zmk_shutdown_input_unlock();
        return;
    }
    struct q_item item = {.wait = 0};

    while (k_msgq_get(&zmk_behavior_queue_msgq, &item, K_NO_WAIT) == 0) {
        LOG_DBG("Invoking %s: 0x%02x 0x%02x", item.binding.behavior_dev, item.binding.param1,
                item.binding.param2);

        struct zmk_behavior_binding_event event = {.position = item.position,
                                                   .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                                                   .source = item.source
#endif
        };

#if IS_ENABLED(CONFIG_K380_LOW_POWER_COORDINATOR)
        int slot = -1;
        for (int i = 0; i < ARRAY_SIZE(delivered); i++) {
            if ((item.press && !delivered[i].held) ||
                (!item.press && delivered[i].held && same_binding(&delivered[i].item, &item))) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            LOG_WRN("Dropping queued binding without a lifecycle slot");
            continue;
        }
        if (!item.press) {
            delivered[slot].held = false;
        } else {
            /* Reserve before invoking: a nested macro can recursively queue more presses. */
            delivered[slot].held = true;
            delivered[slot].item = item;
        }
#endif
        int ret = zmk_behavior_invoke_binding(&item.binding, event, item.press);
#if IS_ENABLED(CONFIG_K380_LOW_POWER_COORDINATOR)
        if (item.press && ret != ZMK_BEHAVIOR_OPAQUE) {
            delivered[slot].held = false;
        }
#else
        ARG_UNUSED(ret);
#endif

        LOG_DBG("Processing next queued behavior in %dms", item.wait);

        if (item.wait > 0) {
            k_work_schedule(&queue_work, K_MSEC(item.wait));
            break;
        }
    }
    zmk_shutdown_input_unlock();
}

static int queue_add(const struct zmk_behavior_binding_event *event,
                           const struct zmk_behavior_binding binding, bool press, uint32_t wait) {
    if (!zmk_shutdown_input_dispatch_allowed(true)) {
        return -ECANCELED;
    }
    struct q_item item = {
        .press = press,
        .binding = binding,
        .wait = wait,
        .position = event->position,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = event->source,
#endif
    };

    const int ret = k_msgq_put(&zmk_behavior_queue_msgq, &item, K_NO_WAIT);
    if (ret < 0) {
        return ret;
    }

    if (!k_work_delayable_is_pending(&queue_work)) {
        behavior_queue_process_next(&queue_work.work);
    }

    return 0;
}

int zmk_behavior_queue_add(const struct zmk_behavior_binding_event *event,
                           const struct zmk_behavior_binding binding, bool press, uint32_t wait) {
    zmk_shutdown_input_lock();
    int ret = queue_add(event, binding, press, wait);
    zmk_shutdown_input_unlock();
    return ret;
}
