#include "nova_compiler.h"
#include "nova_vm.h"
#include <stdio.h>
static uint8_t bytecode[NOVA_COMPILER_MAX_BYTECODE];
static int compile_and_run(const char *source, uint32_t expected_return,
                           const char *expected_console) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    NovaVm *vm;
    NovaEvent event;
    char console[128];
    uint32_t console_size = 0u;
    int32_t status;
    status = nova_compile_c(source, bytecode, (uint32_t)sizeof(bytecode), &result, &diagnostic);
    if (status != NOVAC_OK) {
        fprintf(stderr, "compile failed %d at %u:%u: %s\n", status, diagnostic.line,
                diagnostic.column, diagnostic.message);
        return 20;
    }
    vm = nova_vm_create();
    if (vm == NULL)
        return 21;
    status = nova_vm_load(vm, bytecode, result.bytecode_size, 0u);
    if (status == NOVA_OK)
        status = nova_vm_run(vm, 100000u);
    if (status != NOVA_HALTED) {
        fprintf(stderr, "runtime failed: %d pc=%u\n", status, nova_vm_get_pc(vm));
        nova_vm_destroy(vm);
        return 22;
    }
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR && console_size + 1u < sizeof(console)) {
            console[console_size++] = (char)(event.value & 0xFFu);
        }
    }
    console[console_size] = '\0';
    if (nova_vm_get_register(vm, 0u) != expected_return) {
        fprintf(stderr, "return mismatch: got=%u expected=%u\n", nova_vm_get_register(vm, 0u),
                expected_return);
        nova_vm_destroy(vm);
        return 23;
    }
    {
        uint32_t i = 0u;
        while (expected_console[i] != '\0' && console[i] == expected_console[i])
            i++;
        if (expected_console[i] != console[i]) {
            fprintf(stderr, "console mismatch: got='%s' expected='%s'\n", console,
                    expected_console);
            nova_vm_destroy(vm);
            return 24;
        }
    }
    printf("compiled %u bytes / %u AST nodes / return=%u / console='%s'\n", result.bytecode_size,
           result.ast_node_count, expected_return, console);
    nova_vm_destroy(vm);
    return 0;
}
int main(void) {
    const char *control_flow =
        "int main(void) {\n"

        " int x = 5;\n"
        " int sum = 0;\n"
        " while (x > 0) {\n"
        " sum = sum + x;\n"
        " x = x - 1;\n"
        " }\n"
        " if (sum == 15) { putchar('O'); putchar('K'); } else { putchar('X'); }\n"
        " return sum;\n"
        "}\n";
    const char *heap_pointer = "int main(void) {\n"
                               " int* p = nova_alloc(4);\n"
                               " *p = 42;\n"
                               " int value = *p;\n"
                               " nova_free(p);\n"
                               " return value;\n"
                               "}\n";
    const char *signed_compare = "int main(void) {\n"
                                 " int x = -2;\n"
                                 " if (x < 0) { putchar('N'); }\n"
                                 " return !0;\n"
                                 "}\n";
    int rc = compile_and_run(control_flow, 15u, "OK");
    if (rc)
        return rc;
    rc = compile_and_run(heap_pointer, 42u, "");
    if (rc)
        return rc;
    rc = compile_and_run(signed_compare, 1u, "N");
    if (rc)
        return rc;
    printf("COMPILER PASS NovaC Vol.9 baseline\n");
    return 0;
}
