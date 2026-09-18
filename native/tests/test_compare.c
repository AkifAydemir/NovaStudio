#include "nova_vm.h"
#include <stdio.h>
static int run_case(uint8_t branch, int expect_jump, const char *name) {
    /* CMP INT32_MIN, 1 produces 0x7fffffff with overflow. Signed JL must still jump. */
    const uint8_t program[] = {NOVA_OP_MOV_RI,
                               0,
                               0x00,
                               0x00,
                               0x00,
                               0x80,
                               NOVA_OP_MOV_RI,
                               1,
                               0x01,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_CMP_RR,
                               0,
                               1,
                               branch,
                               0x1C,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_MOV_RI,
                               2,
                               0x00,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_JMP,
                               0x22,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_MOV_RI,
                               2,
                               0x01,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_HALT};
    NovaVm *vm = nova_vm_create();
    int32_t status;
    uint32_t actual;
    if (vm == NULL)
        return 10;
    status = nova_vm_load(vm, program, (uint32_t)sizeof(program), 0u);
    if (status == NOVA_OK)
        status = nova_vm_run(vm, 100u);
    actual = nova_vm_get_register(vm, 2u);
    nova_vm_destroy(vm);
    if (status != NOVA_HALTED || actual != (uint32_t)expect_jump) {
        fprintf(stderr, "%s failed: status=%d actual=%u expected=%d\n", name, status, actual,
                expect_jump);
        return 11;
    }
    return 0;
}
int main(void) {
    int rc;
    rc = run_case(NOVA_OP_JL, 1, "JL");
    if (rc)
        return rc;
    rc = run_case(NOVA_OP_JGE, 0, "JGE");
    if (rc)
        return rc;
    printf("COMPARE PASS signed overflow-aware branches\n");
    return 0;
}
