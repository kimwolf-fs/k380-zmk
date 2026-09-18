cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(k380_system_off_wake)

target_sources(app PRIVATE src/main.c ../../src/ghost_filter.c
  ../../../app/module/lib/zmk_debounce/debounce.c)
target_include_directories(app PRIVATE ../../include ../../../app/module/include)
target_compile_definitions(app PRIVATE
  CONFIG_ZMK_LOG_LEVEL=0 CONFIG_ZMK_KSCAN_MATRIX_POLLING=1
  CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS=-1 CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS=-1
  CONFIG_ZMK_KSCAN_MATRIX_WAIT_BEFORE_INPUTS=0 CONFIG_ZMK_KSCAN_MATRIX_WAIT_BETWEEN_OUTPUTS=0
  CONFIG_K380_LOW_POWER_COORDINATOR=1)
