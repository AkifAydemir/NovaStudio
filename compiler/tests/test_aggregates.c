#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(NovaDebugSymbol) == 104u, "Vol.9 debug-symbol ABI drift");
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static const NovaDebugSymbol *find_symbol(const char *name, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (strcmp(symbols[i].name, name) == 0)
            return &symbols[i];
    }
    return NULL;
}
static const NovaDebugSymbol *find_symbol_type(const char *name, uint32_t type, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        if (symbols[i].type == type && strcmp(symbols[i].name, name) == 0)
            return &symbols[i];
    }
    return NULL;
}
int main(void) {
    const char *source = "struct Pair {\n"
                         " int left;\n"
                         " int right;\n"
                         "};\n"
                         "\n"
                         "int sum2(int* values) {\n"
                         " return values[0] + values[1];\n"
                         "}\n"
                         "\n"
                         "int main(void) {\n"
                         " int values[4];\n"
                         " struct Pair pair;\n"
                         " struct Pair pairs[2];\n"
                         " values[0] = 4;\n"
                         " values[1] = 6;\n"
                         " pair.left = sum2(values);\n"
                         " pair.right = values[1];\n"
                         " pairs[1].left = pair.left + pair.right;\n"
                         " pairs[1].right = 3;\n"
                         " putchar('A');\n"
                         " return pairs[1].left + pairs[1].right;\n"
                         "}\n";
    const char *bad_bounds = "int main(void) {\n"
                             " int values[2];\n"
                             " values[2] = 9;\n"
                             " return 0;\n"
                             "}\n";
    NovaCompileResult result;

    NovaCompilerDiagnostic diagnostic;
    uint32_t map_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    const NovaDebugSymbol *values;
    const NovaDebugSymbol *value1;
    const NovaDebugSymbol *pair_left;
    const NovaDebugSymbol *pairs1right;
    NovaVm *vm;
    NovaHeapStats before;
    NovaHeapStats after;
    NovaEvent event;
    int32_t status;
    uint32_t compiled_size = 0u;
    char output = '\0';
    status = nova_compile_c_debug(source, bytecode, (uint32_t)sizeof(bytecode), &result,
                                  &diagnostic, source_map, NOVA_COMPILER_MAX_SOURCE_MAP, &map_count,
                                  symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS, &symbol_count,
                                  functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "aggregate compile failed: %d @ %u:%u %s\n", status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 1;
    }
    compiled_size = result.bytecode_size;
    values = find_symbol_type("values", NOVAC_DEBUG_ARRAY, symbol_count);
    value1 = find_symbol("values[1]", symbol_count);
    pair_left = find_symbol("pair.left", symbol_count);
    pairs1right = find_symbol("pairs[1].right", symbol_count);
    if (values == NULL || value1 == NULL || pair_left == NULL || pairs1right == NULL) {
        fprintf(stderr, "aggregate debug children missing (symbols=%u)\n", symbol_count);
        return 2;
    }
    if (values->type != NOVAC_DEBUG_ARRAY || values->byte_size != 16u ||
        values->element_count != 4u || values->storage_kind != NOVAC_DEBUG_STORAGE_REGISTER) {
        fprintf(stderr, "array base debug metadata mismatch\n");
        return 3;
    }
    if (value1->storage_kind != NOVAC_DEBUG_STORAGE_MEMORY || value1->byte_offset != 4u ||
        value1->type != NOVAC_DEBUG_INT) {
        fprintf(stderr, "array child debug metadata mismatch\n");
        return 4;
    }
    if (pair_left->storage_kind != NOVAC_DEBUG_STORAGE_MEMORY || pair_left->byte_offset != 0u ||
        pairs1right->byte_offset != 12u) {
        fprintf(stderr, "struct debug offsets mismatch pair.left=%u pairs[1].right=%u\n",
                pair_left->byte_offset, pairs1right->byte_offset);
        return 5;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 6;
    if (nova_vm_get_heap_stats(vm, &before) != NOVA_OK)
        return 7;

    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 8;
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED) {
        fprintf(stderr, "aggregate runtime failed: %d pc=%u\n", status, nova_vm_get_pc(vm));
        nova_vm_destroy(vm);
        return 9;
    }
    if (nova_vm_get_register(vm, 0u) != 19u) {
        fprintf(stderr, "aggregate return mismatch: %u\n", nova_vm_get_register(vm, 0u));
        nova_vm_destroy(vm);
        return 10;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR)
            output = (char)(event.value & 0xFFu);
    }
    if (output != 'A') {
        fprintf(stderr, "aggregate console mismatch: %c\n", output);
        nova_vm_destroy(vm);
        return 11;
    }
    if (nova_vm_get_heap_stats(vm, &after) != NOVA_OK)
        return 12;
    if (after.used_payload_bytes != 0u || after.free_payload_bytes != before.free_payload_bytes ||
        after.block_count != 1u) {
        fprintf(stderr, "aggregate cleanup leak: used=%u free=%u/%u blocks=%u\n",
                after.used_payload_bytes, after.free_payload_bytes, before.free_payload_bytes,
                after.block_count);
        nova_vm_destroy(vm);
        return 13;
    }
    nova_vm_destroy(vm);
    status = nova_compile_c(bad_bounds, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic);
    if (status != NOVAC_ERR_ARRAY_BOUNDS) {
        fprintf(stderr, "expected constant bounds error, got %d @ %u:%u %s\n", status,
                diagnostic.line, diagnostic.column, diagnostic.message);
        return 14;
    }
    printf("AGGREGATE PASS bytes=%u symbols=%u return=19 heap_clean=1 bounds_guard=1\n",
           compiled_size, symbol_count);
    return 0;
}
