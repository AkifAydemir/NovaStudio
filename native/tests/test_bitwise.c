#include "nova_vm.h"
#include <stdio.h>
int main(void) {
    const uint8_t program[] = {NOVA_OP_MOV_RI,
                               0,
                               0xF0,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_MOV_RI,
                               1,
                               0x0F,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_AND_RR,
                               2,
                               0,
                               1,
                               NOVA_OP_OR_RR,
                               3,
                               0,
                               1,
                               NOVA_OP_XOR_RR,
                               4,
                               0,
                               1,
                               NOVA_OP_MOV_RI,
                               5,
                               0x04,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_SHL_RR,
                               6,
                               1,
                               5,
                               NOVA_OP_SHR_RR,
                               7,
                               0,
                               5,
                               NOVA_OP_MOV_RI,
                               8,
                               0x24,
                               0x00,
                               0x00,
                               0x00,
                               NOVA_OP_SHL_RR,
                               9,
                               1,
                               8,
                               NOVA_OP_HALT};
    NovaVm *vm = nova_vm_create();
    int32_t status;
    if (vm == NULL)
        return 2;
    status = nova_vm_load(vm, program, (uint32_t)sizeof(program), 0u);
    if (status == NOVA_OK)
        status = nova_vm_run(vm, 100u);
    if (status != NOVA_HALTED || nova_vm_get_register(vm, 2u) != 0u ||
        nova_vm_get_register(vm, 3u) != 0xFFu || nova_vm_get_register(vm, 4u) != 0xFFu ||
        nova_vm_get_register(vm, 6u) != 0xF0u || nova_vm_get_register(vm, 7u) != 0x0Fu ||
        nova_vm_get_register(vm, 9u) != 0xF0u) {
        fprintf(stderr, "BITWISE mismatch status=%d and=%u or=%u xor=%u shl=%u shr=%u masked=%u\n",
                status, nova_vm_get_register(vm, 2u), nova_vm_get_register(vm, 3u),
                nova_vm_get_register(vm, 4u), nova_vm_get_register(vm, 6u),
                nova_vm_get_register(vm, 7u), nova_vm_get_register(vm, 9u));
        nova_vm_destroy(vm);
        return 3;
    }
    printf("BITWISE PASS and=0 or=255 xor=255 shl=240 shr=15 masked_shift=240\n");
    nova_vm_destroy(vm);
    return 0;
}
