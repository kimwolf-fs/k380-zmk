cmake_minimum_required(VERSION 3.20.0)
list(APPEND DTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../app")
list(APPEND DTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
get_filename_component(ZMK_APP "${CMAKE_CURRENT_SOURCE_DIR}/../../../app" ABSOLUTE)
# Zephyr assembles application-defined syscall parser inputs while find_package()
# runs. Seed the parser include path without replacing the test application's
# source directory (which must remain the test fixture for Kconfig discovery).
set(SYSCALL_INCLUDE_DIRS "${ZMK_APP}/include")
set(CONFIG_APPLICATION_DEFINED_SYSCALL true)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(k380_shutdown_behavior)

zephyr_syscall_include_directories("${ZMK_APP}/include")
zephyr_syscall_header(${ZMK_APP}/include/drivers/behavior.h)
zephyr_linker_sources(SECTIONS ${ZMK_APP}/include/linker/zmk-behaviors.ld)
zephyr_linker_sources(RODATA ${ZMK_APP}/include/linker/zmk-events.ld)
target_include_directories(app PRIVATE ../../include ${ZMK_APP}/include)
target_sources(app PRIVATE src/main.c ../../src/low_power.c
  ${ZMK_APP}/src/behavior.c ${ZMK_APP}/src/event_manager.c
  ${ZMK_APP}/src/shutdown_input.c
  src/hold_tap_fixture.c
  ${ZMK_APP}/src/hid.c ${ZMK_APP}/src/hid_listener.c
  ${ZMK_APP}/src/keymap.c ${ZMK_APP}/src/behavior_queue.c
  ${ZMK_APP}/src/behaviors/behavior_key_press.c
  ${ZMK_APP}/src/behaviors/behavior_momentary_layer.c
  ${ZMK_APP}/src/behaviors/behavior_toggle_layer.c
  ${ZMK_APP}/src/behaviors/behavior_transparent.c
  ${ZMK_APP}/src/behaviors/behavior_macro.c
  ${ZMK_APP}/src/behaviors/behavior_bt.c
  ${ZMK_APP}/src/events/position_state_changed.c
  ${ZMK_APP}/src/events/keycode_state_changed.c
  ${ZMK_APP}/src/events/layer_state_changed.c)
target_compile_definitions(app PRIVATE
  CONFIG_K380_LOW_POWER_TEST=1 CONFIG_K380_LOW_POWER_COORDINATOR=1
  CONFIG_ZMK_BEHAVIOR_HOLD_TAP=1 CONFIG_ZMK_BEHAVIOR_MACRO=1
  CONFIG_ZMK_BEHAVIOR_HOLD_TAP_MAX_HELD=10
  CONFIG_ZMK_BEHAVIOR_HOLD_TAP_MAX_CAPTURED_EVENTS=40
  CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=64
  CONFIG_ZMK_MACRO_DEFAULT_WAIT_MS=0 CONFIG_ZMK_MACRO_DEFAULT_TAP_MS=1
  CONFIG_ZMK_LOG_LEVEL=0 CONFIG_ZMK_HID_REPORT_TYPE_HKRO=1
  CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE=6 CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6
  CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL=1)
