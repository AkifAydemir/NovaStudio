#include "nova_vm.h"
#include <stdio.h>
int main(void) {
    const uint8_t program[] = {NOVA_OP_MOV_RI,
                               1,
                               0x00,
                               0x00,
                               0x0F,
                               0x00,
                               NOVA_OP_MOV_RI,
                               0,
                               'N',
                               0,
                               0,
                               0,
                               NOVA_OP_STORE8,
                               1,
                               0,
                               NOVA_OP_MOV_RI,
                               0,
                               'O',
                               0,
                               0,
                               0,
                               NOVA_OP_STORE8,
                               1,
                               0,
                               NOVA_OP_MOV_RI,
                               0,
                               'V',
                               0,
                               0,
                               0,
                               NOVA_OP_STORE8,
                               1,
                               0,
                               NOVA_OP_MOV_RI,
                               0,
                               'A',
                               0,
                               0,
                               0,
                               NOVA_OP_STORE8,
                               1,
                               0,
                               NOVA_OP_MOV_RI,
                               0,
                               '\n',
                               0,
                               0,
                               0,
                               NOVA_OP_STORE8,
                               1,
                               0,
                               NOVA_OP_HALT};
    NovaVm *vm = nova_vm_create();
    NovaEvent event;
    int32_t status;
    if (!vm)
        return 2;
    status = nova_vm_load(vm, program, (uint32_t)sizeof(program), 0);
    if (status != NOVA_OK)
        return 3;
    status = nova_vm_run(vm, 1000);
    if (status != NOVA_HALTED)
        return 4;
    while (nova_vm_poll_event(vm, &event) > 0) {
        if (event.type == NOVA_EVENT_CONSOLE_CHAR) {
            putchar((char)event.value);
        }
    }
    printf("PC=%u SP=%u INS=%llu\n", nova_vm_get_pc(vm), nova_vm_get_sp(vm),
           (unsigned long long)nova_vm_get_instruction_count(vm));
    nova_vm_destroy(vm);
    return 0;
}
