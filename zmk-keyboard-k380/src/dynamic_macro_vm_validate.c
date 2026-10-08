#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

#include <zmk_keyboard_k380/dynamic_macro_vm.h>

struct decoded_instruction {
    uint8_t opcode;
    uint8_t size;
    uint32_t operand0;
    uint32_t operand1;
    uint32_t operand2;
};

#define K380_MACRO_VM_BOUNDARY_BYTES \
    ((K380_MACRO_VM_MAX_CODE_BYTES + 1U + 7U) / 8U)

struct instruction_boundaries {
    uint8_t bits[K380_MACRO_VM_BOUNDARY_BYTES];
};

static int fail(enum k380_macro_vm_error error)
{
    return (int)error;
}

static bool is_key_opcode(uint8_t opcode)
{
    return opcode == K380_MACRO_VM_OP_PRESS ||
           opcode == K380_MACRO_VM_OP_RELEASE ||
           opcode == K380_MACRO_VM_OP_TAP;
}

static bool is_condition_opcode(uint8_t opcode)
{
    return opcode == K380_MACRO_VM_OP_TIMER_GE ||
           opcode == K380_MACRO_VM_OP_TIMER_LE;
}

static bool is_loop_begin_opcode(uint8_t opcode)
{
    return opcode == K380_MACRO_VM_OP_LOOP_COUNT_BEGIN ||
           opcode == K380_MACRO_VM_OP_LOOP_TIME_BEGIN;
}

static bool valid_key_usage(uint16_t usage)
{
    if ((usage >= 4U && usage <= 126U) ||
        (usage >= 130U && usage <= 164U) ||
        (usage >= 176U && usage <= 221U) ||
        (usage >= 224U && usage <= 231U)) {
        return usage != 102U && usage != 127U;
    }
    return false;
}

static int decode_instruction(const uint8_t *code, size_t code_len,
                              uint16_t offset,
                              struct decoded_instruction *instruction)
{
    if (offset >= code_len || instruction == NULL) {
        return fail(K380_MACRO_VM_INVALID_CODE_LENGTH);
    }

    memset(instruction, 0, sizeof(*instruction));
    instruction->opcode = code[offset];
    switch (instruction->opcode) {
    case K380_MACRO_VM_OP_END:
    case K380_MACRO_VM_OP_RELEASE_ALL:
    case K380_MACRO_VM_OP_RETURN:
        instruction->size = 1U;
        return K380_MACRO_VM_OK;
    case K380_MACRO_VM_OP_PRESS:
    case K380_MACRO_VM_OP_RELEASE:
    case K380_MACRO_VM_OP_TAP:
    case K380_MACRO_VM_OP_LOOP_END:
        instruction->size = 3U;
        if (code_len - offset < instruction->size) {
            return fail(K380_MACRO_VM_TRUNCATED_OPERAND);
        }
        instruction->operand0 = sys_get_le16(&code[offset + 1U]);
        return K380_MACRO_VM_OK;
    case K380_MACRO_VM_OP_WAIT:
        instruction->size = 5U;
        if (code_len - offset < instruction->size) {
            return fail(K380_MACRO_VM_TRUNCATED_OPERAND);
        }
        instruction->operand0 = sys_get_le32(&code[offset + 1U]);
        return K380_MACRO_VM_OK;
    case K380_MACRO_VM_OP_RANDOM_WAIT:
        instruction->size = 9U;
        if (code_len - offset < instruction->size) {
            return fail(K380_MACRO_VM_TRUNCATED_OPERAND);
        }
        instruction->operand0 = sys_get_le32(&code[offset + 1U]);
        instruction->operand1 = sys_get_le32(&code[offset + 5U]);
        return K380_MACRO_VM_OK;
    case K380_MACRO_VM_OP_TIMER_RESET:
        instruction->size = 2U;
        if (code_len - offset < instruction->size) {
            return fail(K380_MACRO_VM_TRUNCATED_OPERAND);
        }
        instruction->operand0 = code[offset + 1U];
        return K380_MACRO_VM_OK;
    case K380_MACRO_VM_OP_TIMER_GE:
    case K380_MACRO_VM_OP_TIMER_LE:
        instruction->size = 8U;
        if (code_len - offset < instruction->size) {
            return fail(K380_MACRO_VM_TRUNCATED_OPERAND);
        }
        instruction->operand0 = code[offset + 1U];
        instruction->operand1 = sys_get_le32(&code[offset + 2U]);
        instruction->operand2 = sys_get_le16(&code[offset + 6U]);
        return K380_MACRO_VM_OK;
    case K380_MACRO_VM_OP_LOOP_COUNT_BEGIN:
    case K380_MACRO_VM_OP_LOOP_TIME_BEGIN:
        instruction->size = 7U;
        if (code_len - offset < instruction->size) {
            return fail(K380_MACRO_VM_TRUNCATED_OPERAND);
        }
        instruction->operand0 = sys_get_le32(&code[offset + 1U]);
        instruction->operand1 = sys_get_le16(&code[offset + 5U]);
        return K380_MACRO_VM_OK;
    case K380_MACRO_VM_OP_CALL:
        instruction->size = 2U;
        if (code_len - offset < instruction->size) {
            return fail(K380_MACRO_VM_TRUNCATED_OPERAND);
        }
        instruction->operand0 = code[offset + 1U];
        return K380_MACRO_VM_OK;
    default:
        return fail(K380_MACRO_VM_UNKNOWN_OPCODE);
    }
}

static void mark_boundary(struct instruction_boundaries *boundaries,
                          uint16_t offset)
{
    boundaries->bits[offset / 8U] |= (uint8_t)(1U << (offset % 8U));
}

static bool boundary(const struct instruction_boundaries *boundaries,
                     uint16_t code_len, uint16_t target)
{
    return target <= code_len &&
           (boundaries->bits[target / 8U] &
            (uint8_t)(1U << (target % 8U))) != 0U;
}

static int previous_instruction(const struct instruction_boundaries *boundaries,
                                const uint8_t *code, uint16_t code_len,
                                uint16_t end, uint16_t *offset,
                                struct decoded_instruction *instruction)
{
    for (uint16_t candidate = 0U; candidate < end; candidate++) {
        if (!boundary(boundaries, code_len, candidate)) {
            continue;
        }
        struct decoded_instruction current;
        int err = decode_instruction(code, code_len, candidate, &current);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
        if ((uint32_t)candidate + current.size == end) {
            if (offset != NULL) {
                *offset = candidate;
            }
            if (instruction != NULL) {
                *instruction = current;
            }
            return K380_MACRO_VM_OK;
        }
    }
    return fail(K380_MACRO_VM_INVALID_FUNCTION_RANGE);
}

static int validate_sequence(const uint8_t *code, uint16_t code_len,
                             const struct instruction_boundaries *boundaries,
                             uint16_t start,
                             uint16_t stop, uint16_t region_start,
                             uint16_t region_terminator, uint8_t loop_depth,
                             uint8_t block_depth)
{
    if (start == stop) {
        return fail(K380_MACRO_VM_EMPTY_BLOCK);
    }

    uint16_t pc = start;
    while (pc < stop) {
        struct decoded_instruction instruction;
        int err = decode_instruction(code, code_len, pc, &instruction);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
        uint16_t next = (uint16_t)(pc + instruction.size);
        if (next > stop) {
            return fail(K380_MACRO_VM_BLOCK_BOUNDARY);
        }
        if (instruction.opcode == K380_MACRO_VM_OP_END ||
            instruction.opcode == K380_MACRO_VM_OP_RETURN ||
            instruction.opcode == K380_MACRO_VM_OP_LOOP_END) {
            return fail(K380_MACRO_VM_UNMATCHED_CONTROL_FLOW);
        }
        if (is_condition_opcode(instruction.opcode)) {
            if (block_depth >= K380_MACRO_VM_MAX_BLOCK_DEPTH) {
                return fail(K380_MACRO_VM_BLOCK_DEPTH);
            }
            uint16_t target = (uint16_t)instruction.operand2;
            if (!boundary(boundaries, code_len, target) ||
                target <= next || target > stop || target < region_start ||
                target > region_terminator) {
                return fail(K380_MACRO_VM_INVALID_TARGET);
            }
            err = validate_sequence(code, code_len, boundaries, next, target,
                                    region_start, region_terminator,
                                    loop_depth, block_depth + 1U);
            if (err != K380_MACRO_VM_OK) {
                return err;
            }
            pc = target;
            continue;
        }
        if (is_loop_begin_opcode(instruction.opcode)) {
            uint16_t target = (uint16_t)instruction.operand1;
            if (!boundary(boundaries, code_len, target) ||
                target <= next || target > stop || target < region_start ||
                target > region_terminator) {
                return fail(K380_MACRO_VM_INVALID_TARGET);
            }
            if (loop_depth >= K380_MACRO_VM_MAX_LOOP_DEPTH) {
                return fail(K380_MACRO_VM_LOOP_DEPTH);
            }
            if (block_depth >= K380_MACRO_VM_MAX_BLOCK_DEPTH) {
                return fail(K380_MACRO_VM_BLOCK_DEPTH);
            }
            uint16_t loop_end_offset;
            struct decoded_instruction loop_end;
            int err = previous_instruction(boundaries, code, code_len, target,
                                            &loop_end_offset, &loop_end);
            if (err != K380_MACRO_VM_OK ||
                loop_end.opcode != K380_MACRO_VM_OP_LOOP_END ||
                loop_end.operand0 != next) {
                return fail(K380_MACRO_VM_UNMATCHED_LOOP);
            }
            err = validate_sequence(code, code_len, boundaries, next,
                                    loop_end_offset, region_start,
                                    region_terminator, loop_depth + 1U,
                                    block_depth + 1U);
            if (err != K380_MACRO_VM_OK) {
                return err;
            }
            pc = target;
            continue;
        }
        pc = next;
    }
    return pc == stop ? K380_MACRO_VM_OK : fail(K380_MACRO_VM_BLOCK_BOUNDARY);
}

struct call_graph_context {
    const uint8_t *code;
    uint16_t code_len;
    const struct k380_macro_vm_package_view *view;
    uint8_t state[1U + K380_MACRO_VM_MAX_FUNCTIONS];
    uint8_t depths[1U + K380_MACRO_VM_MAX_FUNCTIONS];
};

static int walk_call_graph(struct call_graph_context *context, uint8_t region)
{
    if (context->state[region] == 1U) {
        return fail(K380_MACRO_VM_RECURSION);
    }
    if (context->state[region] == 2U) {
        return K380_MACRO_VM_OK;
    }
    context->state[region] = 1U;
    uint8_t maximum = 0U;
    uint16_t start = region == 0U ? 0U : context->view->functions[region - 1U].entry_offset;
    uint16_t end = region == 0U
                       ? (context->view->function_count > 0U
                              ? context->view->functions[0].entry_offset
                              : context->code_len)
                       : context->view->functions[region - 1U].end_offset_exclusive;
    for (uint16_t pc = start; pc < end;) {
        struct decoded_instruction instruction;
        int err = decode_instruction(context->code, context->code_len, pc,
                                     &instruction);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
        if (instruction.opcode == K380_MACRO_VM_OP_CALL) {
            uint8_t target_region = (uint8_t)(instruction.operand0 + 1U);
            err = walk_call_graph(context, target_region);
            if (err != K380_MACRO_VM_OK) {
                return err;
            }
            uint8_t depth = (uint8_t)(context->depths[target_region] + 1U);
            maximum = maximum > depth ? maximum : depth;
        }
        pc = (uint16_t)(pc + instruction.size);
    }
    context->depths[region] = maximum;
    context->state[region] = 2U;
    return maximum > K380_MACRO_VM_MAX_CALL_DEPTH
               ? fail(K380_MACRO_VM_CALL_DEPTH)
               : K380_MACRO_VM_OK;
}

static int validate_call_graph(const uint8_t *code, uint16_t code_len,
                               const struct k380_macro_vm_package_view *view)
{
    struct call_graph_context context = {
        .code = code,
        .code_len = code_len,
        .view = view,
    };
    return walk_call_graph(&context, 0U);
}

struct loop_depth_context {
    const uint8_t *code;
    uint16_t code_len;
    const struct k380_macro_vm_package_view *view;
    const struct instruction_boundaries *boundaries;
    uint8_t call_loop_depth_plus_one[1U + K380_MACRO_VM_MAX_FUNCTIONS]
                                    [K380_MACRO_VM_MAX_FUNCTIONS];
    uint8_t local_maximum[1U + K380_MACRO_VM_MAX_FUNCTIONS];
    uint8_t maximum[1U + K380_MACRO_VM_MAX_FUNCTIONS];
    uint8_t state[1U + K380_MACRO_VM_MAX_FUNCTIONS];
};

static int analyze_loop_sequence(struct loop_depth_context *context,
                                 uint8_t region, uint16_t start, uint16_t stop,
                                 uint8_t loop_depth, uint8_t *maximum)
{
    uint8_t local_maximum = loop_depth;
    uint16_t pc = start;

    while (pc < stop) {
        struct decoded_instruction instruction;
        int err = decode_instruction(context->code, context->code_len, pc,
                                     &instruction);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
        uint16_t next = (uint16_t)(pc + instruction.size);
        if (next > stop) {
            return fail(K380_MACRO_VM_BLOCK_BOUNDARY);
        }
        if (is_condition_opcode(instruction.opcode)) {
            uint8_t branch_maximum = loop_depth;
            err = analyze_loop_sequence(context, region, next,
                                        (uint16_t)instruction.operand2,
                                        loop_depth, &branch_maximum);
            if (err != K380_MACRO_VM_OK) {
                return err;
            }
            local_maximum = local_maximum > branch_maximum
                                ? local_maximum
                                : branch_maximum;
            pc = (uint16_t)instruction.operand2;
            continue;
        }
        if (is_loop_begin_opcode(instruction.opcode)) {
            if (loop_depth >= K380_MACRO_VM_MAX_LOOP_DEPTH) {
                return fail(K380_MACRO_VM_LOOP_DEPTH);
            }
            uint16_t loop_end_offset;
            struct decoded_instruction loop_end;
            err = previous_instruction(context->boundaries, context->code,
                                       context->code_len,
                                       (uint16_t)instruction.operand1,
                                       &loop_end_offset, &loop_end);
            if (err != K380_MACRO_VM_OK) {
                return err;
            }
            uint8_t body_maximum = (uint8_t)(loop_depth + 1U);
            err = analyze_loop_sequence(context, region, next,
                                        loop_end_offset,
                                        (uint8_t)(loop_depth + 1U),
                                        &body_maximum);
            if (err != K380_MACRO_VM_OK) {
                return err;
            }
            local_maximum = local_maximum > body_maximum
                                ? local_maximum
                                : body_maximum;
            pc = (uint16_t)instruction.operand1;
            continue;
        }
        if (instruction.opcode == K380_MACRO_VM_OP_CALL) {
            const uint8_t target = (uint8_t)instruction.operand0;
            const uint8_t encoded_depth = (uint8_t)(loop_depth + 1U);
            if (encoded_depth >
                context->call_loop_depth_plus_one[region][target]) {
                context->call_loop_depth_plus_one[region][target] =
                    encoded_depth;
            }
        }
        pc = next;
    }

    if (maximum != NULL) {
        *maximum = local_maximum;
    }
    return K380_MACRO_VM_OK;
}

static int analyze_loop_region(struct loop_depth_context *context,
                               uint8_t region)
{
    uint16_t start = region == 0U
                         ? 0U
                         : context->view->functions[region - 1U].entry_offset;
    uint16_t end = region == 0U
                       ? (context->view->function_count > 0U
                              ? context->view->functions[0].entry_offset
                              : context->code_len)
                       : context->view->functions[region - 1U]
                             .end_offset_exclusive;
    uint8_t maximum = 0U;
    int err = analyze_loop_sequence(context, region, start,
                                    (uint16_t)(end - 1U), 0U, &maximum);
    if (err != K380_MACRO_VM_OK) {
        return err;
    }
    context->local_maximum[region] = maximum;
    return K380_MACRO_VM_OK;
}

static int combine_loop_region(struct loop_depth_context *context,
                               uint8_t region)
{
    if (context->state[region] == 1U) {
        return fail(K380_MACRO_VM_RECURSION);
    }
    if (context->state[region] == 2U) {
        return K380_MACRO_VM_OK;
    }

    context->state[region] = 1U;
    uint8_t maximum = context->local_maximum[region];
    for (uint8_t target = 0U; target < context->view->function_count;
         target++) {
        const uint8_t encoded_depth =
            context->call_loop_depth_plus_one[region][target];
        if (encoded_depth == 0U) {
            continue;
        }
        const uint8_t target_region = (uint8_t)(target + 1U);
        int err = combine_loop_region(context, target_region);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
        const uint16_t combined_depth =
            (uint16_t)(encoded_depth - 1U) +
            context->maximum[target_region];
        if (combined_depth > K380_MACRO_VM_MAX_LOOP_DEPTH) {
            return fail(K380_MACRO_VM_LOOP_DEPTH);
        }
        if (combined_depth > maximum) {
            maximum = (uint8_t)combined_depth;
        }
    }
    context->maximum[region] = maximum;
    context->state[region] = 2U;
    return K380_MACRO_VM_OK;
}

static int validate_cross_function_loop_depth(
    const uint8_t *code, uint16_t code_len,
    const struct k380_macro_vm_package_view *view,
    const struct instruction_boundaries *boundaries)
{
    struct loop_depth_context context = {
        .code = code,
        .code_len = code_len,
        .view = view,
        .boundaries = boundaries,
    };
    /* Finish bounded block recursion before traversing the call graph. */
    for (uint8_t region = 0U; region <= view->function_count; region++) {
        int err = analyze_loop_region(&context, region);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
    }
    return combine_loop_region(&context, 0U);
}

int k380_macro_vm_validate(const uint8_t *package, size_t len,
                           struct k380_macro_vm_package_view *view)
{
    if (package == NULL || view == NULL) {
        return fail(K380_MACRO_VM_INVALID_PACKAGE);
    }
    if (len < K380_MACRO_VM_PACKAGE_HEADER_SIZE) {
        return fail(K380_MACRO_VM_INVALID_LENGTH);
    }
    if (memcmp(package, K380_MACRO_VM_PACKAGE_MAGIC,
               K380_MACRO_VM_PACKAGE_MAGIC_SIZE) != 0) {
        return fail(K380_MACRO_VM_INVALID_MAGIC);
    }
    if (package[4] != K380_MACRO_VM_PACKAGE_VERSION) {
        return fail(K380_MACRO_VM_INVALID_VERSION);
    }
    if (package[5] != 0U) {
        return fail(K380_MACRO_VM_INVALID_FLAGS);
    }
    uint8_t function_count = package[6];
    if (function_count > K380_MACRO_VM_MAX_FUNCTIONS || package[7] != 0U) {
        return fail(package[7] != 0U ? K380_MACRO_VM_INVALID_RESERVED
                                     : K380_MACRO_VM_INVALID_FUNCTION);
    }
    uint16_t code_len = sys_get_le16(&package[8]);
    uint16_t entry_offset = sys_get_le16(&package[10]);
    if (code_len == 0U || code_len > K380_MACRO_VM_MAX_CODE_BYTES) {
        return fail(K380_MACRO_VM_INVALID_CODE_LENGTH);
    }
    if (entry_offset != 0U) {
        return fail(K380_MACRO_VM_INVALID_ENTRY);
    }
    size_t code_offset = K380_MACRO_VM_PACKAGE_HEADER_SIZE +
                         function_count * K380_MACRO_VM_FUNCTION_ENTRY_SIZE;
    if (code_offset + code_len != len || len > K380_MACRO_VM_MAX_PACKAGE_BYTES) {
        return fail(K380_MACRO_VM_INVALID_LENGTH);
    }

    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0U; index < len; index++) {
        uint8_t value = (index >= 12U && index < 16U) ? 0U : package[index];
        crc ^= value;
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
    }
    crc ^= 0xFFFFFFFFU;
    if (crc != sys_get_le32(&package[12])) {
        return fail(K380_MACRO_VM_CRC_MISMATCH);
    }

    const uint8_t *code = &package[code_offset];
    struct instruction_boundaries boundaries = {0};
    uint16_t offset = 0U;
    while (offset < code_len) {
        mark_boundary(&boundaries, offset);
        struct decoded_instruction instruction;
        int err = decode_instruction(code, code_len, offset, &instruction);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
        if (is_key_opcode(instruction.opcode) &&
            !valid_key_usage((uint16_t)instruction.operand0)) {
            return fail(K380_MACRO_VM_INVALID_KEY_USAGE);
        }
        if ((instruction.opcode == K380_MACRO_VM_OP_TIMER_RESET ||
             is_condition_opcode(instruction.opcode)) &&
            (instruction.operand0 < 1U || instruction.operand0 > 3U)) {
            return fail(K380_MACRO_VM_INVALID_TIMER);
        }
        if (instruction.opcode == K380_MACRO_VM_OP_WAIT && instruction.operand0 == 0U) {
            return fail(K380_MACRO_VM_INVALID_LIMIT);
        }
        if (instruction.opcode == K380_MACRO_VM_OP_RANDOM_WAIT &&
            (instruction.operand0 == 0U || instruction.operand1 == 0U ||
             instruction.operand0 > instruction.operand1)) {
            return fail(K380_MACRO_VM_INVALID_LIMIT);
        }
        if (is_loop_begin_opcode(instruction.opcode) && instruction.operand0 == 0U) {
            return fail(K380_MACRO_VM_INVALID_LIMIT);
        }
        if (instruction.opcode == K380_MACRO_VM_OP_CALL &&
            instruction.operand0 >= function_count) {
            return fail(K380_MACRO_VM_INVALID_FUNCTION);
        }
        offset = (uint16_t)(offset + instruction.size);
    }
    if (offset != code_len) {
        return fail(K380_MACRO_VM_INVALID_CODE_LENGTH);
    }
    mark_boundary(&boundaries, code_len);

    memset(view, 0, sizeof(*view));
    view->package = package;
    view->package_len = len;
    view->code = code;
    view->code_len = code_len;
    view->function_count = function_count;
    view->entry_offset = entry_offset;
    uint16_t previous_entry = 0U;
    uint16_t previous_end = 0U;
    for (uint8_t index = 0U; index < function_count; index++) {
        size_t table_offset = K380_MACRO_VM_PACKAGE_HEADER_SIZE +
                              index * K380_MACRO_VM_FUNCTION_ENTRY_SIZE;
        uint16_t function_entry = sys_get_le16(&package[table_offset]);
        uint16_t function_end = sys_get_le16(&package[table_offset + 2U]);
        if (function_entry >= function_end || function_end > code_len ||
            !boundary(&boundaries, code_len, function_entry) ||
            !boundary(&boundaries, code_len, function_end)) {
            return fail(K380_MACRO_VM_INVALID_FUNCTION_RANGE);
        }
        if ((index > 0U && function_entry <= previous_entry) ||
            function_entry < previous_end) {
            return fail(K380_MACRO_VM_OVERLAPPING_FUNCTIONS);
        }
        struct decoded_instruction terminator;
        int err = previous_instruction(&boundaries, code, code_len, function_end,
                                       NULL, &terminator);
        if (err != K380_MACRO_VM_OK || terminator.opcode != K380_MACRO_VM_OP_RETURN) {
            return fail(K380_MACRO_VM_INVALID_FUNCTION_TERMINATOR);
        }
        for (uint16_t pc = function_entry; pc < function_end;) {
            struct decoded_instruction instruction;
            err = decode_instruction(code, code_len, pc, &instruction);
            if (err != K380_MACRO_VM_OK) {
                return err;
            }
            if (instruction.opcode == K380_MACRO_VM_OP_END) {
                return fail(K380_MACRO_VM_INVALID_FUNCTION_TERMINATOR);
            }
            pc = (uint16_t)(pc + instruction.size);
        }
        view->functions[index].entry_offset = function_entry;
        view->functions[index].end_offset_exclusive = function_end;
        previous_entry = function_entry;
        previous_end = function_end;
    }
    uint16_t entry_end = function_count > 0U ? view->functions[0].entry_offset : code_len;
    struct decoded_instruction entry_terminator;
    int err = previous_instruction(&boundaries, code, code_len, entry_end, NULL,
                                   &entry_terminator);
    if (err != K380_MACRO_VM_OK || entry_terminator.opcode != K380_MACRO_VM_OP_END) {
        return fail(K380_MACRO_VM_INVALID_ENTRY_TERMINATOR);
    }
    for (uint16_t pc = 0U; pc < entry_end;) {
        struct decoded_instruction instruction;
        err = decode_instruction(code, code_len, pc, &instruction);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
        if (instruction.opcode == K380_MACRO_VM_OP_RETURN) {
            return fail(K380_MACRO_VM_INVALID_ENTRY_TERMINATOR);
        }
        pc = (uint16_t)(pc + instruction.size);
    }

    if (entry_end > 1U) {
        err = validate_sequence(code, code_len, &boundaries, 0U,
                                (uint16_t)(entry_end - 1U), 0U,
                                (uint16_t)(entry_end - 1U), 0U, 0U);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
    }
    for (uint8_t index = 0U; index < function_count; index++) {
        uint16_t start = view->functions[index].entry_offset;
        uint16_t end = view->functions[index].end_offset_exclusive;
        struct decoded_instruction terminator;
        (void)previous_instruction(&boundaries, code, code_len, end, NULL,
                                   &terminator);
        err = validate_sequence(code, code_len, &boundaries, start,
                                (uint16_t)(end - 1U), start,
                                (uint16_t)(end - 1U), 0U, 0U);
        if (err != K380_MACRO_VM_OK) {
            return err;
        }
    }
    err = validate_call_graph(code, code_len, view);
    if (err != K380_MACRO_VM_OK) {
        return err;
    }
    return validate_cross_function_loop_depth(code, code_len, view,
                                              &boundaries);
}
