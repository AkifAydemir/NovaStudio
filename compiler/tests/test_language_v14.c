#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(NovaDebugSymbol) == 104u, "Vol.14 debug-symbol ABI drift");
_Static_assert(sizeof(NovaDebugString) == 68u, "Vol.14 debug-string ABI drift");
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
int main(void) {
    static const char *source =
        "char marker = 'M';\n"
        "struct Pair { char tag; int value; };\n"
        "int echo(int x) {\n"
        " { int x = 9; if (x != 9) return 90; }\n"
        " return x;\n"
        "}\n"
        "int main(void) {\n"
        " int x = 1;\n"
        " char text[3] = \"hi\";\n"
        " int numbers[3] = {2, 3};\n"
        " struct Pair pair = {'Q', 7};\n"
        " char* p = \"OK\";\n"
        " { int x = 9; if (x != 9) return 91; }\n"
        " marker = 'N';\n"
        " putchar(p[0]);\n"
        " putchar(text[1]);\n"
        " return x + numbers[0] + numbers[1] + numbers[2] + pair.tag + pair.value + sizeof(char) + "
        "sizeof(pair) + p[1] + marker + echo(4);\n"
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
    int main_fn;
    const NovaDebugSymbol *outer_x = NULL;
    const NovaDebugSymbol *inner_x = NULL;
    uint32_t i;
    NovaVm *vm;
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
        fprintf(stderr, "Vol.14 compile failed %d file=%u @ %u:%u %s\n", status, diagnostic.file_id,
                diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    if (string_count != 2u || global_count != 1u) {
        fprintf(stderr, "metadata mismatch strings=%u globals=%u\n", string_count, global_count);
        return 2;
    }
    if (strcmp(debug_strings[0].preview, "hi") != 0 || debug_strings[0].byte_size != 3u ||
        strcmp(debug_strings[1].preview, "OK") != 0 || debug_strings[1].byte_size != 3u ||
        debug_strings[1].address != debug_strings[0].address + debug_strings[0].byte_size) {
        fprintf(stderr, "string metadata mismatch [%s,%u,0x%X] [%s,%u,0x%X]\n",
                debug_strings[0].preview, debug_strings[0].byte_size, debug_strings[0].address,
                debug_strings[1].preview, debug_strings[1].byte_size, debug_strings[1].address);
        return 3;
    }
    if (globals[0].byte_size != 1u || globals[0].address != NOVA_COMPILER_GLOBAL_BASE) {
        fprintf(stderr, "char global layout mismatch bytes=%u addr=0x%X\n", globals[0].byte_size,
                globals[0].address);
        return 4;
    }
    main_fn = function_index("main", function_count);
    if (main_fn < 0)
        return 5;
    for (i = 0u; i < symbol_count; i++) {
        if (symbols[i].function_index != (uint32_t)main_fn || strcmp(symbols[i].name, "x") != 0)
            continue;
        if (symbols[i].scope_depth == 1u)
            outer_x = &symbols[i];
        if (symbols[i].scope_depth == 2u)
            inner_x = &symbols[i];
    }
    if (outer_x == NULL || inner_x == NULL || outer_x->lifetime_start >= inner_x->lifetime_start ||

        inner_x->lifetime_end <= inner_x->lifetime_start ||
        outer_x->lifetime_end <= inner_x->lifetime_end) {
        fprintf(stderr, "shadow lifetime mismatch outer=[%u,%u) inner=[%u,%u)\n",
                outer_x ? outer_x->lifetime_start : 0u, outer_x ? outer_x->lifetime_end : 0u,
                inner_x ? inner_x->lifetime_start : 0u, inner_x ? inner_x->lifetime_end : 0u);
        return 6;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 7;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 8;
    }
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 0u) != 257u) {
        fprintf(stderr, "Vol.14 runtime mismatch status=%d r0=%u bytes=%u\n", status,
                nova_vm_get_register(vm, 0u), result.bytecode_size);
        nova_vm_destroy(vm);
        return 9;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR && console_count + 1u < sizeof(console))
            console[console_count++] = (char)(event.value & 0xFFu);
    }
    console[console_count] = '\0';
    if (strcmp(console, "Oi") != 0) {
        fprintf(stderr, "console mismatch '%s'\n", console);
        nova_vm_destroy(vm);
        return 10;
    }
    {
        uint8_t bytes[3];
        if (nova_vm_read_memory(vm, debug_strings[1].address, bytes, 3u) != NOVA_OK ||
            bytes[0] != 'O' || bytes[1] != 'K' || bytes[2] != 0u) {
            fprintf(stderr, "static string bytes mismatch\n");
            nova_vm_destroy(vm);
            return 11;
        }
        if (nova_vm_read_memory(vm, NOVA_COMPILER_GLOBAL_BASE, bytes, 1u) != NOVA_OK ||
            bytes[0] != 'N') {
            fprintf(stderr, "char global runtime mismatch value=%u\n", (unsigned)bytes[0]);
            nova_vm_destroy(vm);
            return 12;
        }
    }
    nova_vm_destroy(vm);
    if (!expect_error("int main(void) { int x=1; int x=2; return x; }\n", NOVAC_ERR_UNKNOWN_SYMBOL))
        return 13;
    if (!expect_error("int f(int x) { int x=2; return x; } int main(void){ return f(1); }\n",
                      NOVAC_ERR_UNKNOWN_SYMBOL))
        return 14;
    if (!expect_error("int main(void) { char s[2]=\"abc\"; return 0; }\n", NOVAC_ERR_ARRAY_BOUNDS))
        return 15;
    if (!expect_error("int main(void) { int a[2]={1,2,3}; return 0; }\n", NOVAC_ERR_ARRAY_BOUNDS))
        return 16;
    printf("VOL14 LANGUAGE PASS bytes=%u symbols=%u strings=%u globals=%u r0=257 console=Oi "
           "outer_scope=1 inner_scope=2\n",
           result.bytecode_size, symbol_count, string_count, global_count);
    return 0;
}
