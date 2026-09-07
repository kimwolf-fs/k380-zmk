#ifndef ZMK_KEYBOARD_K380_DYNAMIC_KEYMAP_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_KEYMAP_H_

#include <stdint.h>

int k380_dynamic_keymap_press(uint8_t layer, uint8_t key_index, uint16_t default_usage);
int k380_dynamic_keymap_release(uint8_t layer, uint8_t key_index, uint16_t default_usage);

#endif
