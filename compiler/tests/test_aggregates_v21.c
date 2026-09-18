#include "nova_compiler.h"
#include "nova_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static NovaDebugGlobal globals[NOVA_COMPILER_MAX_GLOBALS];
static NovaSymbolReference references[NOVA_COMPILER_MAX_REFERENCES];
static NovaIncludeDependency includes[NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES];
static NovaDebugString strings[NOVA_COMPILER_MAX_DEBUG_STRINGS];
static NovaDeclarationMetadata declarations[NOVA_COMPILER_MAX_DECLARATIONS];
static NovaMacroDefinition macros[NOVA_COMPILER_MAX_MACROS];

static int find_symbol(const char *name, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (strcmp(symbols[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static int expect_failure(const char *source) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    int32_t status =
        nova_compile_c(source, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic);
    if (status == NOVAC_OK) {
        fprintf(stderr, "expected Vol.21 negative case to fail\n");
        return 0;
    }
    return 1;
}

int main(void) {
    static const NovaTranslationUnit units[] = {
        {"src/main.c",
         "struct Inner { char tag; int value; };\n"
         "union Word { int whole; char byte; };\n"
         "struct Outer { struct Inner inner[2]; int nums[3]; union Word u; };\n"
         "struct Outer global = {\n"
         "  .inner = { [0] = { .tag = 'G', .value = 40 }, [1] = { .tag = 'H', .value = 1 } },\n"
         "  .nums = { [1] = 2, [2] = 3 },\n"
         "  .u = { .whole = 9 }\n"
         "};\n"
         "int main(void) {\n"
         "  struct Outer local = {\n"
         "    .inner = { [1] = { .tag = 'L', .value = 4 } },\n"
         "    .nums = { 1, 2, 3 },\n"
         "    .u = { .byte = 7 }\n"
         "  };\n"
         "  putchar(global.inner[0].tag);\n"
         "  return global.inner[0].value + global.nums[1] + global.u.byte\n"
         "       + local.inner[1].value + local.nums[2] + local.u.whole;\n"
         "}\n"}};
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t map_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    uint32_t global_count = 0u;
    uint32_t reference_count = 0u;
    uint32_t include_count = 0u;
    uint32_t string_count = 0u;
    uint32_t declaration_count = 0u;
    uint32_t macro_count = 0u;
    int32_t status;
    NovaVm *vm;
    NovaEvent event;
    char console = '\0';
    int nested_value;
    int nested_array;
    int union_child;

    status = nova_compile_project_debug_ex5(
        units, 1u, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic, source_map,
        NOVA_COMPILER_MAX_SOURCE_MAP, &map_count, symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS,
        &symbol_count, functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count, globals,
        NOVA_COMPILER_MAX_GLOBALS, &global_count, references, NOVA_COMPILER_MAX_REFERENCES,
        &reference_count, includes, NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES, &include_count, strings,
        NOVA_COMPILER_MAX_DEBUG_STRINGS, &string_count, declarations,
        NOVA_COMPILER_MAX_DECLARATIONS, &declaration_count, macros, NOVA_COMPILER_MAX_MACROS,
        &macro_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "Vol.21 compile failed %d file=%u @ %u:%u %s\n", status, diagnostic.file_id,
                diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    if (global_count != 1u || globals[0].byte_size != 26u || globals[0].element_count != 3u) {
        fprintf(stderr, "Vol.21 global layout mismatch count=%u bytes=%u elements=%u\n",
                global_count, global_count ? globals[0].byte_size : 0u,
                global_count ? globals[0].element_count : 0u);
        return 2;
    }
    nested_value = find_symbol("local.inner[1].value", symbol_count);
    nested_array = find_symbol("local.nums[2]", symbol_count);
    union_child = find_symbol("local.u.byte", symbol_count);
    if (nested_value < 0 || nested_array < 0 || union_child < 0 ||
        symbols[(uint32_t)union_child].type != NOVAC_DEBUG_CHAR) {
        fprintf(stderr,
                "Vol.21 recursive debug children missing value=%d array=%d union=%d symbols=%u\n",
                nested_value, nested_array, union_child, symbol_count);
        return 3;
    }

    vm = nova_vm_create();
    if (vm == NULL)
        return 4;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 5;
    status = nova_vm_run(vm, 300000u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 0u) != 65u) {
        fprintf(stderr, "Vol.21 runtime mismatch status=%d r0=%u\n", status,
                nova_vm_get_register(vm, 0u));
        return 6;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR)
            console = (char)event.value;
    }
    if (console != 'G') {
        fprintf(stderr, "Vol.21 console mismatch %c\n", console ? console : '?');
        return 7;
    }
    nova_vm_destroy(vm);

    if (!expect_failure("struct A { struct A self; }; int main(void){return 0;}\n"))
        return 8;
    if (!expect_failure(
            "union U { int a; int b; }; int main(void){ union U u={1,2}; return 0; }\n"))
        return 9;
    if (!expect_failure("struct S { int x; }; struct S g={.bad=1}; int main(void){return 0;}\n"))
        return 10;

    printf("VOL21 AGGREGATES PASS bytes=%u maps=%u symbols=%u globals=%u r0=65 console=G layout=26 "
           "nested_debug=1 designated=1 union=1\n",
           result.bytecode_size, map_count, symbol_count, global_count);
    return 0;
}
