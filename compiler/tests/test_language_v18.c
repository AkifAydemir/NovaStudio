#include "nova_compiler.h"
#include "nova_vm.h"

#include <stdint.h>
#include <stdio.h>

static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static NovaDebugGlobal globals[NOVA_COMPILER_MAX_GLOBALS];
static NovaSymbolReference references[NOVA_COMPILER_MAX_REFERENCES];
static NovaIncludeDependency includes[NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES];
static NovaDebugString strings[NOVA_COMPILER_MAX_DEBUG_STRINGS];
static NovaDeclarationMetadata declarations[NOVA_COMPILER_MAX_DECLARATIONS];

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

int main(void) {
    static const char *source = "int main(void) {\n"
                                " int i = 0;\n"
                                " int sum = 0;\n"
                                " do {\n"
                                "   i++;\n"
                                "   if (i == 2) continue;\n"
                                "   sum += i;\n"
                                " } while (i < 4);\n"
                                " (sum++, sum++);\n"
                                " int widened = 0;\n"
                                " {\n"
                                "   char c = (char)511;\n"
                                "   int* p = nova_alloc(4);\n"
                                "   *p = 9;\n"
                                "   int raw = (int)p;\n"
                                "   int* q = (int*)raw;\n"
                                "   sum += *q;\n"
                                "   widened = (int)c;\n"
                                "   nova_free(p);\n"
                                " }\n"
                                " int loops = 0;\n"
                                "again:\n"
                                " {\n"
                                "   int box[1] = {3};\n"
                                "   loops++;\n"
                                "   sum += box[0];\n"
                                "   if (loops < 2) goto again;\n"
                                " }\n"
                                " {\n"
                                "   int temp[1] = {5};\n"
                                "   sum += temp[0];\n"
                                "   goto done;\n"
                                " }\n"
                                "done:\n"
                                " putchar('G');\n"
                                " return sum + widened + loops;\n"
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
    uint32_t declaration_count = 0u;
    uint32_t do_count = 0u;
    uint32_t label_count = 0u;
    uint32_t goto_count = 0u;
    uint32_t i;
    int32_t status;
    NovaVm *vm;
    NovaHeapStats heap;
    NovaEvent event;
    char console = '\0';

    unit.name = "main.c";
    unit.source = source;
    status = nova_compile_project_debug_ex4(
        &unit, 1u, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic, source_map,
        NOVA_COMPILER_MAX_SOURCE_MAP, &map_count, symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS,
        &symbol_count, functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count, globals,
        NOVA_COMPILER_MAX_GLOBALS, &global_count, references, NOVA_COMPILER_MAX_REFERENCES,
        &reference_count, includes, NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES, &include_count, strings,
        NOVA_COMPILER_MAX_DEBUG_STRINGS, &string_count, declarations,
        NOVA_COMPILER_MAX_DECLARATIONS, &declaration_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "Vol.18 compile failed %d @ %u:%u %s\n", status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 1;
    }
    for (i = 0u; i < map_count; i++) {
        if (source_map[i].statement_kind == NOVAC_SOURCE_DO_WHILE)
            do_count++;
        else if (source_map[i].statement_kind == NOVAC_SOURCE_LABEL)
            label_count++;
        else if (source_map[i].statement_kind == NOVAC_SOURCE_GOTO)
            goto_count++;
    }
    if (do_count != 1u || label_count != 2u || goto_count != 2u) {
        fprintf(stderr, "Vol.18 source-map kinds mismatch do=%u label=%u goto=%u maps=%u\n",
                do_count, label_count, goto_count, map_count);
        return 2;
    }

    vm = nova_vm_create();
    if (vm == NULL)
        return 3;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 4;
    status = nova_vm_run(vm, 300000u);
    if (status != NOVA_HALTED) {
        fprintf(stderr, "Vol.18 runtime status=%d pc=%u\n", status, nova_vm_get_pc(vm));
        return 5;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR)
            console = (char)event.value;
    }
    /* do-while sum = 1+3+4 = 8; comma ++ twice => 10; pointer value => 19;
       two box iterations => 25; temp => 30; widened char(511)=255; loops=2. */
    if (nova_vm_get_register(vm, 0u) != 287u || console != 'G') {
        fprintf(stderr, "Vol.18 runtime mismatch r0=%u console=%c\n", nova_vm_get_register(vm, 0u),
                console ? console : '?');
        return 6;
    }
    if (nova_vm_get_heap_stats(vm, &heap) != NOVA_OK || heap.allocation_count != 0u ||
        heap.block_count != 1u) {
        fprintf(stderr, "Vol.18 goto cleanup leak alloc=%u blocks=%u\n", heap.allocation_count,
                heap.block_count);
        return 7;
    }
    nova_vm_destroy(vm);

    if (!expect_error("int main(void){ goto missing; return 0; }\n", NOVAC_ERR_UNKNOWN_SYMBOL))
        return 8;
    if (!expect_error("int main(void){ a: return 1; a: return 2; }\n",
                      NOVAC_ERR_CONFLICTING_DECLARATION))
        return 9;
    if (!expect_error("int main(void){ goto inner; { inner: return 1; } }\n",
                      NOVAC_ERR_UNSUPPORTED))
        return 10;
    if (!expect_error("int main(void){ goto later; int x=1; later: return x; }\n",
                      NOVAC_ERR_UNSUPPORTED))
        return 11;
    if (!expect_error("int main(void){ { goto later; } int box[1]={1}; later: return 0; }\n",
                      NOVAC_ERR_UNSUPPORTED))
        return 12;
    if (!expect_error("struct P { int x; }; int main(void){ int x=1; return (struct P)x; }\n",
                      NOVAC_ERR_UNSUPPORTED))
        return 13;

    printf("VOL18 CONTROL/CAST PASS bytes=%u maps=%u do=%u labels=%u gotos=%u r0=287 console=G "
           "heap=1\n",
           result.bytecode_size, map_count, do_count, label_count, goto_count);
    return 0;
}
