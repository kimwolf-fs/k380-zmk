cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(k380_dynamic_macro_tests)

target_sources(app PRIVATE
  ../../src/dynamic_config.c
  ../../src/dynamic_macro_vm.c
  ../../src/dynamic_macro_vm_validate.c
  ../../src/dynamic_macro_store.c
  ../../src/dynamic_macro.c
  ../dynamic-macro-store/src/fake_settings.c
  src/main.c
)
target_include_directories(app PRIVATE ../../include ../../../app/include
                            ../dynamic-macro-store/src)
target_compile_definitions(app PRIVATE
  K380_DYNAMIC_MACRO_STORE_TEST_BACKEND
  CONFIG_K380_DYNAMIC_MACRO_THREAD_STACK_SIZE=2048
  CONFIG_K380_DYNAMIC_MACRO_THREAD_PRIORITY=5
)
