#ifndef ZMK_KEYBOARD_K380_DYNAMIC_MACRO_VM_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_MACRO_VM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk_keyboard_k380/dynamic_macro_vm_format.h>

enum k380_macro_vm_error {
    K380_MACRO_VM_OK = 0,
    K380_MACRO_VM_INVALID_PACKAGE = 1,
    K380_MACRO_VM_INVALID_MAGIC = 2,
    K380_MACRO_VM_INVALID_VERSION = 3,
    K380_MACRO_VM_INVALID_FLAGS = 4,
    K380_MACRO_VM_INVALID_RESERVED = 5,
    K380_MACRO_VM_INVALID_LENGTH = 6,
    K380_MACRO_VM_INVALID_CODE_LENGTH = 7,
    K380_MACRO_VM_INVALID_ENTRY = 8,
    K380_MACRO_VM_CRC_MISMATCH = 9,
    K380_MACRO_VM_UNKNOWN_OPCODE = 10,
    K380_MACRO_VM_TRUNCATED_OPERAND = 11,
    K380_MACRO_VM_INVALID_KEY_USAGE = 12,
    K380_MACRO_VM_INVALID_TIMER = 13,
    K380_MACRO_VM_INVALID_LIMIT = 14,
    K380_MACRO_VM_INVALID_FUNCTION = 15,
    K380_MACRO_VM_INVALID_FUNCTION_RANGE = 16,
    K380_MACRO_VM_OVERLAPPING_FUNCTIONS = 17,
    K380_MACRO_VM_INVALID_FUNCTION_TERMINATOR = 18,
    K380_MACRO_VM_INVALID_ENTRY_TERMINATOR = 19,
    K380_MACRO_VM_INVALID_TARGET = 20,
    K380_MACRO_VM_CROSS_REGION_TARGET = 21,
    K380_MACRO_VM_BLOCK_BOUNDARY = 22,
    K380_MACRO_VM_UNMATCHED_CONTROL_FLOW = 23,
    K380_MACRO_VM_UNMATCHED_LOOP = 24,
    K380_MACRO_VM_RECURSION = 25,
    K380_MACRO_VM_CALL_DEPTH = 26,
    K380_MACRO_VM_LOOP_DEPTH = 27,
    K380_MACRO_VM_EMPTY_BLOCK = 28,
    K380_MACRO_VM_STOPPED = 29,
    K380_MACRO_VM_CALL_STACK_UNDERFLOW = 30,
    K380_MACRO_VM_LOOP_STACK_OVERFLOW = 31,
    K380_MACRO_VM_LOOP_STACK_UNDERFLOW = 32,
    K380_MACRO_VM_LOOP_FRAME_MISMATCH = 33,
    K380_MACRO_VM_HOST_FAILURE = 34,
    K380_MACRO_VM_INVALID_RUNTIME_STATE = 35,
};

enum k380_macro_vm_trace_event {
    K380_MACRO_VM_TRACE_PRESS = 1,
    K380_MACRO_VM_TRACE_RELEASE = 2,
    K380_MACRO_VM_TRACE_TAP = 3,
    K380_MACRO_VM_TRACE_RELEASE_ALL = 4,
    K380_MACRO_VM_TRACE_WAIT = 5,
    K380_MACRO_VM_TRACE_RANDOM_WAIT = 6,
    K380_MACRO_VM_TRACE_TIMER_RESET = 7,
    K380_MACRO_VM_TRACE_TIMER_CHECK = 8,
    K380_MACRO_VM_TRACE_LOOP_BEGIN = 9,
    K380_MACRO_VM_TRACE_LOOP_END = 10,
    K380_MACRO_VM_TRACE_CALL = 11,
    K380_MACRO_VM_TRACE_RETURN = 12,
};

struct k380_macro_vm_host {
    int (*key_event)(uint16_t usage, bool pressed, void *user_data);
    int (*release_all)(void *user_data);
    int (*wait_ms)(uint32_t duration_ms, void *user_data);
    uint64_t (*now_ms)(void *user_data);
    uint32_t (*random_u32)(void *user_data);
    bool (*stop_requested)(void *user_data);
    void (*yield_cpu)(void *user_data);
    void (*trace)(uint16_t pc, uint8_t event, uint8_t result,
                  uint32_t value, uint32_t elapsed_ms, void *user_data);
    void *user_data;
};

struct k380_macro_vm_loop_frame {
    uint8_t opcode;
    uint8_t reserved;
    uint16_t body_pc;
    uint16_t end_pc;
    uint16_t reserved2;
    uint32_t duration_ms;
    union {
        uint32_t remaining;
        uint64_t start_ms;
    } limit;
};

struct k380_macro_vm_context {
    uint16_t return_pc[K380_MACRO_VM_MAX_CALL_DEPTH];
    uint8_t return_loop_depth[K380_MACRO_VM_MAX_CALL_DEPTH];
    uint8_t call_depth;
    struct k380_macro_vm_loop_frame loops[K380_MACRO_VM_MAX_LOOP_DEPTH];
    uint8_t loop_depth;
    uint64_t timer_origin_ms[3];
    uint64_t start_ms;
    uint16_t pc;
    uint32_t instruction_count;
    enum k380_macro_vm_error error;
};

int k380_macro_vm_validate(const uint8_t *package, size_t len,
                           struct k380_macro_vm_package_view *view);

enum k380_macro_vm_error
k380_macro_vm_run(const struct k380_macro_vm_package_view *view,
                  const struct k380_macro_vm_host *host,
                  struct k380_macro_vm_context *context);

/* Start another entry pass while preserving this macro instance's timers. */
enum k380_macro_vm_error
k380_macro_vm_run_pass(const struct k380_macro_vm_package_view *view,
                       const struct k380_macro_vm_host *host,
                       struct k380_macro_vm_context *context);

#endif
