#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zmk_keyboard_k380/dynamic_macro_vm.h>

struct decoded_instruction {
    uint8_t opcode;
    uint16_t size;
    uint32_t operand0;
    uint32_t operand1;
    uint16_t operand2;
};

static enum k380_macro_vm_error decode_instruction(
    const uint8_t *code, uint16_t code_len, uint16_t pc,
    struct decoded_instruction *instruction)
{
    if (code == NULL || instruction == NULL || pc >= code_len) {
        return K380_MACRO_VM_INVALID_RUNTIME_STATE;
    }

    memset(instruction, 0, sizeof(*instruction));
    instruction->opcode = code[pc];
    switch (instruction->opcode) {
    case K380_MACRO_VM_OP_END:
    case K380_MACRO_VM_OP_RELEASE_ALL:
    case K380_MACRO_VM_OP_RETURN:
        instruction->size = 1U;
        break;
    case K380_MACRO_VM_OP_PRESS:
    case K380_MACRO_VM_OP_RELEASE:
    case K380_MACRO_VM_OP_TAP:
    case K380_MACRO_VM_OP_LOOP_END:
        instruction->size = 3U;
        if ((uint32_t)pc + instruction->size > code_len) {
            return K380_MACRO_VM_TRUNCATED_OPERAND;
        }
        instruction->operand0 = sys_get_le16(&code[pc + 1U]);
        break;
    case K380_MACRO_VM_OP_WAIT:
        instruction->size = 5U;
        if ((uint32_t)pc + instruction->size > code_len) {
            return K380_MACRO_VM_TRUNCATED_OPERAND;
        }
        instruction->operand0 = sys_get_le32(&code[pc + 1U]);
        break;
    case K380_MACRO_VM_OP_RANDOM_WAIT:
        instruction->size = 9U;
        if ((uint32_t)pc + instruction->size > code_len) {
            return K380_MACRO_VM_TRUNCATED_OPERAND;
        }
        instruction->operand0 = sys_get_le32(&code[pc + 1U]);
        instruction->operand1 = sys_get_le32(&code[pc + 5U]);
        break;
    case K380_MACRO_VM_OP_TIMER_RESET:
        instruction->size = 2U;
        if ((uint32_t)pc + instruction->size > code_len) {
            return K380_MACRO_VM_TRUNCATED_OPERAND;
        }
        instruction->operand0 = code[pc + 1U];
        break;
    case K380_MACRO_VM_OP_TIMER_GE:
    case K380_MACRO_VM_OP_TIMER_LE:
        instruction->size = 8U;
        if ((uint32_t)pc + instruction->size > code_len) {
            return K380_MACRO_VM_TRUNCATED_OPERAND;
        }
        instruction->operand0 = code[pc + 1U];
        instruction->operand1 = sys_get_le32(&code[pc + 2U]);
        instruction->operand2 = sys_get_le16(&code[pc + 6U]);
        break;
    case K380_MACRO_VM_OP_LOOP_COUNT_BEGIN:
    case K380_MACRO_VM_OP_LOOP_TIME_BEGIN:
        instruction->size = 7U;
        if ((uint32_t)pc + instruction->size > code_len) {
            return K380_MACRO_VM_TRUNCATED_OPERAND;
        }
        instruction->operand0 = sys_get_le32(&code[pc + 1U]);
        instruction->operand2 = sys_get_le16(&code[pc + 5U]);
        break;
    case K380_MACRO_VM_OP_CALL:
        instruction->size = 2U;
        if ((uint32_t)pc + instruction->size > code_len) {
            return K380_MACRO_VM_TRUNCATED_OPERAND;
        }
        instruction->operand0 = code[pc + 1U];
        break;
    default:
        return K380_MACRO_VM_UNKNOWN_OPCODE;
    }
    return K380_MACRO_VM_OK;
}

static uint32_t saturate_elapsed(uint64_t start_ms, uint64_t now_ms)
{
    if (now_ms < start_ms) {
        return 0U;
    }
    return now_ms - start_ms > UINT32_MAX ? UINT32_MAX
                                           : (uint32_t)(now_ms - start_ms);
}

static uint64_t now_ms(const struct k380_macro_vm_host *host)
{
    return host->now_ms(host->user_data);
}

static void trace_instruction(const struct k380_macro_vm_host *host,
                              const struct k380_macro_vm_context *context,
                              uint16_t pc, uint8_t event, uint8_t result,
                              uint32_t value)
{
    if (host->trace != NULL) {
        host->trace(pc, event, result, value,
                    saturate_elapsed(context->start_ms,
                                     now_ms(host)),
                    host->user_data);
    }
}

static enum k380_macro_vm_error host_result(
    const struct k380_macro_vm_host *host, int result)
{
    if (result == 0) {
        return K380_MACRO_VM_OK;
    }
    return host->stop_requested(host->user_data)
               ? K380_MACRO_VM_STOPPED
               : K380_MACRO_VM_HOST_FAILURE;
}

static enum k380_macro_vm_error fail_run(
    struct k380_macro_vm_context *context,
    enum k380_macro_vm_error error)
{
    context->error = error;
    return error;
}

static enum k380_macro_vm_error check_stop(
    const struct k380_macro_vm_host *host,
    struct k380_macro_vm_context *context)
{
    return host->stop_requested(host->user_data)
               ? fail_run(context, K380_MACRO_VM_STOPPED)
               : K380_MACRO_VM_OK;
}

static enum k380_macro_vm_error execute_key(
    const struct k380_macro_vm_host *host,
    struct k380_macro_vm_context *context, uint16_t pc, uint8_t event,
    uint8_t opcode, uint16_t usage)
{
    int result = host->key_event(usage, true, host->user_data);
    enum k380_macro_vm_error error = host_result(host, result);
    if (error == K380_MACRO_VM_OK && opcode == K380_MACRO_VM_OP_TAP) {
        result = host->key_event(usage, false, host->user_data);
        error = host_result(host, result);
    }
    trace_instruction(host, context, pc, event, (uint8_t)error, usage);
    return error;
}

static enum k380_macro_vm_error execute_wait(
    const struct k380_macro_vm_host *host,
    struct k380_macro_vm_context *context, uint16_t pc, uint8_t event,
    uint32_t duration_ms)
{
    const enum k380_macro_vm_error error =
        host_result(host, host->wait_ms(duration_ms, host->user_data));
    trace_instruction(host, context, pc, event, (uint8_t)error, duration_ms);
    return error;
}

enum k380_macro_vm_error
k380_macro_vm_run(const struct k380_macro_vm_package_view *view,
                  const struct k380_macro_vm_host *host,
                  struct k380_macro_vm_context *context)
{
    if (view == NULL || host == NULL || context == NULL || view->code == NULL ||
        view->code_len == 0U || view->code_len > K380_MACRO_VM_MAX_CODE_BYTES ||
        view->entry_offset >= view->code_len ||
        view->function_count > K380_MACRO_VM_MAX_FUNCTIONS ||
        host->key_event == NULL ||
        host->release_all == NULL || host->wait_ms == NULL ||
        host->now_ms == NULL || host->stop_requested == NULL) {
        if (context != NULL) {
            context->error = K380_MACRO_VM_INVALID_RUNTIME_STATE;
        }
        return K380_MACRO_VM_INVALID_RUNTIME_STATE;
    }

    memset(context, 0, sizeof(*context));
    context->pc = view->entry_offset;
    context->error = K380_MACRO_VM_OK;
    context->start_ms = now_ms(host);
    for (size_t index = 0U; index < ARRAY_SIZE(context->timer_origin_ms);
         index++) {
        context->timer_origin_ms[index] = context->start_ms;
    }

    uint8_t fairness_count = 0U;
    while (true) {
        enum k380_macro_vm_error error = check_stop(host, context);
        if (error != K380_MACRO_VM_OK) {
            return error;
        }

        const uint16_t pc = context->pc;
        struct decoded_instruction instruction;
        error = decode_instruction(view->code, view->code_len, pc,
                                   &instruction);
        if (error != K380_MACRO_VM_OK) {
            return fail_run(context, error);
        }
        const uint16_t next = (uint16_t)(pc + instruction.size);
        if (next > view->code_len) {
            return fail_run(context, K380_MACRO_VM_INVALID_RUNTIME_STATE);
        }

        bool blocking = false;
        switch (instruction.opcode) {
        case K380_MACRO_VM_OP_END:
            if (context->call_depth != 0U || context->loop_depth != 0U) {
                return fail_run(context, K380_MACRO_VM_INVALID_RUNTIME_STATE);
            }
            return K380_MACRO_VM_OK;

        case K380_MACRO_VM_OP_PRESS:
            error = execute_key(host, context, pc, K380_MACRO_VM_TRACE_PRESS,
                                K380_MACRO_VM_OP_PRESS,
                                (uint16_t)instruction.operand0);
            break;
        case K380_MACRO_VM_OP_RELEASE:
            error = host_result(
                host, host->key_event((uint16_t)instruction.operand0, false,
                                      host->user_data));
            trace_instruction(host, context, pc, K380_MACRO_VM_TRACE_RELEASE,
                               (uint8_t)error,
                               (uint16_t)instruction.operand0);
            break;
        case K380_MACRO_VM_OP_TAP:
            error = execute_key(host, context, pc, K380_MACRO_VM_TRACE_TAP,
                                K380_MACRO_VM_OP_TAP,
                                (uint16_t)instruction.operand0);
            break;
        case K380_MACRO_VM_OP_RELEASE_ALL:
            error = host_result(host, host->release_all(host->user_data));
            trace_instruction(host, context, pc,
                              K380_MACRO_VM_TRACE_RELEASE_ALL, (uint8_t)error,
                              0U);
            break;
        case K380_MACRO_VM_OP_WAIT:
            blocking = true;
            error = execute_wait(host, context, pc, K380_MACRO_VM_TRACE_WAIT,
                                 instruction.operand0);
            break;
        case K380_MACRO_VM_OP_RANDOM_WAIT: {
            const uint32_t minimum = instruction.operand0;
            const uint32_t maximum = instruction.operand1;
            if (host->random_u32 == NULL || minimum > maximum) {
                return fail_run(context, K380_MACRO_VM_INVALID_RUNTIME_STATE);
            }
            const uint64_t span = (uint64_t)maximum - minimum + 1U;
            const uint32_t duration =
                minimum + (uint32_t)((uint64_t)host->random_u32(
                                         host->user_data) % span);
            blocking = true;
            error = execute_wait(host, context,
                                 pc, K380_MACRO_VM_TRACE_RANDOM_WAIT,
                                 duration);
            break;
        }
        case K380_MACRO_VM_OP_TIMER_RESET: {
            const uint8_t timer = (uint8_t)instruction.operand0;
            if (timer < 1U || timer > ARRAY_SIZE(context->timer_origin_ms)) {
                return fail_run(context, K380_MACRO_VM_INVALID_RUNTIME_STATE);
            }
            context->timer_origin_ms[timer - 1U] = now_ms(host);
            trace_instruction(host, context, pc,
                              K380_MACRO_VM_TRACE_TIMER_RESET, 0U, timer);
            break;
        }
        case K380_MACRO_VM_OP_TIMER_GE:
        case K380_MACRO_VM_OP_TIMER_LE: {
            const uint8_t timer = (uint8_t)instruction.operand0;
            if (timer < 1U || timer > ARRAY_SIZE(context->timer_origin_ms) ||
                instruction.operand2 >= view->code_len) {
                return fail_run(context, K380_MACRO_VM_INVALID_RUNTIME_STATE);
            }
            const uint64_t elapsed = now_ms(host) -
                                     context->timer_origin_ms[timer - 1U];
            const bool condition = instruction.opcode == K380_MACRO_VM_OP_TIMER_GE
                                       ? elapsed >= instruction.operand1
                                       : elapsed <= instruction.operand1;
            trace_instruction(host, context, pc,
                              K380_MACRO_VM_TRACE_TIMER_CHECK, 0U,
                              saturate_elapsed(0U, elapsed));
            context->pc = condition ? next : instruction.operand2;
            goto instruction_complete;
        }
        case K380_MACRO_VM_OP_LOOP_COUNT_BEGIN:
        case K380_MACRO_VM_OP_LOOP_TIME_BEGIN: {
            if (context->loop_depth >= K380_MACRO_VM_MAX_LOOP_DEPTH ||
                instruction.operand0 == 0U ||
                instruction.operand2 >= view->code_len) {
                return fail_run(context, K380_MACRO_VM_LOOP_STACK_OVERFLOW);
            }
            struct k380_macro_vm_loop_frame *frame =
                &context->loops[context->loop_depth++];
            memset(frame, 0, sizeof(*frame));
            frame->opcode = instruction.opcode;
            frame->body_pc = next;
            frame->end_pc = instruction.operand2;
            if (instruction.opcode == K380_MACRO_VM_OP_LOOP_COUNT_BEGIN) {
                frame->limit.remaining = instruction.operand0;
            } else {
                frame->duration_ms = instruction.operand0;
                frame->limit.start_ms = now_ms(host);
            }
            trace_instruction(host, context, pc,
                              K380_MACRO_VM_TRACE_LOOP_BEGIN, 0U,
                              instruction.operand0);
            break;
        }
        case K380_MACRO_VM_OP_LOOP_END: {
            if (context->loop_depth == 0U) {
                return fail_run(context, K380_MACRO_VM_LOOP_STACK_UNDERFLOW);
            }
            struct k380_macro_vm_loop_frame *frame =
                &context->loops[context->loop_depth - 1U];
            if (frame->end_pc != next || frame->body_pc != instruction.operand0) {
                return fail_run(context, K380_MACRO_VM_LOOP_FRAME_MISMATCH);
            }
            uint32_t trace_value;
            bool repeat;
            if (frame->opcode == K380_MACRO_VM_OP_LOOP_COUNT_BEGIN) {
                trace_value = frame->limit.remaining;
                repeat = frame->limit.remaining > 1U;
                if (repeat) {
                    frame->limit.remaining--;
                }
            } else {
                const uint64_t elapsed = now_ms(host) - frame->limit.start_ms;
                trace_value = saturate_elapsed(0U, elapsed);
                repeat = elapsed < frame->duration_ms;
            }
            trace_instruction(host, context, pc,
                              K380_MACRO_VM_TRACE_LOOP_END, 0U, trace_value);
            if (repeat) {
                context->pc = frame->body_pc;
                goto instruction_complete;
            }
            context->loop_depth--;
            break;
        }
        case K380_MACRO_VM_OP_CALL: {
            const uint8_t function = (uint8_t)instruction.operand0;
            if (function >= view->function_count) {
                return fail_run(context, K380_MACRO_VM_INVALID_FUNCTION);
            }
            if (context->call_depth >= K380_MACRO_VM_MAX_CALL_DEPTH) {
                return fail_run(context, K380_MACRO_VM_CALL_DEPTH);
            }
            const struct k380_macro_vm_function_range *range =
                &view->functions[function];
            if (range->entry_offset >= range->end_offset_exclusive ||
                range->end_offset_exclusive > view->code_len) {
                return fail_run(context, K380_MACRO_VM_INVALID_FUNCTION_RANGE);
            }
            context->return_pc[context->call_depth] = next;
            context->return_loop_depth[context->call_depth] =
                context->loop_depth;
            context->call_depth++;
            context->pc = range->entry_offset;
            trace_instruction(host, context, pc, K380_MACRO_VM_TRACE_CALL, 0U,
                              function);
            goto instruction_complete;
        }
        case K380_MACRO_VM_OP_RETURN:
            if (context->call_depth == 0U) {
                return fail_run(context, K380_MACRO_VM_CALL_STACK_UNDERFLOW);
            }
            if (context->loop_depth !=
                context->return_loop_depth[context->call_depth - 1U]) {
                return fail_run(context, K380_MACRO_VM_LOOP_FRAME_MISMATCH);
            }
            context->call_depth--;
            context->pc = context->return_pc[context->call_depth];
            trace_instruction(host, context, pc, K380_MACRO_VM_TRACE_RETURN, 0U,
                              0U);
            goto instruction_complete;
        default:
            return fail_run(context, K380_MACRO_VM_UNKNOWN_OPCODE);
        }

        if (error != K380_MACRO_VM_OK) {
            return fail_run(context, error);
        }
        context->pc = next;

    instruction_complete:
        context->instruction_count = context->instruction_count == UINT32_MAX
                                          ? UINT32_MAX
                                          : context->instruction_count + 1U;
        if (blocking) {
            fairness_count = 0U;
        } else if (++fairness_count >= 32U) {
            if (host->yield_cpu != NULL) {
                host->yield_cpu(host->user_data);
            }
            fairness_count = 0U;
        }
    }
}
