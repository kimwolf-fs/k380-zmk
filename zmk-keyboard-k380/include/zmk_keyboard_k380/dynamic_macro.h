#ifndef ZMK_KEYBOARD_K380_DYNAMIC_MACRO_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_MACRO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_macro_record.h>

enum k380_dynamic_macro_run_state_value {
    K380_DYNAMIC_MACRO_RUN_IDLE = 0,
    K380_DYNAMIC_MACRO_RUN_RUNNING = 1,
    K380_DYNAMIC_MACRO_RUN_COMPLETED = 2,
    K380_DYNAMIC_MACRO_RUN_STOPPED = 3,
    K380_DYNAMIC_MACRO_RUN_ERROR = 4,
};

struct k380_dynamic_macro_trace_event {
    uint32_t sequence;
    uint16_t pc;
    uint8_t event;
    uint8_t result;
    uint32_t value;
    uint32_t elapsed_ms;
};

struct k380_dynamic_macro_run_state {
    uint32_t run_id;
    uint8_t state;
    uint8_t vm_error;
    uint16_t reserved;
    uint32_t next_cursor;
    uint32_t dropped;
};

_Static_assert(sizeof(struct k380_dynamic_macro_trace_event) == 16U,
               "macro trace event wire layout changed");

int k380_dynamic_macro_trigger(uint8_t preset, uint8_t slot, bool pressed);
int k380_dynamic_macro_test(uint8_t slot);
int k380_dynamic_macro_test_temporary(uint8_t preset, uint8_t slot,
                                      const struct k380_dynamic_macro *macro);
int k380_dynamic_macro_test_record(
    uint8_t preset, uint8_t slot,
    const struct k380_dynamic_macro_record *record);
int k380_dynamic_macro_test_record_start(
    uint8_t preset, uint8_t slot,
    const struct k380_dynamic_macro_record *record, uint32_t *run_id);
int k380_dynamic_macro_stop(void);
int k380_dynamic_macro_stop_if_run_id(uint32_t run_id);
bool k380_dynamic_macro_is_running(void);
void k380_dynamic_macro_get_run_state(
    struct k380_dynamic_macro_run_state *state);
int k380_dynamic_macro_trace_read(
    uint32_t cursor, struct k380_dynamic_macro_trace_event *events,
    size_t capacity, struct k380_dynamic_macro_run_state *state);
void k380_dynamic_macro_stop_before_preset_switch(void);
void k380_dynamic_macro_physical_key_state(uint16_t usage, bool pressed);

#endif
