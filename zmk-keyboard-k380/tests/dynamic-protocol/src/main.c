#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/dynamic_macro_record.h>
#include <zmk_keyboard_k380/dynamic_macro_vm.h>
#include <zmk_keyboard_k380/dynamic_macro_vm_format.h>
#include <zmk_keyboard_k380/dynamic_protocol.h>
#include <zmk_keyboard_k380/dynamic_settings.h>
#include <zmk_keyboard_k380/dynamic_macro.h>
#include <zmk_keyboard_k380/dynamic_macro_store.h>

static bool unlocked = true;
static struct k380_dynamic_config config;
static bool saved_macro_test_called;
static bool temporary_macro_test_called;
static uint8_t last_test_preset;
static uint8_t last_test_slot;
static struct k380_dynamic_macro last_test_macro;
static struct k380_dynamic_macro_record stored_records[K380_DYNAMIC_PRESET_COUNT]
                                                    [K380_DYNAMIC_MACRO_SLOT_COUNT];
static bool stored_record_valid[K380_DYNAMIC_PRESET_COUNT]
                               [K380_DYNAMIC_MACRO_SLOT_COUNT];
static int store_save_result;
static size_t store_save_calls;
static bool fake_macro_running;
static struct k380_dynamic_macro_record last_test_record;
static uint64_t fake_uptime_ms;
static struct k380_dynamic_macro_run_state fake_run_state;
static struct k380_dynamic_macro_trace_event fake_trace_events[4];
static size_t fake_trace_event_count;
static size_t stop_calls;

bool k380_dynamic_protocol_is_unlocked(void) { return unlocked; }

uint64_t k380_dynamic_protocol_uptime_ms(void) { return fake_uptime_ms; }

void k380_dynamic_settings_lock(void) {}
void k380_dynamic_settings_unlock(void) {}

static int fake_settings_read(void *cb_arg, void *data, size_t len)
{
    const struct k380_dynamic_macro_record *record = cb_arg;
    uint8_t wire[K380_DYNAMIC_MACRO_RECORD_MAX_BYTES];
    size_t wire_len;

    if (k380_dynamic_macro_record_encode(record, wire, sizeof(wire),
                                         &wire_len) != 0 || len != wire_len) {
        return -EINVAL;
    }
    memcpy(data, wire, len);
    return (int)len;
}

int k380_dynamic_macro_test_settings_load_subtree_direct(
    const char *subtree, settings_load_direct_cb callback, void *cb_arg)
{
    if (subtree == NULL || callback == NULL) {
        return -EINVAL;
    }
    if (strcmp(subtree, K380_DYNAMIC_SETTINGS_ROOT "/v2") != 0) {
        return 0;
    }
    for (uint8_t preset = 0U; preset < K380_DYNAMIC_PRESET_COUNT; preset++) {
        for (uint8_t slot = 0U; slot < K380_DYNAMIC_MACRO_SLOT_COUNT; slot++) {
            if (!stored_record_valid[preset][slot]) {
                continue;
            }
            char name[16];
            snprintf(name, sizeof(name), "p/%u/m/%u", preset, slot);
            const int err = callback(name,
                                     k380_dynamic_macro_record_wire_size(
                                         &stored_records[preset][slot]),
                                     fake_settings_read,
                                     &stored_records[preset][slot], cb_arg);
            if (err != 0) {
                return err;
            }
        }
    }
    return 0;
}

int k380_dynamic_macro_test_settings_save_one(const char *key,
                                              const void *value, size_t len)
{
    unsigned int preset;
    unsigned int slot;
    struct k380_dynamic_macro_record record;

    store_save_calls++;
    if (store_save_result != 0) {
        return store_save_result;
    }
    if (key == NULL || value == NULL ||
        sscanf(key, K380_DYNAMIC_SETTINGS_ROOT "/v2/p/%u/m/%u", &preset,
               &slot) != 2 || preset >= K380_DYNAMIC_PRESET_COUNT ||
        slot >= K380_DYNAMIC_MACRO_SLOT_COUNT ||
        k380_dynamic_macro_record_decode(&record, value, len) != 0) {
        return -EINVAL;
    }
    stored_records[preset][slot] = record;
    stored_record_valid[preset][slot] = true;
    return 0;
}

int k380_dynamic_macro_test_settings_delete(const char *key)
{
    ARG_UNUSED(key);
    return 0;
}

int k380_dynamic_macro_test_record(
    uint8_t preset, uint8_t slot,
    const struct k380_dynamic_macro_record *record) {
    if (record == NULL || preset >= K380_DYNAMIC_PRESET_COUNT ||
        slot >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
        return -EINVAL;
    }
    last_test_record = *record;
    fake_macro_running = true;
    fake_run_state.run_id = 0x12345678U;
    fake_run_state.state = K380_DYNAMIC_MACRO_RUN_RUNNING;
    fake_run_state.vm_error = 0U;
    return 0;
}

int k380_dynamic_macro_stop(void) {
    stop_calls++;
    fake_macro_running = false;
    fake_run_state.state = K380_DYNAMIC_MACRO_RUN_STOPPED;
    return 0;
}

bool k380_dynamic_macro_is_running(void) { return fake_macro_running; }

void k380_dynamic_macro_get_run_state(
    struct k380_dynamic_macro_run_state *state) {
    if (state != NULL) {
        *state = fake_run_state;
    }
}

int k380_dynamic_macro_trace_read(
    uint32_t cursor, struct k380_dynamic_macro_trace_event *events,
    size_t capacity, struct k380_dynamic_macro_run_state *state) {
    if (state == NULL || (capacity > 0U && events == NULL)) {
        return -EINVAL;
    }
    *state = fake_run_state;
    if (cursor > fake_run_state.next_cursor) {
        return -EOVERFLOW;
    }
    size_t copied = 0U;
    for (size_t index = 0U; index < fake_trace_event_count &&
                           copied < capacity; index++) {
        if (fake_trace_events[index].sequence > cursor) {
            events[copied++] = fake_trace_events[index];
        }
    }
    if (copied > 0U) {
        state->next_cursor = events[copied - 1U].sequence;
    }
    return (int)copied;
}

int k380_dynamic_settings_load(struct k380_dynamic_config *cfg) {
    *cfg = config;
    return 0;
}

int k380_dynamic_settings_save(const struct k380_dynamic_config *cfg) {
    config = *cfg;
    return 0;
}

int k380_dynamic_settings_restore_all(void) {
    k380_dynamic_config_init_defaults(&config);
    return 0;
}

int k380_dynamic_set_active_preset(uint8_t preset) {
    config.active_preset = preset;
    return 0;
}

int k380_dynamic_macro_test(uint8_t slot) {
    saved_macro_test_called = true;
    last_test_slot = slot;
    return slot < K380_DYNAMIC_MACRO_SLOT_COUNT ? 0 : -1;
}

int k380_dynamic_macro_test_temporary(uint8_t preset, uint8_t slot,
                                      const struct k380_dynamic_macro *macro) {
    temporary_macro_test_called = true;
    last_test_preset = preset;
    last_test_slot = slot;
    if (macro != NULL) {
        last_test_macro = *macro;
    }
    return slot < K380_DYNAMIC_MACRO_SLOT_COUNT && macro != NULL ? 0 : -1;
}

static size_t make_frame(uint8_t *frame, uint8_t command, uint16_t sequence,
                         const uint8_t *payload, uint16_t payload_len)
{
    memcpy(frame, K380_DYNAMIC_PROTOCOL_MAGIC, K380_DYNAMIC_PROTOCOL_MAGIC_SIZE);
    frame[8] = K380_DYNAMIC_PROTOCOL_VERSION;
    frame[9] = command;
    sys_put_le16(sequence, &frame[10]);
    sys_put_le16(payload_len, &frame[12]);
    if (payload_len > 0U) {
        memcpy(&frame[K380_DYNAMIC_PROTOCOL_HEADER_SIZE], payload, payload_len);
    }
    sys_put_le32(crc32_ieee(frame, K380_DYNAMIC_PROTOCOL_HEADER_SIZE + payload_len),
                 &frame[K380_DYNAMIC_PROTOCOL_HEADER_SIZE + payload_len]);
    return K380_DYNAMIC_PROTOCOL_FRAME_SIZE(payload_len);
}

static enum k380_dynamic_result transact(uint8_t command, uint16_t sequence,
                                         const uint8_t *payload,
                                         uint16_t payload_len, uint8_t *data,
                                         size_t data_capacity, size_t *data_len)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t frame[K380_DYNAMIC_PROTOCOL_FRAME_SIZE(payload_len)];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0U;
    const size_t frame_len = make_frame(frame, command, sequence, payload,
                                        payload_len);

    k380_dynamic_protocol_parser_init(&parser);
    (void)k380_dynamic_protocol_feed(
        &parser, frame, frame_len, response, sizeof(response), &response_len,
        NULL);
    zassert_true(response_len >= K380_DYNAMIC_PROTOCOL_FRAME_SIZE(1));
    const uint16_t response_payload_len = sys_get_le16(&response[12]);
    zassert_true(response_payload_len >= 1U);
    zassert_true(response_payload_len - 1U <= data_capacity);
    if (data_len != NULL) {
        *data_len = response_payload_len - 1U;
    }
    if (data != NULL && response_payload_len > 1U) {
        memcpy(data, &response[K380_DYNAMIC_PROTOCOL_HEADER_SIZE + 1U],
               response_payload_len - 1U);
    }
    return response[K380_DYNAMIC_PROTOCOL_HEADER_SIZE];
}

static uint32_t package_crc(const uint8_t *package, size_t length)
{
    uint8_t copy[K380_MACRO_VM_MAX_PACKAGE_BYTES];

    zassert_true(length <= sizeof(copy));
    memcpy(copy, package, length);
    memset(&copy[12], 0, 4U);
    return crc32_ieee(copy, length);
}

static void make_record(struct k380_dynamic_macro_record *record,
                        uint16_t package_len, bool maximum_code)
{
    memset(record, 0, sizeof(*record));
    record->record_version = K380_DYNAMIC_MACRO_RECORD_VERSION;
    record->trigger = K380_DYNAMIC_MACRO_TRIGGER_ONCE;
    record->repeat_count = 1U;
    record->name_len = 4U;
    memcpy(record->name, "demo", 4U);
    record->package_len = package_len;
    memcpy(record->package, K380_MACRO_VM_PACKAGE_MAGIC,
           K380_MACRO_VM_PACKAGE_MAGIC_SIZE);
    record->package[4] = K380_MACRO_VM_PACKAGE_VERSION;
    const uint8_t function_count = maximum_code ?
                                       K380_MACRO_VM_MAX_FUNCTIONS :
                                       0U;
    const uint16_t code_len = package_len - K380_MACRO_VM_PACKAGE_HEADER_SIZE -
                              function_count * K380_MACRO_VM_FUNCTION_ENTRY_SIZE;
    record->package[6] = function_count;
    sys_put_le16(code_len, &record->package[8]);
    uint8_t *code = &record->package[K380_MACRO_VM_PACKAGE_HEADER_SIZE +
                                    function_count *
                                        K380_MACRO_VM_FUNCTION_ENTRY_SIZE];
    if (maximum_code) {
        code[0] = K380_MACRO_VM_OP_END;
        uint16_t code_offset = 1U;
        for (uint8_t index = 0U; index < function_count; index++) {
            sys_put_le16(code_offset,
                         &record->package[K380_MACRO_VM_PACKAGE_HEADER_SIZE +
                                          index * K380_MACRO_VM_FUNCTION_ENTRY_SIZE]);
            code[code_offset++] = K380_MACRO_VM_OP_RELEASE_ALL;
            code[code_offset++] = K380_MACRO_VM_OP_RETURN;
            sys_put_le16(code_offset,
                         &record->package[K380_MACRO_VM_PACKAGE_HEADER_SIZE +
                                          index * K380_MACRO_VM_FUNCTION_ENTRY_SIZE + 2U]);
        }
        memset(&code[code_offset], K380_MACRO_VM_OP_RELEASE_ALL,
               code_len - code_offset);
    } else {
        code[0] = K380_MACRO_VM_OP_END;
    }
    record->package_crc32 = package_crc(record->package, package_len);
    sys_put_le32(record->package_crc32, &record->package[12]);
}

static size_t make_begin_payload(uint8_t *payload, uint8_t operation,
                                 uint8_t preset, uint8_t slot,
                                 const struct k380_dynamic_macro_record *record)
{
    payload[0] = operation;
    payload[1] = preset;
    payload[2] = slot;
    payload[3] = record->trigger;
    payload[4] = record->name_len;
    payload[5] = 0U;
    sys_put_le32(record->repeat_count, &payload[6]);
    sys_put_le16(record->package_len, &payload[10]);
    sys_put_le16(0U, &payload[12]);
    sys_put_le32(record->package_crc32, &payload[14]);
    memcpy(&payload[18], record->name, K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
    return 50U;
}

static void reset_test_state(void)
{
    memset(stored_records, 0, sizeof(stored_records));
    memset(stored_record_valid, 0, sizeof(stored_record_valid));
    store_save_result = 0;
    store_save_calls = 0U;
    fake_macro_running = false;
    memset(&last_test_record, 0, sizeof(last_test_record));
    fake_uptime_ms = 0U;
    memset(&fake_run_state, 0, sizeof(fake_run_state));
    memset(fake_trace_events, 0, sizeof(fake_trace_events));
    fake_trace_event_count = 0U;
    stop_calls = 0U;
}

ZTEST(dynamic_protocol, test_valid_hello_returns_ready)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t frame[K380_DYNAMIC_PROTOCOL_FRAME_SIZE(0)];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0;

    unlocked = true;
    k380_dynamic_protocol_parser_init(&parser);
    const size_t frame_len = make_frame(frame, K380_DYNAMIC_COMMAND_HELLO, 0x1234, NULL, 0);

    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  k380_dynamic_protocol_feed(&parser, frame, frame_len, response,
                                             sizeof(response), &response_len, NULL));
    zassert_equal(K380_DYNAMIC_PROTOCOL_FRAME_SIZE(1), response_len);
    zassert_mem_equal(response, K380_DYNAMIC_PROTOCOL_MAGIC, K380_DYNAMIC_PROTOCOL_MAGIC_SIZE);
    zassert_equal(K380_DYNAMIC_COMMAND_HELLO | K380_DYNAMIC_COMMAND_RESPONSE_FLAG, response[9]);
    zassert_equal(K380_DYNAMIC_RESULT_READY, response[K380_DYNAMIC_PROTOCOL_HEADER_SIZE]);
}

ZTEST(dynamic_protocol, test_wrong_magic_returns_not_k380)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t frame[K380_DYNAMIC_PROTOCOL_FRAME_SIZE(0)];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0;

    k380_dynamic_protocol_parser_init(&parser);
    make_frame(frame, K380_DYNAMIC_COMMAND_HELLO, 1, NULL, 0);
    frame[0] = 'X';

    zassert_equal(K380_DYNAMIC_RESULT_NOT_K380,
                  k380_dynamic_protocol_feed(&parser, frame, sizeof(frame), response,
                                             sizeof(response), &response_len, NULL));
    zassert_equal(K380_DYNAMIC_RESULT_NOT_K380, response[K380_DYNAMIC_PROTOCOL_HEADER_SIZE]);
}

ZTEST(dynamic_protocol, test_unsupported_version_returns_version_mismatch)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t frame[K380_DYNAMIC_PROTOCOL_FRAME_SIZE(0)];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0;

    k380_dynamic_protocol_parser_init(&parser);
    make_frame(frame, K380_DYNAMIC_COMMAND_HELLO, 1, NULL, 0);
    frame[8] = K380_DYNAMIC_PROTOCOL_VERSION + 1U;
    sys_put_le32(crc32_ieee(frame, K380_DYNAMIC_PROTOCOL_HEADER_SIZE),
                 &frame[K380_DYNAMIC_PROTOCOL_HEADER_SIZE]);

    zassert_equal(K380_DYNAMIC_RESULT_VERSION_MISMATCH,
                  k380_dynamic_protocol_feed(&parser, frame, sizeof(frame), response,
                                             sizeof(response), &response_len, NULL));
}

ZTEST(dynamic_protocol, test_crc_mismatch_returns_typed_error)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t frame[K380_DYNAMIC_PROTOCOL_FRAME_SIZE(0)];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0;

    k380_dynamic_protocol_parser_init(&parser);
    make_frame(frame, K380_DYNAMIC_COMMAND_HELLO, 1, NULL, 0);
    frame[K380_DYNAMIC_PROTOCOL_HEADER_SIZE] ^= 0x01;

    zassert_equal(K380_DYNAMIC_RESULT_CRC_MISMATCH,
                  k380_dynamic_protocol_feed(&parser, frame, sizeof(frame), response,
                                             sizeof(response), &response_len, NULL));
}

ZTEST(dynamic_protocol, test_payload_larger_than_limit_is_rejected)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t frame[K380_DYNAMIC_PROTOCOL_HEADER_SIZE];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0;

    k380_dynamic_protocol_parser_init(&parser);
    memcpy(frame, K380_DYNAMIC_PROTOCOL_MAGIC, K380_DYNAMIC_PROTOCOL_MAGIC_SIZE);
    frame[8] = K380_DYNAMIC_PROTOCOL_VERSION;
    frame[9] = K380_DYNAMIC_COMMAND_HELLO;
    sys_put_le16(1, &frame[10]);
    sys_put_le16(K380_DYNAMIC_MAX_PAYLOAD + 1U, &frame[12]);

    zassert_equal(K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH,
                  k380_dynamic_protocol_feed(&parser, frame, sizeof(frame), response,
                                             sizeof(response), &response_len, NULL));
}

ZTEST(dynamic_protocol, test_fragmented_frame_is_buffered_until_complete)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t frame[K380_DYNAMIC_PROTOCOL_FRAME_SIZE(0)];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0;

    unlocked = true;
    k380_dynamic_protocol_parser_init(&parser);
    const size_t frame_len = make_frame(frame, K380_DYNAMIC_COMMAND_HELLO, 1, NULL, 0);

    zassert_equal(K380_DYNAMIC_RESULT_NEED_MORE,
                  k380_dynamic_protocol_feed(&parser, frame, 5, response,
                                             sizeof(response), &response_len, NULL));
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  k380_dynamic_protocol_feed(&parser, &frame[5], frame_len - 5, response,
                                             sizeof(response), &response_len, NULL));
}

ZTEST(dynamic_protocol, test_coalesced_frames_report_first_frame_consumption)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t frame[K380_DYNAMIC_PROTOCOL_FRAME_SIZE(0) * 2U];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0;
    size_t consumed_len = 0;
    const size_t first_len = make_frame(frame, K380_DYNAMIC_COMMAND_HELLO, 1, NULL, 0);
    const size_t second_len = make_frame(&frame[first_len], K380_DYNAMIC_COMMAND_HELLO, 2, NULL, 0);

    unlocked = true;
    k380_dynamic_protocol_parser_init(&parser);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  k380_dynamic_protocol_feed(&parser, frame, first_len + second_len, response,
                                             sizeof(response), &response_len, &consumed_len));
    zassert_equal(first_len, consumed_len);
}

ZTEST(dynamic_protocol, test_test_macro_accepts_temporary_macro_payload)
{
    struct k380_dynamic_protocol_parser parser;
    uint8_t payload[1U + K380_DYNAMIC_MACRO_NAME_MAX_BYTES + 4U + 3U];
    uint8_t frame[K380_DYNAMIC_PROTOCOL_FRAME_SIZE(sizeof(payload))];
    uint8_t response[K380_DYNAMIC_PROTOCOL_MAX_FRAME_SIZE];
    size_t response_len = 0;
    size_t offset = 0;

    k380_dynamic_config_init_defaults(&config);
    config.active_preset = 2U;
    saved_macro_test_called = false;
    temporary_macro_test_called = false;
    memset(&last_test_macro, 0, sizeof(last_test_macro));

    payload[offset++] = 3U;
    memset(&payload[offset], 0, K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
    memcpy(&payload[offset], "temp", 4U);
    offset += K380_DYNAMIC_MACRO_NAME_MAX_BYTES;
    payload[offset++] = 1U;
    payload[offset++] = K380_DYNAMIC_MACRO_TRIGGER_ONCE;
    sys_put_le16(1U, &payload[offset]);
    offset += 2U;
    payload[offset++] = K380_DYNAMIC_MACRO_PRESS_KEY;
    sys_put_le16(4U, &payload[offset]);
    offset += 2U;

    k380_dynamic_protocol_parser_init(&parser);
    const size_t frame_len = make_frame(frame, K380_DYNAMIC_COMMAND_TEST_MACRO, 1,
                                        payload, sizeof(payload));

    zassert_equal(K380_DYNAMIC_RESULT_INVALID_ARGUMENT,
                  k380_dynamic_protocol_feed(&parser, frame, frame_len, response,
                                             sizeof(response), &response_len, NULL));
    zassert_false(saved_macro_test_called);
    zassert_false(temporary_macro_test_called);
}

ZTEST(dynamic_protocol, test_info_exposes_v2_macro_transfer_limits)
{
    uint8_t data[32];
    size_t data_len;

    reset_test_state();
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_GET_INFO, 1U, NULL, 0U, data,
                           sizeof(data), &data_len));
    zassert_true(data_len >= 12U);
    zassert_equal(2U, data[0]);
    zassert_equal(2U, data[1]);
    zassert_equal(K380_MACRO_VM_MAX_PACKAGE_BYTES,
                  sys_get_le16(&data[7]));
    zassert_equal(K380_MACRO_VM_MAX_CODE_BYTES,
                  sys_get_le16(&data[9]));
    zassert_equal(K380_DYNAMIC_MACRO_CHUNK_MAX, sys_get_le16(&data[11]));
}

ZTEST(dynamic_protocol, test_macro_meta_returns_saved_record_and_empty_slot)
{
    struct k380_dynamic_macro_record record;
    uint8_t data[64];
    size_t data_len;

    reset_test_state();
    make_record(&record, 17U, false);
    stored_records[1U][2U] = record;
    stored_record_valid[1U][2U] = true;

    uint8_t request[] = {1U, 2U};
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_GET_MACRO_META, 2U, request,
                           sizeof(request), data, sizeof(data), &data_len));
    zassert_equal(48U, data_len);
    zassert_equal(1U, data[0]);
    zassert_equal(2U, data[1]);
    zassert_equal(K380_DYNAMIC_MACRO_RECORD_VERSION, data[2]);
    zassert_equal(17U, sys_get_le16(&data[10]));
    zassert_equal(record.package_crc32, sys_get_le32(&data[12]));
    zassert_mem_equal(&data[16], "demo", 4U);

    request[1] = 3U;
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_GET_MACRO_META, 3U, request,
                           sizeof(request), data, sizeof(data), &data_len));
    zassert_equal(48U, data_len);
    zassert_equal(K380_DYNAMIC_MACRO_RECORD_VERSION, data[2]);
    zassert_equal(0U, data[4]);
    zassert_equal(0U, sys_get_le16(&data[10]));
    zassert_equal(0U, sys_get_le32(&data[12]));
}

ZTEST(dynamic_protocol, test_read_macro_chunk_returns_only_requested_package_bytes)
{
    struct k380_dynamic_macro_record record;
    uint8_t request[6];
    uint8_t data[64];
    size_t data_len;

    reset_test_state();
    make_record(&record, 17U, false);
    stored_records[0U][0U] = record;
    stored_record_valid[0U][0U] = true;
    request[0] = 0U;
    request[1] = 0U;
    sys_put_le16(0U, &request[2]);
    sys_put_le16(K380_DYNAMIC_MACRO_CHUNK_MAX, &request[4]);

    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_READ_MACRO_CHUNK, 4U, request,
                           sizeof(request), data, sizeof(data), &data_len));
    zassert_equal(6U + record.package_len, data_len);
    zassert_equal(record.package_len, sys_get_le16(&data[4]));
    zassert_mem_equal(&data[6], record.package, record.package_len);
}

ZTEST(dynamic_protocol, test_write_without_session_returns_upload_not_found)
{
    uint8_t chunk[9] = {0};
    uint8_t data[8];
    size_t data_len;

    reset_test_state();
    sys_put_le16(1U, &chunk[6]);
    zassert_equal(K380_DYNAMIC_RESULT_UPLOAD_NOT_FOUND,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 5U, chunk,
                           sizeof(chunk), data, sizeof(data), &data_len));
}

ZTEST(dynamic_protocol, test_maximum_package_upload_uses_384_byte_chunks)
{
    struct k380_dynamic_macro_record record;
    uint8_t begin[64];
    uint8_t data[32];
    size_t data_len;
    uint32_t session_id;

    reset_test_state();
    make_record(&record, K380_MACRO_VM_MAX_PACKAGE_BYTES, true);
    const size_t begin_len = make_begin_payload(begin, 0U, 0U, 4U, &record);
    zassert_equal(50U, begin_len);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_BEGIN_MACRO_UPLOAD, 4U, begin,
                           begin_len, data, sizeof(data), &data_len));
    zassert_equal(6U, data_len);
    session_id = sys_get_le32(data);
    zassert_not_equal(0U, session_id);
    zassert_equal(K380_DYNAMIC_MACRO_CHUNK_MAX, sys_get_le16(&data[4]));

    uint8_t chunk[8U + K380_DYNAMIC_MACRO_CHUNK_MAX];
    sys_put_le32(session_id, &chunk[0]);
    sys_put_le16(0U, &chunk[4]);
    sys_put_le16(K380_DYNAMIC_MACRO_CHUNK_MAX, &chunk[6]);
    memcpy(&chunk[8], record.package, K380_DYNAMIC_MACRO_CHUNK_MAX);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 5U, chunk,
                           sizeof(chunk), data, sizeof(data), &data_len));
    zassert_equal(K380_DYNAMIC_MACRO_CHUNK_MAX, sys_get_le16(&data[4]));

    sys_put_le16(K380_DYNAMIC_MACRO_CHUNK_MAX, &chunk[4]);
    memcpy(&chunk[8], &record.package[K380_DYNAMIC_MACRO_CHUNK_MAX],
           K380_DYNAMIC_MACRO_CHUNK_MAX);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 6U, chunk,
                           sizeof(chunk), data, sizeof(data), &data_len));
    zassert_equal(2U * K380_DYNAMIC_MACRO_CHUNK_MAX,
                  sys_get_le16(&data[4]));

    const uint16_t final_len = K380_MACRO_VM_MAX_PACKAGE_BYTES -
                               2U * K380_DYNAMIC_MACRO_CHUNK_MAX;
    sys_put_le16(2U * K380_DYNAMIC_MACRO_CHUNK_MAX, &chunk[4]);
    sys_put_le16(final_len, &chunk[6]);
    memcpy(&chunk[8], &record.package[2U * K380_DYNAMIC_MACRO_CHUNK_MAX],
           final_len);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 7U, chunk,
                           (uint16_t)(8U + final_len), data, sizeof(data),
                           &data_len));
    zassert_equal(K380_MACRO_VM_MAX_PACKAGE_BYTES,
                  sys_get_le16(&data[4]));

    uint8_t commit[4];
    sys_put_le32(session_id, commit);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_COMMIT_MACRO_UPLOAD, 8U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
    zassert_equal(0U, sys_get_le32(data));
    zassert_equal(1U, store_save_calls);
    zassert_true(stored_record_valid[0U][4U]);
    zassert_mem_equal(stored_records[0U][4U].package, record.package,
                      sizeof(record.package));
}

ZTEST(dynamic_protocol, test_upload_rejects_offset_gap_and_expiry)
{
    struct k380_dynamic_macro_record record;
    uint8_t begin[64];
    uint8_t data[32];
    size_t data_len;
    uint8_t chunk[8U + K380_DYNAMIC_MACRO_CHUNK_MAX];

    reset_test_state();
    make_record(&record, 17U, false);
    const size_t begin_len = make_begin_payload(begin, 0U, 0U, 0U, &record);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_BEGIN_MACRO_UPLOAD, 9U, begin,
                           begin_len, data, sizeof(data), &data_len));
    const uint32_t session_id = sys_get_le32(data);
    sys_put_le32(session_id, chunk);
    sys_put_le16(1U, &chunk[4]);
    sys_put_le16(1U, &chunk[6]);
    chunk[8] = record.package[1];
    zassert_equal(K380_DYNAMIC_RESULT_UPLOAD_OFFSET_MISMATCH,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 10U, chunk,
                           9U, data, sizeof(data), &data_len));

    fake_uptime_ms = 5000U;
    zassert_equal(K380_DYNAMIC_RESULT_UPLOAD_EXPIRED,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 11U, chunk,
                           9U, data, sizeof(data), &data_len));
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_ABORT_MACRO_UPLOAD, 12U,
                           (uint8_t[]){session_id, 0U, 0U, 0U}, 4U, data,
                           sizeof(data), &data_len));
}

ZTEST(dynamic_protocol, test_crc_and_vm_failures_preserve_previous_saved_record)
{
    struct k380_dynamic_macro_record old_record;
    struct k380_dynamic_macro_record record;
    uint8_t begin[64];
    uint8_t data[32];
    size_t data_len;
    uint8_t chunk[32];
    uint8_t commit[4];

    reset_test_state();
    make_record(&old_record, 17U, false);
    stored_records[0U][1U] = old_record;
    stored_record_valid[0U][1U] = true;
    make_record(&record, 17U, false);
    record.package_crc32 ^= 1U;
    const size_t begin_len = make_begin_payload(begin, 0U, 0U, 1U, &record);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_BEGIN_MACRO_UPLOAD, 13U, begin,
                           begin_len, data, sizeof(data), &data_len));
    const uint32_t session_id = sys_get_le32(data);
    sys_put_le32(session_id, chunk);
    sys_put_le16(0U, &chunk[4]);
    sys_put_le16(record.package_len, &chunk[6]);
    memcpy(&chunk[8], record.package, record.package_len);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 14U, chunk,
                           8U + record.package_len, data, sizeof(data),
                           &data_len));

    sys_put_le32(session_id, commit);
    zassert_equal(K380_DYNAMIC_RESULT_CRC_MISMATCH,
                  transact(K380_DYNAMIC_COMMAND_COMMIT_MACRO_UPLOAD, 15U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
    zassert_equal(0U, store_save_calls);
    zassert_mem_equal(stored_records[0U][1U].package, old_record.package,
                      sizeof(old_record.package));
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_ABORT_MACRO_UPLOAD, 16U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));

    make_record(&record, 17U, false);
    record.package[16U] = K380_MACRO_VM_OP_PRESS;
    record.package_crc32 = package_crc(record.package, record.package_len);
    sys_put_le32(record.package_crc32, &record.package[12]);
    const size_t invalid_begin_len =
        make_begin_payload(begin, 0U, 0U, 1U, &record);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_BEGIN_MACRO_UPLOAD, 17U,
                           begin, invalid_begin_len, data, sizeof(data),
                           &data_len));
    const uint32_t invalid_session_id = sys_get_le32(data);
    sys_put_le32(invalid_session_id, chunk);
    sys_put_le16(0U, &chunk[4]);
    sys_put_le16(record.package_len, &chunk[6]);
    memcpy(&chunk[8], record.package, record.package_len);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 18U, chunk,
                           8U + record.package_len, data, sizeof(data),
                           &data_len));
    sys_put_le32(invalid_session_id, commit);
    zassert_equal(K380_DYNAMIC_RESULT_INVALID_VM_PACKAGE,
                  transact(K380_DYNAMIC_COMMAND_COMMIT_MACRO_UPLOAD, 19U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
    zassert_equal(0U, store_save_calls);
    zassert_mem_equal(stored_records[0U][1U].package, old_record.package,
                      sizeof(old_record.package));
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_ABORT_MACRO_UPLOAD, 24U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
}

ZTEST(dynamic_protocol, test_save_failure_keeps_previous_record)
{
    struct k380_dynamic_macro_record old_record;
    struct k380_dynamic_macro_record record;
    uint8_t begin[64];
    uint8_t data[32];
    size_t data_len;
    uint8_t chunk[32];
    uint8_t commit[4];

    reset_test_state();
    make_record(&old_record, 17U, false);
    stored_records[0U][5U] = old_record;
    stored_record_valid[0U][5U] = true;
    make_record(&record, 17U, false);
    record.name[0] = 'n';
    const size_t begin_len = make_begin_payload(begin, 0U, 0U, 5U, &record);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_BEGIN_MACRO_UPLOAD, 20U, begin,
                           begin_len, data, sizeof(data), &data_len));
    const uint32_t session_id = sys_get_le32(data);
    sys_put_le32(session_id, chunk);
    sys_put_le16(0U, &chunk[4]);
    sys_put_le16(record.package_len, &chunk[6]);
    memcpy(&chunk[8], record.package, record.package_len);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 21U, chunk,
                           8U + record.package_len, data, sizeof(data),
                           &data_len));
    store_save_result = -EIO;
    sys_put_le32(session_id, commit);
    zassert_equal(K380_DYNAMIC_RESULT_SAVE_FAILURE,
                  transact(K380_DYNAMIC_COMMAND_COMMIT_MACRO_UPLOAD, 22U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
    zassert_equal(1U, store_save_calls);
    zassert_mem_equal(stored_records[0U][5U].name, old_record.name,
                      sizeof(old_record.name));
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_ABORT_MACRO_UPLOAD, 23U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
}

ZTEST(dynamic_protocol, test_test_upload_is_unsaved_and_busy_test_is_rejected)
{
    struct k380_dynamic_macro_record record;
    uint8_t begin[64];
    uint8_t data[32];
    size_t data_len;
    uint8_t chunk[32];
    uint8_t commit[4];

    reset_test_state();
    make_record(&record, 17U, false);
    const size_t begin_len = make_begin_payload(begin, 1U, 2U, 3U, &record);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_BEGIN_MACRO_UPLOAD, 16U, begin,
                           begin_len, data, sizeof(data), &data_len));
    const uint32_t session_id = sys_get_le32(data);
    sys_put_le32(session_id, chunk);
    sys_put_le16(0U, &chunk[4]);
    sys_put_le16(record.package_len, &chunk[6]);
    memcpy(&chunk[8], record.package, record.package_len);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 17U, chunk,
                           8U + record.package_len, data, sizeof(data),
                           &data_len));
    sys_put_le32(session_id, commit);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_COMMIT_MACRO_UPLOAD, 18U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
    zassert_equal(0U, store_save_calls);
    zassert_mem_equal(last_test_record.package, record.package,
                      sizeof(record.package));

    struct k380_dynamic_macro_record second;
    make_record(&second, 17U, false);
    const size_t second_len = make_begin_payload(begin, 1U, 2U, 4U, &second);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_BEGIN_MACRO_UPLOAD, 19U, begin,
                           second_len, data, sizeof(data), &data_len));
    const uint32_t second_session_id = sys_get_le32(data);
    sys_put_le32(second_session_id, chunk);
    sys_put_le16(0U, &chunk[4]);
    sys_put_le16(second.package_len, &chunk[6]);
    memcpy(&chunk[8], second.package, second.package_len);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK, 20U, chunk,
                           8U + second.package_len, data, sizeof(data),
                           &data_len));
    sys_put_le32(second_session_id, commit);
    zassert_equal(K380_DYNAMIC_RESULT_BUSY,
                  transact(K380_DYNAMIC_COMMAND_COMMIT_MACRO_UPLOAD, 21U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_ABORT_MACRO_UPLOAD, 22U,
                           commit, sizeof(commit), data, sizeof(data),
                           &data_len));
}

ZTEST(dynamic_protocol, test_run_state_paginates_trace_and_stop_checks_run_id)
{
    uint8_t request[12];
    uint8_t data[501];
    size_t data_len;

    reset_test_state();
    fake_run_state.run_id = 0xAABBCCDDU;
    fake_run_state.state = K380_DYNAMIC_MACRO_RUN_COMPLETED;
    fake_run_state.next_cursor = 2U;
    fake_run_state.dropped = 7U;
    fake_trace_event_count = 2U;
    fake_trace_events[0] = (struct k380_dynamic_macro_trace_event){
        .sequence = 1U, .pc = 4U, .event = K380_MACRO_VM_TRACE_PRESS,
        .result = 0U, .value = 4U, .elapsed_ms = 237U};
    fake_trace_events[1] = (struct k380_dynamic_macro_trace_event){
        .sequence = 2U, .pc = 8U, .event = K380_MACRO_VM_TRACE_RANDOM_WAIT,
        .result = 0U, .value = 241U, .elapsed_ms = 478U};
    sys_put_le32(fake_run_state.run_id, &request[0]);
    sys_put_le32(0U, &request[4]);
    request[8] = 1U;
    memset(&request[9], 0, 3U);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_GET_MACRO_RUN_STATE, 20U,
                           request, sizeof(request), data, sizeof(data),
                           &data_len));
    zassert_equal(36U, data_len);
    zassert_equal(fake_run_state.run_id, sys_get_le32(data));
    zassert_equal(1U, data[16]);
    zassert_equal(1U, sys_get_le32(&data[20]));
    zassert_equal(4U, sys_get_le16(&data[24]));
    zassert_equal(K380_MACRO_VM_TRACE_PRESS, data[26]);
    zassert_equal(4U, sys_get_le32(&data[28]));
    zassert_equal(237U, sys_get_le32(&data[32]));
    zassert_equal(7U, sys_get_le32(&data[12]));

    sys_put_le32(0xDEADBEEFU, &request[0]);
    zassert_equal(K380_DYNAMIC_RESULT_STALE_RUN,
                  transact(K380_DYNAMIC_COMMAND_GET_MACRO_RUN_STATE, 21U,
                           request, sizeof(request), data, sizeof(data),
                           &data_len));

    sys_put_le32(fake_run_state.run_id, &request[0]);
    sys_put_le32(3U, &request[4]);
    zassert_equal(K380_DYNAMIC_RESULT_TRACE_CURSOR_LOST,
                  transact(K380_DYNAMIC_COMMAND_GET_MACRO_RUN_STATE, 22U,
                           request, sizeof(request), data, sizeof(data),
                           &data_len));

    sys_put_le32(0U, &request[0]);
    zassert_equal(K380_DYNAMIC_RESULT_READY,
                  transact(K380_DYNAMIC_COMMAND_STOP_MACRO, 23U, request, 4U,
                           data, sizeof(data), &data_len));
    zassert_equal(1U, stop_calls);
}

ZTEST_SUITE(dynamic_protocol, NULL, NULL, NULL, NULL, NULL);
