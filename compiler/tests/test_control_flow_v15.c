#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
#include <string.h>
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static NovaDebugGlobal globals[NOVA_COMPILER_MAX_GLOBALS];
static NovaSymbolReference references[NOVA_COMPILER_MAX_REFERENCES];
static NovaIncludeDependency includes[NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES];
static NovaDebugString debug_strings[NOVA_COMPILER_MAX_DEBUG_STRINGS];
static int function_index(const char *name, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++)
        if (strcmp(functions[i].name, name) == 0)
            return (int)i;
    return -1;
}
static int expect_error(const char *source, int32_t expected) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    int32_t status =
        nova_compile_c(source, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic);
    if (status != expected) {
        fprintf(stderr, "expected error %d got %d @ %u:%u %s\n", expected, status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 0;
    }
    return 1;
}
static const NovaSourceMapEntry *find_line(uint32_t function_index, uint32_t line, uint32_t kind,
                                           uint32_t map_count) {
    uint32_t i;
    const NovaSourceMapEntry *best = NULL;
    uint32_t best_span = 0xFFFFFFFFu;
    for (i = 0u; i < map_count; i++) {
        uint32_t span;
        if (source_map[i].function_index != function_index || source_map[i].line != line)
            continue;
        if (kind != 0u && source_map[i].statement_kind != kind)
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
static const NovaSourceMapEntry *find_pc(uint32_t pc, uint32_t map_count) {
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
static int32_t source_step_into(NovaVm *vm, uint32_t map_count) {
    const NovaSourceMapEntry *start = find_pc(nova_vm_get_pc(vm), map_count);
    uint32_t start_depth = nova_vm_get_call_frame_count(vm);
    uint32_t i;
    int32_t status = NOVA_OK;
    for (i = 0u; i < 100000u; i++) {
        const NovaSourceMapEntry *current;
        uint32_t depth;
        status = nova_vm_step(vm);
        if (status == NOVA_HALTED || status < 0)
            return status;
        depth = nova_vm_get_call_frame_count(vm);
        if (depth != start_depth)
            return status;
        current = find_pc(nova_vm_get_pc(vm), map_count);
        if (current != NULL &&
            (start == NULL || current->line != start->line || current->file_id != start->file_id ||
             current->function_index != start->function_index))
            return status;
    }
    return status;
}
static int32_t source_step_over(NovaVm *vm, uint32_t map_count) {
    const NovaSourceMapEntry *start = find_pc(nova_vm_get_pc(vm), map_count);
    uint32_t start_depth = nova_vm_get_call_frame_count(vm);
    uint32_t i;
    int32_t status = NOVA_OK;
    for (i = 0u; i < 100000u; i++) {
        const NovaSourceMapEntry *current;
        uint32_t depth;
        status = nova_vm_step(vm);
        if (status == NOVA_HALTED || status < 0)
            return status;
        depth = nova_vm_get_call_frame_count(vm);
        if (depth > start_depth)
            continue;
        if (depth < start_depth)
            return status;
        current = find_pc(nova_vm_get_pc(vm), map_count);
        if (current != NULL &&
            (start == NULL || current->line != start->line || current->file_id != start->file_id ||
             current->function_index != start->function_index))
            return status;
    }
    return status;
}
static int32_t source_step_out(NovaVm *vm) {
    uint32_t start_depth = nova_vm_get_call_frame_count(vm);
    uint32_t i;
    int32_t status = NOVA_OK;
    if (start_depth == 0u)
        return NOVA_ERR_ARGUMENT;
    for (i = 0u; i < 100000u; i++) {
        status = nova_vm_step(vm);
        if (status == NOVA_HALTED || status < 0)
            return status;
        if (nova_vm_get_call_frame_count(vm) < start_depth)
            return status;
    }
    return status;
}
int main(void) {
    static const char *source = "int side = 0;\n"
                                "int tick(void) { side += 1; return 1; }\n"
                                "int main(void) {\n"
                                " int sum = 0;\n"

                                " int i = 0;\n"
                                " int x = 1;\n"
                                " int a[3] = {1, 2, 3};\n"
                                " int* q = a;\n"
                                " for (i = 0; i < 8; i++) {\n"
                                " if (i == 2) continue;\n"
                                " if (i == 6) break;\n"
                                " sum += i;\n"
                                " }\n"
                                " int old = x++;\n"
                                " sum += ++x;\n"
                                " x += 4;\n"
                                " x -= 1;\n"
                                " q++;\n"
                                " sum += *q;\n"
                                " --q;\n"
                                " sum += *q;\n"
                                " q += 2;\n"
                                " sum += *q;\n"
                                " q -= 1;\n"
                                " sum += *q;\n"
                                " if (0 && tick()) sum += 100;\n"
                                " if (1 || tick()) sum += 10;\n"
                                " if (1 && tick()) sum += 1;\n"
                                " if (0 || tick()) sum += 1;\n"
                                " for (int j = 0; j < 3; ++j) {\n"
                                " int box[1] = {j};\n"
                                " if (j == 1) continue;\n"
                                " sum += box[0];\n"
                                " }\n"
                                " putchar('C');\n"
                                " return sum + old + x + side;\n"
                                "}\n";
    NovaTranslationUnit unit;
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t map_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    uint32_t global_count = 0u;
    uint32_t reference_count = 0u;
    uint32_t include_count = 0u;
    uint32_t string_count = 0u;
    uint32_t for_count = 0u;
    uint32_t break_count = 0u;
    uint32_t continue_count = 0u;
    int main_fn;
    const NovaSourceMapEntry *continue_entry;
    const NovaSourceMapEntry *short_circuit_entry;
    const NovaSourceMapEntry *second_for_entry;
    const NovaDebugSymbol *j_symbol = NULL;
    const NovaDebugSymbol *box_symbol = NULL;
    uint32_t i;
    NovaVm *vm;
    NovaHeapStats heap;
    NovaEvent event;
    char console[8];
    uint32_t console_count = 0u;
    int32_t status;

    unit.name = "main.c";
    unit.source = source;
    status = nova_compile_project_debug_ex3(
        &unit, 1u, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic, source_map,
        NOVA_COMPILER_MAX_SOURCE_MAP, &map_count, symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS,
        &symbol_count, functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count, globals,
        NOVA_COMPILER_MAX_GLOBALS, &global_count, references, NOVA_COMPILER_MAX_REFERENCES,
        &reference_count, includes, NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES, &include_count,
        debug_strings, NOVA_COMPILER_MAX_DEBUG_STRINGS, &string_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "Vol.15 compile failed %d file=%u @ %u:%u %s\n", status, diagnostic.file_id,
                diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    for (i = 0u; i < map_count; i++) {
        if (source_map[i].statement_kind == NOVAC_SOURCE_FOR)
            for_count++;
        else if (source_map[i].statement_kind == NOVAC_SOURCE_BREAK)
            break_count++;
        else if (source_map[i].statement_kind == NOVAC_SOURCE_CONTINUE)
            continue_count++;
    }
    if (for_count != 2u || break_count != 1u || continue_count != 2u) {
        fprintf(stderr, "source-map loop kinds mismatch for=%u break=%u continue=%u maps=%u\n",
                for_count, break_count, continue_count, map_count);
        return 2;
    }
    main_fn = function_index("main", function_count);
    if (main_fn < 0)
        return 3;
    for (i = 0u; i < symbol_count; i++) {
        if (symbols[i].function_index != (uint32_t)main_fn)
            continue;
        if (strcmp(symbols[i].name, "j") == 0)
            j_symbol = &symbols[i];
        if (strcmp(symbols[i].name, "box") == 0 && symbols[i].type == NOVAC_DEBUG_ARRAY)
            box_symbol = &symbols[i];
    }
    if (j_symbol == NULL || box_symbol == NULL || j_symbol->scope_depth != 2u ||
        box_symbol->scope_depth != 3u || j_symbol->lifetime_start >= box_symbol->lifetime_start ||
        box_symbol->lifetime_end <= box_symbol->lifetime_start ||
        j_symbol->lifetime_end <= box_symbol->lifetime_end) {
        fprintf(stderr, "for-scope lifetime mismatch j=[%u,%u)d%u box=[%u,%u)d%u\n",
                j_symbol ? j_symbol->lifetime_start : 0u, j_symbol ? j_symbol->lifetime_end : 0u,
                j_symbol ? j_symbol->scope_depth : 0u, box_symbol ? box_symbol->lifetime_start : 0u,
                box_symbol ? box_symbol->lifetime_end : 0u,
                box_symbol ? box_symbol->scope_depth : 0u);
        return 4;
    }
    continue_entry = find_line((uint32_t)main_fn, 32u, NOVAC_SOURCE_CONTINUE, map_count);
    short_circuit_entry = find_line((uint32_t)main_fn, 27u, NOVAC_SOURCE_IF, map_count);
    second_for_entry = find_line((uint32_t)main_fn, 30u, NOVAC_SOURCE_FOR, map_count);
    if (continue_entry == NULL || short_circuit_entry == NULL || second_for_entry == NULL)
        return 5;
    /* Step Into across continue must run the jump cleanup and leave box out of scope/storage. */
    vm = nova_vm_create();
    if (vm == NULL)
        return 6;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 7;
    if (nova_vm_add_breakpoint(vm, continue_entry->bytecode_start) != NOVA_OK)
        return 8;
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_BREAKPOINT)
        return 9;

    if (nova_vm_get_heap_stats(vm, &heap) != NOVA_OK || heap.allocation_count != 2u) {
        fprintf(stderr, "expected a+box allocations before continue, got %u\n",
                heap.allocation_count);
        return 10;
    }
    if (nova_vm_remove_breakpoint(vm, continue_entry->bytecode_start) != NOVA_OK)
        return 11;
    status = source_step_into(vm, map_count);
    if (status < 0 || find_pc(nova_vm_get_pc(vm), map_count) == NULL ||
        find_pc(nova_vm_get_pc(vm), map_count)->line == 32u)
        return 12;
    if (nova_vm_get_heap_stats(vm, &heap) != NOVA_OK || heap.allocation_count != 1u) {
        fprintf(stderr, "continue cleanup did not release box, alloc=%u\n", heap.allocation_count);
        return 13;
    }
    nova_vm_destroy(vm);
    /* Step Over on 1 || tick() must skip the callee and preserve side==0. */
    vm = nova_vm_create();
    if (vm == NULL)
        return 14;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 15;
    if (nova_vm_add_breakpoint(vm, short_circuit_entry->bytecode_start) != NOVA_OK)
        return 16;
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_BREAKPOINT || nova_vm_get_call_frame_count(vm) != 1u)
        return 17;
    if (nova_vm_remove_breakpoint(vm, short_circuit_entry->bytecode_start) != NOVA_OK)
        return 18;
    status = source_step_over(vm, map_count);
    if (status < 0 || nova_vm_get_call_frame_count(vm) != 1u)
        return 19;
    {
        uint8_t side_bytes[4];
        uint32_t side_value;
        if (global_count == 0u ||
            nova_vm_read_memory(vm, globals[0].address, side_bytes, 4u) != NOVA_OK)
            return 20;
        side_value = (uint32_t)side_bytes[0] | ((uint32_t)side_bytes[1] << 8u) |
                     ((uint32_t)side_bytes[2] << 16u) | ((uint32_t)side_bytes[3] << 24u);
        if (side_value != 0u) {
            fprintf(stderr, "short-circuit Step Over called tick, side=%u\n", side_value);
            return 21;
        }
    }
    nova_vm_destroy(vm);
    /* Step Out from the second for-loop must cross break/continue edges without corrupting frame
     * depth. */
    vm = nova_vm_create();
    if (vm == NULL)
        return 22;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 23;
    if (nova_vm_add_breakpoint(vm, second_for_entry->bytecode_start) != NOVA_OK)
        return 24;
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_BREAKPOINT || nova_vm_get_call_frame_count(vm) != 1u)
        return 25;
    if (nova_vm_remove_breakpoint(vm, second_for_entry->bytecode_start) != NOVA_OK)
        return 26;
    status = source_step_out(vm);
    if (status < 0 || nova_vm_get_call_frame_count(vm) != 0u ||
        nova_vm_get_register(vm, 0u) != 47u) {
        fprintf(stderr, "Step Out across loop failed status=%d depth=%u r0=%u\n", status,
                nova_vm_get_call_frame_count(vm), nova_vm_get_register(vm, 0u));
        return 27;
    }
    nova_vm_destroy(vm);
    vm = nova_vm_create();
    if (vm == NULL)
        return 5;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 6;
    }
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 0u) != 47u) {
        fprintf(stderr, "Vol.15 runtime mismatch status=%d r0=%u bytes=%u\n", status,
                nova_vm_get_register(vm, 0u), result.bytecode_size);
        nova_vm_destroy(vm);
        return 7;
    }

    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR && console_count + 1u < sizeof(console))
            console[console_count++] = (char)(event.value & 0xFFu);
    }
    console[console_count] = '\0';
    if (strcmp(console, "C") != 0) {
        fprintf(stderr, "console mismatch '%s'\n", console);
        nova_vm_destroy(vm);
        return 8;
    }
    if (nova_vm_get_heap_stats(vm, &heap) != NOVA_OK || heap.allocation_count != 0u ||
        heap.block_count != 1u) {
        fprintf(stderr, "loop cleanup heap mismatch alloc=%u blocks=%u used=%u\n",
                heap.allocation_count, heap.block_count, heap.used_payload_bytes);
        nova_vm_destroy(vm);
        return 9;
    }
    nova_vm_destroy(vm);
    if (!expect_error("int main(void) { break; return 0; }\n", NOVAC_ERR_PARSE))
        return 10;
    if (!expect_error("int main(void) { continue; return 0; }\n", NOVAC_ERR_PARSE))
        return 11;
    if (!expect_error("int main(void) { int a[2]={1,2}; int* p=a; p += p; return 0; }\n",
                      NOVAC_ERR_INVALID_POINTER))
        return 12;
    if (!expect_error("int main(void) { int a[2]={1,2}; a++; return 0; }\n", NOVAC_ERR_UNSUPPORTED))
        return 13;
    printf("VOL15 CONTROL PASS bytes=%u maps=%u for=%u break=%u continue=%u r0=47 side=2 console=C "
           "heap_blocks=1 step_into/over/out=PASS\n",
           result.bytecode_size, map_count, for_count, break_count, continue_count);
    return 0;
}
