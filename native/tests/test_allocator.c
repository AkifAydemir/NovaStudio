#include "nova_vm.h"
#include <stdio.h>
static int fail(const char *message, int code) {
    fprintf(stderr, "FAIL: %s\n", message);
    return code;
}
int main(void) {
    NovaVm *vm = nova_vm_create();
    NovaHeapStats stats;
    NovaHeapBlock block;
    uint32_t a = 0u;
    uint32_t b = 0u;
    int32_t status;
    if (!vm)
        return fail("vm allocation", 2);
    if (nova_vm_get_heap_stats(vm, &stats) != NOVA_OK)
        return fail("initial stats", 3);
    if (stats.block_count != 1u || stats.allocation_count != 0u)
        return fail("initial heap shape", 4);
    if (nova_vm_heap_alloc(vm, 100u, &a) != NOVA_OK)
        return fail("alloc a", 5);
    if (nova_vm_heap_alloc(vm, 200u, &b) != NOVA_OK)
        return fail("alloc b", 6);
    if (a == 0u || b == 0u || a == b)
        return fail("allocation addresses", 7);
    if (nova_vm_get_heap_stats(vm, &stats) != NOVA_OK)
        return fail("stats after alloc", 8);
    if (stats.block_count != 3u || stats.allocation_count != 2u)
        return fail("split shape", 9);
    if (nova_vm_get_heap_block(vm, 0u, &block) != NOVA_OK || block.is_used == 0u ||
        block.payload_address != a)
        return fail("first block inspection", 10);
    if (nova_vm_heap_free(vm, a) != NOVA_OK)
        return fail("free a", 11);
    if (nova_vm_heap_free(vm, b) != NOVA_OK)
        return fail("free b", 12);
    if (nova_vm_get_heap_stats(vm, &stats) != NOVA_OK)
        return fail("stats after free", 13);
    if (stats.block_count != 1u || stats.allocation_count != 0u)
        return fail("coalescing did not restore single free block", 14);
    if (nova_vm_heap_free(vm, a) != NOVA_ERR_INVALID_FREE)
        return fail("double free guard", 15);
    {
        const uint8_t program[] = {NOVA_OP_MOV_RI, 0, 64,          0, 0, 0, NOVA_OP_ALLOC, 1, 0,
                                   NOVA_OP_FREE,   1, NOVA_OP_HALT};
        if (nova_vm_reset(vm) != NOVA_OK)

            return fail("reset before bytecode heap test", 16);
        if (nova_vm_load(vm, program, (uint32_t)sizeof(program), 0u) != NOVA_OK)
            return fail("load heap bytecode", 17);
        status = nova_vm_run(vm, 100u);
        if (status != NOVA_HALTED)
            return fail("heap bytecode did not halt", 18);
        if (nova_vm_get_register(vm, 1u) < NOVA_HEAP_BASE ||
            nova_vm_get_register(vm, 1u) >= NOVA_HEAP_LIMIT)
            return fail("ALLOC opcode returned invalid pointer", 19);
        if (nova_vm_get_heap_stats(vm, &stats) != NOVA_OK || stats.block_count != 1u ||
            stats.allocation_count != 0u)
            return fail("ALLOC/FREE opcode did not coalesce", 20);
    }
    printf("ALLOCATOR PASS blocks=%u free=%u largest=%u\n", stats.block_count,
           stats.free_payload_bytes, stats.largest_free_block);
    nova_vm_destroy(vm);
    return 0;
}
