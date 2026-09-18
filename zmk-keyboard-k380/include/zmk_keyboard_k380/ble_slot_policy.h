#pragma once

#include <stdint.h>

#define K380_BLE_SLOT_COUNT 3
#define K380_BLE_CONNECTED_PROMPT_MS 5000
#ifndef CONFIG_K380_BLE_WAIT_TIMEOUT_MS
#define CONFIG_K380_BLE_WAIT_TIMEOUT_MS 10000
#endif
#ifndef CONFIG_K380_BLE_PAIRING_TIMEOUT_MS
#define CONFIG_K380_BLE_PAIRING_TIMEOUT_MS 60000
#endif
#define K380_BLE_WAIT_TIMEOUT_MS CONFIG_K380_BLE_WAIT_TIMEOUT_MS
#define K380_BLE_PAIRING_TIMEOUT_MS CONFIG_K380_BLE_PAIRING_TIMEOUT_MS

int k380_ble_slot_select(uint8_t slot);
int k380_ble_slot_pair(uint8_t slot);
uint8_t k380_ble_slot_current(void);

#ifdef CONFIG_ZTEST
void k380_ble_slot_policy_reset_for_test(void);
void k380_ble_slot_active_profile_changed_for_test(void);
void k380_ble_slot_connected_prompt_expire_for_test(void);
void k380_ble_slot_wait_timeout_expire_for_test(void);
void k380_ble_slot_pairing_timeout_expire_for_test(void);
#endif
