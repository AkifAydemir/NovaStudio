#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(NovaIncludeDependency) == 16u, "Vol.12 include dependency ABI drift");
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[128];
static NovaDebugSymbol symbols[128];
static NovaDebugFunction functions[16];
static NovaDebugGlobal globals[16];
static NovaSymbolReference references[128];
static NovaIncludeDependency includes[64];
static int has_include(uint32_t from, uint32_t to, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (includes[i].from_file_id == from && includes[i].to_file_id == to)
            return 1;
    }
    return 0;
}
int main(void) {
    const NovaTranslationUnit units[] = {
        {"src/main.c", "#include \"../include/api.h\"\n"
                       "int main(void) {\n"
                       " struct Pair pair;\n"
                       " pair.left = 4;\n"
                       " pair.right = add(3, 5);\n"
                       " return pair.left + pair.right + bias;\n"
                       "}\n"},
        {"src/math.c", "#include \"../include/api.h\"\n"
                       "int add(int a, int b) { return a + b; }\n"
                       "int bias = 2;\n"},
        {"include/api.h", "#include \"types.h\"\n"
                          "int add(int a, int b);\n"
                          "extern int bias;\n"},
        {"include/types.h", "struct Pair {\n"
                            " int left;\n"
                            " int right;\n"
                            "};\n"},
        {"include/unused.h", "int illegal_header_definition(void) { return 99; }\n"}};
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t source_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    uint32_t global_count = 0u;
    uint32_t reference_count = 0u;
    uint32_t include_count = 0u;
    int32_t status;
    NovaVm *vm;

    uint32_t good_size;
    status = nova_compile_project_debug_ex2(
        units, 5u, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic, source_map, 128u,
        &source_count, symbols, 128u, &symbol_count, functions, 16u, &function_count, globals, 16u,
        &global_count, references, 128u, &reference_count, includes, 64u, &include_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "header project compile failed: %d file=%u %u:%u %s\n", status,
                diagnostic.file_id, diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    good_size = result.bytecode_size;
    if (include_count != 3u || !has_include(0u, 2u, include_count) ||
        !has_include(1u, 2u, include_count) || !has_include(2u, 3u, include_count)) {
        fprintf(stderr, "include graph mismatch count=%u\n", include_count);
        return 2;
    }
    if (function_count != 2u || global_count != 1u || strcmp(globals[0].name, "bias") != 0 ||
        globals[0].file_id != 1u) {
        fprintf(stderr, "semantic metadata mismatch funcs=%u globals=%u global_name=%s file=%u\n",
                function_count, global_count, global_count ? globals[0].name : "<none>",
                global_count ? globals[0].file_id : 999u);
        return 3;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 4;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 5;
    }
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 0u) != 14u) {
        fprintf(stderr, "header runtime mismatch status=%d r0=%u\n", status,
                nova_vm_get_register(vm, 0u));
        nova_vm_destroy(vm);
        return 6;
    }
    nova_vm_destroy(vm);
    /* A project-owned header that is not reachable from a .c include graph is
    not parsed into the semantic program. This keeps include identity real. */
    {
        const NovaTranslationUnit inactive_header[] = {
            {"main.c", "int main(void) { return 7; }\n"},
            {"unused.h", "int forbidden(void) { return 1; }\n"}};
        status = nova_compile_project(inactive_header, 2u, bytecode, (uint32_t)sizeof(bytecode),
                                      &result, &diagnostic);
        if (status != NOVAC_OK) {
            fprintf(stderr, "inactive header leaked into semantics: %d file=%u %s\n", status,
                    diagnostic.file_id, diagnostic.message);
            return 7;
        }
    }
    {
        const NovaTranslationUnit commented[] = {

            {"main.c", "/*\n#include \"missing.h\"\n*/\nint main(void) { return 9; }\n"}};
        status = nova_compile_project(commented, 1u, bytecode, (uint32_t)sizeof(bytecode), &result,
                                      &diagnostic);
        if (status != NOVAC_OK) {
            fprintf(stderr, "commented include was treated as a directive: %d file=%u %s\n", status,
                    diagnostic.file_id, diagnostic.message);
            return 8;
        }
    }
    {
        const NovaTranslationUnit bad_header[] = {
            {"main.c", "#include \"bad.h\"\nint main(void) { return 0; }\n"},
            {"bad.h", "int forbidden(void) { return 1; }\n"}};
        status = nova_compile_project(bad_header, 2u, bytecode, (uint32_t)sizeof(bytecode), &result,
                                      &diagnostic);
        if (status != NOVAC_ERR_HEADER_DEFINITION || diagnostic.file_id != 1u) {
            fprintf(stderr, "header definition not rejected: %d file=%u %s\n", status,
                    diagnostic.file_id, diagnostic.message);
            return 8;
        }
    }
    {
        const NovaTranslationUnit missing[] = {
            {"src/main.c", "#include \"../include/missing.h\"\nint main(void) { return 0; }\n"}};
        status = nova_compile_project(missing, 1u, bytecode, (uint32_t)sizeof(bytecode), &result,
                                      &diagnostic);
        if (status != NOVAC_ERR_INCLUDE_NOT_FOUND || diagnostic.file_id != 0u ||
            diagnostic.line != 1u) {
            fprintf(stderr, "missing include not rejected: %d file=%u line=%u %s\n", status,
                    diagnostic.file_id, diagnostic.line, diagnostic.message);
            return 9;
        }
    }
    {
        const NovaTranslationUnit extern_mismatch[] = {
            {"main.c", "#include \"api.h\"\nint main(void) { return 0; }\n"},
            {"api.h", "extern int bias;\n"},
            {"state.c", "int* bias;\n"}};
        status = nova_compile_project(extern_mismatch, 3u, bytecode, (uint32_t)sizeof(bytecode),
                                      &result, &diagnostic);
        if (status != NOVAC_ERR_CONFLICTING_DECLARATION || diagnostic.file_id != 2u) {
            fprintf(stderr, "extern mismatch not rejected: %d file=%u %s\n", status,
                    diagnostic.file_id, diagnostic.message);
            return 10;
        }
    }
    {
        const NovaTranslationUnit cycle[] = {
            {"main.c", "#include \"a.h\"\nint main(void) { return 0; }\n"},
            {"a.h", "#include \"b.h\"\nint helper(void);\n"},
            {"b.h", "#include \"a.h\"\n"},
            {"helper.c", "int helper(void) { return 1; }\n"}};
        status = nova_compile_project(cycle, 4u, bytecode, (uint32_t)sizeof(bytecode), &result,
                                      &diagnostic);
        if (status != NOVAC_ERR_INCLUDE_CYCLE) {

            fprintf(stderr, "include cycle not rejected: %d file=%u %s\n", status,
                    diagnostic.file_id, diagnostic.message);
            return 11;
        }
    }
    printf("VOL12 HEADERS PASS bytes=%u includes=3 funcs=2 globals=1 r0=14 header_guard=1 "
           "missing=1 extern=1 cycle=1\n",
           good_size);
    return 0;
}
