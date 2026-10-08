#ifndef ZMK_KEYBOARD_K380_DYNAMIC_MACRO_RECORD_H_
#define ZMK_KEYBOARD_K380_DYNAMIC_MACRO_RECORD_H_

#include <stddef.h>
#include <stdint.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_macro_vm_format.h>

#define K380_DYNAMIC_MACRO_RECORD_VERSION 2U
#define K380_DYNAMIC_MACRO_RECORD_HEADER_BYTES 48U
#define K380_DYNAMIC_MACRO_PACKAGE_MAX_BYTES \
    K380_MACRO_VM_MAX_PACKAGE_BYTES
#define K380_DYNAMIC_MACRO_RECORD_MAX_BYTES \
    (K380_DYNAMIC_MACRO_RECORD_HEADER_BYTES + \
     K380_DYNAMIC_MACRO_PACKAGE_MAX_BYTES)

struct k380_dynamic_macro_record {
    uint8_t record_version;
    uint8_t trigger;
    uint8_t name_len;
    uint8_t reserved;
    uint32_t repeat_count;
    uint16_t package_len;
    uint16_t reserved2;
    uint32_t package_crc32;
    uint8_t name[K380_DYNAMIC_MACRO_NAME_MAX_BYTES];
    uint8_t package[K380_DYNAMIC_MACRO_PACKAGE_MAX_BYTES];
};

_Static_assert(offsetof(struct k380_dynamic_macro_record, repeat_count) == 4U,
               "macro record repeat_count wire offset changed");
_Static_assert(offsetof(struct k380_dynamic_macro_record, package_len) == 8U,
               "macro record package_len wire offset changed");
_Static_assert(offsetof(struct k380_dynamic_macro_record, package_crc32) == 12U,
               "macro record CRC wire offset changed");
_Static_assert(offsetof(struct k380_dynamic_macro_record, name) == 16U,
               "macro record name wire offset changed");
_Static_assert(offsetof(struct k380_dynamic_macro_record, package) == 48U,
               "macro record package wire offset changed");
_Static_assert(sizeof(struct k380_dynamic_macro_record) ==
                   K380_DYNAMIC_MACRO_RECORD_MAX_BYTES,
               "macro record RAM layout changed");

int k380_dynamic_macro_record_validate(
    const struct k380_dynamic_macro_record *record);
size_t k380_dynamic_macro_record_wire_size(
    const struct k380_dynamic_macro_record *record);
int k380_dynamic_macro_record_encode(
    const struct k380_dynamic_macro_record *record, uint8_t *wire,
    size_t wire_capacity, size_t *wire_len);
int k380_dynamic_macro_record_decode(
    struct k380_dynamic_macro_record *record, const uint8_t *wire,
    size_t wire_len);
int k380_dynamic_macro_record_from_legacy(
    const struct k380_dynamic_macro *legacy,
    struct k380_dynamic_macro_record *record);

#endif
