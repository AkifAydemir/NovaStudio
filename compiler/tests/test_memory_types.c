#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(NovaDebugSymbol) == 104u, "Vol.13 debug-symbol ABI drift");
_Static_assert(sizeof(NovaDebugGlobal) == 84u, "Vol.13 debug-global ABI drift");
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static NovaDebugGlobal globals[NOVA_COMPILER_MAX_GLOBALS];
static NovaSymbolReference references[NOVA_COMPILER_MAX_REFERENCES];
static NovaIncludeDependency includes[NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES];
static const NovaDebugGlobal *find_global(const char *name, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (strcmp(globals[i].name, name) == 0)
            return &globals[i];
    }
    return NULL;
}
static const NovaDebugSymbol *find_symbol(const char *name, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (strcmp(symbols[i].name, name) == 0)
            return &symbols[i];
    }
    return NULL;
}
static uint32_t count_reference(const char *name, uint32_t kind, uint32_t count) {
    uint32_t i;
    uint32_t matches = 0u;
    for (i = 0u; i < count; i++) {
        if (references[i].kind == kind && strcmp(references[i].name, name) == 0)
            matches++;
    }
    return matches;
}
static uint32_t read_u32(NovaVm *vm, uint32_t address) {
    uint8_t bytes[4];
    if (nova_vm_read_memory(vm, address, bytes, 4u) != NOVA_OK)
        return 0xFFFFFFFFu;
    return ((uint32_t)bytes[0]) | ((uint32_t)bytes[1] << 8u) | ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}
static int expect_compile_error(const char *source, int32_t expected) {
    NovaTranslationUnit unit = {"bad.c", source};
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    int32_t status =
        nova_compile_project(&unit, 1u, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic);
    if (status != expected) {
        fprintf(stderr, "expected compile error %d, got %d at %u:%u: %s\n", expected, status,
                diagnostic.line, diagnostic.column, diagnostic.message);

        return 0;
    }
    return 1;
}
int main(void) {
    const NovaTranslationUnit units[] = {
        {"main.c", "#include \"api.h\"\n"
                   "int main(void) {\n"
                   " int x = 5;\n"
                   " int *px = &x;\n"
                   " struct Pair *p;\n"
                   " int values[2];\n"
                   " int *q = values;\n"
                   " x = 6;\n"
                   " *px = *px + 1;\n"
                   " values[0] = 1;\n"
                   " values[1] = 7;\n"
                   " q = q + 1;\n"
                   " pairs[0].left = 3;\n"
                   " pairs[0].right = 4;\n"
                   " pairs[1].left = 10;\n"
                   " pairs[1].right = 20;\n"
                   " p = &pairs[0];\n"
                   " p = p + 1;\n"
                   " tail = sizeof(struct Pair) + sizeof(pairs) + sizeof(*p) + sizeof(int*);\n"
                   " return sum_pair(p) + x + tail + *q;\n"
                   "}\n"},
        {"api.h", "struct Pair { int left; int right; };\n"
                  "extern struct Pair pairs[2];\n"
                  "extern int tail;\n"
                  "int sum_pair(struct Pair *p);\n"},
        {"state.c", "#include \"api.h\"\n"
                    "struct Pair pairs[2];\n"
                    "int tail = 1;\n"
                    "int sum_pair(struct Pair *p) {\n"
                    " return p->left + p->right;\n"
                    "}\n"}};
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t source_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    uint32_t global_count = 0u;
    uint32_t reference_count = 0u;
    uint32_t include_count = 0u;
    const NovaDebugGlobal *pairs_meta;
    const NovaDebugGlobal *tail_meta;
    const NovaDebugSymbol *x_meta;
    const NovaDebugSymbol *p_meta;
    NovaHeapStats heap;
    NovaVm *vm;
    int32_t status;
    status = nova_compile_project_debug_ex2(
        units, 3u,

        bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic, source_map,
        NOVA_COMPILER_MAX_SOURCE_MAP, &source_count, symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS,
        &symbol_count, functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count, globals,
        NOVA_COMPILER_MAX_GLOBALS, &global_count, references, NOVA_COMPILER_MAX_REFERENCES,
        &reference_count, includes, NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES, &include_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "Vol.13 project compile failed: %d file=%u %u:%u %s\n", status,
                diagnostic.file_id, diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    pairs_meta = find_global("pairs", global_count);
    tail_meta = find_global("tail", global_count);
    x_meta = find_symbol("x", symbol_count);
    p_meta = find_symbol("p", symbol_count);
    if (pairs_meta == NULL || tail_meta == NULL || x_meta == NULL || p_meta == NULL) {
        fprintf(stderr, "Vol.13 metadata missing globals=%u symbols=%u\n", global_count,
                symbol_count);
        return 2;
    }
    if (pairs_meta->address != NOVA_COMPILER_GLOBAL_BASE || pairs_meta->type != NOVAC_DEBUG_ARRAY ||
        pairs_meta->byte_size != 16u || pairs_meta->element_count != 2u ||
        tail_meta->address != NOVA_COMPILER_GLOBAL_BASE + 16u || tail_meta->byte_size != 4u) {
        fprintf(stderr, "global aggregate layout mismatch pairs=0x%08X/%u/%u tail=0x%08X/%u\n",
                pairs_meta->address, pairs_meta->byte_size, pairs_meta->element_count,
                tail_meta->address, tail_meta->byte_size);
        return 3;
    }
    if (x_meta->storage_kind != NOVAC_DEBUG_STORAGE_MEMORY || x_meta->byte_size != 4u ||
        p_meta->type != NOVAC_DEBUG_STRUCT_PTR) {
        fprintf(stderr, "debug promotion/type mismatch x_storage=%u x_size=%u p_type=%u\n",
                x_meta->storage_kind, x_meta->byte_size, p_meta->type);
        return 4;
    }
    if (count_reference("pairs", NOVAC_REF_GLOBAL_WRITE, reference_count) != 4u ||
        count_reference("pairs", NOVAC_REF_GLOBAL_READ, reference_count) != 0u ||
        count_reference("tail", NOVAC_REF_GLOBAL_WRITE, reference_count) != 1u ||
        count_reference("tail", NOVAC_REF_GLOBAL_READ, reference_count) != 1u) {
        fprintf(
            stderr,
            "aggregate semantic refs mismatch refs=%u pairs_w=%u pairs_r=%u tail_w=%u tail_r=%u\n",
            reference_count, count_reference("pairs", NOVAC_REF_GLOBAL_WRITE, reference_count),
            count_reference("pairs", NOVAC_REF_GLOBAL_READ, reference_count),
            count_reference("tail", NOVAC_REF_GLOBAL_WRITE, reference_count),
            count_reference("tail", NOVAC_REF_GLOBAL_READ, reference_count));
        return 5;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 5;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK) {
        nova_vm_destroy(vm);
        return 6;
    }
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 0u) != 80u) {
        fprintf(stderr, "Vol.13 runtime mismatch status=%d r0=%u bytes=%u\n", status,
                nova_vm_get_register(vm, 0u), result.bytecode_size);
        nova_vm_destroy(vm);

        return 7;
    }
    if (read_u32(vm, NOVA_COMPILER_GLOBAL_BASE + 8u) != 10u ||
        read_u32(vm, NOVA_COMPILER_GLOBAL_BASE + 12u) != 20u ||
        read_u32(vm, NOVA_COMPILER_GLOBAL_BASE + 16u) != 36u) {
        fprintf(stderr, "Vol.13 static data mismatch pair1=(%u,%u) tail=%u\n",
                read_u32(vm, NOVA_COMPILER_GLOBAL_BASE + 8u),
                read_u32(vm, NOVA_COMPILER_GLOBAL_BASE + 12u),
                read_u32(vm, NOVA_COMPILER_GLOBAL_BASE + 16u));
        nova_vm_destroy(vm);
        return 8;
    }
    if (nova_vm_get_heap_stats(vm, &heap) != NOVA_OK || heap.block_count != 1u ||
        heap.allocation_count != 0u) {
        fprintf(stderr, "address-taken local leaked heap blocks=%u allocs=%u\n", heap.block_count,
                heap.allocation_count);
        nova_vm_destroy(vm);
        return 9;
    }
    nova_vm_destroy(vm);
    if (!expect_compile_error("int main(void) { int *p; int *q = &p; return 0; }\n",
                              NOVAC_ERR_INVALID_POINTER))
        return 10;
    if (!expect_compile_error("int main(void) { int a[2]; int *p = a; return p + p; }\n",
                              NOVAC_ERR_INVALID_POINTER))
        return 11;
    if (!expect_compile_error("int main(void) { return sizeof(struct Missing); }\n",
                              NOVAC_ERR_UNKNOWN_TYPE))
        return 12;
    printf("VOL13 MEMORY TYPES PASS bytes=%u globals=%u refs=%u includes=%u r0=80 pairs=16 tail=36 "
           "promoted_x=1 int_ptr_scale=4 struct_ptr_scale=8\n",
           result.bytecode_size, global_count, reference_count, include_count);
    return 0;
}
