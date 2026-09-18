#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(NovaMacroDefinition) == 264u, "Vol.20 macro metadata ABI drift");

static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[256];
static NovaDebugSymbol symbols[256];
static NovaDebugFunction functions[32];
static NovaDebugGlobal globals[32];
static NovaSymbolReference references[256];
static NovaIncludeDependency includes[64];
static NovaDebugString strings[64];
static NovaDeclarationMetadata declarations[128];
static NovaMacroDefinition macros[64];

static int find_macro(const char *name, uint32_t file_id, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (macros[i].file_id == file_id && strcmp(macros[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static int expect_failure(const NovaTranslationUnit *units, uint32_t unit_count, int32_t expected) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    int32_t status = nova_compile_project(units, unit_count, bytecode, (uint32_t)sizeof(bytecode),
                                          &result, &diagnostic);
    if (status != expected) {
        fprintf(stderr, "expected failure %d got %d file=%u %u:%u %s\n", expected, status,
                diagnostic.file_id, diagnostic.line, diagnostic.column, diagnostic.message);
        return 0;
    }
    return 1;
}

int main(void) {
    static const NovaTranslationUnit units[] = {
        {"src/main.c", "#define LOCAL 3\n"
                       "#include \"../include/config.h\"\n"
                       "#include \"../include/config.h\"\n"
                       "#if 0\n"
                       "#include \"../include/missing.h\"\n"
                       "#endif\n"
                       "#if FEATURE && (BASE == 10) && defined(CONFIG_H)\n"
                       "#define EXTRA 4\n"
                       "#else\n"
                       "#define EXTRA 99\n"
                       "#endif\n"
                       "#undef LOCAL\n"
                       "#ifndef LOCAL\n"
                       "#define LOCAL 5\n"
                       "#endif\n"
                       "int main(void) {\n"
                       " int v = SUM(BASE, EXTRA);\n"
                       " putchar(TEXT[0]);\n"
                       " return v + LOCAL + BIAS_NAME + MARK;\n"
                       "}\n"},
        {"src/state.c", "#include \"../include/config.h\"\n"
                        "int bias = 2;\n"},
        {"include/config.h", "#ifndef CONFIG_H\n"
                             "#define CONFIG_H\n"
                             "#define FEATURE 1\n"
                             "#define BASE 10\n"
                             "#define SUM(a, b) ((a) + (b))\n"
                             "#define TEXT \"P\"\n"
                             "#define MARK 'M'\n"
                             "#define BIAS_NAME bias\n"
                             "#ifdef FEATURE\n"
                             "extern int bias;\n"
                             "#else\n"
                             "extern int impossible;\n"
                             "#endif\n"
                             "#endif\n"}};
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t source_count = 0u;
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
    char console[8];
    uint32_t console_count = 0u;
    int sum_macro;
    int text_macro;
    int guard_macro;

    status = nova_compile_project_debug_ex5(
        units, 3u, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic, source_map, 256u,
        &source_count, symbols, 256u, &symbol_count, functions, 32u, &function_count, globals, 32u,
        &global_count, references, 256u, &reference_count, includes, 64u, &include_count, strings,
        64u, &string_count, declarations, 128u, &declaration_count, macros, 64u, &macro_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "Vol.20 compile failed: %d file=%u %u:%u %s\n", status, diagnostic.file_id,
                diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    if (include_count != 3u) {
        fprintf(stderr, "active include metadata mismatch: %u\n", include_count);
        return 2;
    }
    if (macro_count != 10u) {
        fprintf(stderr, "macro metadata count mismatch: %u\n", macro_count);
        return 3;
    }
    sum_macro = find_macro("SUM", 2u, macro_count);
    text_macro = find_macro("TEXT", 2u, macro_count);
    guard_macro = find_macro("CONFIG_H", 2u, macro_count);
    if (sum_macro < 0 || macros[(uint32_t)sum_macro].kind != NOVAC_MACRO_FUNCTION ||
        macros[(uint32_t)sum_macro].parameter_count != 2u || text_macro < 0 ||
        strcmp(macros[(uint32_t)text_macro].replacement, "\"P\"") != 0 || guard_macro < 0 ||
        (macros[(uint32_t)guard_macro].flags & NOVAC_MACRO_FLAG_HEADER) == 0u) {
        fprintf(stderr, "macro metadata content mismatch sum=%d text=%d guard=%d\n", sum_macro,
                text_macro, guard_macro);
        return 4;
    }
    if (find_macro("EXTRA", 0u, macro_count) < 0 || find_macro("LOCAL", 0u, macro_count) < 0) {
        fprintf(stderr, "source macro metadata missing\n");
        return 5;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 6;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 7;
    }
    status = nova_vm_run(vm, 200000u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 0u) != 98u) {
        fprintf(stderr, "runtime mismatch status=%d r0=%u\n", status, nova_vm_get_register(vm, 0u));
        nova_vm_destroy(vm);
        return 8;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR && console_count + 1u < (uint32_t)sizeof(console))
            console[console_count++] = (char)event.value;
    }
    console[console_count] = '\0';
    if (strcmp(console, "P") != 0) {
        fprintf(stderr, "console mismatch: %s\n", console);
        nova_vm_destroy(vm);
        return 9;
    }
    nova_vm_destroy(vm);

    {
        static const NovaTranslationUnit bad_redefine[] = {
            {"src/main.c", "#define X 1\n#define X 2\nint main(void) { return X; }\n"}};
        if (!expect_failure(bad_redefine, 1u, NOVAC_ERR_CONFLICTING_DECLARATION))
            return 10;
    }
    {
        static const NovaTranslationUnit bad_recursive[] = {
            {"src/main.c", "#define A B\n#define B A\nint main(void) { return A; }\n"}};
        if (!expect_failure(bad_recursive, 1u, NOVAC_ERR_MACRO_RECURSION))
            return 11;
    }
    {
        static const NovaTranslationUnit bad_paste[] = {
            {"src/main.c", "#define JOIN(a,b) a ## b\nint main(void) { return 0; }\n"}};
        if (!expect_failure(bad_paste, 1u, NOVAC_ERR_UNSUPPORTED))
            return 12;
    }
    {
        static const NovaTranslationUnit bad_directive[] = {
            {"src/main.c", "#elif 1\nint main(void) { return 0; }\n"}};
        if (!expect_failure(bad_directive, 1u, NOVAC_ERR_PREPROCESSOR))
            return 13;
    }

    printf("VOL20 PREPROCESSOR PASS bytes=%u maps=%u includes=%u macros=%u decls=%u r0=98 "
           "console=P ex5=1\n",
           result.bytecode_size, source_count, include_count, macro_count, declaration_count);
    return 0;
}
