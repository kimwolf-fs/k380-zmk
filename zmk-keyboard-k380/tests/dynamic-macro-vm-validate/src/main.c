#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/dynamic_macro_vm.h>

#include "macro_vm_vectors.h"

static const uint8_t *canonical_package = k380_macro_vm_vectors[0].package;
static const size_t canonical_package_len = k380_macro_vm_vectors[0].package_len;

static uint32_t crc32_without_package_crc(const uint8_t *package, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0U; index < len; index++) {
        const uint8_t value = index >= 12U && index < 16U ? 0U : package[index];
        crc ^= value;
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

static void update_crc(uint8_t *package, size_t len)
{
    sys_put_le32(crc32_without_package_crc(package, len), &package[12]);
}

static size_t make_code_package(uint8_t *package, const uint8_t *code,
                                uint16_t code_len)
{
    memcpy(package, canonical_package, K380_MACRO_VM_PACKAGE_HEADER_SIZE);
    package[6] = 0U;
    sys_put_le16(code_len, &package[8]);
    memcpy(&package[16], code, code_len);
    update_crc(package, 16U + code_len);
    return 16U + code_len;
}

static size_t make_nested_call_package(uint8_t *package)
{
    uint8_t code[50] = {
        K380_MACRO_VM_OP_CALL, 0U,
        K380_MACRO_VM_OP_END,
        K380_MACRO_VM_OP_LOOP_COUNT_BEGIN, 1U, 0U, 0U, 0U, 36U, 0U,
        K380_MACRO_VM_OP_LOOP_COUNT_BEGIN, 1U, 0U, 0U, 0U, 36U, 0U,
        K380_MACRO_VM_OP_LOOP_COUNT_BEGIN, 1U, 0U, 0U, 0U, 36U, 0U,
        K380_MACRO_VM_OP_CALL, 1U,
        K380_MACRO_VM_OP_LOOP_END, 17U, 0U,
        K380_MACRO_VM_OP_LOOP_END, 10U, 0U,
        K380_MACRO_VM_OP_LOOP_END, 3U, 0U,
        K380_MACRO_VM_OP_RETURN,
        K380_MACRO_VM_OP_LOOP_COUNT_BEGIN, 1U, 0U, 0U, 0U, 49U, 0U,
        K380_MACRO_VM_OP_PRESS, 4U, 0U,
        K380_MACRO_VM_OP_LOOP_END, 36U, 0U,
        K380_MACRO_VM_OP_RETURN,
    };

    memcpy(package, canonical_package, K380_MACRO_VM_PACKAGE_HEADER_SIZE);
    package[6] = 2U;
    sys_put_le16(3U, &package[16]);
    sys_put_le16(36U, &package[18]);
    sys_put_le16(36U, &package[20]);
    sys_put_le16(50U, &package[22]);
    sys_put_le16(sizeof(code), &package[8]);
    memcpy(&package[24], code, sizeof(code));
    update_crc(package, 24U + sizeof(code));
    return 24U + sizeof(code);
}

ZTEST(dynamic_macro_vm_validate, test_canonical_package_is_accepted)
{
    struct k380_macro_vm_package_view view;

    zassert_equal(K380_MACRO_VM_OK,
                  k380_macro_vm_validate(canonical_package,
                                          canonical_package_len, &view));
    zassert_equal(12U, view.code_len);
    zassert_equal(0U, view.function_count);
}

ZTEST(dynamic_macro_vm_validate, test_header_and_crc_mutations_are_rejected)
{
    uint8_t package[28];
    struct k380_macro_vm_package_view view;

    memcpy(package, canonical_package, sizeof(package));
    package[0] = 'X';
    zassert_equal(K380_MACRO_VM_INVALID_MAGIC,
                  k380_macro_vm_validate(package, sizeof(package), &view));

    memcpy(package, canonical_package, sizeof(package));
    package[4] = 2U;
    zassert_equal(K380_MACRO_VM_INVALID_VERSION,
                  k380_macro_vm_validate(package, sizeof(package), &view));

    memcpy(package, canonical_package, sizeof(package));
    package[7] = 1U;
    zassert_equal(K380_MACRO_VM_INVALID_RESERVED,
                  k380_macro_vm_validate(package, sizeof(package), &view));

    memcpy(package, canonical_package, sizeof(package));
    package[27] ^= 1U;
    zassert_equal(K380_MACRO_VM_CRC_MISMATCH,
                  k380_macro_vm_validate(package, sizeof(package), &view));
}

ZTEST(dynamic_macro_vm_validate, test_opcode_key_and_truncated_operands_are_rejected)
{
    uint8_t package[32];
    struct k380_macro_vm_package_view view;
    const uint8_t unknown[] = {0xff, 0x00};
    size_t length = make_code_package(package, unknown, sizeof(unknown));
    zassert_equal(K380_MACRO_VM_UNKNOWN_OPCODE,
                  k380_macro_vm_validate(package, length, &view));

    const uint8_t invalid_key[] = {K380_MACRO_VM_OP_PRESS, 0xff, 0xff,
                                   K380_MACRO_VM_OP_END};
    length = make_code_package(package, invalid_key, sizeof(invalid_key));
    zassert_equal(K380_MACRO_VM_INVALID_KEY_USAGE,
                  k380_macro_vm_validate(package, length, &view));

    const uint8_t truncated[] = {K380_MACRO_VM_OP_WAIT, 0x01, 0x00};
    length = make_code_package(package, truncated, sizeof(truncated));
    zassert_equal(K380_MACRO_VM_TRUNCATED_OPERAND,
                  k380_macro_vm_validate(package, length, &view));
}

ZTEST(dynamic_macro_vm_validate, test_targets_must_be_boundaries_and_inside_code)
{
    uint8_t package[32];
    struct k380_macro_vm_package_view view;
    const uint8_t inside_operand[] = {
        K380_MACRO_VM_OP_TIMER_GE, 1, 0, 0, 0, 0, 1, 0,
        K380_MACRO_VM_OP_END,
    };
    size_t length = make_code_package(package, inside_operand,
                                      sizeof(inside_operand));
    zassert_not_equal(K380_MACRO_VM_OK,
                      k380_macro_vm_validate(package, length, &view));

    const uint8_t outside_code[] = {
        K380_MACRO_VM_OP_TIMER_GE, 1, 0, 0, 0, 0, 100, 0,
        K380_MACRO_VM_OP_END,
    };
    length = make_code_package(package, outside_code, sizeof(outside_code));
    zassert_equal(K380_MACRO_VM_INVALID_TARGET,
                  k380_macro_vm_validate(package, length, &view));
}

ZTEST(dynamic_macro_vm_validate, test_entry_and_function_terminators_are_strict)
{
    uint8_t package[32];
    struct k380_macro_vm_package_view view;
    const uint8_t top_return[] = {K380_MACRO_VM_OP_RETURN};
    size_t length = make_code_package(package, top_return, sizeof(top_return));
    zassert_equal(K380_MACRO_VM_INVALID_ENTRY_TERMINATOR,
                  k380_macro_vm_validate(package, length, &view));

    memcpy(package, canonical_package, canonical_package_len);
    package[6] = 1U;
    sys_put_le16(2U, &package[8]);
    sys_put_le16(1U, &package[16]);
    sys_put_le16(2U, &package[18]);
    package[20] = K380_MACRO_VM_OP_END;
    package[21] = K380_MACRO_VM_OP_END;
    update_crc(package, 22U);
    zassert_equal(K380_MACRO_VM_INVALID_FUNCTION_TERMINATOR,
                  k380_macro_vm_validate(package, 22U, &view));
}

ZTEST(dynamic_macro_vm_validate,
      test_function_call_in_nested_loops_counts_callee_loop_depth)
{
    uint8_t package[96];
    struct k380_macro_vm_package_view view;
    size_t length = make_nested_call_package(package);

    zassert_equal(K380_MACRO_VM_LOOP_DEPTH,
                  k380_macro_vm_validate(package, length, &view));
}

ZTEST_SUITE(dynamic_macro_vm_validate, NULL, NULL, NULL, NULL, NULL);
