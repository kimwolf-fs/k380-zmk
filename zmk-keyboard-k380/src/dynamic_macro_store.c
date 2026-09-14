#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

#include <zmk_keyboard_k380/dynamic_macro_record.h>
#include <zmk_keyboard_k380/dynamic_macro_store.h>
#include <zmk_keyboard_k380/dynamic_settings.h>
#include <zmk_keyboard_k380/dynamic_macro_vm.h>

#ifdef K380_DYNAMIC_MACRO_STORE_TEST_BACKEND
extern int k380_dynamic_macro_test_settings_load_subtree_direct(
    const char *subtree, settings_load_direct_cb callback, void *cb_arg);
extern int k380_dynamic_macro_test_settings_save_one(const char *key,
                                                     const void *value,
                                                     size_t len);
extern int k380_dynamic_macro_test_settings_delete(const char *key);
#define settings_load_subtree_direct \
    k380_dynamic_macro_test_settings_load_subtree_direct
#define settings_save_one k380_dynamic_macro_test_settings_save_one
#define settings_delete k380_dynamic_macro_test_settings_delete
#endif

#define K380_DYNAMIC_MACRO_V2_SETTINGS_ROOT \
    K380_DYNAMIC_SETTINGS_ROOT "/v2"
#define K380_DYNAMIC_MACRO_V2_SETTINGS_PREFIX \
    K380_DYNAMIC_MACRO_V2_SETTINGS_ROOT "/p"

BUILD_ASSERT(sizeof(struct k380_dynamic_macro) == 0x1a4U,
             "legacy macro settings record layout changed");

static struct k380_dynamic_macro_record *load_target;
static bool load_found;
static int load_status;
static uint8_t load_preset;
static uint8_t load_slot;
static struct k380_dynamic_macro *legacy_target;
static bool legacy_found;
static int legacy_status;
static uint8_t legacy_preset;
static uint8_t legacy_slot;
static uint8_t store_wire_scratch[K380_DYNAMIC_MACRO_RECORD_MAX_BYTES];
static struct k380_dynamic_macro legacy_scratch;

static bool is_keyboard_keypad_usage(uint16_t usage)
{
    return ((usage >= 4U && usage <= 126U) && usage != 102U) ||
           (usage >= 130U && usage <= 164U) ||
           (usage >= 176U && usage <= 221U) ||
           (usage >= 224U && usage <= 231U);
}

static uint32_t package_crc32_without_field(const uint8_t *package,
                                            size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t index = 0U; index < length; index++) {
        uint8_t value = index >= 12U && index < 16U ? 0U : package[index];
        crc ^= value;
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

static int check_formatted_len(int written, size_t capacity)
{
    return written < 0 || (size_t)written >= capacity ? -ENAMETOOLONG : 0;
}

static int format_v2_key(char *key, size_t key_len, uint8_t preset,
                         uint8_t slot)
{
    return check_formatted_len(
        snprintf(key, key_len, K380_DYNAMIC_MACRO_V2_SETTINGS_PREFIX
                              "/%u/m/%u", preset, slot),
        key_len);
}

static int parse_index(const char *name, uint8_t limit, const char **next)
{
    uint16_t value = 0U;
    bool has_digit = false;

    while (*name >= '0' && *name <= '9') {
        has_digit = true;
        value = (uint16_t)(value * 10U + (uint16_t)(*name - '0'));
        if (value >= limit) {
            return -EINVAL;
        }
        name++;
    }
    if (!has_digit) {
        return -EINVAL;
    }
    if (*name == '/') {
        if (next != NULL) {
            *next = name + 1;
        }
        return value;
    }
    if (*name == '\0' || *name == '=') {
        if (next != NULL) {
            *next = NULL;
        }
        return value;
    }
    return -EINVAL;
}

int k380_dynamic_macro_record_validate(
    const struct k380_dynamic_macro_record *record)
{
    if (record == NULL || record->record_version != K380_DYNAMIC_MACRO_RECORD_VERSION ||
        record->reserved != 0U || record->reserved2 != 0U ||
        record->name_len > K380_DYNAMIC_MACRO_NAME_MAX_BYTES ||
        record->package_len > K380_DYNAMIC_MACRO_PACKAGE_MAX_BYTES) {
        return -EINVAL;
    }
    for (uint8_t index = record->name_len;
         index < K380_DYNAMIC_MACRO_NAME_MAX_BYTES; index++) {
        if (record->name[index] != 0U) {
            return -EINVAL;
        }
    }
    if (record->trigger > K380_DYNAMIC_MACRO_TRIGGER_COUNT ||
        (record->trigger == K380_DYNAMIC_MACRO_TRIGGER_COUNT &&
         (record->repeat_count == 0U || record->repeat_count > 65535U))) {
        return -EINVAL;
    }
    if (record->package_len == 0U) {
        return record->package_crc32 == 0U ? 0 : -EINVAL;
    }
    if (record->package_crc32 != package_crc32_without_field(
                                    record->package, record->package_len)) {
        return -EINVAL;
    }
    struct k380_macro_vm_package_view view;
    return k380_macro_vm_validate(record->package, record->package_len, &view) ==
                   K380_MACRO_VM_OK
               ? 0
               : -EINVAL;
}

size_t k380_dynamic_macro_record_wire_size(
    const struct k380_dynamic_macro_record *record)
{
    return record == NULL ? 0U : K380_DYNAMIC_MACRO_RECORD_HEADER_BYTES +
                                      record->package_len;
}

int k380_dynamic_macro_record_encode(
    const struct k380_dynamic_macro_record *record, uint8_t *wire,
    size_t wire_capacity, size_t *wire_len)
{
    if (k380_dynamic_macro_record_validate(record) != 0 || wire == NULL ||
        wire_len == NULL) {
        return -EINVAL;
    }
    size_t length = k380_dynamic_macro_record_wire_size(record);
    if (wire_capacity < length) {
        return -ENOSPC;
    }
    memset(wire, 0, length);
    wire[0] = record->record_version;
    wire[1] = record->trigger;
    wire[2] = record->name_len;
    sys_put_le32(record->repeat_count, &wire[4]);
    sys_put_le16(record->package_len, &wire[8]);
    sys_put_le32(record->package_crc32, &wire[12]);
    memcpy(&wire[16], record->name, sizeof(record->name));
    memcpy(&wire[48], record->package, record->package_len);
    *wire_len = length;
    return 0;
}

int k380_dynamic_macro_record_decode(
    struct k380_dynamic_macro_record *record, const uint8_t *wire,
    size_t wire_len)
{
    if (record == NULL || wire == NULL || wire_len < 48U) {
        return -EINVAL;
    }
    uint16_t package_len = sys_get_le16(&wire[8]);
    if (package_len > K380_DYNAMIC_MACRO_PACKAGE_MAX_BYTES ||
        wire_len != 48U + package_len || wire[3] != 0U ||
        sys_get_le16(&wire[10]) != 0U) {
        return -EMSGSIZE;
    }
    memset(record, 0, sizeof(*record));
    record->record_version = wire[0];
    record->trigger = wire[1];
    record->name_len = wire[2];
    record->repeat_count = sys_get_le32(&wire[4]);
    record->package_len = package_len;
    record->package_crc32 = sys_get_le32(&wire[12]);
    memcpy(record->name, &wire[16], sizeof(record->name));
    memcpy(record->package, &wire[48], package_len);
    return k380_dynamic_macro_record_validate(record);
}

static void emit_u16(uint8_t *code, size_t *offset, uint16_t value)
{
    sys_put_le16(value, &code[*offset]);
    *offset += 2U;
}

static void emit_u32(uint8_t *code, size_t *offset, uint32_t value)
{
    sys_put_le32(value, &code[*offset]);
    *offset += 4U;
}

int k380_dynamic_macro_record_from_legacy(
    const struct k380_dynamic_macro *legacy,
    struct k380_dynamic_macro_record *record)
{
    if (legacy == NULL || record == NULL ||
        legacy->step_count > K380_DYNAMIC_MACRO_MAX_STEPS ||
        legacy->trigger > K380_DYNAMIC_MACRO_TRIGGER_COUNT ||
        (legacy->trigger == K380_DYNAMIC_MACRO_TRIGGER_COUNT &&
         legacy->count == 0U)) {
        return -EINVAL;
    }
    memset(record, 0, sizeof(*record));
    record->record_version = K380_DYNAMIC_MACRO_RECORD_VERSION;
    record->trigger = legacy->trigger;
    record->repeat_count = legacy->count == 0U ? 1U : legacy->count;
    record->name_len = 0U;
    while (record->name_len < K380_DYNAMIC_MACRO_NAME_MAX_BYTES &&
           legacy->name[record->name_len] != 0U) {
        record->name[record->name_len] = legacy->name[record->name_len];
        record->name_len++;
    }
    if (legacy->step_count == 0U) {
        return k380_dynamic_macro_record_validate(record);
    }

    uint8_t *code = &record->package[16];
    size_t code_offset = 0U;
    for (uint8_t index = 0U; index < legacy->step_count; index++) {
        const struct k380_dynamic_macro_step *step = &legacy->steps[index];
        switch (step->type) {
        case K380_DYNAMIC_MACRO_PRESS_KEY:
        case K380_DYNAMIC_MACRO_RELEASE_KEY:
        case K380_DYNAMIC_MACRO_TAP_KEY:
            if (!is_keyboard_keypad_usage(step->value.key_usage) ||
                code_offset + 3U > K380_MACRO_VM_MAX_CODE_BYTES) {
                return -EINVAL;
            }
            code[code_offset++] = step->type;
            emit_u16(code, &code_offset, step->value.key_usage);
            break;
        case K380_DYNAMIC_MACRO_WAIT_MS:
            if (step->value.wait_ms == 0U ||
                code_offset + 5U > K380_MACRO_VM_MAX_CODE_BYTES) {
                return -EINVAL;
            }
            code[code_offset++] = K380_MACRO_VM_OP_WAIT;
            emit_u32(code, &code_offset, step->value.wait_ms);
            break;
        case K380_DYNAMIC_MACRO_WAIT_RANDOM:
            if (step->value.wait_random.min_ms == 0U ||
                step->value.wait_random.min_ms > step->value.wait_random.max_ms ||
                code_offset + 9U > K380_MACRO_VM_MAX_CODE_BYTES) {
                return -EINVAL;
            }
            code[code_offset++] = K380_MACRO_VM_OP_RANDOM_WAIT;
            emit_u32(code, &code_offset, step->value.wait_random.min_ms);
            emit_u32(code, &code_offset, step->value.wait_random.max_ms);
            break;
        case K380_DYNAMIC_MACRO_RELEASE_ALL:
            if (code_offset + 1U > K380_MACRO_VM_MAX_CODE_BYTES) {
                return -EINVAL;
            }
            code[code_offset++] = K380_MACRO_VM_OP_RELEASE_ALL;
            break;
        default:
            return -EINVAL;
        }
    }
    if (code_offset + 1U > K380_MACRO_VM_MAX_CODE_BYTES) {
        return -EINVAL;
    }
    code[code_offset++] = K380_MACRO_VM_OP_END;
    record->package[0] = 'K';
    record->package[1] = 'V';
    record->package[2] = 'M';
    record->package[3] = '1';
    record->package[4] = K380_MACRO_VM_PACKAGE_VERSION;
    sys_put_le16((uint16_t)code_offset, &record->package[8]);
    record->package_len = (uint16_t)(16U + code_offset);
    record->package_crc32 = package_crc32_without_field(record->package,
                                                        record->package_len);
    sys_put_le32(record->package_crc32, &record->package[12]);
    return k380_dynamic_macro_record_validate(record);
}

static int read_wire(size_t len, settings_read_cb read_cb, void *cb_arg,
                     void *target, size_t target_capacity)
{
    if (len > target_capacity) {
        return -EMSGSIZE;
    }
    int read_len = read_cb(cb_arg, target, len);
    return read_len < 0 ? read_len : (size_t)read_len == len ? 0 : -EMSGSIZE;
}

static int load_v2_record(const char *name, size_t len,
                          settings_read_cb read_cb, void *cb_arg,
                          void *param)
{
    (void)param;
    const char *next = NULL;
    if (!settings_name_steq(name, "p", &next)) {
        return 0;
    }
    int preset = parse_index(next, K380_DYNAMIC_PRESET_COUNT, &next);
    if (preset < 0 || next == NULL || !settings_name_steq(next, "m", &next)) {
        return 0;
    }
    int slot = parse_index(next, K380_DYNAMIC_MACRO_SLOT_COUNT, &next);
    if (slot < 0 || next != NULL) {
        return 0;
    }
    if (load_target == NULL || (uint8_t)preset != load_preset ||
        (uint8_t)slot != load_slot) {
        return 0;
    }
    int err = read_wire(len, read_cb, cb_arg, store_wire_scratch,
                        sizeof(store_wire_scratch));
    if (err == 0) {
        err = k380_dynamic_macro_record_decode(load_target,
                                                store_wire_scratch, len);
        load_found = err == 0;
    }
    if (err != 0 && load_status == 0) {
        load_status = err;
    }
    return err;
}

static int load_record_from_settings(uint8_t preset, uint8_t slot,
                                     struct k380_dynamic_macro_record *record)
{
    memset(record, 0, sizeof(*record));
    load_preset = preset;
    load_slot = slot;
    load_target = record;
    load_found = false;
    load_status = 0;
    int err = settings_load_subtree_direct(K380_DYNAMIC_MACRO_V2_SETTINGS_ROOT,
                                            load_v2_record, NULL);
    load_target = NULL;
    if (err != 0) {
        return err;
    }
    if (load_status != 0) {
        return load_status;
    }
    return load_found ? 0 : -ENOENT;
}

static int load_legacy_record_callback(const char *name, size_t len,
                                       settings_read_cb read_cb, void *cb_arg,
                                       void *param)
{
    (void)param;
    const char *next = NULL;
    if (!settings_name_steq(name, "p", &next)) {
        return 0;
    }
    int preset = parse_index(next, K380_DYNAMIC_PRESET_COUNT, &next);
    if (preset < 0 || next == NULL || !settings_name_steq(next, "m", &next)) {
        return 0;
    }
    int slot = parse_index(next, K380_DYNAMIC_MACRO_SLOT_COUNT, &next);
    if (slot < 0 || next != NULL || legacy_target == NULL ||
        (uint8_t)preset != legacy_preset || (uint8_t)slot != legacy_slot) {
        return 0;
    }
    if (len != sizeof(*legacy_target)) {
        legacy_status = -EMSGSIZE;
        return legacy_status;
    }
    int err = read_cb(cb_arg, legacy_target, sizeof(*legacy_target));
    if (err < 0) {
        legacy_status = err;
        return err;
    }
    if ((size_t)err != sizeof(*legacy_target)) {
        legacy_status = -EMSGSIZE;
        return legacy_status;
    }
    legacy_found = true;
    return 0;
}

static int load_legacy_record(uint8_t preset, uint8_t slot,
                              struct k380_dynamic_macro *legacy)
{
    memset(legacy, 0, sizeof(*legacy));
    legacy_target = legacy;
    legacy_preset = preset;
    legacy_slot = slot;
    legacy_found = false;
    legacy_status = 0;
    int err = settings_load_subtree_direct(K380_DYNAMIC_SETTINGS_ROOT,
                                            load_legacy_record_callback, NULL);
    legacy_target = NULL;
    if (err != 0) {
        return err;
    }
    if (legacy_status != 0) {
        return legacy_status;
    }
    return legacy_found ? 0 : -ENOENT;
}

static int format_legacy_key(char *key, size_t key_len, uint8_t preset,
                             uint8_t slot)
{
    return check_formatted_len(
        snprintf(key, key_len, K380_DYNAMIC_SETTINGS_ROOT "/p/%u/m/%u",
                 preset, slot),
        key_len);
}

static int save_record_unlocked(uint8_t preset, uint8_t slot,
                                const struct k380_dynamic_macro_record *record)
{
    if (k380_dynamic_macro_record_validate(record) != 0) {
        return -EINVAL;
    }
    size_t wire_len;
    int err = k380_dynamic_macro_record_encode(record, store_wire_scratch,
                                               sizeof(store_wire_scratch),
                                               &wire_len);
    if (err != 0) {
        return err;
    }
    char key[64];
    err = format_v2_key(key, sizeof(key), preset, slot);
    return err == 0 ? settings_save_one(key, store_wire_scratch, wire_len)
                    : err;
}

int k380_dynamic_macro_store_load(uint8_t preset, uint8_t slot,
                                  struct k380_dynamic_macro_record *record)
{
    if (preset >= K380_DYNAMIC_PRESET_COUNT ||
        slot >= K380_DYNAMIC_MACRO_SLOT_COUNT || record == NULL) {
        return -EINVAL;
    }
    k380_dynamic_settings_lock();
    int err = load_record_from_settings(preset, slot, record);
    if (err == -ENOENT) {
        err = load_legacy_record(preset, slot, &legacy_scratch);
        if (err == 0) {
            err = k380_dynamic_macro_record_from_legacy(&legacy_scratch,
                                                        record);
            if (err == 0) {
                err = save_record_unlocked(preset, slot, record);
                if (err == 0) {
                    char key[64];
                    err = format_legacy_key(key, sizeof(key), preset, slot);
                    if (err == 0) {
                        err = settings_delete(key);
                    }
                }
            }
        }
    }
    if (err == -ENOENT) {
        memset(record, 0, sizeof(*record));
    }
    k380_dynamic_settings_unlock();
    return err == -ENOENT ? 0 : err;
}

int k380_dynamic_macro_store_save(uint8_t preset, uint8_t slot,
                                  const struct k380_dynamic_macro_record *record)
{
    if (preset >= K380_DYNAMIC_PRESET_COUNT ||
        slot >= K380_DYNAMIC_MACRO_SLOT_COUNT || record == NULL) {
        return -EINVAL;
    }
    k380_dynamic_settings_lock();
    int err = save_record_unlocked(preset, slot, record);
    k380_dynamic_settings_unlock();
    return err;
}

int k380_dynamic_macro_store_restore_slot(uint8_t preset, uint8_t slot)
{
    if (preset >= K380_DYNAMIC_PRESET_COUNT ||
        slot >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
        return -EINVAL;
    }
    char key[64];
    k380_dynamic_settings_lock();

    int first_err = format_v2_key(key, sizeof(key), preset, slot);
    if (first_err == 0) {
        first_err = settings_delete(key);
    }

    int err = format_legacy_key(key, sizeof(key), preset, slot);
    if (err == 0) {
        err = settings_delete(key);
    }

    k380_dynamic_settings_unlock();
    return first_err != 0 ? first_err : err;
}

int k380_dynamic_macro_store_restore_preset(uint8_t preset)
{
    if (preset >= K380_DYNAMIC_PRESET_COUNT) {
        return -EINVAL;
    }
    int first_err = 0;
    for (uint8_t slot = 0U; slot < K380_DYNAMIC_MACRO_SLOT_COUNT; slot++) {
        int err = k380_dynamic_macro_store_restore_slot(preset, slot);
        if (first_err == 0 && err != 0) {
            first_err = err;
        }
    }
    return first_err;
}

int k380_dynamic_macro_store_restore_all(void)
{
    int first_err = 0;
    for (uint8_t preset = 0U; preset < K380_DYNAMIC_PRESET_COUNT; preset++) {
        int err = k380_dynamic_macro_store_restore_preset(preset);
        if (first_err == 0 && err != 0) {
            first_err = err;
        }
    }
    return first_err;
}
