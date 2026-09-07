#ifndef ZMK_KEYBOARD_K380_DYNAMIC_SETTINGS_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_SETTINGS_H_

#include <zmk_keyboard_k380/dynamic_config.h>

#define K380_DYNAMIC_SETTINGS_ROOT "k380/dynamic_config"

typedef int (*k380_dynamic_settings_config_cb_t)(struct k380_dynamic_config *cfg,
                                                 void *user_data);

int k380_dynamic_settings_load(struct k380_dynamic_config *cfg);
int k380_dynamic_settings_save(const struct k380_dynamic_config *cfg);
int k380_dynamic_settings_with_config(k380_dynamic_settings_config_cb_t callback,
                                      void *user_data);
int k380_dynamic_settings_update(k380_dynamic_settings_config_cb_t callback,
                                 void *user_data);
int k380_dynamic_settings_restore_all(void);

#endif
