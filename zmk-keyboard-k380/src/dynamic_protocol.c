#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_macro.h>
#include <zmk_keyboard_k380/dynamic_macro_store.h>
#include <zmk_keyboard_k380/dynamic_protocol.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

extern int k380_dynamic_set_active_preset(uint8_t preset) __attribute__((weak));

#define PRESET_BINDINGS_WIRE_SIZE (K380_DYNAMIC_LAYER_COUNT * K380_DYNAMIC_KEY_COUNT * 3U)
static uint16_t get_le16(const uint8_t *value) { return sys_get_le16(value); }

static void put_le16(uint16_t value, uint8_t *out) { sys_put_le16(value, out); }

static struct k380_dynamic_config protocol_config;
static uint8_t protocol_payload[K380_DYNAMIC_MAX_PAYLOAD - 1U];

enum k380_dynamic_macro_upload_operation {
    K380_DYNAMIC_MACRO_UPLOAD_SAVE = 0,
    K380_DYNAMIC_MACRO_UPLOAD_TEST = 1,
};

struct k380_dynamic_macro_upload_session {
    bool active;
    bool expired;
    uint8_t operation;
    uint8_t preset;
    uint8_t slot;
    uint16_t next_offset;
    uint32_t session_id;
    uint64_t last_activity_ms;
    struct k380_dynamic_macro_record record;
};

static struct k380_dynamic_macro_upload_session upload_session;
static uint32_t next_upload_session_id = 1U;

BUILD_ASSERT(K380_DYNAMIC_MAX_PAYLOAD == 512U,
             "dynamic protocol payload size changed");
BUILD_ASSERT(sizeof(protocol_payload) == K380_DYNAMIC_MAX_PAYLOAD - 1U,
             "dynamic protocol response buffer must include one result byte");
BUILD_ASSERT(sizeof(upload_session.record) ==
                 K380_DYNAMIC_MACRO_RECORD_MAX_BYTES,
             "dynamic protocol must have one maximum staging record");

bool __attribute__((weak)) k380_dynamic_protocol_is_unlocked(void)
{
    return true;
}

uint64_t __attribute__((weak)) k380_dynamic_protocol_uptime_ms(void)
{
    return (uint64_t)k_uptime_get();
}

void k380_dynamic_protocol_parser_init(struct k380_dynamic_protocol_parser *parser)
{
    if (parser != NULL) {
        memset(parser, 0, sizeof(*parser));
    }
}

static enum k380_dynamic_result make_response(uint8_t command, uint16_t sequence,
                                               enum k380_dynamic_result result,
                                               const uint8_t *payload, size_t payload_len,
                                               uint8_t *response, size_t response_capacity,
                                               size_t *response_len)
{
    const size_t encoded_payload_len = payload_len + 1U;
    const size_t frame_len = K380_DYNAMIC_PROTOCOL_FRAME_SIZE(encoded_payload_len);

    if (response_len == NULL) {
        return K380_DYNAMIC_RESULT_IO_FAILURE;
    }
    *response_len = 0;
    if (encoded_payload_len > K380_DYNAMIC_MAX_PAYLOAD || response == NULL ||
        response_capacity < frame_len) {
        return K380_DYNAMIC_RESULT_IO_FAILURE;
    }

    memcpy(response, K380_DYNAMIC_PROTOCOL_MAGIC, K380_DYNAMIC_PROTOCOL_MAGIC_SIZE);
    response[8] = K380_DYNAMIC_PROTOCOL_VERSION;
    response[9] = command | K380_DYNAMIC_COMMAND_RESPONSE_FLAG;
    put_le16(sequence, &response[10]);
    put_le16(encoded_payload_len, &response[12]);
    response[K380_DYNAMIC_PROTOCOL_HEADER_SIZE] = result;
    if (payload_len > 0U) {
        memcpy(&response[K380_DYNAMIC_PROTOCOL_HEADER_SIZE + 1U], payload, payload_len);
    }
    sys_put_le32(crc32_ieee(response, K380_DYNAMIC_PROTOCOL_HEADER_SIZE + encoded_payload_len),
                 &response[K380_DYNAMIC_PROTOCOL_HEADER_SIZE + encoded_payload_len]);
    *response_len = frame_len;
    return result;
}

static enum k380_dynamic_result result_from_error(int err, bool saving)
{
    if (err == 0) {
        return K380_DYNAMIC_RESULT_READY;
    }
    if (err == -EINVAL || err == -EMSGSIZE) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    if (err == -EBUSY) {
        return K380_DYNAMIC_RESULT_BUSY;
    }
    return saving ? K380_DYNAMIC_RESULT_SAVE_FAILURE : K380_DYNAMIC_RESULT_IO_FAILURE;
}

static enum k380_dynamic_result encode_bindings(const struct k380_dynamic_preset *preset,
                                                 uint8_t *out, size_t *out_len)
{
    size_t offset = 0;
    for (uint8_t layer = 0; layer < K380_DYNAMIC_LAYER_COUNT; layer++) {
        for (uint8_t key = 0; key < K380_DYNAMIC_KEY_COUNT; key++) {
            const struct k380_dynamic_binding *binding = &preset->bindings[layer][key];
            out[offset++] = binding->type;
            put_le16(binding->value.key_usage, &out[offset]);
            offset += 2U;
        }
    }
    *out_len = offset;
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result decode_bindings(struct k380_dynamic_preset *preset,
                                                 const uint8_t *data, size_t data_len)
{
    if (data_len != PRESET_BINDINGS_WIRE_SIZE) {
        return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
    }
    size_t offset = 0;
    for (uint8_t layer = 0; layer < K380_DYNAMIC_LAYER_COUNT; layer++) {
        for (uint8_t key = 0; key < K380_DYNAMIC_KEY_COUNT; key++) {
            struct k380_dynamic_binding *binding = &preset->bindings[layer][key];
            binding->type = data[offset++];
            binding->reserved = 0;
            binding->value.key_usage = get_le16(&data[offset]);
            offset += 2U;
            if (binding->type == K380_DYNAMIC_BINDING_KEY &&
                (binding->value.key_usage < 0x04U || binding->value.key_usage > 0xE7U)) {
                return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
            }
        }
    }
    return K380_DYNAMIC_RESULT_READY;
}

static uint32_t package_crc32_without_field(const uint8_t *package,
                                            size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t index = 0U; index < length; index++) {
        const uint8_t value = index >= 12U && index < 16U ? 0U : package[index];
        crc ^= value;
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

static bool upload_is_expired(void)
{
    if (!upload_session.active) {
        return false;
    }
    if (upload_session.expired ||
        k380_dynamic_protocol_uptime_ms() - upload_session.last_activity_ms >=
            5000U) {
        upload_session.expired = true;
        return true;
    }
    return false;
}

static enum k380_dynamic_result upload_require(uint32_t session_id)
{
    if (!upload_session.active) {
        return K380_DYNAMIC_RESULT_UPLOAD_NOT_FOUND;
    }
    if (upload_is_expired()) {
        return K380_DYNAMIC_RESULT_UPLOAD_EXPIRED;
    }
    if (upload_session.session_id != session_id) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    return K380_DYNAMIC_RESULT_READY;
}

static void upload_clear(void)
{
    memset(&upload_session, 0, sizeof(upload_session));
}

static enum k380_dynamic_result validate_upload_record(void)
{
    const struct k380_dynamic_macro_record *record = &upload_session.record;

    if (record->package_len == 0U) {
        return record->package_crc32 == 0U ? K380_DYNAMIC_RESULT_READY
                                           : K380_DYNAMIC_RESULT_CRC_MISMATCH;
    }
    if (record->package_crc32 !=
        package_crc32_without_field(record->package, record->package_len)) {
        return K380_DYNAMIC_RESULT_CRC_MISMATCH;
    }
    return k380_dynamic_macro_record_validate(record) == 0
               ? K380_DYNAMIC_RESULT_READY
               : K380_DYNAMIC_RESULT_INVALID_VM_PACKAGE;
}

static enum k380_dynamic_result handle_get_macro_meta(
    const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len)
{
    if (payload_len != 2U || payload[0] >= K380_DYNAMIC_PRESET_COUNT ||
        payload[1] >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    if (upload_session.active) {
        return K380_DYNAMIC_RESULT_BUSY;
    }

    const int err = k380_dynamic_macro_store_load(payload[0], payload[1],
                                                  &upload_session.record);
    if (err != 0) {
        return result_from_error(err, false);
    }
    out[0] = payload[0];
    out[1] = payload[1];
    out[2] = upload_session.record.record_version;
    out[3] = upload_session.record.trigger;
    out[4] = upload_session.record.name_len;
    out[5] = 0U;
    sys_put_le32(upload_session.record.repeat_count, &out[6]);
    put_le16(upload_session.record.package_len, &out[10]);
    sys_put_le32(upload_session.record.package_crc32, &out[12]);
    memcpy(&out[16], upload_session.record.name,
           K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
    *out_len = 48U;
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result handle_read_macro_chunk(
    const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len)
{
    if (payload_len != 6U || payload[0] >= K380_DYNAMIC_PRESET_COUNT ||
        payload[1] >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    const uint16_t offset = get_le16(&payload[2]);
    const uint16_t max_len = get_le16(&payload[4]);
    if (max_len == 0U || max_len > K380_DYNAMIC_MACRO_CHUNK_MAX) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    if (upload_session.active) {
        return K380_DYNAMIC_RESULT_BUSY;
    }

    int err = k380_dynamic_macro_store_load(payload[0], payload[1],
                                            &upload_session.record);
    if (err != 0) {
        return result_from_error(err, false);
    }
    if (offset > upload_session.record.package_len) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    const uint16_t remaining = upload_session.record.package_len - offset;
    const uint16_t data_len = MIN(max_len, remaining);
    out[0] = payload[0];
    out[1] = payload[1];
    put_le16(offset, &out[2]);
    put_le16(data_len, &out[4]);
    if (data_len > 0U) {
        memcpy(&out[6], &upload_session.record.package[offset], data_len);
    }
    *out_len = 6U + data_len;
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result handle_begin_macro_upload(
    const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len)
{
    if (payload_len != 50U ||
        (payload[0] != K380_DYNAMIC_MACRO_UPLOAD_SAVE &&
         payload[0] != K380_DYNAMIC_MACRO_UPLOAD_TEST) ||
        payload[1] >= K380_DYNAMIC_PRESET_COUNT ||
        payload[2] >= K380_DYNAMIC_MACRO_SLOT_COUNT ||
        payload[3] > K380_DYNAMIC_MACRO_TRIGGER_COUNT || payload[5] != 0U ||
        payload[4] > K380_DYNAMIC_MACRO_NAME_MAX_BYTES ||
        get_le16(&payload[10]) > K380_DYNAMIC_MACRO_PACKAGE_MAX_BYTES ||
        get_le16(&payload[12]) != 0U) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    const uint32_t repeat_count = sys_get_le32(&payload[6]);
    if ((payload[3] == K380_DYNAMIC_MACRO_TRIGGER_COUNT &&
         repeat_count == 0U) ||
        (payload[3] != K380_DYNAMIC_MACRO_TRIGGER_COUNT && repeat_count != 1U)) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    for (uint8_t index = payload[4];
         index < K380_DYNAMIC_MACRO_NAME_MAX_BYTES; index++) {
        if (payload[18U + index] != 0U) {
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        }
    }
    if (upload_session.active && !upload_is_expired()) {
        return K380_DYNAMIC_RESULT_BUSY;
    }

    upload_clear();
    upload_session.active = true;
    upload_session.operation = payload[0];
    upload_session.preset = payload[1];
    upload_session.slot = payload[2];
    upload_session.session_id = next_upload_session_id;
    next_upload_session_id++;
    if (next_upload_session_id == 0U) {
        next_upload_session_id = 1U;
    }
    upload_session.last_activity_ms = k380_dynamic_protocol_uptime_ms();
    upload_session.record.record_version = K380_DYNAMIC_MACRO_RECORD_VERSION;
    upload_session.record.trigger = payload[3];
    upload_session.record.name_len = payload[4];
    upload_session.record.repeat_count = repeat_count;
    upload_session.record.package_len = get_le16(&payload[10]);
    upload_session.record.package_crc32 = sys_get_le32(&payload[14]);
    memcpy(upload_session.record.name, &payload[18],
           K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
    sys_put_le32(upload_session.record.package_crc32,
                 &upload_session.record.package[12]);

    sys_put_le32(upload_session.session_id, &out[0]);
    put_le16(K380_DYNAMIC_MACRO_CHUNK_MAX, &out[4]);
    *out_len = 6U;
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result handle_write_macro_chunk(
    const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len)
{
    if (payload_len < 9U) {
        return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
    }
    const uint32_t session_id = sys_get_le32(&payload[0]);
    enum k380_dynamic_result result = upload_require(session_id);
    if (result != K380_DYNAMIC_RESULT_READY) {
        return result;
    }
    const uint16_t offset = get_le16(&payload[4]);
    const uint16_t data_len = get_le16(&payload[6]);
    if (data_len == 0U || data_len > K380_DYNAMIC_MACRO_CHUNK_MAX ||
        payload_len != (size_t)8U + data_len) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    if (offset != upload_session.next_offset) {
        return K380_DYNAMIC_RESULT_UPLOAD_OFFSET_MISMATCH;
    }
    if ((uint32_t)offset + data_len > upload_session.record.package_len) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    memcpy(&upload_session.record.package[offset], &payload[8], data_len);
    upload_session.next_offset = (uint16_t)(offset + data_len);
    upload_session.last_activity_ms = k380_dynamic_protocol_uptime_ms();
    sys_put_le32(session_id, &out[0]);
    put_le16(upload_session.next_offset, &out[4]);
    *out_len = 6U;
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result handle_commit_macro_upload(
    const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len)
{
    if (payload_len != 4U) {
        return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
    }
    const uint32_t session_id = sys_get_le32(payload);
    enum k380_dynamic_result result = upload_require(session_id);
    if (result != K380_DYNAMIC_RESULT_READY) {
        return result;
    }
    if (upload_session.next_offset != upload_session.record.package_len) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    result = validate_upload_record();
    if (result != K380_DYNAMIC_RESULT_READY) {
        return result;
    }

    uint32_t run_id = 0U;
    if (upload_session.operation == K380_DYNAMIC_MACRO_UPLOAD_SAVE) {
        result = result_from_error(
            k380_dynamic_macro_store_save(upload_session.preset,
                                          upload_session.slot,
                                          &upload_session.record),
            true);
    } else {
        if (k380_dynamic_macro_is_running()) {
            return K380_DYNAMIC_RESULT_BUSY;
        }
        result = result_from_error(
            k380_dynamic_macro_test_record(upload_session.preset,
                                           upload_session.slot,
                                           &upload_session.record),
            false);
        if (result == K380_DYNAMIC_RESULT_READY) {
            struct k380_dynamic_macro_run_state state;
            k380_dynamic_macro_get_run_state(&state);
            run_id = state.run_id;
            if (run_id == 0U) {
                result = K380_DYNAMIC_RESULT_IO_FAILURE;
            }
        }
    }
    if (result != K380_DYNAMIC_RESULT_READY) {
        return result;
    }
    sys_put_le32(session_id, &out[0]);
    sys_put_le32(run_id, &out[4]);
    *out_len = 8U;
    upload_clear();
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result handle_abort_macro_upload(
    const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len)
{
    if (payload_len != 4U) {
        return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
    }
    if (upload_session.active &&
        upload_session.session_id != sys_get_le32(payload)) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    upload_clear();
    *out_len = 0U;
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result handle_get_macro_run_state(
    const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len)
{
    if (payload_len != 12U || payload[9] != 0U || payload[10] != 0U ||
        payload[11] != 0U || payload[8] > 30U) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    const uint32_t requested_run_id = sys_get_le32(&payload[0]);
    const uint32_t cursor = sys_get_le32(&payload[4]);
    struct k380_dynamic_macro_run_state state;
    k380_dynamic_macro_get_run_state(&state);
    if (requested_run_id != 0U && requested_run_id != state.run_id) {
        return K380_DYNAMIC_RESULT_STALE_RUN;
    }

    struct k380_dynamic_macro_trace_event events[30];
    const int event_count = k380_dynamic_macro_trace_read(
        cursor, events, payload[8], &state);
    if (event_count < 0) {
        return event_count == -EOVERFLOW ? K380_DYNAMIC_RESULT_TRACE_CURSOR_LOST
                                         : K380_DYNAMIC_RESULT_IO_FAILURE;
    }
    if (requested_run_id != 0U && requested_run_id != state.run_id) {
        return K380_DYNAMIC_RESULT_STALE_RUN;
    }
    sys_put_le32(state.run_id, &out[0]);
    out[4] = state.state;
    out[5] = state.vm_error;
    put_le16(0U, &out[6]);
    sys_put_le32(state.next_cursor, &out[8]);
    sys_put_le32(state.dropped, &out[12]);
    out[16] = (uint8_t)event_count;
    memset(&out[17], 0, 3U);
    for (int index = 0; index < event_count; index++) {
        uint8_t *wire = &out[20U + (size_t)index * 16U];
        sys_put_le32(events[index].sequence, &wire[0]);
        put_le16(events[index].pc, &wire[4]);
        wire[6] = events[index].event;
        wire[7] = events[index].result;
        sys_put_le32(events[index].value, &wire[8]);
        sys_put_le32(events[index].elapsed_ms, &wire[12]);
    }
    *out_len = 20U + (size_t)event_count * 16U;
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result handle_stop_macro(
    const uint8_t *payload, size_t payload_len)
{
    if (payload_len != 4U) {
        return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
    }
    const uint32_t requested_run_id = sys_get_le32(payload);
    struct k380_dynamic_macro_run_state state;
    k380_dynamic_macro_get_run_state(&state);
    if (requested_run_id != 0U && requested_run_id != state.run_id) {
        return K380_DYNAMIC_RESULT_STALE_RUN;
    }
    return result_from_error(k380_dynamic_macro_stop(), false);
}

static enum k380_dynamic_result handle_command(uint8_t command, const uint8_t *payload,
                                                size_t payload_len, uint8_t *out, size_t *out_len)
{
    struct k380_dynamic_config *config = &protocol_config;
    int err;

    *out_len = 0;
    if (!k380_dynamic_protocol_is_unlocked()) {
        return K380_DYNAMIC_RESULT_LOCKED;
    }
    if (command == K380_DYNAMIC_COMMAND_HELLO) {
        if (payload_len != 0U) {
            return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
        }
        return K380_DYNAMIC_RESULT_READY;
    }
    if (command == K380_DYNAMIC_COMMAND_GET_INFO) {
        if (payload_len != 0U) {
            return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
        }
        out[0] = K380_DYNAMIC_PROTOCOL_VERSION;
        out[1] = K380_DYNAMIC_CONFIG_VERSION;
        out[2] = K380_DYNAMIC_PRESET_COUNT;
        out[3] = K380_DYNAMIC_LAYER_COUNT;
        out[4] = K380_DYNAMIC_KEY_COUNT;
        out[5] = K380_DYNAMIC_MACRO_SLOT_COUNT;
        out[6] = K380_DYNAMIC_MACRO_MAX_STEPS;
        put_le16(K380_MACRO_VM_MAX_PACKAGE_BYTES, &out[7]);
        put_le16(K380_MACRO_VM_MAX_CODE_BYTES, &out[9]);
        put_le16(K380_DYNAMIC_MACRO_CHUNK_MAX, &out[11]);
        out[13] = K380_MACRO_VM_MAX_TRACE_EVENTS;
        *out_len = 14U;
        return K380_DYNAMIC_RESULT_READY;
    }

    if (command == K380_DYNAMIC_COMMAND_GET_MACRO_META) {
        return handle_get_macro_meta(payload, payload_len, out, out_len);
    }
    if (command == K380_DYNAMIC_COMMAND_READ_MACRO_CHUNK) {
        return handle_read_macro_chunk(payload, payload_len, out, out_len);
    }
    if (command == K380_DYNAMIC_COMMAND_BEGIN_MACRO_UPLOAD) {
        return handle_begin_macro_upload(payload, payload_len, out, out_len);
    }
    if (command == K380_DYNAMIC_COMMAND_WRITE_MACRO_CHUNK) {
        return handle_write_macro_chunk(payload, payload_len, out, out_len);
    }
    if (command == K380_DYNAMIC_COMMAND_COMMIT_MACRO_UPLOAD) {
        return handle_commit_macro_upload(payload, payload_len, out, out_len);
    }
    if (command == K380_DYNAMIC_COMMAND_ABORT_MACRO_UPLOAD) {
        return handle_abort_macro_upload(payload, payload_len, out, out_len);
    }
    if (command == K380_DYNAMIC_COMMAND_GET_MACRO_RUN_STATE) {
        return handle_get_macro_run_state(payload, payload_len, out, out_len);
    }
    if (command == K380_DYNAMIC_COMMAND_STOP_MACRO) {
        return handle_stop_macro(payload, payload_len);
    }

    err = k380_dynamic_settings_load(config);
    if (err != 0) {
        return result_from_error(err, false);
    }
    if (command == K380_DYNAMIC_COMMAND_GET_PRESETS) {
        if (payload_len != 0U) {
            return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
        }
        out[0] = config->active_preset;
        size_t offset = 1U;
        for (uint8_t preset = 0; preset < K380_DYNAMIC_PRESET_COUNT; preset++) {
            out[offset++] = preset;
            memcpy(&out[offset], config->presets[preset].name, K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
            offset += K380_DYNAMIC_MACRO_NAME_MAX_BYTES;
        }
        *out_len = offset;
        return K380_DYNAMIC_RESULT_READY;
    }
    if (command == K380_DYNAMIC_COMMAND_GET_PRESET) {
        if (payload_len < 2U || payload[0] >= K380_DYNAMIC_PRESET_COUNT) {
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        }
        const uint8_t preset = payload[0];
        const uint8_t section = payload[1];
        out[0] = config->active_preset;
        out[1] = preset;
        out[2] = section;
        if (section == K380_DYNAMIC_PRESET_SECTION_BINDINGS && payload_len == 2U) {
            size_t section_len;
            encode_bindings(&config->presets[preset], &out[3], &section_len);
            *out_len = section_len + 3U;
            return K380_DYNAMIC_RESULT_READY;
        }
        if (section == K380_DYNAMIC_PRESET_SECTION_NAME && payload_len == 2U) {
            memcpy(&out[3], config->presets[preset].name, K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
            *out_len = K380_DYNAMIC_MACRO_NAME_MAX_BYTES + 3U;
            return K380_DYNAMIC_RESULT_READY;
        }
        return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
    }
    if (command == K380_DYNAMIC_COMMAND_SAVE_PRESET) {
        if (payload_len < 2U || payload[0] >= K380_DYNAMIC_PRESET_COUNT) {
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        }
        struct k380_dynamic_preset *preset = &config->presets[payload[0]];
        enum k380_dynamic_result result;
        if (payload[1] == K380_DYNAMIC_PRESET_SECTION_BINDINGS) {
            result = decode_bindings(preset, &payload[2], payload_len - 2U);
        } else if (payload[1] == K380_DYNAMIC_PRESET_SECTION_MACRO) {
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        } else if (payload[1] == K380_DYNAMIC_PRESET_SECTION_NAME &&
                   payload_len == K380_DYNAMIC_MACRO_NAME_MAX_BYTES + 2U) {
            memcpy(preset->name, &payload[2], K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
            result = K380_DYNAMIC_RESULT_READY;
        } else {
            return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
        }
        if (result != K380_DYNAMIC_RESULT_READY || k380_dynamic_config_validate(config) != 0) {
            return result == K380_DYNAMIC_RESULT_READY ? K380_DYNAMIC_RESULT_INVALID_ARGUMENT : result;
        }
        return result_from_error(k380_dynamic_settings_save(config), true);
    }
    if (command == K380_DYNAMIC_COMMAND_SET_ACTIVE_PRESET) {
        if (payload_len != 1U || payload[0] >= K380_DYNAMIC_PRESET_COUNT) {
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        }
        if (k380_dynamic_set_active_preset == NULL) {
            return K380_DYNAMIC_RESULT_IO_FAILURE;
        }
        return result_from_error(k380_dynamic_set_active_preset(payload[0]), true);
    }
    if (command == K380_DYNAMIC_COMMAND_RESTORE_KEY) {
        if (payload_len != 3U || payload[0] >= K380_DYNAMIC_PRESET_COUNT ||
            payload[1] >= K380_DYNAMIC_LAYER_COUNT || payload[2] >= K380_DYNAMIC_KEY_COUNT) {
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        }
        k380_dynamic_config_restore_key(config, payload[0], payload[1], payload[2]);
        return result_from_error(k380_dynamic_settings_save(config), true);
    }
    if (command == K380_DYNAMIC_COMMAND_RESTORE_PRESET) {
        if (payload_len != 1U || payload[0] >= K380_DYNAMIC_PRESET_COUNT) {
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        }
        k380_dynamic_config_restore_preset(config, payload[0]);
        return result_from_error(k380_dynamic_settings_save(config), true);
    }
    if (command == K380_DYNAMIC_COMMAND_RESTORE_ALL) {
        if (payload_len != 0U) {
            return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
        }
        return result_from_error(k380_dynamic_settings_restore_all(), true);
    }
    if (command == K380_DYNAMIC_COMMAND_TEST_MACRO) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
}

enum k380_dynamic_result k380_dynamic_protocol_feed(
    struct k380_dynamic_protocol_parser *parser, const uint8_t *data, size_t data_len,
    uint8_t *response, size_t response_capacity, size_t *response_len, size_t *consumed_len)
{
    uint8_t command = 0;
    uint16_t sequence = 0;

    if (parser == NULL || (data_len > 0U && data == NULL)) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    if (response_len != NULL) {
        *response_len = 0;
    }
    if (consumed_len != NULL) {
        *consumed_len = 0;
    }
    for (size_t input_offset = 0; input_offset < data_len; input_offset++) {
        if (parser->frame_len == sizeof(parser->frame)) {
            k380_dynamic_protocol_parser_init(parser);
            if (consumed_len != NULL) {
                *consumed_len = input_offset + 1U;
            }
            return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
        }
        parser->frame[parser->frame_len++] = data[input_offset];
        if (parser->frame_len == K380_DYNAMIC_PROTOCOL_HEADER_SIZE) {
            command = parser->frame[9];
            sequence = get_le16(&parser->frame[10]);
            const uint16_t payload_len = get_le16(&parser->frame[12]);
            if (payload_len > K380_DYNAMIC_MAX_PAYLOAD) {
                k380_dynamic_protocol_parser_init(parser);
                if (consumed_len != NULL) {
                    *consumed_len = input_offset + 1U;
                }
                return make_response(command, sequence, K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH,
                                     NULL, 0, response, response_capacity, response_len);
            }
            parser->expected_len = K380_DYNAMIC_PROTOCOL_FRAME_SIZE(payload_len);
        }
        if (parser->expected_len != 0U && parser->frame_len == parser->expected_len) {
            command = parser->frame[9];
            sequence = get_le16(&parser->frame[10]);
            const uint16_t payload_len = get_le16(&parser->frame[12]);
            enum k380_dynamic_result result;
            if (memcmp(parser->frame, K380_DYNAMIC_PROTOCOL_MAGIC,
                       K380_DYNAMIC_PROTOCOL_MAGIC_SIZE) != 0) {
                result = K380_DYNAMIC_RESULT_NOT_K380;
            } else if (parser->frame[8] != K380_DYNAMIC_PROTOCOL_VERSION) {
                result = K380_DYNAMIC_RESULT_VERSION_MISMATCH;
            } else if (crc32_ieee(parser->frame, K380_DYNAMIC_PROTOCOL_HEADER_SIZE + payload_len) !=
                       sys_get_le32(&parser->frame[K380_DYNAMIC_PROTOCOL_HEADER_SIZE + payload_len])) {
                result = K380_DYNAMIC_RESULT_CRC_MISMATCH;
            } else {
                size_t payload_out_len = 0;
                result = handle_command(command,
                                        &parser->frame[K380_DYNAMIC_PROTOCOL_HEADER_SIZE], payload_len,
                                        protocol_payload, &payload_out_len);
                enum k380_dynamic_result response_result = make_response(
                    command, sequence, result, protocol_payload, payload_out_len, response, response_capacity,
                    response_len);
                k380_dynamic_protocol_parser_init(parser);
                if (consumed_len != NULL) {
                    *consumed_len = input_offset + 1U;
                }
                return response_result;
            }
            k380_dynamic_protocol_parser_init(parser);
            if (consumed_len != NULL) {
                *consumed_len = input_offset + 1U;
            }
            return make_response(command, sequence, result, NULL, 0, response, response_capacity,
                                 response_len);
        }
    }
    return K380_DYNAMIC_RESULT_NEED_MORE;
}
