#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>
#include <zmk/hid.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk_keyboard_k380/low_power.h>

LOG_MODULE_REGISTER(zmk, 0);

extern int hid_listener(const zmk_event_t *eh);
static int reports;
static int clears;
K_MSGQ_DEFINE(queued_keys, sizeof(struct zmk_keycode_state_changed_event), 4, 4);

/* Only transport/event dispatch are replaced; coordinator, listener and HID are real. */
int zmk_endpoint_send_report(uint16_t usage_page) {
    ARG_UNUSED(usage_page);
    reports++;
    return 0;
}
int zmk_event_manager_raise(zmk_event_t *event) { return hid_listener(event); }
int k380_low_power_clear_hid(void) {
    zmk_hid_keyboard_clear_for_shutdown();
    zmk_hid_consumer_clear();
    clears++;
    return 0;
}

static void reset(void) {
    k380_low_power_test_reset();
    zmk_hid_keyboard_clear_for_shutdown();
    zmk_hid_consumer_clear();
    k_msgq_purge(&queued_keys);
    reports = 0;
    clears = 0;
}

static void queue_key(uint32_t code) {
    struct zmk_keycode_state_changed_event ev = {
        .header = {.event = &zmk_event_zmk_keycode_state_changed},
        .data = {.usage_page = HID_USAGE_KEY, .keycode = code, .state = true}};
    zassert_ok(k_msgq_put(&queued_keys, &ev, K_NO_WAIT));
}

static void drain_keys(void) {
    struct zmk_keycode_state_changed_event ev;
    while (k_msgq_get(&queued_keys, &ev, K_NO_WAIT) == 0) {
        zassert_equal(hid_listener(&ev.header), ZMK_EV_EVENT_BUBBLE);
    }
}

ZTEST(k380_shutdown_input, test_prequeued_press_cannot_repopulate_hid_after_clear) {
    reset();
    queue_key(HID_USAGE_KEY_KEYBOARD_A);
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT));
    zassert_true(k380_low_power_is_release_waiting());
    zassert_equal(clears, 1);
    drain_keys();
    zassert_false(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_A));
    zassert_equal(reports, 0, "an event queued before cleanup must not emit HID afterwards");
}

ZTEST(k380_shutdown_input, test_ready_connected_idle_first_key_is_delivered) {
    reset();
    zassert_true(k380_low_power_input_events_allowed());
    queue_key(HID_USAGE_KEY_KEYBOARD_A);
    drain_keys();
    zassert_true(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_A));
    zassert_equal(reports, 1);
}

ZTEST(k380_shutdown_input, test_usb_cancellation_does_not_leave_modifier_reference_count) {
    reset();
    queue_key(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT);
    drain_keys();
    zassert_true(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_LOW_VOLTAGE));
    zassert_true(k380_low_power_is_release_waiting());
    zassert_false(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));

    struct zmk_keycode_state_changed_event release = {
        .header = {.event = &zmk_event_zmk_keycode_state_changed},
        .data = {.usage_page = HID_USAGE_KEY, .keycode = HID_USAGE_KEY_KEYBOARD_LEFTSHIFT,
                 .state = false}};
    zassert_ok(k_msgq_put(&queued_keys, &release, K_NO_WAIT));
    drain_keys();
    k380_low_power_test_set_all_keys_released(true);
    k380_low_power_test_set_battery_charging(true);
    k380_low_power_cancel_usb_pending();
    zassert_true(k380_low_power_input_events_allowed());

    queue_key(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT);
    drain_keys();
    zassert_true(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
    zassert_ok(k_msgq_put(&queued_keys, &release, K_NO_WAIT));
    drain_keys();
    zassert_false(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT),
                  "shutdown must reset counts for releases intentionally consumed while quiet");
    zassert_equal(zmk_hid_get_keyboard_report()->body.modifiers, 0);
}

ZTEST_SUITE(k380_shutdown_input, NULL, NULL, NULL, NULL, NULL);
