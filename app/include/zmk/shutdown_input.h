#pragma once

#include <stdbool.h>
#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_K380_LOW_POWER_COORDINATOR)
void zmk_shutdown_input_lock(void);
void zmk_shutdown_input_unlock(void);
bool zmk_shutdown_input_dispatch_allowed(bool pressed);
bool zmk_shutdown_input_is_aborting(void);
int zmk_shutdown_input_abort(void);
void zmk_keymap_abort_held_bindings(void);
void zmk_hold_tap_abort(void);
void zmk_behavior_queue_abort(void);
void zmk_physical_layouts_abort_input(void);
#else
static inline void zmk_shutdown_input_lock(void) {}
static inline void zmk_shutdown_input_unlock(void) {}
static inline bool zmk_shutdown_input_dispatch_allowed(bool pressed) { return true; }
static inline bool zmk_shutdown_input_is_aborting(void) { return false; }
#endif
