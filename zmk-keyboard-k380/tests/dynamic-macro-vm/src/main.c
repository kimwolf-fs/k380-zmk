#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/dynamic_macro_vm.h>

struct fake_trace {
    uint16_t pc;
    uint8_t event;
    uint8_t result;
    uint32_t value;
    uint32_t elapsed_ms;
};

struct fake_host {
    uint64_t now_ms;
    uint32_t random_values[8];
    size_t random_count;
    size_t random_index;
    bool stop;
    bool stop_during_wait;
    int key_error;
    int wait_error;
    int release_all_error;
    uint16_t key_usages[256];
    bool key_states[256];
    size_t key_count;
    uint32_t waits[32];
    size_t wait_count;
    size_t release_all_count;
    size_t yields;
    struct fake_trace traces[256];
    size_t trace_count;
};

static int fake_key_event(uint16_t usage, bool pressed, void *user_data)
{
    struct fake_host *fake = user_data;

    if (fake->key_error != 0) {
        return fake->key_error;
    }
    zassert_true(fake->key_count < ARRAY_SIZE(fake->key_usages));
    fake->key_usages[fake->key_count] = usage;
    fake->key_states[fake->key_count++] = pressed;
    return 0;
}

static int fake_release_all(void *user_data)
{
    struct fake_host *fake = user_data;

    fake->release_all_count++;
    return fake->release_all_error;
}

static int fake_wait_ms(uint32_t duration_ms, void *user_data)
{
    struct fake_host *fake = user_data;

    zassert_true(fake->wait_count < ARRAY_SIZE(fake->waits));
    fake->waits[fake->wait_count++] = duration_ms;
    if (fake->wait_error != 0) {
        return fake->wait_error;
    }
    if (fake->stop_during_wait) {
        fake->stop = true;
        return -ECANCELED;
    }
    fake->now_ms += duration_ms;
    return 0;
}

static uint64_t fake_now_ms(void *user_data)
{
    return ((struct fake_host *)user_data)->now_ms;
}

static uint32_t fake_random_u32(void *user_data)
{
    struct fake_host *fake = user_data;

    zassert_true(fake->random_index < fake->random_count);
    return fake->random_values[fake->random_index++];
}

static bool fake_stop_requested(void *user_data)
{
    return ((struct fake_host *)user_data)->stop;
}

static void fake_yield_cpu(void *user_data)
{
    ((struct fake_host *)user_data)->yields++;
}

static void fake_trace(uint16_t pc, uint8_t event, uint8_t result,
                       uint32_t value, uint32_t elapsed_ms, void *user_data)
{
    struct fake_host *fake = user_data;

    zassert_true(fake->trace_count < ARRAY_SIZE(fake->traces));
    fake->traces[fake->trace_count++] = (struct fake_trace){
        .pc = pc,
        .event = event,
        .result = result,
        .value = value,
        .elapsed_ms = elapsed_ms,
    };
}

static struct k380_macro_vm_host make_host(struct fake_host *fake)
{
    return (struct k380_macro_vm_host){
        .key_event = fake_key_event,
        .release_all = fake_release_all,
        .wait_ms = fake_wait_ms,
        .now_ms = fake_now_ms,
        .random_u32 = fake_random_u32,
        .stop_requested = fake_stop_requested,
        .yield_cpu = fake_yield_cpu,
        .trace = fake_trace,
        .user_data = fake,
    };
}

static struct k380_macro_vm_package_view make_view(const uint8_t *code,
                                                    uint16_t code_len)
{
    return (struct k380_macro_vm_package_view){
        .code = code,
        .code_len = code_len,
        .entry_offset = 0U,
    };
}

static enum k380_macro_vm_error run_code(const uint8_t *code, uint16_t code_len,
                                         struct fake_host *fake,
                                         struct k380_macro_vm_context *context)
{
    struct k380_macro_vm_package_view view = make_view(code, code_len);
    struct k380_macro_vm_host host = make_host(fake);

    return k380_macro_vm_run(&view, &host, context);
}

static void append_u16(uint8_t *code, size_t *offset, uint16_t value)
{
    sys_put_le16(value, &code[*offset]);
    *offset += 2U;
}

static void append_u32(uint8_t *code, size_t *offset, uint32_t value)
{
    sys_put_le32(value, &code[*offset]);
    *offset += 4U;
}

static size_t append_key(uint8_t *code, size_t *offset, uint8_t opcode,
                         uint16_t usage)
{
    const size_t instruction = *offset;
    code[(*offset)++] = opcode;
    append_u16(code, offset, usage);
    return instruction;
}

static size_t append_wait(uint8_t *code, size_t *offset, uint8_t opcode,
                          uint32_t value0, uint32_t value1)
{
    const size_t instruction = *offset;
    code[(*offset)++] = opcode;
    append_u32(code, offset, value0);
    if (opcode == K380_MACRO_VM_OP_RANDOM_WAIT) {
        append_u32(code, offset, value1);
    }
    return instruction;
}

static size_t append_condition(uint8_t *code, size_t *offset, uint8_t opcode,
                               uint8_t timer, uint32_t threshold)
{
    const size_t instruction = *offset;
    code[(*offset)++] = opcode;
    code[(*offset)++] = timer;
    append_u32(code, offset, threshold);
    append_u16(code, offset, 0U);
    return instruction;
}

static size_t append_loop_begin(uint8_t *code, size_t *offset, uint8_t opcode,
                                uint32_t limit)
{
    const size_t instruction = *offset;
    code[(*offset)++] = opcode;
    append_u32(code, offset, limit);
    append_u16(code, offset, 0U);
    return instruction;
}

static size_t append_loop_end(uint8_t *code, size_t *offset, uint16_t body)
{
    const size_t instruction = *offset;
    code[(*offset)++] = K380_MACRO_VM_OP_LOOP_END;
    append_u16(code, offset, body);
    return instruction;
}

ZTEST(dynamic_macro_vm, test_basic_instructions_and_inclusive_random_wait)
{
    const uint8_t code[] = {
        K380_MACRO_VM_OP_PRESS, 4U, 0U,
        K380_MACRO_VM_OP_WAIT, 200U, 0U, 0U, 0U,
        K380_MACRO_VM_OP_RELEASE, 4U, 0U,
        K380_MACRO_VM_OP_TAP, 5U, 0U,
        K380_MACRO_VM_OP_RELEASE_ALL,
        K380_MACRO_VM_OP_RANDOM_WAIT, 5U, 0U, 0U, 0U, 7U, 0U, 0U, 0U,
        K380_MACRO_VM_OP_END,
    };
    struct fake_host fake = {
        .random_values = { 0U, 2U },
        .random_count = 2U,
    };
    struct k380_macro_vm_context context;

    zassert_equal(K380_MACRO_VM_OK,
                  run_code(code, sizeof(code), &fake, &context));
    zassert_equal(4U, fake.key_count);
    zassert_equal(4U, fake.key_usages[0]);
    zassert_true(fake.key_states[0]);
    zassert_equal(4U, fake.key_usages[1]);
    zassert_false(fake.key_states[1]);
    zassert_equal(5U, fake.key_usages[2]);
    zassert_true(fake.key_states[2]);
    zassert_equal(5U, fake.key_usages[3]);
    zassert_false(fake.key_states[3]);
    zassert_equal(1U, fake.release_all_count);
    zassert_equal(2U, fake.wait_count);
    zassert_equal(200U, fake.waits[0]);
    zassert_equal(5U, fake.waits[1]);
    zassert_equal(K380_MACRO_VM_TRACE_PRESS, fake.traces[0].event);
    zassert_equal(K380_MACRO_VM_TRACE_RANDOM_WAIT, fake.traces[5].event);
    zassert_equal(5U, fake.traces[5].value);
}

ZTEST(dynamic_macro_vm, test_long_wait_values_are_passed_without_truncation)
{
    const uint8_t code[] = {
        K380_MACRO_VM_OP_WAIT, 0xC0U, 0x27U, 0x09U, 0x00U,
        K380_MACRO_VM_OP_WAIT, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        K380_MACRO_VM_OP_END,
    };
    struct fake_host fake = { 0 };
    struct k380_macro_vm_context context;

    zassert_equal(K380_MACRO_VM_OK,
                  run_code(code, sizeof(code), &fake, &context));
    zassert_equal(2U, fake.wait_count);
    zassert_equal(600000U, fake.waits[0]);
    zassert_equal(UINT32_MAX, fake.waits[1]);
    zassert_equal(UINT32_MAX, fake.traces[1].elapsed_ms);
}

ZTEST(dynamic_macro_vm, test_three_timers_and_both_condition_directions)
{
    uint8_t code[64];
    size_t offset = 0U;
    struct fake_host fake = { 0 };
    struct k380_macro_vm_context context;
    size_t condition;

    code[offset++] = K380_MACRO_VM_OP_TIMER_RESET;
    code[offset++] = 1U;
    code[offset++] = K380_MACRO_VM_OP_TIMER_RESET;
    code[offset++] = 2U;
    code[offset++] = K380_MACRO_VM_OP_TIMER_RESET;
    code[offset++] = 3U;
    append_wait(code, &offset, K380_MACRO_VM_OP_WAIT, 10U, 0U);

    condition = append_condition(code, &offset, K380_MACRO_VM_OP_TIMER_GE,
                                 1U, 10U);
    append_key(code, &offset, K380_MACRO_VM_OP_TAP, 4U);
    sys_put_le16((uint16_t)offset, &code[condition + 6U]);

    condition = append_condition(code, &offset, K380_MACRO_VM_OP_TIMER_LE,
                                 2U, 5U);
    append_key(code, &offset, K380_MACRO_VM_OP_TAP, 5U);
    sys_put_le16((uint16_t)offset, &code[condition + 6U]);

    condition = append_condition(code, &offset, K380_MACRO_VM_OP_TIMER_GE,
                                 3U, 0U);
    append_key(code, &offset, K380_MACRO_VM_OP_TAP, 6U);
    sys_put_le16((uint16_t)offset, &code[condition + 6U]);

    condition = append_condition(code, &offset, K380_MACRO_VM_OP_TIMER_LE,
                                 1U, 5U);
    append_key(code, &offset, K380_MACRO_VM_OP_TAP, 7U);
    sys_put_le16((uint16_t)offset, &code[condition + 6U]);
    code[offset++] = K380_MACRO_VM_OP_END;

    zassert_equal(K380_MACRO_VM_OK,
                  run_code(code, (uint16_t)offset, &fake, &context));
    zassert_equal(4U, fake.key_count);
    zassert_equal(4U, fake.key_usages[0]);
    zassert_equal(6U, fake.key_usages[2]);
    zassert_equal(10U, fake.traces[3].value);
    zassert_equal(10U, fake.traces[4].value);
    zassert_equal(10U, fake.traces[6].value);
    zassert_equal(10U, fake.traces[7].value);
    zassert_equal(10U, fake.traces[9].value);
}

ZTEST(dynamic_macro_vm, test_count_and_time_loops_execute_expected_bodies)
{
    uint8_t count_code[32];
    size_t offset = 0U;
    size_t begin = append_loop_begin(count_code, &offset,
                                     K380_MACRO_VM_OP_LOOP_COUNT_BEGIN, 3U);
    const uint16_t body = (uint16_t)offset;
    append_key(count_code, &offset, K380_MACRO_VM_OP_TAP, 4U);
    append_loop_end(count_code, &offset, body);
    sys_put_le16((uint16_t)offset, &count_code[begin + 5U]);
    count_code[offset++] = K380_MACRO_VM_OP_END;

    struct fake_host fake = { 0 };
    struct k380_macro_vm_context context;
    zassert_equal(K380_MACRO_VM_OK,
                  run_code(count_code, (uint16_t)offset, &fake, &context));
    zassert_equal(6U, fake.key_count);
    zassert_equal(11U, context.instruction_count);

    uint8_t time_code[32];
    offset = 0U;
    begin = append_loop_begin(time_code, &offset,
                              K380_MACRO_VM_OP_LOOP_TIME_BEGIN, 5U);
    const uint16_t time_body = (uint16_t)offset;
    append_wait(time_code, &offset, K380_MACRO_VM_OP_WAIT, 3U, 0U);
    append_key(time_code, &offset, K380_MACRO_VM_OP_TAP, 5U);
    append_loop_end(time_code, &offset, time_body);
    sys_put_le16((uint16_t)offset, &time_code[begin + 5U]);
    time_code[offset++] = K380_MACRO_VM_OP_END;

    memset(&fake, 0, sizeof(fake));
    zassert_equal(K380_MACRO_VM_OK,
                  run_code(time_code, (uint16_t)offset, &fake, &context));
    zassert_equal(2U, fake.wait_count);
    zassert_equal(4U, fake.key_count);
}

ZTEST(dynamic_macro_vm, test_nested_loops_and_function_calls_share_state)
{
    uint8_t loop_code[40];
    size_t offset = 0U;
    const size_t outer = append_loop_begin(
        loop_code, &offset, K380_MACRO_VM_OP_LOOP_COUNT_BEGIN, 2U);
    const uint16_t outer_body = (uint16_t)offset;
    const size_t inner = append_loop_begin(
        loop_code, &offset, K380_MACRO_VM_OP_LOOP_COUNT_BEGIN, 2U);
    const uint16_t inner_body = (uint16_t)offset;
    append_key(loop_code, &offset, K380_MACRO_VM_OP_TAP, 4U);
    append_loop_end(loop_code, &offset, inner_body);
    sys_put_le16((uint16_t)offset, &loop_code[inner + 5U]);
    append_loop_end(loop_code, &offset, outer_body);
    sys_put_le16((uint16_t)offset, &loop_code[outer + 5U]);
    loop_code[offset++] = K380_MACRO_VM_OP_END;

    struct fake_host fake = { 0 };
    struct k380_macro_vm_context context;
    zassert_equal(K380_MACRO_VM_OK,
                  run_code(loop_code, (uint16_t)offset, &fake, &context));
    zassert_equal(8U, fake.key_count);
    const uint8_t function_code[] = {
        K380_MACRO_VM_OP_TIMER_RESET, 1U,
        K380_MACRO_VM_OP_CALL, 0U,
        K380_MACRO_VM_OP_CALL, 1U,
        K380_MACRO_VM_OP_END,
        K380_MACRO_VM_OP_PRESS, 4U, 0U,
        K380_MACRO_VM_OP_WAIT, 10U, 0U, 0U, 0U,
        K380_MACRO_VM_OP_RETURN,
        K380_MACRO_VM_OP_RELEASE, 4U, 0U,
        K380_MACRO_VM_OP_RETURN,
    };
    struct k380_macro_vm_package_view view = make_view(
        function_code, sizeof(function_code));
    view.function_count = 2U;
    view.functions[0] = (struct k380_macro_vm_function_range){ 7U, 16U };
    view.functions[1] = (struct k380_macro_vm_function_range){ 16U, 20U };
    struct k380_macro_vm_host host = make_host(&fake);
    memset(&fake, 0, sizeof(fake));
    memset(&context, 0, sizeof(context));

    zassert_equal(K380_MACRO_VM_OK,
                  k380_macro_vm_run(&view, &host, &context));
    zassert_equal(2U, fake.key_count);
    zassert_true(fake.key_states[0]);
    zassert_false(fake.key_states[1]);
    zassert_equal(1U, fake.wait_count);
}

ZTEST(dynamic_macro_vm, test_stop_before_opcode_and_during_wait)
{
    const uint8_t code[] = {
        K380_MACRO_VM_OP_PRESS, 4U, 0U,
        K380_MACRO_VM_OP_END,
    };
    struct fake_host fake = { .stop = true };
    struct k380_macro_vm_context context;

    zassert_equal(K380_MACRO_VM_STOPPED,
                  run_code(code, sizeof(code), &fake, &context));
    zassert_equal(0U, fake.key_count);

    const uint8_t wait_code[] = {
        K380_MACRO_VM_OP_WAIT, 200U, 0U, 0U, 0U,
        K380_MACRO_VM_OP_PRESS, 4U, 0U,
        K380_MACRO_VM_OP_END,
    };
    memset(&fake, 0, sizeof(fake));
    fake.stop_during_wait = true;
    zassert_equal(K380_MACRO_VM_STOPPED,
                  run_code(wait_code, sizeof(wait_code), &fake, &context));
    zassert_equal(0U, fake.key_count);
    zassert_equal(1U, fake.wait_count);
}

ZTEST(dynamic_macro_vm, test_yields_after_32_nonblocking_instructions)
{
    uint8_t code[3U * 32U + 1U];
    size_t offset = 0U;
    for (size_t i = 0U; i < 32U; i++) {
        append_key(code, &offset, K380_MACRO_VM_OP_TAP, 4U);
    }
    code[offset++] = K380_MACRO_VM_OP_END;

    struct fake_host fake = { 0 };
    struct k380_macro_vm_context context;
    zassert_equal(K380_MACRO_VM_OK,
                  run_code(code, (uint16_t)offset, &fake, &context));
    zassert_equal(1U, fake.yields);
    zassert_equal(32U, context.instruction_count);
}

ZTEST(dynamic_macro_vm, test_runtime_defenses_reject_bad_stack_and_host)
{
    const uint8_t recursive_code[] = {
        K380_MACRO_VM_OP_CALL, 0U,
        K380_MACRO_VM_OP_END,
        K380_MACRO_VM_OP_CALL, 0U,
        K380_MACRO_VM_OP_RETURN,
    };
    struct k380_macro_vm_package_view view = make_view(
        recursive_code, sizeof(recursive_code));
    view.function_count = 1U;
    view.functions[0] = (struct k380_macro_vm_function_range){ 2U, 5U };
    struct fake_host fake = { 0 };
    struct k380_macro_vm_host host = make_host(&fake);
    struct k380_macro_vm_context context;
    zassert_equal(K380_MACRO_VM_CALL_DEPTH,
                  k380_macro_vm_run(&view, &host, &context));

    uint8_t loop_code[29];
    for (size_t i = 0U; i < 4U; i++) {
        loop_code[i * 7U] = K380_MACRO_VM_OP_LOOP_COUNT_BEGIN;
        sys_put_le32(1U, &loop_code[i * 7U + 1U]);
        sys_put_le16(28U, &loop_code[i * 7U + 5U]);
    }
    loop_code[28] = K380_MACRO_VM_OP_END;
    zassert_equal(K380_MACRO_VM_LOOP_STACK_OVERFLOW,
                  run_code(loop_code, sizeof(loop_code), &fake, &context));

    const uint8_t empty_return[] = { K380_MACRO_VM_OP_RETURN };
    zassert_equal(K380_MACRO_VM_CALL_STACK_UNDERFLOW,
                  run_code(empty_return, sizeof(empty_return), &fake,
                           &context));

    const uint8_t key_code[] = {
        K380_MACRO_VM_OP_PRESS, 4U, 0U,
        K380_MACRO_VM_OP_END,
    };
    memset(&fake, 0, sizeof(fake));
    fake.key_error = -EIO;
    zassert_equal(K380_MACRO_VM_HOST_FAILURE,
                  run_code(key_code, sizeof(key_code), &fake, &context));
}

ZTEST_SUITE(dynamic_macro_vm, NULL, NULL, NULL, NULL, NULL);
