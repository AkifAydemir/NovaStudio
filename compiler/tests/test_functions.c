#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
_Static_assert(sizeof(NovaCallFrame) == 16u, "call-frame ABI drift");
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static NovaSourceMapEntry source_map[NOVA_COMPILER_MAX_SOURCE_MAP];
static NovaDebugSymbol symbols[NOVA_COMPILER_MAX_DEBUG_SYMBOLS];
static NovaDebugFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
static const char *source = "int add(int a, int b) {\n"
                            " int sum = a + b;\n"
                            " return sum;\n"
                            "}\n"
                            "\n"
                            "int factorial(int n) {\n"
                            " if (n <= 1) {\n"
                            " return 1;\n"
                            " }\n"
                            " return n * factorial(n - 1);\n"
                            "}\n"
                            "\n"
                            "int combine(int x) {\n"
                            " return add(x, factorial(5));\n"
                            "}\n"
                            "\n"
                            "int main(void) {\n"
                            " int value = combine(7);\n"
                            " putchar('F');\n"
                            " return value;\n"
                            "}\n";
static const NovaSourceMapEntry *find_line(uint32_t line) {
    uint32_t i;
    for (i = 0u; i < NOVA_COMPILER_MAX_SOURCE_MAP; i++) {
        if (source_map[i].line == line && source_map[i].bytecode_end > source_map[i].bytecode_start)
            return &source_map[i];
    }
    return NULL;
}
static int find_function(const char *name, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; i++) {
        uint32_t j = 0u;
        while (name[j] != '\0' && functions[i].name[j] == name[j])
            j++;
        if (name[j] == '\0' && functions[i].name[j] == '\0')
            return (int)i;
    }
    return -1;
}
int main(void) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    uint32_t map_count = 0u;
    uint32_t symbol_count = 0u;
    uint32_t function_count = 0u;
    const NovaSourceMapEntry *base_return;

    NovaVm *vm;
    NovaCallFrame frame;
    NovaEvent event;
    int factorial_index;
    int main_index;
    int32_t status;
    char console[8];
    uint32_t console_count = 0u;
    status = nova_compile_c_debug(source, bytecode, (uint32_t)sizeof(bytecode), &result,
                                  &diagnostic, source_map, NOVA_COMPILER_MAX_SOURCE_MAP, &map_count,
                                  symbols, NOVA_COMPILER_MAX_DEBUG_SYMBOLS, &symbol_count,
                                  functions, NOVA_COMPILER_MAX_DEBUG_FUNCTIONS, &function_count);
    if (status != NOVAC_OK) {
        fprintf(stderr, "compile failed: %d @ %u:%u %s\n", status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 1;
    }
    if (function_count != 4u) {
        fprintf(stderr, "expected four functions, got %u\n", function_count);
        return 2;
    }
    factorial_index = find_function("factorial", function_count);
    main_index = find_function("main", function_count);
    if (factorial_index < 0 || main_index < 0 || functions[factorial_index].parameter_count != 1u ||
        functions[main_index].parameter_count != 0u) {
        fprintf(stderr, "function metadata mismatch\n");
        return 3;
    }
    base_return = find_line(8u);
    if (base_return == NULL || base_return->function_index != (uint32_t)factorial_index) {
        fprintf(stderr, "missing factorial base-case source map\n");
        return 4;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 5;
    if (nova_vm_load(vm, bytecode, result.bytecode_size, 0u) != NOVA_OK)
        return 6;
    if (nova_vm_add_breakpoint(vm, base_return->bytecode_start) != NOVA_OK)
        return 7;
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_BREAKPOINT) {
        fprintf(stderr, "expected recursive source breakpoint, got %d pc=%u\n", status,
                nova_vm_get_pc(vm));
        nova_vm_destroy(vm);
        return 8;
    }
    if (nova_vm_get_register(vm, 1u) != 1u) {
        fprintf(stderr, "base-case parameter n should be 1, got %u\n",
                nova_vm_get_register(vm, 1u));
        nova_vm_destroy(vm);
        return 9;
    }
    if (nova_vm_get_call_frame_count(vm) != 7u) {

        fprintf(stderr, "expected 7 active frames at recursion base, got %u\n",
                nova_vm_get_call_frame_count(vm));
        nova_vm_destroy(vm);
        return 10;
    }
    if (nova_vm_get_call_frame(vm, 6u, &frame) != NOVA_OK ||
        frame.target_address != functions[factorial_index].bytecode_start || frame.depth != 6u) {
        fprintf(stderr, "deepest frame metadata mismatch\n");
        nova_vm_destroy(vm);
        return 11;
    }
    if (nova_vm_remove_breakpoint(vm, base_return->bytecode_start) != NOVA_OK)
        return 12;
    status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED) {
        fprintf(stderr, "runtime did not halt: %d\n", status);
        nova_vm_destroy(vm);
        return 13;
    }
    if (nova_vm_get_register(vm, 0u) != 127u) {
        fprintf(stderr, "recursive return mismatch: %u\n", nova_vm_get_register(vm, 0u));
        nova_vm_destroy(vm);
        return 14;
    }
    if (nova_vm_get_call_frame_count(vm) != 0u) {
        fprintf(stderr, "call frame stack did not unwind\n");
        nova_vm_destroy(vm);
        return 15;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR && console_count + 1u < sizeof(console))
            console[console_count++] = (char)event.value;
    }
    console[console_count] = '\0';
    if (console_count != 1u || console[0] != 'F') {
        fprintf(stderr, "console mismatch: '%s'\n", console);
        nova_vm_destroy(vm);
        return 16;
    }
    printf(
        "FUNCTION PASS bytes=%u functions=%u symbols=%u recursive_frames=7 return=127 console=F\n",
        result.bytecode_size, function_count, symbol_count);
    nova_vm_destroy(vm);
    return 0;
}
