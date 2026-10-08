#ifndef ZMK_KEYBOARD_K380_DYNAMIC_MACRO_VM_FORMAT_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_MACRO_VM_FORMAT_H_

#include <stddef.h>
#include <stdint.h>

#define K380_MACRO_VM_PACKAGE_MAGIC "KVM1"
#define K380_MACRO_VM_PACKAGE_MAGIC_SIZE 4U
#define K380_MACRO_VM_PACKAGE_VERSION 1U
#define K380_MACRO_VM_PACKAGE_HEADER_SIZE 16U
#define K380_MACRO_VM_FUNCTION_ENTRY_SIZE 4U
#define K380_MACRO_VM_MAX_CODE_BYTES 1024U
#define K380_MACRO_VM_MAX_FUNCTIONS 16U
#define K380_MACRO_VM_MAX_PACKAGE_BYTES \
    (K380_MACRO_VM_PACKAGE_HEADER_SIZE + \
     K380_MACRO_VM_MAX_FUNCTIONS * K380_MACRO_VM_FUNCTION_ENTRY_SIZE + \
     K380_MACRO_VM_MAX_CODE_BYTES)
#define K380_MACRO_VM_MAX_CALL_DEPTH 8U
#define K380_MACRO_VM_MAX_LOOP_DEPTH 3U
#define K380_MACRO_VM_MAX_BLOCK_DEPTH 8U
#define K380_MACRO_VM_MAX_TRACE_EVENTS 64U

enum k380_macro_vm_opcode {
    K380_MACRO_VM_OP_END = 0x00,
    K380_MACRO_VM_OP_PRESS = 0x01,
    K380_MACRO_VM_OP_RELEASE = 0x02,
    K380_MACRO_VM_OP_TAP = 0x03,
    K380_MACRO_VM_OP_RELEASE_ALL = 0x04,
    K380_MACRO_VM_OP_WAIT = 0x10,
    K380_MACRO_VM_OP_RANDOM_WAIT = 0x11,
    K380_MACRO_VM_OP_TIMER_RESET = 0x20,
    K380_MACRO_VM_OP_TIMER_GE = 0x21,
    K380_MACRO_VM_OP_TIMER_LE = 0x22,
    K380_MACRO_VM_OP_LOOP_COUNT_BEGIN = 0x30,
    K380_MACRO_VM_OP_LOOP_TIME_BEGIN = 0x31,
    K380_MACRO_VM_OP_LOOP_END = 0x32,
    K380_MACRO_VM_OP_CALL = 0x40,
    K380_MACRO_VM_OP_RETURN = 0x41,
};

struct k380_macro_vm_function_range {
    uint16_t entry_offset;
    uint16_t end_offset_exclusive;
};

struct k380_macro_vm_package_view {
    const uint8_t *package;
    size_t package_len;
    const uint8_t *code;
    uint16_t code_len;
    uint8_t function_count;
    uint16_t entry_offset;
    struct k380_macro_vm_function_range
        functions[K380_MACRO_VM_MAX_FUNCTIONS];
};

#endif
