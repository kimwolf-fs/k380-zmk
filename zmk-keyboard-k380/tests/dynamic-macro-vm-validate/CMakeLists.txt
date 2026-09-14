cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(k380_dynamic_macro_vm_validate_tests)

target_sources(app PRIVATE
  ../../src/dynamic_macro_vm_validate.c
  src/main.c
)
target_include_directories(app PRIVATE ../../include ../macro-vm-vectors)
