#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
#include <string.h>
#define CALL_INSTRUCTION_SIZE 5u
#define FIRST_SAVED_REGISTER 1u
#define LAST_SAVED_REGISTER 14u
#define STEP_BUDGET 100000u
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static uint32_t map_count;
static uint32_t symbol_count;
static uint32_t function_count;
static const char *source = "int leaf(int x) {\n"
                            " int doubled = x * 2;\n"
                            " return doubled;\n"
                            "}\n"
                            "\n"
                            "int middle(int x) {\n"
                            " int y = x + 1;\n"
                            " return leaf(y) + 1;\n"
                            "}\n"
                            "\n"
                            "int main(void) {\n"
                            " int start = 5;\n"
                            " int result = middle(start);\n"
                            " return result;\n"
                            "}\n";
static int find_function(const char *name) {
    uint32_t i;
    for (i = 0u; i < function_count; i++) {
        if (strcmp(functions[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}
static const NovaDebugSymbol *find_symbol(uint32_t function_index, const char *name) {
    uint32_t i;
    for (i = 0u; i < symbol_count; i++) {
        if (symbols[i].function_index == function_index && strcmp(symbols[i].name, name) == 0)
            return &symbols[i];
    }
    return NULL;
}
static const NovaSourceMapEntry *find_line(uint32_t function_index, uint32_t line) {
    uint32_t i;
    const NovaSourceMapEntry *best = NULL;
    uint32_t best_span = 0xFFFFFFFFu;
    for (i = 0u; i < map_count; i++) {
        uint32_t span;
        if (source_map[i].function_index != function_index || source_map[i].line != line)
            continue;
        if (source_map[i].bytecode_end <= source_map[i].bytecode_start)
            continue;

        span = source_map[i].bytecode_end - source_map[i].bytecode_start;
        if (best == NULL || span < best_span) {
            best = &source_map[i];
            best_span = span;
        }
    }
    return best;
}
static const NovaSourceMapEntry *find_pc(uint32_t pc) {
    uint32_t i;
    const NovaSourceMapEntry *best = NULL;
    uint32_t best_span = 0xFFFFFFFFu;
    for (i = 0u; i < map_count; i++) {
        uint32_t span;
        if (pc < source_map[i].bytecode_start || pc >= source_map[i].bytecode_end)
            continue;
        span = source_map[i].bytecode_end - source_map[i].bytecode_start;
        if (best == NULL || span < best_span) {
            best = &source_map[i];
            best_span = span;
        }
    }
    return best;
}
static uint32_t read_u32_le(const uint8_t bytes[4]) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) | ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}
static int read_frame_register(NovaVm *vm, uint32_t frame_index, uint32_t register_index,
                               uint32_t *value_out) {
    uint32_t frame_count = nova_vm_get_call_frame_count(vm);
    NovaCallFrame child;
    uint32_t slot;
    uint32_t address;
    uint8_t bytes[4];
    if (value_out == NULL || frame_index >= frame_count || register_index >= 16u)
        return 0;
    if (frame_index + 1u == frame_count) {
        *value_out = nova_vm_get_register(vm, register_index);
        return 1;
    }
    if (register_index < FIRST_SAVED_REGISTER || register_index > LAST_SAVED_REGISTER)
        return 0;
    if (nova_vm_get_call_frame(vm, frame_index + 1u, &child) != NOVA_OK)
        return 0;
    slot = 1u + (LAST_SAVED_REGISTER - register_index);
    address = child.stack_pointer + slot * 4u;
    if (nova_vm_read_memory(vm, address, bytes, 4u) != NOVA_OK)
        return 0;
    *value_out = read_u32_le(bytes);
    return 1;
}
static int source_step_over(NovaVm *vm) {
    uint32_t start_pc = nova_vm_get_pc(vm);
    const NovaSourceMapEntry *start = find_pc(start_pc);
    uint32_t start_depth = nova_vm_get_call_frame_count(vm);
    uint32_t i;
    int32_t status = NOVA_OK;
    for (i = 0u; i < STEP_BUDGET; i++) {
        uint32_t depth;

        const NovaSourceMapEntry *current;
        status = nova_vm_step(vm);
        if (status == NOVA_HALTED || status < 0)
            return status;
        depth = nova_vm_get_call_frame_count(vm);
        current = find_pc(nova_vm_get_pc(vm));
        if (depth > start_depth)
            continue;
        if (depth < start_depth)
            return status;
        if (current != NULL && (start == NULL || current->line != start->line ||
                                current->function_index != start->function_index))
            return status;
    }
    return status;
}
static int source_step_out(NovaVm *vm) {
    uint32_t start_depth = nova_vm_get_call_frame_count(vm);
    uint32_t i;
    int32_t status = NOVA_OK;
    if (start_depth == 0u)
        return NOVA_ERR_ARGUMENT;
    for (i = 0u; i < STEP_BUDGET; i++) {
        status = nova_vm_step(vm);
        if (status == NOVA_HALTED || status < 0)
            return status;
        if (nova_vm_get_call_frame_count(vm) < start_depth)
            return status;
    }
    return status;
}
static NovaVm *new_vm(const NovaCompileResult *result) {
    NovaVm *vm = nova_vm_create();
    if (vm == NULL)
        return NULL;
    if (nova_vm_load(vm, bytecode, result->bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return NULL;
    }
    return vm;
}
int main(void) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    NovaVm *vm;
    const NovaSourceMapEntry *leaf_return;
    const NovaSourceMapEntry *middle_return;
    const NovaSourceMapEntry *main_call;
    const NovaDebugSymbol *middle_y;
    const NovaDebugSymbol *main_start;
    const NovaDebugSymbol *main_result;
    int leaf_index;
    int middle_index;
    int main_index;
    int32_t status;
    uint32_t value;
    status = nova_compile_c_debug(source, bytecode, (uint32_t)sizeof(bytecode), &result,
                                  &diagnostic, source_map, NOVA_COMPILER_MAX_SOURCE_MAP, &map_count,
                                  symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS, &symbol_count,
                                  functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "compile failed: %d @ %u:%u %s\n", status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 1;
    }

    leaf_index = find_function("leaf");
    middle_index = find_function("middle");
    main_index = find_function("main");
    if (leaf_index < 0 || middle_index < 0 || main_index < 0)
        return 2;
    leaf_return = find_line((uint32_t)leaf_index, 3u);
    middle_return = find_line((uint32_t)middle_index, 8u);
    main_call = find_line((uint32_t)main_index, 13u);
    middle_y = find_symbol((uint32_t)middle_index, "y");
    main_start = find_symbol((uint32_t)main_index, "start");
    main_result = find_symbol((uint32_t)main_index, "result");
    if (leaf_return == NULL || middle_return == NULL || main_call == NULL || middle_y == NULL ||
        main_start == NULL || main_result == NULL)
        return 3;
    /* Frame selection/reconstruction: pause in leaf, then inspect middle and main. */
    vm = new_vm(&result);
    if (vm == NULL)
        return 4;
    if (nova_vm_add_breakpoint(vm, leaf_return->bytecode_start) != NOVA_OK)
        return 5;
    status = nova_vm_run(vm, STEP_BUDGET);
    if (status != NOVA_BREAKPOINT || nova_vm_get_call_frame_count(vm) != 3u) {
        fprintf(stderr, "expected leaf breakpoint at depth 3, got status=%d depth=%u\n", status,
                nova_vm_get_call_frame_count(vm));
        return 6;
    }
    if (!read_frame_register(vm, 1u, middle_y->register_index, &value) || value != 6u) {
        fprintf(stderr, "middle.y reconstruction failed: %u\n", value);
        return 7;
    }
    if (!read_frame_register(vm, 0u, main_start->register_index, &value) || value != 5u) {
        fprintf(stderr, "main.start reconstruction failed: %u\n", value);
        return 8;
    }
    nova_vm_destroy(vm);
    /* Step Over: stop on the main call line, then cross middle+leaf without entering them. */
    vm = new_vm(&result);
    if (vm == NULL)
        return 9;
    if (nova_vm_add_breakpoint(vm, main_call->bytecode_start) != NOVA_OK)
        return 10;
    status = nova_vm_run(vm, STEP_BUDGET);
    if (status != NOVA_BREAKPOINT || nova_vm_get_call_frame_count(vm) != 1u)
        return 11;
    if (nova_vm_remove_breakpoint(vm, main_call->bytecode_start) != NOVA_OK)
        return 12;
    status = source_step_over(vm);
    if (status < 0 || nova_vm_get_call_frame_count(vm) != 1u) {
        fprintf(stderr, "step over depth mismatch status=%d depth=%u\n", status,
                nova_vm_get_call_frame_count(vm));
        return 13;
    }
    if (find_pc(nova_vm_get_pc(vm)) == NULL || find_pc(nova_vm_get_pc(vm))->line != 14u) {
        fprintf(stderr, "step over did not land on main line 14, pc=%u\n", nova_vm_get_pc(vm));
        return 14;
    }
    value = nova_vm_get_register(vm, main_result->register_index);
    if (value != 13u) {
        fprintf(stderr, "step over result mismatch: %u\n", value);
        return 15;
    }
    nova_vm_destroy(vm);
    /* Step Out: pause in middle, then leave the frame and return to main. */
    vm = new_vm(&result);
    if (vm == NULL)
        return 16;
    if (nova_vm_add_breakpoint(vm, middle_return->bytecode_start) != NOVA_OK)
        return 17;
    status = nova_vm_run(vm, STEP_BUDGET);

    if (status != NOVA_BREAKPOINT || nova_vm_get_call_frame_count(vm) != 2u)
        return 18;
    if (nova_vm_remove_breakpoint(vm, middle_return->bytecode_start) != NOVA_OK)
        return 19;
    status = source_step_out(vm);
    if (status < 0 || nova_vm_get_call_frame_count(vm) != 1u) {
        fprintf(stderr, "step out depth mismatch status=%d depth=%u\n", status,
                nova_vm_get_call_frame_count(vm));
        return 20;
    }
    if (find_pc(nova_vm_get_pc(vm)) == NULL ||
        find_pc(nova_vm_get_pc(vm))->function_index != (uint32_t)main_index) {
        fprintf(stderr, "step out did not return to main source, pc=%u\n", nova_vm_get_pc(vm));
        return 21;
    }
    nova_vm_destroy(vm);
    printf("DEBUG STEPS PASS frames=3 middle.y=6 main.start=5 step_over_result=13 step_out_depth=1 "
           "call_size=%u\n",
           CALL_INSTRUCTION_SIZE);
    return 0;
}
