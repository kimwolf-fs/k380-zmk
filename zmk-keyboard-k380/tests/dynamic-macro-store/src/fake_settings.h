#ifndef K380_DYNAMIC_MACRO_STORE_FAKE_SETTINGS_H_
#define K380_DYNAMIC_MACRO_STORE_FAKE_SETTINGS_H_

#include <stdbool.h>
#include <stddef.h>

void k380_dynamic_macro_test_settings_reset(void);
void k380_dynamic_macro_test_settings_put(const char *key, const void *data,
                                           size_t len);
bool k380_dynamic_macro_test_settings_has(const char *key);
void k380_dynamic_macro_test_settings_fail_next_save(void);
size_t k380_dynamic_macro_test_settings_operation_count(void);
const char *k380_dynamic_macro_test_settings_operation(size_t index);

#endif
