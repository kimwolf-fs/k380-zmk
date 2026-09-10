#include <errno.h>
#include <string.h>

#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zmk_keyboard_k380/dynamic_config.h>
#include <zmk_keyboard_k380/dynamic_macro.h>
#include <zmk_keyboard_k380/dynamic_protocol.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

extern int k380_dynamic_set_active_preset(uint8_t preset) __attribute__((weak));

#define PRESET_BINDINGS_WIRE_SIZE (K380_DYNAMIC_LAYER_COUNT * K380_DYNAMIC_KEY_COUNT * 3U)
static uint16_t get_le16(const uint8_t *value) { return sys_get_le16(value); }

static void put_le16(uint16_t value, uint8_t *out) { sys_put_le16(value, out); }

static struct k380_dynamic_config protocol_config;
static uint8_t protocol_payload[K380_DYNAMIC_MAX_PAYLOAD - 1U];
static struct k380_dynamic_macro decoded_macro;

bool __attribute__((weak)) k380_dynamic_protocol_is_unlocked(void)
{
    return true;
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

static enum k380_dynamic_result encode_macro(const struct k380_dynamic_macro *macro,
                                              uint8_t *out, size_t *out_len)
{
    size_t offset = 0;
    memcpy(&out[offset], macro->name, K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
    offset += K380_DYNAMIC_MACRO_NAME_MAX_BYTES;
    out[offset++] = macro->step_count;
    out[offset++] = macro->trigger;
    put_le16(macro->count, &out[offset]);
    offset += 2U;
    for (uint8_t step = 0; step < macro->step_count; step++) {
        const struct k380_dynamic_macro_step *source = &macro->steps[step];
        out[offset++] = source->type;
        switch (source->type) {
        case K380_DYNAMIC_MACRO_WAIT_RANDOM:
            put_le16(source->value.wait_random.min_ms, &out[offset]);
            put_le16(source->value.wait_random.max_ms, &out[offset + 2U]);
            offset += 4U;
            break;
        case K380_DYNAMIC_MACRO_RELEASE_ALL:
            break;
        default:
            put_le16(source->value.key_usage, &out[offset]);
            offset += 2U;
            break;
        }
    }
    *out_len = offset;
    return K380_DYNAMIC_RESULT_READY;
}

static enum k380_dynamic_result decode_macro(struct k380_dynamic_macro *macro,
                                              const uint8_t *data, size_t data_len)
{
    if (data_len < K380_DYNAMIC_MACRO_NAME_MAX_BYTES + 4U) {
        return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
    }
    size_t offset = 0;
    struct k380_dynamic_macro *decoded = &decoded_macro;
    memset(decoded, 0, sizeof(*decoded));
    memcpy(decoded->name, &data[offset], K380_DYNAMIC_MACRO_NAME_MAX_BYTES);
    offset += K380_DYNAMIC_MACRO_NAME_MAX_BYTES;
    decoded->step_count = data[offset++];
    decoded->trigger = data[offset++];
    decoded->count = get_le16(&data[offset]);
    offset += 2U;
    if (decoded->step_count > K380_DYNAMIC_MACRO_MAX_STEPS) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    for (uint8_t step = 0; step < decoded->step_count; step++) {
        if (offset >= data_len) {
            return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
        }
        struct k380_dynamic_macro_step *current = &decoded->steps[step];
        current->type = data[offset++];
        switch (current->type) {
        case K380_DYNAMIC_MACRO_PRESS_KEY:
        case K380_DYNAMIC_MACRO_RELEASE_KEY:
        case K380_DYNAMIC_MACRO_TAP_KEY:
            if (data_len - offset < 2U) {
                return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
            }
            current->value.key_usage = get_le16(&data[offset]);
            offset += 2U;
            if (current->value.key_usage < 0x04U ||
                current->value.key_usage > 0xE7U) {
                return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
            }
            break;
        case K380_DYNAMIC_MACRO_WAIT_MS:
            if (data_len - offset < 2U) {
                return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
            }
            current->value.wait_ms = get_le16(&data[offset]);
            offset += 2U;
            if (current->value.wait_ms < K380_DYNAMIC_WAIT_MIN_MS ||
                current->value.wait_ms > K380_DYNAMIC_WAIT_MAX_MS) {
                return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
            }
            break;
        case K380_DYNAMIC_MACRO_WAIT_RANDOM:
            if (data_len - offset < 4U) {
                return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
            }
            current->value.wait_random.min_ms = get_le16(&data[offset]);
            current->value.wait_random.max_ms = get_le16(&data[offset + 2U]);
            offset += 4U;
            if (current->value.wait_random.min_ms < K380_DYNAMIC_WAIT_MIN_MS ||
                current->value.wait_random.min_ms > K380_DYNAMIC_WAIT_MAX_MS ||
                current->value.wait_random.max_ms < K380_DYNAMIC_WAIT_MIN_MS ||
                current->value.wait_random.max_ms > K380_DYNAMIC_WAIT_MAX_MS ||
                current->value.wait_random.min_ms > current->value.wait_random.max_ms) {
                return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
            }
            break;
        case K380_DYNAMIC_MACRO_RELEASE_ALL:
            break;
        default:
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        }
    }
    if (offset != data_len) {
        return K380_DYNAMIC_RESULT_INVALID_PAYLOAD_LENGTH;
    }
    if (decoded->trigger > K380_DYNAMIC_MACRO_TRIGGER_COUNT ||
        (decoded->trigger == K380_DYNAMIC_MACRO_TRIGGER_COUNT && decoded->count == 0U)) {
        return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
    }
    *macro = *decoded;
    return K380_DYNAMIC_RESULT_READY;
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
        *out_len = 7U;
        return K380_DYNAMIC_RESULT_READY;
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
        if (section == K380_DYNAMIC_PRESET_SECTION_MACRO && payload_len == 3U &&
            payload[2] < K380_DYNAMIC_MACRO_SLOT_COUNT) {
            out[3] = payload[2];
            size_t section_len;
            encode_macro(&config->presets[preset].macros[payload[2]], &out[4], &section_len);
            *out_len = section_len + 4U;
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
        } else if (payload[1] == K380_DYNAMIC_PRESET_SECTION_MACRO && payload_len >= 3U &&
                   payload[2] < K380_DYNAMIC_MACRO_SLOT_COUNT) {
            result = decode_macro(&preset->macros[payload[2]], &payload[3], payload_len - 3U);
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
        if (payload_len < 1U || payload[0] >= K380_DYNAMIC_MACRO_SLOT_COUNT) {
            return K380_DYNAMIC_RESULT_INVALID_ARGUMENT;
        }
        if (k380_dynamic_macro_is_running()) {
            return K380_DYNAMIC_RESULT_BUSY;
        }
        if (payload_len > 1U) {
            enum k380_dynamic_result result = decode_macro(&decoded_macro, &payload[1],
                                                           payload_len - 1U);
            if (result != K380_DYNAMIC_RESULT_READY) {
                return result;
            }
            return result_from_error(k380_dynamic_macro_test_temporary(
                                         config->active_preset, payload[0], &decoded_macro),
                                     false);
        }
        return result_from_error(k380_dynamic_macro_test(payload[0]), false);
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
