cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(k380_shutdown_input)

target_sources(app PRIVATE src/main.c ../../src/low_power.c
  ../../../app/src/hid.c ../../../app/src/hid_listener.c
  ../../../app/src/shutdown_input.c
  ../../../app/src/events/keycode_state_changed.c)
target_include_directories(app PRIVATE ../../include ../../../app/include)
target_compile_definitions(app PRIVATE
  CONFIG_K380_LOW_POWER_TEST=1 CONFIG_K380_LOW_POWER_COORDINATOR=1
  CONFIG_ZMK_LOG_LEVEL=0 CONFIG_ZMK_HID_REPORT_TYPE_HKRO=1
  CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE=6 CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6
  CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL=1)
