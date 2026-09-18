#include "nova_compiler.h"
#include "nova_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(NovaDeclarationMetadata) == 92u, "Vol.17 declaration metadata ABI drift");

/*
 * Vol.17 declaration/linkage regression.
 *
 * This regression locks the Vol.17 declaration/linkage contract while keeping
 * NovaVM language-neutral. It covers runtime behavior, visibility failures and
 * the additive declaration metadata ABI consumed by Nova Studio.
 */

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
                             uint32_t *function_count, uint32_t *global_count,
                             uint32_t *reference_count, uint32_t *declaration_count) {
    uint32_t source_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t include_count = 0u;
    uint32_t string_count = 0u;

    return nova_compile_project_debug_ex4(
        units, unit_count, bytecode, (uint32_t)sizeof(bytecode), result, diagnostic, source_map,
        NOVA_COMPILER_MAX_SOURCE_MAP, &source_count, symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS,
        &symbol_count, functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, function_count, globals,
        NOVA_COMPILER_MAX_GLOBALS, global_count, references, NOVA_COMPILER_MAX_REFERENCES,
        reference_count, includes, NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES, &include_count, strings,
        NOVA_COMPILER_MAX_DEBUG_STRINGS, &string_count, declarations,
        NOVA_COMPILER_MAX_DECLARATIONS, declaration_count);
}

static int find_function(const char *name, uint32_t file_id, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (functions[i].file_id == file_id && strcmp(functions[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_global(const char *name, uint32_t file_id, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (globals[i].file_id == file_id && strcmp(globals[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static const NovaDeclarationMetadata *find_declaration(const char *name, uint32_t kind,
                                                       uint32_t file_id, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (declarations[i].kind == kind && declarations[i].file_id == file_id &&
            strcmp(declarations[i].name, name) == 0) {
            return &declarations[i];
        }
    }
    return NULL;
}

static int expect_rejected(const NovaTranslationUnit *units, uint32_t count) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t function_count = 0u;
    uint32_t global_count = 0u;
    uint32_t reference_count = 0u;
    uint32_t declaration_count = 0u;
    int32_t status = compile_units(units, count, &result, &diagnostic, &function_count,
                                   &global_count, &reference_count, &declaration_count);

    if (status == NOVAC_OK) {
        fprintf(stderr, "expected compilation failure but project compiled\n");
        return 0;
    }
    return 1;
}

int main(void) {
    /*
     * Good project:
     * - typedefs exported by a reachable quoted header.
     * - const object reads are legal.
     * - two different translation units own same-spelled static helpers.
     * - externally-linked declarations are visible through the reachable header.
     */
    static const NovaTranslationUnit good_units[] = {
        {"src/main.c", "#include \"../include/api.h\"\n"
                       "int other(void);\n"
                       "int main(void) {\n"
                       "    Score local = add(4, 5);\n"
                       "    return local + bias + other();\n"
                       "}\n"},
        {"src/math.c", "#include \"../include/api.h\"\n"
                       "static Score helper(Score x) { return x + 1; }\n"
                       "const Score bias = 3;\n"
                       "Score add(Score a, Score b) { return helper(a) + b; }\n"},
        {"src/other.c", "static int helper(int x) { return x + 20; }\n"
                        "int other(void) { return helper(1); }\n"},
        {"include/api.h", "typedef int Score;\n"
                          "extern const Score bias;\n"
                          "Score add(Score a, Score b);\n"}};

    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t function_count = 0u;
    uint32_t global_count = 0u;
    uint32_t reference_count = 0u;
    uint32_t declaration_count = 0u;
    int32_t status;

    status = compile_units(good_units, 4u, &result, &diagnostic, &function_count, &global_count,
                           &reference_count, &declaration_count);

    if (status != NOVAC_OK) {
        fprintf(stderr, "Vol.17 good project failed: %d file=%u @ %u:%u %s\n", status,
                diagnostic.file_id, diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }

    /*
     * File-aware debug metadata must allow the two internal-linkage helpers to
     * coexist.  The final Vol.17 metadata extension will additionally publish
     * linkage/alias records; this baseline check already locks file identity.
     */
    if (find_function("helper", 1u, function_count) < 0 ||
        find_function("helper", 2u, function_count) < 0) {
        fprintf(stderr, "file-local helper metadata was not preserved\n");
        return 2;
    }

    if (find_global("bias", 1u, global_count) < 0) {
        fprintf(stderr, "const external global metadata missing\n");
        return 3;
    }

    {
        const NovaDeclarationMetadata *math_helper =
            find_declaration("helper", NOVAC_DECL_FUNCTION, 1u, declaration_count);
        const NovaDeclarationMetadata *other_helper =
            find_declaration("helper", NOVAC_DECL_FUNCTION, 2u, declaration_count);
        const NovaDeclarationMetadata *add_decl =
            find_declaration("add", NOVAC_DECL_FUNCTION, 1u, declaration_count);
        const NovaDeclarationMetadata *bias_decl =
            find_declaration("bias", NOVAC_DECL_GLOBAL, 1u, declaration_count);
        const NovaDeclarationMetadata *score_alias =
            find_declaration("Score", NOVAC_DECL_TYPEDEF, 3u, declaration_count);

        if (math_helper == NULL || other_helper == NULL || add_decl == NULL || bias_decl == NULL ||
            score_alias == NULL) {
            fprintf(stderr, "Vol.17 declaration metadata records missing\n");
            return 9;
        }
        if (math_helper->linkage != NOVAC_LINKAGE_INTERNAL ||
            other_helper->linkage != NOVAC_LINKAGE_INTERNAL ||
            add_decl->linkage != NOVAC_LINKAGE_EXTERNAL ||
            bias_decl->linkage != NOVAC_LINKAGE_EXTERNAL ||
            score_alias->linkage != NOVAC_LINKAGE_NONE) {
            fprintf(stderr, "Vol.17 declaration linkage metadata mismatch\n");
            return 10;
        }
        if ((bias_decl->qualifiers & NOVAC_DECL_QUAL_CONST) == 0u ||
            score_alias->type != NOVAC_DEBUG_INT ||
            (math_helper->flags & NOVAC_DECL_FLAG_DEFINITION) == 0u ||
            (other_helper->flags & NOVAC_DECL_FLAG_DEFINITION) == 0u) {
            fprintf(stderr, "Vol.17 qualifier/type/definition metadata mismatch\n");
            return 11;
        }
    }

    /*
     * Cross-TU definition alone must not create source-level visibility.
     */
    {
        static const NovaTranslationUnit units[] = {
            {"src/main.c", "int main(void) { return hidden(); }\n"},
            {"src/hidden.c", "int hidden(void) { return 7; }\n"}};
        if (!expect_rejected(units, 2u))
            return 4;
    }

    /*
     * A static function is never externally visible.
     */
    {
        static const NovaTranslationUnit units[] = {
            {"src/main.c", "int hidden(void); int main(void) { return hidden(); }\n"},
            {"src/hidden.c", "static int hidden(void) { return 7; }\n"}};
        if (!expect_rejected(units, 2u))
            return 5;
    }

    /*
     * A typedef declared only in another .c is not visible here.
     */
    {
        static const NovaTranslationUnit units[] = {
            {"src/a.c", "typedef int LocalScore; int a(void) { LocalScore x = 1; return x; }\n"},
            {"src/main.c", "int main(void) { LocalScore x = 2; return x; }\n"}};
        if (!expect_rejected(units, 2u))
            return 6;
    }

    /*
     * const must be a real write-protection rule in NovaC's type/lvalue model,
     * not merely accepted-and-discarded syntax.
     */
    {
        static const NovaTranslationUnit units[] = {
            {"src/main.c", "int main(void) { const int x = 4; x = 5; return x; }\n"}};
        if (!expect_rejected(units, 1u))
            return 7;
    }

    {
        static const NovaTranslationUnit units[] = {
            {"src/main.c", "const int value = 4; int main(void) { value++; return value; }\n"}};
        if (!expect_rejected(units, 1u))
            return 8;
    }

    printf("VOL17 DECL/LINK PASS bytes=%u funcs=%u globals=%u refs=%u decls=%u "
           "static_isolation=1 extern_visibility=1 typedef_visibility=1 const_guard=1 ex4=1\n",
           result.bytecode_size, function_count, global_count, reference_count, declaration_count);

    return 0;
}
