#pragma once

#include <stdint.h>

#define K380_BLE_SLOT_COUNT 3
#define K380_BLE_CONNECTED_PROMPT_MS 5000

int k380_ble_slot_select(uint8_t slot);
int k380_ble_slot_pair(uint8_t slot);
uint8_t k380_ble_slot_current(void);

#ifdef CONFIG_ZTEST
void k380_ble_slot_policy_reset_for_test(void);
void k380_ble_slot_connected_prompt_expire_for_test(void);
#endif
