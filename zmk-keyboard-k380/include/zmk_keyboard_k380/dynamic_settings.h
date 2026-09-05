#ifndef ZMK_KEYBOARD_K380_DYNAMIC_SETTINGS_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_SETTINGS_H_

#include <zmk_keyboard_k380/dynamic_config.h>

#define K380_DYNAMIC_SETTINGS_ROOT "k380/dynamic_config"

int k380_dynamic_settings_load(struct k380_dynamic_config *cfg);
int k380_dynamic_settings_save(const struct k380_dynamic_config *cfg);
int k380_dynamic_settings_restore_all(void);

#endif
