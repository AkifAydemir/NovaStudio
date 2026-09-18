#include "nova_vm.h"
#include <stdio.h>
static int fail(const char *message, int code) {
    fprintf(stderr, "FAIL: %s\n", message);
    return code;
}
int main(void) {
    const uint8_t program[] = {
        NOVA_OP_MOV_RI, 0, 10, 0, 0, 0, /* 0 */
        NOVA_OP_MOV_RI, 1, 20, 0, 0, 0, /* 6 */
        NOVA_OP_ADD_RR, 2, 0,  1,       /* 12 */
        NOVA_OP_HALT                    /* 16 */
    };
    NovaVm *vm = nova_vm_create();
    int32_t status;
    if (!vm)
        return fail("vm allocation", 2);
    if (nova_vm_load(vm, program, (uint32_t)sizeof(program), 0u) != NOVA_OK)
        return fail("program load", 3);
    if (nova_vm_add_breakpoint(vm, 12u) != NOVA_OK)
        return fail("add breakpoint", 4);
    if (nova_vm_get_breakpoint_count(vm) != 1u)
        return fail("breakpoint count", 5);
    status = nova_vm_run(vm, 100u);
    if (status != NOVA_BREAKPOINT)
        return fail("run did not stop at breakpoint", 6);
    if (nova_vm_get_pc(vm) != 12u)
        return fail("pc is not breakpoint address", 7);
    if (nova_vm_get_register(vm, 2u) != 0u)
        return fail("breakpoint executed instruction too early", 8);
    status = nova_vm_run(vm, 100u);
    if (status != NOVA_HALTED)
        return fail("resume did not complete", 9);
    if (nova_vm_get_register(vm, 2u) != 30u)
        return fail("ADD result after resume", 10);
    if (nova_vm_remove_breakpoint(vm, 12u) != NOVA_OK)
        return fail("remove breakpoint", 11);
    if (nova_vm_has_breakpoint(vm, 12u) != 0)
        return fail("breakpoint still present", 12);
    printf("DEBUGGER PASS PC=%u R2=%u INS=%llu\n", nova_vm_get_pc(vm), nova_vm_get_register(vm, 2u),
           (unsigned long long)nova_vm_get_instruction_count(vm));
    nova_vm_destroy(vm);
    return 0;
}
