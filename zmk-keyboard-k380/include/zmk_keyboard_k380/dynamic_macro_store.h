#ifndef ZMK_KEYBOARD_K380_DYNAMIC_MACRO_STORE_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_MACRO_STORE_H_

#include <stdint.h>

#include <zmk_keyboard_k380/dynamic_macro_record.h>

int k380_dynamic_macro_store_load(uint8_t preset, uint8_t slot,
                                  struct k380_dynamic_macro_record *record);
int k380_dynamic_macro_store_save(uint8_t preset, uint8_t slot,
                                  const struct k380_dynamic_macro_record *record);
int k380_dynamic_macro_store_restore_slot(uint8_t preset, uint8_t slot);
int k380_dynamic_macro_store_restore_preset(uint8_t preset);
int k380_dynamic_macro_store_restore_all(void);

#endif
