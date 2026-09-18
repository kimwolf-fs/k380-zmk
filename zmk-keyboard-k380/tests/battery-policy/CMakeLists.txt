cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(k380_battery_policy)

target_sources(app PRIVATE
  src/main.c
  ../../src/battery_policy.c
  ../../src/status_indicator.c
  ../../../app/module/drivers/sensor/battery/battery_common.c
)
target_include_directories(app PRIVATE
  ../../include
  ../../../app/module/drivers/sensor/battery
)
if(CONFIG_SENSOR)
  target_sources(app PRIVATE src/runtime.c)
  target_include_directories(app PRIVATE ../../../app/include)
  target_compile_definitions(app PRIVATE CONFIG_K380_BATTERY_POLICY_RUNTIME=1)
endif()
