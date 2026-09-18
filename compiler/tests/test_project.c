#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(NovaCompilerDiagnostic) == 208u, "Vol.9 diagnostic ABI drift");
_Static_assert(sizeof(NovaSourceMapEntry) == 28u, "Vol.9 source-map ABI drift");
_Static_assert(sizeof(NovaDebugSymbol) == 104u, "Vol.9 debug-symbol ABI drift");
_Static_assert(sizeof(NovaDebugFunction) == 76u, "Vol.9 debug-function ABI drift");
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static int function_index(const char *name, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (strcmp(functions[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}
static const NovaSourceMapEntry *find_map(uint32_t file_id, uint32_t line, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (source_map[i].file_id == file_id && source_map[i].line == line &&
            source_map[i].bytecode_end > source_map[i].bytecode_start)
            return &source_map[i];
    }
    return NULL;
}
int main(void) {
    static const char *main_source = "int add(int a, int b);\n"
                                     "int main(void) {\n"
                                     " struct Pair pair;\n"
                                     " pair.left = 5;\n"
                                     " pair.right = 8;\n"
                                     " putchar('P');\n"
                                     " return add(pair.left, pair.right);\n"
                                     "}\n";
    static const char *math_source = "int add(int a, int b) {\n"
                                     " return a + b;\n"
                                     "}\n";
    static const char *types_source = "struct Pair {\n"
                                      " int left;\n"
                                      " int right;\n"
                                      "};\n";
    static const char *bad_source = "int helper(void) { return 1; }\n"
                                    "int helper(void) { return 2; }\n";
    NovaTranslationUnit units[3];
    NovaTranslationUnit bad_units[2];
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t map_count = 0u;
    uint32_t symbol_count = 0u;

    uint32_t function_count = 0u;
    int main_fn;
    int add_fn;
    const NovaSourceMapEntry *math_return;
    NovaVm *vm;
    NovaEvent event;
    int32_t status;
    uint32_t compiled_size = 0u;
    char console = '\0';
    /* Deliberately place the struct definition last: Vol.9 must resolve project
    types independently of translation-unit order. */
    units[0].name = "src/main.c";
    units[0].source = main_source;
    units[1].name = "src/math.c";
    units[1].source = math_source;
    units[2].name = "src/types.c";
    units[2].source = types_source;
    status = nova_compile_project_debug(
        units, 3u, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic, source_map,
        NOVA_COMPILER_MAX_SOURCE_MAP, &map_count, symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS,
        &symbol_count, functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "project compile failed: %d file=%u @ %u:%u %s\n", status,
                diagnostic.file_id, diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    compiled_size = result.bytecode_size;
    if (function_count != 2u) {
        fprintf(stderr, "expected 2 project functions, got %u\n", function_count);
        return 2;
    }
    main_fn = function_index("main", function_count);
    add_fn = function_index("add", function_count);
    if (main_fn < 0 || add_fn < 0) {
        fprintf(stderr, "project functions missing\n");
        return 3;
    }
    if (functions[(uint32_t)main_fn].file_id != 0u || functions[(uint32_t)add_fn].file_id != 1u) {
        fprintf(stderr, "function file ids mismatch main=%u add=%u\n",
                functions[(uint32_t)main_fn].file_id, functions[(uint32_t)add_fn].file_id);
        return 4;
    }
    math_return = find_map(1u, 2u, map_count);
    if (math_return == NULL) {
        fprintf(stderr, "math.c source map was not file-aware\n");
        return 5;
    }
    if (symbol_count < 4u) {
        fprintf(stderr, "expected aggregate + parameter debug symbols, got %u\n", symbol_count);
        return 6;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 7;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 8;
    }
    if (nova_vm_add_breakpoint(vm, math_return->bytecode_start) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 9;
    }
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_BREAKPOINT || nova_vm_get_pc(vm) != math_return->bytecode_start) {
        fprintf(stderr, "file-aware breakpoint mismatch status=%d pc=%u expected=%u\n", status,
                nova_vm_get_pc(vm), math_return->bytecode_start);
        nova_vm_destroy(vm);
        return 10;
    }
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 0u) != 13u) {
        fprintf(stderr, "project runtime mismatch status=%d r0=%u\n", status,
                nova_vm_get_register(vm, 0u));
        nova_vm_destroy(vm);
        return 11;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR)
            console = (char)(event.value & 0xFFu);
    }
    nova_vm_destroy(vm);
    if (console != 'P') {
        fprintf(stderr, "project console mismatch: %c\n", console);
        return 12;
    }
    bad_units[0].name = "src/main.c";
    bad_units[0].source = "int main(void) { return helper(); }\n";
    bad_units[1].name = "src/duplicate.c";
    bad_units[1].source = bad_source;
    status = nova_compile_project(bad_units, 2u, bytecode, (uint32_t)sizeof(bytecode), &result,
                                  &diagnostic);
    if (status != NOVAC_ERR_DUPLICATE_FUNCTION || diagnostic.file_id != 1u) {
        fprintf(stderr, "expected file-aware duplicate function error, got %d file=%u @ %u:%u %s\n",
                status, diagnostic.file_id, diagnostic.line, diagnostic.column, diagnostic.message);
        return 13;
    }
    bad_units[0].name = "src/a.c";
    bad_units[0].source = "int main(void) { struct Missing item; return 0; }\n";
    bad_units[1].name = "src/b.c";
    bad_units[1].source = "int ok(void) { return 1; }\n";
    status = nova_compile_project(bad_units, 2u, bytecode, (uint32_t)sizeof(bytecode), &result,
                                  &diagnostic);
    if (status != NOVAC_ERR_UNKNOWN_TYPE || diagnostic.file_id != 0u) {
        fprintf(stderr, "expected unresolved project type error, got %d file=%u @ %u:%u %s\n",
                status, diagnostic.file_id, diagnostic.line, diagnostic.column, diagnostic.message);
        return 14;
    }
    printf("PROJECT PASS units=3 bytes=%u funcs=%u map=%u symbols=%u return=13 file_breakpoint=1 "
           "file_diag=1\n",
           compiled_size, function_count, map_count, symbol_count);
    return 0;
}
