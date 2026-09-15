#ifndef ZMK_KEYBOARD_K380_DYNAMIC_SETTINGS_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_SETTINGS_H_

#include <zmk_keyboard_k380/dynamic_config.h>

#define K380_DYNAMIC_SETTINGS_ROOT "k380/dynamic_config"

typedef int (*k380_dynamic_settings_read_cb_t)(
    const struct k380_dynamic_config *cfg, void *user_data);
typedef int (*k380_dynamic_settings_update_cb_t)(
    struct k380_dynamic_config *cfg, void *user_data);

void k380_dynamic_settings_lock(void);
void k380_dynamic_settings_unlock(void);
int k380_dynamic_settings_with_config(k380_dynamic_settings_read_cb_t callback,
                                      void *user_data);
int k380_dynamic_settings_update(k380_dynamic_settings_update_cb_t callback,
                                 void *user_data);
int k380_dynamic_settings_restore_preset(uint8_t preset);
int k380_dynamic_settings_restore_all(void);

#endif
