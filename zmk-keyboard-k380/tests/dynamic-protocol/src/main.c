#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/dynamic_protocol.h>
#include <zmk_keyboard_k380/dynamic_settings.h>
#include <zmk_keyboard_k380/dynamic_macro.h>

static bool unlocked = true;
static struct k380_dynamic_config config;

bool k380_dynamic_protocol_is_unlocked(void) { return unlocked; }

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

int k380_dynamic_macro_test(uint8_t slot) { return slot < K380_DYNAMIC_MACRO_SLOT_COUNT ? 0 : -1; }

bool k380_dynamic_macro_is_running(void) { return false; }

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

ZTEST_SUITE(dynamic_protocol, NULL, NULL, NULL, NULL, NULL);
