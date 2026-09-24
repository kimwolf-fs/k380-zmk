#include <errno.h>
#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <zmk/ble.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/shutdown_input.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk_keyboard_k380/low_power.h>

LOG_MODULE_REGISTER(zmk, 0);

static int reports;
static int selects;
static int bond_writes;
static int quiet_presses;
static int b_presses;
static int abort_error;
static bool cancel_during_abort;
void shutdown_test_wait_timer_running(uint32_t position);

/* Only hardware/transport boundaries are replaced; keymap, dispatch, hold-tap,
 * macro, queue, event subscriptions and HID accounting are production sources. */
int zmk_physical_layouts_get_selected_to_stock_position_map(const uint32_t **map) {
    static const uint32_t positions[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    *map = positions;
    return ARRAY_SIZE(positions);
}
void zmk_physical_layouts_abort_input(void) {}
int zmk_endpoint_send_report(uint16_t page) {
    ARG_UNUSED(page);
    reports++;
    return 0;
}
int k380_low_power_clear_hid(void) {
    zmk_hid_keyboard_clear_for_shutdown();
    zmk_hid_consumer_clear();
    return 0;
}
int k380_low_power_abort_input(void) {
    zassert_false(k380_low_power_input_events_allowed());
    if (abort_error) {
        return abort_error;
    }
    int ret = zmk_shutdown_input_abort();
    if (cancel_during_abort) {
        cancel_during_abort = false;
        k380_low_power_cancel_usb_pending();
        zassert_false(k380_low_power_input_events_allowed(), "cancel must await cleanup owner");
    }
    return ret;
}
int zmk_ble_prof_select(uint8_t slot) {
    ARG_UNUSED(slot);
    selects++;
    /* Reproduce the policy callback that formerly reopened quiet input. */
    k380_low_power_cancel_pending();
    return 0;
}
void zmk_ble_clear_bonds(void) { bond_writes++; }
void zmk_ble_clear_all_bonds(void) { bond_writes++; }
int zmk_ble_prof_next(void) { return zmk_ble_prof_select(1); }
int zmk_ble_prof_prev(void) { return zmk_ble_prof_select(0); }
int zmk_ble_prof_disconnect(uint8_t slot) { ARG_UNUSED(slot); return 0; }

static int observe_keycode(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev && ev->state && !k380_low_power_input_events_allowed()) {
        quiet_presses++;
    }
    if (ev && ev->state && ev->keycode == HID_USAGE_KEY_KEYBOARD_B) {
        b_presses++;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(shutdown_test_observer, observe_keycode);
ZMK_SUBSCRIPTION(shutdown_test_observer, zmk_keycode_state_changed);

static void position(uint32_t pos, bool pressed) {
    zassert_ok(raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
        .position = pos, .state = pressed, .timestamp = k_uptime_get()}));
}
static void begin_shutdown(void) {
    k380_low_power_test_set_all_keys_released(false);
    zassert_ok(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT));
    zassert_false(k380_low_power_input_events_allowed());
}
static void cancel_usb(void) {
    k380_low_power_cancel_usb_pending();
    zassert_true(k380_low_power_input_events_allowed());
}
static void assert_base_key(void) {
    position(1, true);
    zassert_true(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_A));
    zassert_false(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_Z));
    position(1, false);
}
static void reset(void *fixture) {
    ARG_UNUSED(fixture);
    abort_error = 0;
    cancel_during_abort = false;
    k380_low_power_test_reset();
    begin_shutdown();
    cancel_usb();
    zmk_keymap_layer_deactivate(1, true);
    zmk_keymap_layer_deactivate(2, true);
    reports = selects = bond_writes = quiet_presses = 0;
    b_presses = 0;
}

ZTEST(shutdown_behavior, test_fn_release_before_and_after_usb_cancel) {
    for (int before = 0; before < 2; before++) {
        position(0, true);
        zassert_true(zmk_keymap_layer_active(1));
        begin_shutdown();
        zassert_false(zmk_keymap_layer_active(1));
        int quiet_reports = reports;
        if (before) {
            position(0, false);
            k380_low_power_test_set_all_keys_released(true);
        }
        cancel_usb();
        if (!before) {
            position(0, false);
        }
        zassert_equal(reports, quiet_reports);
        assert_base_key();
    }
}

ZTEST(shutdown_behavior, test_undecided_and_decided_hold_taps_are_aborted) {
    for (int decided = 0; decided < 2; decided++) {
        position(2, true);
        if (decided) {
            k_msleep(130);
            zassert_true(zmk_keymap_layer_active(1));
        }
        begin_shutdown();
        position(2, false);
        k_msleep(130);
        zassert_false(zmk_keymap_layer_active(1));
        zassert_equal(quiet_presses, 0, "abort must never choose a tap");
        cancel_usb();
        assert_base_key();
    }
    /* Reuse more slots than the maximum held count, then verify a fresh tap. */
    for (int i = 0; i < 12; i++) {
        position(2, true);
        begin_shutdown();
        cancel_usb();
        position(2, false);
    }
    position(2, true);
    position(2, false);
    zassert_equal(b_presses, 1, "a fresh hold-tap must work after all aborted slots are reclaimed");
    zassert_equal(zmk_hid_get_keyboard_report()->body.modifiers, 0);
    zassert_false(zmk_keymap_layer_active(1));
    position(8, true);
    zassert_true(zmk_hid_keyboard_is_pressed(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
    position(8, false);
    zassert_equal(zmk_hid_get_keyboard_report()->body.modifiers, 0);
}

ZTEST(shutdown_behavior, test_captured_other_position_is_discarded) {
    position(2, true);
    position(1, true);
    position(1, false);
    int before = reports;
    begin_shutdown();
    position(1, false);
    position(2, false);
    k_msleep(130);
    zassert_equal(reports, before);
    zassert_equal(quiet_presses, 0);
    cancel_usb();
    position(1, false); /* Captured down never received a behavior release. */
    zassert_equal(reports, before);
    assert_base_key();
}

ZTEST(shutdown_behavior, test_bt_timer_and_release_cannot_pair_select_or_cancel) {
    position(3, true);
    begin_shutdown();
    k_msleep(130);
    position(3, false);
    zassert_equal(selects, 0);
    zassert_equal(bond_writes, 0);
    zassert_false(k380_low_power_input_events_allowed());
    cancel_usb();
    k_msleep(130);
    zassert_equal(selects, 0);
    zassert_equal(bond_writes, 0);
    /* Confirm that the fixture really resolves the BLE select tap when READY. */
    position(3, true);
    position(3, false);
    k_msleep(10);
    zassert_equal(selects, 1);
}

ZTEST(shutdown_behavior, test_old_macro_queue_and_executed_layer_are_reclaimed) {
    position(4, true);
    zassert_true(zmk_keymap_layer_active(1));
    begin_shutdown();
    zassert_false(zmk_keymap_layer_active(1));
    cancel_usb();
    position(4, false);
    k_msleep(240);
    zassert_equal(selects, 0);
    zassert_equal(bond_writes, 0);
    assert_base_key();
}

ZTEST(shutdown_behavior, test_hold_while_undecided_and_retro_do_not_choose_tap) {
    for (int pos = 5; pos <= 6; pos++) {
        position(pos, true);
        if (pos == 5) {
            zassert_true(zmk_keymap_layer_active(1));
        } else {
            k_msleep(130);
            zassert_false(zmk_keymap_layer_active(1));
        }
        begin_shutdown();
        position(pos, false);
        zassert_false(zmk_keymap_layer_active(1));
        zassert_equal(quiet_presses, 0);
        cancel_usb();
        assert_base_key();
    }
}

ZTEST(shutdown_behavior, test_persistent_toggled_and_locked_layers_survive) {
    position(7, true);
    position(7, false);
    zassert_true(zmk_keymap_layer_active(2));
    position(0, true);
    zmk_keymap_layer_activate(1, true);
    begin_shutdown();
    zassert_true(zmk_keymap_layer_active(1));
    zassert_true(zmk_keymap_layer_locked(1));
    zassert_true(zmk_keymap_layer_active(2));
    cancel_usb();
    position(0, false);
    zassert_true(zmk_keymap_layer_active(1));
    zassert_true(zmk_keymap_layer_active(2));
}

ZTEST(shutdown_behavior, test_inflight_timer_acknowledges_abort_before_slot_reuse) {
    position(2, true);
    zmk_shutdown_input_lock();
    k_msleep(130); /* Timer is running but blocked on the lifecycle lock. */
    begin_shutdown();
    cancel_usb();
    position(2, true);
    position(2, false);
    zmk_shutdown_input_unlock();
    k_msleep(10);
    zassert_equal(b_presses, 1, "release must find the new slot, never a retired timer slot");
    zassert_false(zmk_keymap_layer_active(1));
    assert_base_key();
}

ZTEST(shutdown_behavior, test_normal_release_retires_running_timer_before_next_bt_press) {
    position(2, true);
    zmk_shutdown_input_lock();
    shutdown_test_wait_timer_running(2);
    position(2, false);
    position(3, true);
    zmk_shutdown_input_unlock();

    /* Drain the stale running callback, not the new key's future delayed timer. */
    zassert_true(k_work_queue_drain(&k_sys_work_q, false) >= 0);
    zassert_equal(selects, 0, "old timer must not decide the fresh BT hold-tap");
    zassert_equal(bond_writes, 0);
    zassert_false(zmk_keymap_layer_active(1));

    position(3, false);
    k_msleep(10);
    zassert_equal(selects, 1, "fresh short BT release must select exactly once");
    zassert_equal(bond_writes, 0, "fresh short BT release must never pair/clear bonds");
}

ZTEST(shutdown_behavior, test_failed_barrier_stays_closed_and_concurrent_cancel_is_deferred) {
    position(0, true);
    abort_error = -EBUSY;
    k380_low_power_test_set_all_keys_released(false);
    zassert_equal(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT), -EBUSY);
    k380_low_power_cancel_usb_pending();
    zassert_false(k380_low_power_input_events_allowed());
    abort_error = 0;
    cancel_usb();
    zassert_false(zmk_keymap_layer_active(1));
    assert_base_key();

    position(0, true);
    cancel_during_abort = true;
    k380_low_power_test_set_all_keys_released(false);
    zassert_equal(k380_low_power_request(K380_SHUTDOWN_PAIRING_TIMEOUT), -ECANCELED);
    zassert_true(k380_low_power_input_events_allowed());
    zassert_false(zmk_keymap_layer_active(1));
    assert_base_key();
}

ZTEST_SUITE(shutdown_behavior, NULL, NULL, reset, NULL, NULL);
