#ifndef ZMK_KEYBOARD_K380_DYNAMIC_CONFIG_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_CONFIG_H_

#include <stdint.h>

#define K380_DYNAMIC_PRESET_COUNT 4
#define K380_DYNAMIC_LAYER_COUNT 2
#define K380_DYNAMIC_KEY_COUNT 80
#define K380_DYNAMIC_MACRO_SLOT_COUNT 16
#define K380_DYNAMIC_MACRO_NAME_MAX_BYTES 32
#define K380_DYNAMIC_MACRO_MAX_STEPS 64
#define K380_DYNAMIC_WAIT_MIN_MS 1
#define K380_DYNAMIC_WAIT_MAX_MS 60000

#define K380_HID_USAGE_HOME 0x4A

enum k380_dynamic_binding_type {
    K380_DYNAMIC_BINDING_DEFAULT = 0,
    K380_DYNAMIC_BINDING_KEY = 1,
    K380_DYNAMIC_BINDING_MACRO = 2,
};

enum k380_dynamic_macro_step_type {
    K380_DYNAMIC_MACRO_PRESS_KEY = 1,
    K380_DYNAMIC_MACRO_RELEASE_KEY = 2,
    K380_DYNAMIC_MACRO_TAP_KEY = 3,
    K380_DYNAMIC_MACRO_WAIT_MS = 4,
    K380_DYNAMIC_MACRO_WAIT_RANDOM = 5,
    K380_DYNAMIC_MACRO_RELEASE_ALL = 6,
};

enum k380_dynamic_macro_trigger_type {
    K380_DYNAMIC_MACRO_TRIGGER_ONCE = 0,
    K380_DYNAMIC_MACRO_TRIGGER_HOLD = 1,
    K380_DYNAMIC_MACRO_TRIGGER_TOGGLE = 2,
    K380_DYNAMIC_MACRO_TRIGGER_COUNT = 3,
};

struct k380_dynamic_binding {
    uint8_t type;
    uint8_t reserved;
    union {
        uint16_t key_usage;
        uint8_t macro_index;
    } value;
};

struct k380_dynamic_macro_step {
    uint8_t type;
    uint8_t reserved;
    union {
        uint16_t key_usage;
        uint16_t wait_ms;
        struct {
            uint16_t min_ms;
            uint16_t max_ms;
        } wait_random;
    } value;
};

struct k380_dynamic_macro {
    uint8_t name[K380_DYNAMIC_MACRO_NAME_MAX_BYTES];
    uint8_t step_count;
    uint8_t trigger;
    uint16_t count;
    struct k380_dynamic_macro_step steps[K380_DYNAMIC_MACRO_MAX_STEPS];
};

struct k380_dynamic_preset {
    uint8_t name[K380_DYNAMIC_MACRO_NAME_MAX_BYTES];
    struct k380_dynamic_binding
        bindings[K380_DYNAMIC_LAYER_COUNT][K380_DYNAMIC_KEY_COUNT];
    struct k380_dynamic_macro macros[K380_DYNAMIC_MACRO_SLOT_COUNT];
};

struct k380_dynamic_config {
    uint8_t active_preset;
    uint8_t reserved[3];
    struct k380_dynamic_preset presets[K380_DYNAMIC_PRESET_COUNT];
};

void k380_dynamic_config_init_defaults(struct k380_dynamic_config *cfg);
int k380_dynamic_config_validate(const struct k380_dynamic_config *cfg);
void k380_dynamic_config_restore_key(struct k380_dynamic_config *cfg,
                                     uint8_t preset, uint8_t layer,
                                     uint8_t key_index);
void k380_dynamic_config_restore_preset(struct k380_dynamic_config *cfg,
                                        uint8_t preset);
void k380_dynamic_config_restore_all(struct k380_dynamic_config *cfg);
uint32_t k380_dynamic_config_crc32(const struct k380_dynamic_config *cfg);

#endif
