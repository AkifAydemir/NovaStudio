#include "nova_compiler.h"
#include "nova_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(NovaDeclarationMetadata) == 92u, "Vol.19 declaration metadata ABI drift");

static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static NovaDebugGlobal globals[NOVA_COMPILER_MAX_GLOBALS];
static NovaSymbolReference references[NOVA_COMPILER_MAX_REFERENCES];
static NovaIncludeDependency includes[NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES];
static NovaDebugString strings[NOVA_COMPILER_MAX_DEBUG_STRINGS];
static NovaDeclarationMetadata declarations[NOVA_COMPILER_MAX_DECLARATIONS];

static int32_t compile_units(const NovaTranslationUnit *units, uint32_t unit_count,
                             NovaCompileResult *result, NovaCompilerDiagnostic *diagnostic,
                             uint32_t *global_count, uint32_t *declaration_count) {
    uint32_t map_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    uint32_t reference_count = 0u;
    uint32_t include_count = 0u;
    uint32_t string_count = 0u;
    return nova_compile_project_debug_ex4(
        units, unit_count, bytecode, (uint32_t)sizeof(bytecode), result, diagnostic, source_map,
        NOVA_COMPILER_MAX_SOURCE_MAP, &map_count, symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS,
        &symbol_count, functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count, globals,
        NOVA_COMPILER_MAX_GLOBALS, global_count, references, NOVA_COMPILER_MAX_REFERENCES,
        &reference_count, includes, NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES, &include_count, strings,
        NOVA_COMPILER_MAX_DEBUG_STRINGS, &string_count, declarations,
        NOVA_COMPILER_MAX_DECLARATIONS, declaration_count);
}

static const NovaDeclarationMetadata *find_decl(const char *name, uint32_t kind, uint32_t count,
                                                uint32_t occurrence) {
    uint32_t i;
    uint32_t seen = 0u;
    for (i = 0u; i < count; i++) {
        if (declarations[i].kind == kind && strcmp(declarations[i].name, name) == 0) {
            if (seen == occurrence)
                return &declarations[i];
            seen++;
        }
    }
    return NULL;
}

static int expect_error(const char *source) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    int32_t status =
        nova_compile_c(source, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic);
    if (status == NOVAC_OK) {
        fprintf(stderr, "expected Vol.19 negative case to fail\n");
        return 0;
    }
    return 1;
}

int main(void) {
    static const NovaTranslationUnit units[] = {
        {"src/main.c", "#include \"../include/api.h\"\n"
                       "int bump(void) {\n"
                       "  typedef volatile int Count;\n"
                       "  static volatile Count hits = 1;\n"
                       "  hits += 2;\n"
                       "  return hits;\n"
                       "}\n"
                       "int main(void) {\n"
                       "  typedef int Score;\n"
                       "  Score a = bump();\n"
                       "  Score b = bump();\n"
                       "  { typedef char Score; Score c = 7; shared += c; }\n"
                       "  Score z = shared;\n"
                       "  cursor = (int*)0;\n"
                       "  putchar('V');\n"
                       "  return a * 100 + b * 10 + strong + z;\n"
                       "}\n"},
        {"src/state_a.c", "#include \"../include/api.h\"\n"
                          "volatile int shared;\n"
                          "volatile int strong;\n"
                          "int * restrict cursor;\n"},
        {"src/state_b.c", "#include \"../include/api.h\"\n"
                          "volatile int shared;\n"
                          "volatile int strong = 4;\n"
                          "int * restrict cursor;\n"},
        {"include/api.h", "extern volatile int shared;\n"
                          "extern volatile int strong;\n"
                          "extern int * restrict cursor;\n"}};
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t global_count = 0u;
    uint32_t declaration_count = 0u;
    int32_t status;
    NovaVm *vm;
    NovaEvent event;
    char console = '\0';
    const NovaDeclarationMetadata *shared_decl;
    const NovaDeclarationMetadata *strong_decl;
    const NovaDeclarationMetadata *cursor_decl;
    const NovaDeclarationMetadata *static_decl;
    const NovaDeclarationMetadata *count_alias;
    const NovaDeclarationMetadata *score_outer;
    const NovaDeclarationMetadata *score_inner;

    status = compile_units(units, 4u, &result, &diagnostic, &global_count, &declaration_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "Vol.19 compile failed %d file=%u @ %u:%u %s\n", status, diagnostic.file_id,
                diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    if (global_count != 3u) {
        fprintf(stderr, "Vol.19 expected 3 resolved globals, got %u\n", global_count);
        return 2;
    }

    shared_decl = find_decl("shared", NOVAC_DECL_GLOBAL, declaration_count, 0u);
    strong_decl = find_decl("strong", NOVAC_DECL_GLOBAL, declaration_count, 0u);
    cursor_decl = find_decl("cursor", NOVAC_DECL_GLOBAL, declaration_count, 0u);
    static_decl = find_decl("hits", NOVAC_DECL_STATIC_LOCAL, declaration_count, 0u);
    count_alias = find_decl("Count", NOVAC_DECL_TYPEDEF, declaration_count, 0u);
    score_outer = find_decl("Score", NOVAC_DECL_TYPEDEF, declaration_count, 0u);
    score_inner = find_decl("Score", NOVAC_DECL_TYPEDEF, declaration_count, 1u);
    if (shared_decl == NULL || strong_decl == NULL || cursor_decl == NULL || static_decl == NULL ||
        count_alias == NULL || score_outer == NULL || score_inner == NULL) {
        fprintf(stderr, "Vol.19 declaration metadata missing\n");
        return 3;
    }
    if ((shared_decl->flags & (NOVAC_DECL_FLAG_DEFINITION | NOVAC_DECL_FLAG_TENTATIVE)) !=
            (NOVAC_DECL_FLAG_DEFINITION | NOVAC_DECL_FLAG_TENTATIVE) ||
        (shared_decl->qualifiers & NOVAC_DECL_QUAL_VOLATILE) == 0u ||
        (strong_decl->flags & NOVAC_DECL_FLAG_TENTATIVE) != 0u ||
        (strong_decl->flags & NOVAC_DECL_FLAG_DEFINITION) == 0u ||
        (cursor_decl->flags & NOVAC_DECL_FLAG_TENTATIVE) == 0u ||
        (cursor_decl->qualifiers & NOVAC_DECL_QUAL_RESTRICT) == 0u) {
        fprintf(stderr, "Vol.19 tentative/qualifier metadata mismatch\n");
        return 4;
    }
    if ((static_decl->flags & (NOVAC_DECL_FLAG_DEFINITION | NOVAC_DECL_FLAG_BLOCK_SCOPE |
                               NOVAC_DECL_FLAG_STATIC_STORAGE)) !=
            (NOVAC_DECL_FLAG_DEFINITION | NOVAC_DECL_FLAG_BLOCK_SCOPE |
             NOVAC_DECL_FLAG_STATIC_STORAGE) ||
        (static_decl->qualifiers & NOVAC_DECL_QUAL_VOLATILE) == 0u ||
        (count_alias->flags & NOVAC_DECL_FLAG_BLOCK_SCOPE) == 0u ||
        (count_alias->qualifiers & NOVAC_DECL_QUAL_VOLATILE) == 0u ||
        score_outer->type != NOVAC_DEBUG_INT || score_inner->type != NOVAC_DEBUG_CHAR) {
        fprintf(stderr, "Vol.19 block typedef/static metadata mismatch\n");
        return 5;
    }

    vm = nova_vm_create();
    if (vm == NULL)
        return 6;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 7;
    status = nova_vm_run(vm, 300000u);
    if (status != NOVA_HALTED) {
        fprintf(stderr, "Vol.19 runtime status=%d pc=%u\n", status, nova_vm_get_pc(vm));
        return 8;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR)
            console = (char)event.value;
    }
    /* hits is initialized once to 1, then bump returns 3 and 5. shared is
       tentatively zero-defined and becomes 7; strong resolves to initializer 4. */
    if (nova_vm_get_register(vm, 0u) != 361u || console != 'V') {
        fprintf(stderr, "Vol.19 runtime mismatch r0=%u console=%c\n", nova_vm_get_register(vm, 0u),
                console ? console : '?');
        return 9;
    }
    nova_vm_destroy(vm);

    if (!expect_error("int main(void){ { typedef int T; T x=1; } T y=2; return y; }\n"))
        return 10;
    if (!expect_error("int main(void){ restrict int *p; return 0; }\n"))
        return 11;
    if (!expect_error("int x=1; int x=2; int main(void){ return x; }\n"))
        return 12;
    if (!expect_error("extern int x; int main(void){ return 0; }\n"))
        return 13;
    if (!expect_error("int main(void){ int x=1; static int y=x; return y; }\n"))
        return 14;
    if (!expect_error("int main(void){ static int a[2]={1,2}; return 0; }\n"))
        return 15;

    printf("VOL19 STORAGE/QUAL PASS bytes=%u globals=%u decls=%u r0=361 console=V static_once=1 "
           "tentative=1 typedef_scope=1\n",
           result.bytecode_size, global_count, declaration_count);
    return 0;
}
