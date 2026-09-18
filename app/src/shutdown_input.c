#include <errno.h>
#include <zmk/shutdown_input.h>
#include <zmk_keyboard_k380/low_power.h>
#if IS_ENABLED(CONFIG_K380_DYNAMIC_MACRO)
#include <zmk_keyboard_k380/dynamic_macro.h>
#endif

K_MUTEX_DEFINE(dispatch_lock);
static k_tid_t abort_owner;

void zmk_shutdown_input_lock(void) { k_mutex_lock(&dispatch_lock, K_FOREVER); }
void zmk_shutdown_input_unlock(void) { k_mutex_unlock(&dispatch_lock); }
bool zmk_shutdown_input_is_aborting(void) { return abort_owner == k_current_get(); }
bool zmk_shutdown_input_dispatch_allowed(bool pressed) {
    return k380_low_power_input_events_allowed() ||
           (!pressed && zmk_shutdown_input_is_aborting());
}

int zmk_shutdown_input_abort(void) {
    if (k380_low_power_input_events_allowed()) {
        return -EINVAL;
    }
#if IS_ENABLED(CONFIG_K380_DYNAMIC_MACRO)
    /* The runner can emit events, so never wait for it while owning dispatch_lock. */
    int err = k380_dynamic_macro_abort_for_shutdown();
    if (err) {
        return err;
    }
#endif
    zmk_shutdown_input_lock();
    abort_owner = k_current_get();
    zmk_physical_layouts_abort_input();
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_HOLD_TAP)
    zmk_hold_tap_abort();
#endif
    zmk_behavior_queue_abort();
    zmk_keymap_abort_held_bindings();
    abort_owner = NULL;
    zmk_shutdown_input_unlock();
    return 0;
}
