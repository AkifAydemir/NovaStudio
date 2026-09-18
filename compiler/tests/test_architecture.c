#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdint.h>
#include <stdio.h>
static uint32_t read_u32(NovaVm *vm, uint32_t address) {
    uint8_t bytes[4];
    if (nova_vm_read_memory(vm, address, bytes, 4u) != NOVA_OK)
        return 0xFFFFFFFFu;
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) | ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}
int main(void) {
    const NovaTranslationUnit units[] = {{"src/main.c", "int add(int a, int b);\n"
                                                        "extern int counter;\n"
                                                        "int main(void) {\n"
                                                        " return add(3, 4) + counter;\n"
                                                        "}\n"},
                                         {"src/math.c", "extern int counter;\n"
                                                        "int add(int a, int b) {\n"
                                                        " counter = counter + 1;\n"
                                                        " return a + b;\n"
                                                        "}\n"},
                                         {"src/state.c", "int counter = 5;\n"}};
    uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    NovaSourceMapEntry source_map[128];
    NovaDebugSymbol symbols[128];
    NovaDebugFunction functions[16];
    NovaDebugGlobal globals[16];
    NovaSymbolReference refs[128];
    uint32_t source_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    uint32_t global_count = 0u;
    uint32_t ref_count = 0u;
    uint32_t call_refs = 0u;
    uint32_t read_refs = 0u;
    uint32_t write_refs = 0u;
    uint32_t i;
    int32_t status;
    uint32_t good_bytecode_size = 0u;
    NovaVm *vm;
    status = nova_compile_project_debug_ex(units, 3u, bytecode, (uint32_t)sizeof(bytecode), &result,
                                           &diagnostic, source_map, 128u, &source_count, symbols,
                                           128u, &symbol_count, functions, 16u, &function_count,
                                           globals, 16u, &global_count, refs, 128u, &ref_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "compile failed: %d file=%u %u:%u %s\n", status, diagnostic.file_id,
                diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    good_bytecode_size = result.bytecode_size;
    if (function_count != 2u || global_count != 1u) {
        fprintf(stderr, "unexpected metadata counts funcs=%u globals=%u\n", function_count,
                global_count);
        return 2;
    }
    if (globals[0].address != NOVA_COMPILER_GLOBAL_BASE || globals[0].initial_value != 5u ||
        globals[0].file_id != 2u) {
        fprintf(stderr, "bad global metadata addr=0x%08X init=%u file=%u\n", globals[0].address,
                globals[0].initial_value, globals[0].file_id);
        return 3;
    }
    for (i = 0u; i < ref_count; i++) {
        if (refs[i].kind == NOVAC_REF_FUNCTION_CALL)
            call_refs++;
        else if (refs[i].kind == NOVAC_REF_GLOBAL_READ)
            read_refs++;
        else if (refs[i].kind == NOVAC_REF_GLOBAL_WRITE)
            write_refs++;
    }
    if (call_refs != 1u || read_refs != 2u || write_refs != 1u) {
        fprintf(stderr, "unexpected refs total=%u call=%u read=%u write=%u\n", ref_count, call_refs,
                read_refs, write_refs);
        return 4;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 5;
    if (nova_vm_reset(vm) != NOVA_OK ||
        nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 6;
    }
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 0u) != 13u) {
        fprintf(stderr, "runtime status=%d r0=%u\n", status, nova_vm_get_register(vm, 0u));
        nova_vm_destroy(vm);
        return 7;
    }
    if (read_u32(vm, NOVA_COMPILER_GLOBAL_BASE) != 6u) {
        fprintf(stderr, "global runtime value=%u\n", read_u32(vm, NOVA_COMPILER_GLOBAL_BASE));
        nova_vm_destroy(vm);
        return 8;
    }
    nova_vm_destroy(vm);
    {
        const NovaTranslationUnit bad_units[] = {
            {"main.c", "int add(int a); int main(void) { return add(1); }"},
            {"math.c", "int add(int a, int b) { return a + b; }"}};
        status = nova_compile_project(bad_units, 2u, bytecode, (uint32_t)sizeof(bytecode), &result,
                                      &diagnostic);
        if (status != NOVAC_ERR_CONFLICTING_DECLARATION || diagnostic.file_id != 1u) {
            fprintf(stderr, "prototype mismatch was not rejected: status=%d file=%u\n", status,
                    diagnostic.file_id);
            return 9;
        }
    }
    printf("vol10 architecture: bytes=%u funcs=%u globals=%u refs=%u r0=13 counter=6 PASS\n",
           good_bytecode_size, function_count, global_count, ref_count);
    return 0;
}
