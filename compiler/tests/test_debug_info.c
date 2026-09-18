#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
_Static_assert(sizeof(NovaSourceMapEntry) == 28u, "source-map ABI drift");
_Static_assert(sizeof(NovaDebugSymbol) == 104u, "debug-symbol ABI drift");
_Static_assert(sizeof(NovaDebugFunction) == 76u, "debug-function ABI drift");
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static const char *source = "int main(void) {\n"
                            " int n = 3;\n"
                            " int sum = 0;\n"
                            " while (n > 0) {\n"
                            " sum = sum + n;\n"
                            " n = n - 1;\n"
                            " }\n"
                            " putchar('D');\n"
                            " return sum;\n"
                            "}\n";
static const NovaSourceMapEntry *find_line(uint32_t line) {
    uint32_t i;
    for (i = 0u; i < NOVA_COMPILER_MAX_SOURCE_MAP; i++) {
        if (source_map[i].line == line &&
            source_map[i].bytecode_end > source_map[i].bytecode_start) {
            return &source_map[i];
        }
    }
    return NULL;
}
int main(void) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t source_map_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    const NovaSourceMapEntry *putchar_line;
    NovaVm *vm;
    int32_t status;
    status = nova_compile_c_debug(source, bytecode, (uint32_t)sizeof(bytecode), &result,
                                  &diagnostic, source_map, NOVA_COMPILER_MAX_SOURCE_MAP,
                                  &source_map_count, symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS,
                                  &symbol_count, functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS,

                                  &function_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "compile failed: %d @ %u:%u %s\n", status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 1;
    }
    if (source_map_count < 6u || symbol_count != 2u || function_count != 1u) {
        fprintf(stderr, "unexpected debug counts: map=%u symbols=%u functions=%u\n",
                source_map_count, symbol_count, function_count);
        return 2;
    }
    if (symbols[0].register_index != 1u || symbols[1].register_index != 2u) {
        fprintf(stderr, "unexpected register bindings: %u %u\n", symbols[0].register_index,
                symbols[1].register_index);
        return 3;
    }
    if (functions[0].bytecode_start != 6u || functions[0].bytecode_end > result.bytecode_size ||
        functions[0].parameter_count != 0u || functions[0].local_count != 2u) {
        fprintf(stderr, "function range mismatch: start=%u end=%u size=%u\n",
                functions[0].bytecode_start, functions[0].bytecode_end, result.bytecode_size);
        return 4;
    }
    putchar_line = find_line(8u);
    if (putchar_line == NULL) {
        fprintf(stderr, "no source map for putchar line\n");
        return 5;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 6;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 7;
    }
    if (nova_vm_add_breakpoint(vm, putchar_line->bytecode_start) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 8;
    }
    status = nova_vm_run(vm, 10000u);
    if (status != NOVA_BREAKPOINT || nova_vm_get_pc(vm) != putchar_line->bytecode_start) {
        fprintf(stderr, "source breakpoint mismatch: status=%d pc=%u expected=%u\n", status,
                nova_vm_get_pc(vm), putchar_line->bytecode_start);
        nova_vm_destroy(vm);
        return 9;
    }
    /* At this point n==0 and sum==6; Vol.9 scalar watches still read locals directly from their
     * bound VM registers. */
    if (nova_vm_get_register(vm, symbols[0].register_index) != 0u ||
        nova_vm_get_register(vm, symbols[1].register_index) != 6u) {
        fprintf(stderr, "watch/register mismatch: n=%u sum=%u\n",
                nova_vm_get_register(vm, symbols[0].register_index),
                nova_vm_get_register(vm, symbols[1].register_index),
                nova_vm_get_call_frame_count(vm));
        nova_vm_destroy(vm);
        return 10;
    }
    if (nova_vm_get_call_frame_count(vm) != 1u) {
        fprintf(stderr, "expected main frame at source breakpoint\n");
        nova_vm_destroy(vm);
        return 11;
    }

    printf("NovaC debug map Vol.9: entries=%u symbols=%u breakpoint_line=8 address=%u sum=%u "
           "frames=%u\n",
           source_map_count, symbol_count, putchar_line->bytecode_start,
           nova_vm_get_register(vm, symbols[1].register_index), nova_vm_get_call_frame_count(vm));
    nova_vm_destroy(vm);
    return 0;
}
