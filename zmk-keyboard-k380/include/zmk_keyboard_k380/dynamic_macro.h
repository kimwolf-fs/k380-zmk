#ifndef ZMK_KEYBOARD_K380_DYNAMIC_MACRO_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_MACRO_H_

#include <stdbool.h>
#include <stdint.h>

int k380_dynamic_macro_trigger(uint8_t preset, uint8_t slot, bool pressed);
int k380_dynamic_macro_test(uint8_t slot);
int k380_dynamic_macro_stop(void);
bool k380_dynamic_macro_is_running(void);
void k380_dynamic_macro_stop_before_preset_switch(void);

#endif
