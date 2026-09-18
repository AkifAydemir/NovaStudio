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
static const NovaSourceMapEntry *find_line(uint32_t function_index_value, uint32_t line,
                                           uint32_t kind, uint32_t map_count) {
    uint32_t i;
    const NovaSourceMapEntry *best = NULL;
    uint32_t best_span = 0xFFFFFFFFu;
    for (i = 0u; i < map_count; i++) {
        uint32_t span;
        if (source_map[i].function_index != function_index_value || source_map[i].line != line)
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
static int expect_error(const char *source, int32_t expected) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    int32_t status =
        nova_compile_c(source, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic);
    if (status != expected) {
        fprintf(stderr, "expected %d got %d @ %u:%u %s\n", expected, status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 0;
    }
    return 1;
}
static const NovaDebugGlobal *find_global(const char *name, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++)
        if (strcmp(globals[i].name, name) == 0)
            return &globals[i];
    return NULL;
}

int main(void) {
    static const char *source =
        "enum Mode { MODE_ZERO, MODE_A = 1 << 2, MODE_B = MODE_A | 3, MODE_C };\n"
        "int seed = (MODE_B << 1) ^ 3;\n"
        "int calls = 0;\n"
        "int bump(void) { calls += 1; return 9; }\n"
        "int main(void) {\n"
        " int result = 1 ? seed : bump();\n"
        " result += 0 ? bump() : 2;\n"
        " result += 240 & 60;\n"
        " result += 1 << 5;\n"
        " result += 64 >> 2;\n"
        " result += (5 ^ 3) | 8;\n"
        " result += (~0) & 3;\n"
        " switch (MODE_B) {\n"
        " case MODE_A:\n"
        " result += 100;\n"
        " break;\n"
        " case MODE_B:\n"
        " { int box[2] = {4, 5}; result += box[1]; }\n"
        " case MODE_C:\n"
        " result += 7;\n"
        " break;\n"
        " default:\n"
        " result += 1000;\n"
        " }\n"
        " switch (MODE_ZERO) {\n"
        " case 1: result += 100; break;\n"
        " default: result += 3;\n"
        " }\n"
        " for (int i = 0; i < 3; i++) {\n"
        " switch (i) {\n"
        " case 1: { int temp[1] = {9}; continue; }\n"
        " default: result += 1;\n"
        " }\n"
        " result += 2;\n"
        " }\n"
        " putchar('S');\n"
        " return result + calls;\n"
        "}\n";
    NovaTranslationUnit unit;
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t map_count = 0u, symbol_count = 0u, function_count = 0u, global_count = 0u;
    uint32_t reference_count = 0u, include_count = 0u, string_count = 0u;
    uint32_t switch_count = 0u, case_count = 0u, default_count = 0u, i;
    const NovaDebugGlobal *seed;
    const NovaDebugGlobal *calls;
    const NovaSourceMapEntry *step_entry;
    const NovaSourceMapEntry *current_map;
    int main_index;
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
        fprintf(stderr, "Vol.16 compile failed %d @ %u:%u %s\n", status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 1;
    }
    seed = find_global("seed", global_count);
    if (seed == NULL || seed->initial_value != 13u) {
        fprintf(stderr, "constant global mismatch seed=%u\n", seed ? seed->initial_value : 0u);
        return 2;
    }
    for (i = 0u; i < map_count; i++) {
        if (source_map[i].statement_kind == NOVAC_SOURCE_SWITCH)
            switch_count++;
        else if (source_map[i].statement_kind == NOVAC_SOURCE_CASE)
            case_count++;
        else if (source_map[i].statement_kind == NOVAC_SOURCE_DEFAULT)
            default_count++;
    }
    if (switch_count != 3u || case_count != 5u || default_count != 3u) {
        fprintf(stderr, "source-map mismatch switch=%u case=%u default=%u maps=%u\n", switch_count,
                case_count, default_count, map_count);
        return 3;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 4;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 5;
    status = nova_vm_run(vm, 200000u);
    if (status != NOVA_HALTED) {
        fprintf(stderr, "runtime status=%d pc=%u\n", status, nova_vm_get_pc(vm));
        return 6;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR && console_count < sizeof(console))
            console[console_count++] = (char)event.value;
    }
    if (console_count != 1u || console[0] != 'S' || nova_vm_get_register(vm, 0u) != 149u) {
        fprintf(stderr, "runtime mismatch r0=%u console_count=%u first=%c\n",
                nova_vm_get_register(vm, 0u), console_count, console_count ? console[0] : '?');
        return 7;
    }
    if (nova_vm_get_heap_stats(vm, &heap) != NOVA_OK || heap.allocation_count != 0u ||
        heap.block_count != 1u) {
        fprintf(stderr, "switch cleanup leak alloc=%u blocks=%u\n", heap.allocation_count,
                heap.block_count);
        return 8;
    }
    nova_vm_destroy(vm);
    main_index = function_index("main", function_count);
    calls = find_global("calls", global_count);
    if (main_index < 0 || calls == NULL)
        return 9;
    /* Step Over must honor the ternary short-circuit and not enter bump(). */
    step_entry = find_line((uint32_t)main_index, 6u, NOVAC_SOURCE_VAR_DECL, map_count);

    if (step_entry == NULL)
        return 10;
    vm = nova_vm_create();
    if (vm == NULL)
        return 11;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK ||
        nova_vm_add_breakpoint(vm, step_entry->bytecode_start) != NOVA_OK ||
        nova_vm_run(vm, 200000u) != NOVA_BREAKPOINT)
        return 12;
    if (source_step_over(vm, map_count) < 0)
        return 13;
    {
        uint8_t calls_bytes[4];
        uint32_t calls_value;
        if (nova_vm_read_memory(vm, calls->address, calls_bytes, 4u) != NOVA_OK)
            return 14;
        calls_value = (uint32_t)calls_bytes[0] | ((uint32_t)calls_bytes[1] << 8u) |
                      ((uint32_t)calls_bytes[2] << 16u) | ((uint32_t)calls_bytes[3] << 24u);
        if (calls_value != 0u) {
            fprintf(stderr, "ternary step-over executed dead branch calls=%u\n", calls_value);
            return 14;
        }
    }
    nova_vm_destroy(vm);
    /* Step Into from switch dispatch must land on the selected MODE_B case. */
    step_entry = find_line((uint32_t)main_index, 13u, NOVAC_SOURCE_SWITCH, map_count);
    if (step_entry == NULL)
        return 15;
    vm = nova_vm_create();
    if (vm == NULL)
        return 16;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK ||
        nova_vm_add_breakpoint(vm, step_entry->bytecode_start) != NOVA_OK ||
        nova_vm_run(vm, 200000u) != NOVA_BREAKPOINT)
        return 17;
    if (source_step_into(vm, map_count) < 0)
        return 18;
    current_map = find_pc(nova_vm_get_pc(vm), map_count);
    if (current_map == NULL || (current_map->line != 17u && current_map->line != 18u)) {
        fprintf(stderr, "switch step-into landed at line=%u kind=%u pc=%u\n",
                current_map ? current_map->line : 0u,
                current_map ? current_map->statement_kind : 0u, nova_vm_get_pc(vm));
        return 19;
    }
    nova_vm_destroy(vm);
    /* Step Out from a selected case body must unwind main normally through switch control flow. */
    step_entry = find_line((uint32_t)main_index, 18u, 0u, map_count);
    if (step_entry == NULL)
        return 20;
    vm = nova_vm_create();
    if (vm == NULL)
        return 21;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK ||
        nova_vm_add_breakpoint(vm, step_entry->bytecode_start) != NOVA_OK ||
        nova_vm_run(vm, 200000u) != NOVA_BREAKPOINT)
        return 22;
    if (source_step_out(vm) < 0 || nova_vm_get_call_frame_count(vm) != 0u)
        return 23;
    nova_vm_destroy(vm);
    if (!expect_error("int main(void){ switch(1){ case 1: break; case 1: break; } return 0; }\n",
                      NOVAC_ERR_CONFLICTING_DECLARATION))
        return 9;
    if (!expect_error("int x=1; int main(void){ switch(1){ case x: break; } return 0; }\n",
                      NOVAC_ERR_CONSTANT_EXPRESSION))
        return 10;
    if (!expect_error("int main(void){ switch(1){ default: break; default: break; } return 0; }\n",
                      NOVAC_ERR_CONFLICTING_DECLARATION))
        return 11;
    if (!expect_error("enum E { A = 1 / 0 }; int main(void){ return 0; }\n",
                      NOVAC_ERR_CONSTANT_EXPRESSION))
        return 27;
    if (!expect_error(
            "int main(void){ int a[1]; int* p=a; switch(p){ case 0: break; } return 0; }\n",
            NOVAC_ERR_UNSUPPORTED))
        return 28;
    {

        static const char *project_main = "#include \"flags.h\"\n"
                                          "int header_seed = F2 ^ 3;\n"
                                          "int main(void) { switch (F1) { case 8: return "
                                          "header_seed + F2; default: return 0; } }\n";
        static const char *project_header = "enum Flags { F0 = 2, F1 = F0 << 2, F2 = F1 | 1 };\n";
        NovaTranslationUnit project_units[2];
        NovaCompileResult project_result;
        NovaCompilerDiagnostic project_diagnostic;
        NovaVm *project_vm;
        project_units[0].name = "main.c";
        project_units[0].source = project_main;
        project_units[1].name = "flags.h";
        project_units[1].source = project_header;
        status = nova_compile_project(project_units, 2u, bytecode, (uint32_t)sizeof(bytecode),
                                      &project_result, &project_diagnostic);
        if (status != NOVAC_OK) {
            fprintf(stderr, "header enum compile failed %d file=%u @ %u:%u %s\n", status,
                    project_diagnostic.file_id, project_diagnostic.line, project_diagnostic.column,
                    project_diagnostic.message);
            return 29;
        }
        project_vm = nova_vm_create();
        if (project_vm == NULL)
            return 30;
        if (nova_vm_load(project_vm, bytecode, project_result.bytecode_size, 0u) != NOVA_OK ||
            nova_vm_run(project_vm, 100000u) != NOVA_HALTED ||
            nova_vm_get_register(project_vm, 0u) != 19u) {
            fprintf(stderr, "header enum runtime mismatch r0=%u bytes=%u\n",
                    nova_vm_get_register(project_vm, 0u), project_result.bytecode_size);
            nova_vm_destroy(project_vm);
            return 31;
        }
        nova_vm_destroy(project_vm);
    }
    printf("VOL16 CONST/SWITCH PASS bytes=%u maps=%u switch=%u case=%u default=%u globals=%u "
           "r0=149 seed=13 console=S heap=1 header_enum=19 step_into/over/out=PASS\n",
           result.bytecode_size, map_count, switch_count, case_count, default_count, global_count);
    return 0;
}
