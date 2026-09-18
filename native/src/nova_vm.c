#define NOVA_VM_BUILD
#include "nova_vm.h"
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#define NOVA_HEAP_HEADER_SIZE 16u
#define NOVA_HEAP_ALIGNMENT 8u
#define NOVA_HEAP_FLAG_USED 1u
#define NOVA_HEAP_MIN_PAYLOAD 8u
#define NOVA_HEAP_OFF_TOTAL 0u
#define NOVA_HEAP_OFF_PREV 4u
#define NOVA_HEAP_OFF_FLAGS 8u
#define NOVA_HEAP_OFF_ID 12u
struct NovaVm {
    uint32_t registers[NOVA_REGISTER_COUNT];
    uint32_t pc;
    uint32_t sp;
    uint32_t flags;
    uint64_t instruction_count;
    uint8_t last_opcode;
    uint8_t halted;
    int32_t last_status;
    uint32_t event_read;
    uint32_t event_write;
    NovaEvent events[NOVA_EVENT_CAPACITY];
    uint32_t breakpoint_count;
    uint32_t breakpoints[NOVA_BREAKPOINT_CAPACITY];
    uint32_t breakpoint_resume_pc;
    uint8_t breakpoint_resume_armed;
    uint32_t next_allocation_id;
    uint32_t call_frame_count;
    NovaCallFrame call_frames[NOVA_CALL_FRAME_CAPACITY];
    uint8_t *memory;
};
static void *nova_platform_alloc(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *result = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result == MAP_FAILED ? NULL : result;
#endif
}
static void nova_platform_free(void *memory, size_t size) {
    if (memory == NULL) {
        return;
    }
#ifdef _WIN32
    (void)size;
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, size);
#endif
}
static void nova_zero_bytes(uint8_t *destination, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) {
        destination[i] = 0;
    }
}
static void nova_copy_bytes(uint8_t *destination, const uint8_t *source, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) {
        destination[i] = source[i];
    }
}
static int nova_range_valid(uint32_t address, uint32_t size) {
    if (address >= NOVA_MEMORY_SIZE) {
        return 0;
    }
    if (size > NOVA_MEMORY_SIZE - address) {
        return 0;
    }
    return 1;
}
static int nova_register_valid(uint8_t index) {
    return index < NOVA_REGISTER_COUNT;
}
static int nova_breakpoint_index(const NovaVm *vm, uint32_t address) {
    uint32_t i;
    for (i = 0u; i < vm->breakpoint_count; ++i) {
        if (vm->breakpoints[i] == address) {
            return (int)i;
        }
    }
    return -1;
}
static void nova_set_zn_flags(NovaVm *vm, uint32_t value) {
    vm->flags &= ~(NOVA_FLAG_ZERO | NOVA_FLAG_NEGATIVE);
    if (value == 0u) {
        vm->flags |= NOVA_FLAG_ZERO;
    }
    if ((value & 0x80000000u) != 0u) {
        vm->flags |= NOVA_FLAG_NEGATIVE;
    }
}
static int32_t nova_fail(NovaVm *vm, int32_t status) {
    vm->last_status = status;
    return status;
}
static int32_t nova_fetch_u8(NovaVm *vm, uint8_t *value) {
    if (!nova_range_valid(vm->pc, 1u)) {
        return NOVA_ERR_BOUNDS;
    }
    *value = vm->memory[vm->pc++];
    return NOVA_OK;
}
static int32_t nova_fetch_u32(NovaVm *vm, uint32_t *value) {
    uint32_t p = vm->pc;
    if (!nova_range_valid(p, 4u)) {
        return NOVA_ERR_BOUNDS;
    }
    *value = ((uint32_t)vm->memory[p]) | ((uint32_t)vm->memory[p + 1u] << 8u) |
             ((uint32_t)vm->memory[p + 2u] << 16u) | ((uint32_t)vm->memory[p + 3u] << 24u);
    vm->pc += 4u;
    return NOVA_OK;
}
static int32_t nova_read_u32(const NovaVm *vm, uint32_t address, uint32_t *value) {
    if (!nova_range_valid(address, 4u)) {
        return NOVA_ERR_BOUNDS;
    }
    *value = ((uint32_t)vm->memory[address]) | ((uint32_t)vm->memory[address + 1u] << 8u) |
             ((uint32_t)vm->memory[address + 2u] << 16u) |
             ((uint32_t)vm->memory[address + 3u] << 24u);
    return NOVA_OK;
}
static void nova_enqueue_event(NovaVm *vm, uint32_t type, uint32_t value, uint32_t address) {
    uint32_t next = (vm->event_write + 1u) % NOVA_EVENT_CAPACITY;
    NovaEvent *event;
    if (next == vm->event_read) {
        vm->event_read = (vm->event_read + 1u) % NOVA_EVENT_CAPACITY;
    }
    event = &vm->events[vm->event_write];
    event->type = type;
    event->value = value;
    event->address = address;
    event->instruction_count = vm->instruction_count;
    vm->event_write = next;
}
static int32_t nova_write_u8(NovaVm *vm, uint32_t address, uint8_t value) {
    if (!nova_range_valid(address, 1u)) {
        return NOVA_ERR_BOUNDS;
    }
    if (address == NOVA_MMIO_CONSOLE) {
        nova_enqueue_event(vm, NOVA_EVENT_CONSOLE_CHAR, value, address);
        return NOVA_OK;
    }

    vm->memory[address] = value;
    return NOVA_OK;
}
static int32_t nova_write_u32(NovaVm *vm, uint32_t address, uint32_t value) {
    if (!nova_range_valid(address, 4u)) {
        return NOVA_ERR_BOUNDS;
    }
    if (address >= NOVA_MMIO_BASE) {
        uint32_t i;
        for (i = 0; i < 4u; ++i) {
            int32_t status = nova_write_u8(vm, address + i, (uint8_t)(value >> (8u * i)));
            if (status != NOVA_OK) {
                return status;
            }
        }
        return NOVA_OK;
    }
    vm->memory[address] = (uint8_t)(value & 0xFFu);
    vm->memory[address + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
    vm->memory[address + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
    vm->memory[address + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
    return NOVA_OK;
}
static uint32_t nova_align_up_u32(uint32_t value, uint32_t alignment) {
    uint32_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}
static int32_t nova_heap_read_header(const NovaVm *vm, uint32_t address, uint32_t *total_size,
                                     uint32_t *prev_size, uint32_t *flags,
                                     uint32_t *allocation_id) {
    if (address < NOVA_HEAP_BASE || address > NOVA_HEAP_LIMIT - NOVA_HEAP_HEADER_SIZE) {
        return NOVA_ERR_HEAP_CORRUPT;
    }
    if (nova_read_u32(vm, address + NOVA_HEAP_OFF_TOTAL, total_size) != NOVA_OK ||
        nova_read_u32(vm, address + NOVA_HEAP_OFF_PREV, prev_size) != NOVA_OK ||
        nova_read_u32(vm, address + NOVA_HEAP_OFF_FLAGS, flags) != NOVA_OK ||
        nova_read_u32(vm, address + NOVA_HEAP_OFF_ID, allocation_id) != NOVA_OK) {
        return NOVA_ERR_HEAP_CORRUPT;
    }
    if (*total_size < NOVA_HEAP_HEADER_SIZE || (*total_size % NOVA_HEAP_ALIGNMENT) != 0u ||
        *total_size > NOVA_HEAP_LIMIT - address || (*flags & ~NOVA_HEAP_FLAG_USED) != 0u) {
        return NOVA_ERR_HEAP_CORRUPT;
    }
    return NOVA_OK;
}
static int32_t nova_heap_write_header(

    NovaVm *vm, uint32_t address, uint32_t total_size, uint32_t prev_size, uint32_t flags,
    uint32_t allocation_id) {
    if (address < NOVA_HEAP_BASE || address > NOVA_HEAP_LIMIT - NOVA_HEAP_HEADER_SIZE ||
        total_size < NOVA_HEAP_HEADER_SIZE || total_size > NOVA_HEAP_LIMIT - address) {
        return NOVA_ERR_HEAP_CORRUPT;
    }
    if (nova_write_u32(vm, address + NOVA_HEAP_OFF_TOTAL, total_size) != NOVA_OK ||
        nova_write_u32(vm, address + NOVA_HEAP_OFF_PREV, prev_size) != NOVA_OK ||
        nova_write_u32(vm, address + NOVA_HEAP_OFF_FLAGS, flags) != NOVA_OK ||
        nova_write_u32(vm, address + NOVA_HEAP_OFF_ID, allocation_id) != NOVA_OK) {
        return NOVA_ERR_HEAP_CORRUPT;
    }
    return NOVA_OK;
}
static int32_t nova_heap_update_prev_size(NovaVm *vm, uint32_t address, uint32_t prev_size) {
    uint32_t total_size, old_prev, flags, allocation_id;
    if (address == NOVA_HEAP_LIMIT) {
        return NOVA_OK;
    }
    if (nova_heap_read_header(vm, address, &total_size, &old_prev, &flags, &allocation_id) !=
        NOVA_OK) {
        return NOVA_ERR_HEAP_CORRUPT;
    }
    (void)old_prev;
    return nova_heap_write_header(vm, address, total_size, prev_size, flags, allocation_id);
}
static int32_t nova_heap_init(NovaVm *vm) {
    uint32_t total_size = NOVA_HEAP_LIMIT - NOVA_HEAP_BASE;
    vm->next_allocation_id = 1u;
    return nova_heap_write_header(vm, NOVA_HEAP_BASE, total_size, 0u, 0u, 0u);
}
uint32_t nova_vm_get_call_frame_count(const NovaVm *vm) {
    return vm == NULL ? 0u : vm->call_frame_count;
}
int32_t nova_vm_get_call_frame(const NovaVm *vm, uint32_t index, NovaCallFrame *frame_out) {
    if (vm == NULL || frame_out == NULL)
        return NOVA_ERR_ARGUMENT;
    if (index >= vm->call_frame_count)
        return NOVA_ERR_BOUNDS;
    *frame_out = vm->call_frames[index];
    return NOVA_OK;
}
int32_t nova_vm_heap_alloc(NovaVm *vm, uint32_t size, uint32_t *address_out) {
    uint32_t aligned_size;
    uint32_t needed;
    uint32_t address;
    if (vm == NULL || address_out == NULL || size == 0u) {
        return NOVA_ERR_ARGUMENT;
    }
    if (size >
        (NOVA_HEAP_LIMIT - NOVA_HEAP_BASE) - NOVA_HEAP_HEADER_SIZE - (NOVA_HEAP_ALIGNMENT - 1u)) {
        return nova_fail(vm, NOVA_ERR_OUT_OF_MEMORY);
    }
    aligned_size = nova_align_up_u32(size, NOVA_HEAP_ALIGNMENT);
    needed = NOVA_HEAP_HEADER_SIZE + aligned_size;
    address = NOVA_HEAP_BASE;
    while (address < NOVA_HEAP_LIMIT) {
        uint32_t total_size, prev_size, flags, allocation_id;
        uint32_t payload_size;
        int32_t status =
            nova_heap_read_header(vm, address, &total_size, &prev_size, &flags, &allocation_id);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        payload_size = total_size - NOVA_HEAP_HEADER_SIZE;
        if ((flags & NOVA_HEAP_FLAG_USED) == 0u && payload_size >= aligned_size) {
            uint32_t remainder = total_size - needed;
            uint32_t id = vm->next_allocation_id++;
            uint32_t payload_address = address + NOVA_HEAP_HEADER_SIZE;
            uint32_t allocated_total = total_size;
            if (id == 0u) {
                id = vm->next_allocation_id++;
            }
            if (remainder >= NOVA_HEAP_HEADER_SIZE + NOVA_HEAP_MIN_PAYLOAD) {
                uint32_t split_address = address + needed;
                allocated_total = needed;
                if (nova_heap_write_header(vm, split_address, remainder, needed, 0u, 0u) !=
                        NOVA_OK ||
                    nova_heap_update_prev_size(vm, split_address + remainder, remainder) !=
                        NOVA_OK) {
                    return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
                }
            }
            if (nova_heap_write_header(vm, address, allocated_total, prev_size, NOVA_HEAP_FLAG_USED,
                                       id) != NOVA_OK) {
                return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
            }
            nova_zero_bytes(vm->memory + payload_address, allocated_total - NOVA_HEAP_HEADER_SIZE);
            *address_out = payload_address;
            vm->last_status = NOVA_OK;
            return NOVA_OK;
        }
        address += total_size;
    }
    return nova_fail(vm, NOVA_ERR_OUT_OF_MEMORY);
}
int32_t nova_vm_heap_free(NovaVm *vm, uint32_t payload_address) {
    uint32_t address = NOVA_HEAP_BASE;
    if (vm == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    while (address < NOVA_HEAP_LIMIT) {
        uint32_t total_size, prev_size, flags, allocation_id;
        int32_t status =
            nova_heap_read_header(vm, address, &total_size, &prev_size, &flags, &allocation_id);
        if (status != NOVA_OK) {

            return nova_fail(vm, status);
        }
        if (address + NOVA_HEAP_HEADER_SIZE == payload_address) {
            uint32_t current_address = address;
            uint32_t current_total = total_size;
            uint32_t current_prev = prev_size;
            uint32_t next_address;
            if ((flags & NOVA_HEAP_FLAG_USED) == 0u) {
                return nova_fail(vm, NOVA_ERR_INVALID_FREE);
            }
            if (nova_heap_write_header(vm, current_address, current_total, current_prev, 0u, 0u) !=
                NOVA_OK) {
                return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
            }
            next_address = current_address + current_total;
            if (next_address < NOVA_HEAP_LIMIT) {
                uint32_t next_total, next_prev, next_flags, next_id;
                if (nova_heap_read_header(vm, next_address, &next_total, &next_prev, &next_flags,
                                          &next_id) != NOVA_OK ||
                    next_prev != current_total) {
                    return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
                }
                if ((next_flags & NOVA_HEAP_FLAG_USED) == 0u) {
                    current_total += next_total;
                    if (nova_heap_write_header(vm, current_address, current_total, current_prev, 0u,
                                               0u) != NOVA_OK ||
                        nova_heap_update_prev_size(vm, current_address + current_total,
                                                   current_total) != NOVA_OK) {
                        return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
                    }
                }
            }
            if (current_prev != 0u) {
                uint32_t prev_address;
                uint32_t prev_total, prev_prev, prev_flags, prev_id;
                if (current_prev > current_address - NOVA_HEAP_BASE) {
                    return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
                }
                prev_address = current_address - current_prev;
                if (nova_heap_read_header(vm, prev_address, &prev_total, &prev_prev, &prev_flags,
                                          &prev_id) != NOVA_OK ||
                    prev_total != current_prev) {
                    return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
                }
                if ((prev_flags & NOVA_HEAP_FLAG_USED) == 0u) {
                    uint32_t merged_total = prev_total + current_total;
                    if (nova_heap_write_header(vm, prev_address, merged_total, prev_prev, 0u, 0u) !=
                            NOVA_OK ||
                        nova_heap_update_prev_size(vm, prev_address + merged_total, merged_total) !=
                            NOVA_OK) {
                        return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
                    }
                }
            }
            vm->last_status = NOVA_OK;
            return NOVA_OK;
        }
        address += total_size;
    }

    return nova_fail(vm, NOVA_ERR_INVALID_FREE);
}
int32_t nova_vm_get_heap_stats(const NovaVm *vm, NovaHeapStats *stats_out) {
    uint32_t address;
    if (vm == NULL || stats_out == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    stats_out->heap_base = NOVA_HEAP_BASE;
    stats_out->heap_limit = NOVA_HEAP_LIMIT;
    stats_out->total_payload_bytes = 0u;
    stats_out->used_payload_bytes = 0u;
    stats_out->free_payload_bytes = 0u;
    stats_out->largest_free_block = 0u;
    stats_out->block_count = 0u;
    stats_out->allocation_count = 0u;
    address = NOVA_HEAP_BASE;
    while (address < NOVA_HEAP_LIMIT) {
        uint32_t total_size, prev_size, flags, allocation_id;
        uint32_t payload_size;
        int32_t status =
            nova_heap_read_header(vm, address, &total_size, &prev_size, &flags, &allocation_id);
        (void)prev_size;
        (void)allocation_id;
        if (status != NOVA_OK) {
            return status;
        }
        payload_size = total_size - NOVA_HEAP_HEADER_SIZE;
        stats_out->total_payload_bytes += payload_size;
        stats_out->block_count++;
        if ((flags & NOVA_HEAP_FLAG_USED) != 0u) {
            stats_out->used_payload_bytes += payload_size;
            stats_out->allocation_count++;
        } else {
            stats_out->free_payload_bytes += payload_size;
            if (payload_size > stats_out->largest_free_block) {
                stats_out->largest_free_block = payload_size;
            }
        }
        address += total_size;
    }
    return address == NOVA_HEAP_LIMIT ? NOVA_OK : NOVA_ERR_HEAP_CORRUPT;
}
int32_t nova_vm_get_heap_block(const NovaVm *vm, uint32_t index, NovaHeapBlock *block_out) {
    uint32_t address;
    uint32_t current_index = 0u;
    if (vm == NULL || block_out == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    address = NOVA_HEAP_BASE;
    while (address < NOVA_HEAP_LIMIT) {
        uint32_t total_size, prev_size, flags, allocation_id;
        int32_t status =
            nova_heap_read_header(vm, address, &total_size, &prev_size, &flags, &allocation_id);
        (void)prev_size;
        if (status != NOVA_OK) {
            return status;
        }
        if (current_index == index) {
            block_out->header_address = address;
            block_out->payload_address = address + NOVA_HEAP_HEADER_SIZE;
            block_out->payload_size = total_size - NOVA_HEAP_HEADER_SIZE;
            block_out->total_size = total_size;
            block_out->allocation_id = allocation_id;
            block_out->is_used = (flags & NOVA_HEAP_FLAG_USED) != 0u ? 1u : 0u;
            return NOVA_OK;
        }
        current_index++;
        address += total_size;
    }
    return NOVA_ERR_BOUNDS;
}
static int32_t nova_push_u32(NovaVm *vm, uint32_t value) {
    if (vm->sp < NOVA_STACK_BASE + 4u) {
        return NOVA_ERR_STACK;
    }
    vm->sp -= 4u;
    return nova_write_u32(vm, vm->sp, value);
}
static int32_t nova_pop_u32(NovaVm *vm, uint32_t *value) {
    int32_t status;
    if (vm->sp > NOVA_STACK_TOP - 4u) {
        return NOVA_ERR_STACK;
    }
    status = nova_read_u32(vm, vm->sp, value);
    if (status != NOVA_OK) {
        return status;
    }
    vm->sp += 4u;
    return NOVA_OK;
}
NovaVm *nova_vm_create(void) {
    size_t total_size = sizeof(NovaVm) + NOVA_MEMORY_SIZE;
    NovaVm *vm = (NovaVm *)nova_platform_alloc(total_size);
    if (vm == NULL) {
        return NULL;
    }
    nova_zero_bytes((uint8_t *)vm, total_size);
    vm->memory = ((uint8_t *)vm) + sizeof(NovaVm);
    if (nova_vm_reset(vm) != NOVA_OK) {
        nova_platform_free(vm, total_size);
        return NULL;
    }
    return vm;
}
void nova_vm_destroy(NovaVm *vm) {
    nova_platform_free(vm, sizeof(NovaVm) + NOVA_MEMORY_SIZE);
}
int32_t nova_vm_reset(NovaVm *vm) {
    if (vm == NULL) {
        return NOVA_ERR_ARGUMENT;
    }

    nova_zero_bytes((uint8_t *)vm->registers, sizeof(vm->registers));
    nova_zero_bytes(vm->memory, NOVA_MEMORY_SIZE);
    nova_zero_bytes((uint8_t *)vm->events, sizeof(vm->events));
    nova_zero_bytes((uint8_t *)vm->call_frames, sizeof(vm->call_frames));
    vm->pc = 0u;
    vm->sp = NOVA_STACK_TOP;
    vm->flags = 0u;
    vm->instruction_count = 0u;
    vm->last_opcode = NOVA_OP_NOP;
    vm->halted = 0u;
    vm->last_status = NOVA_OK;
    vm->event_read = 0u;
    vm->event_write = 0u;
    vm->call_frame_count = 0u;
    vm->breakpoint_resume_pc = 0u;
    vm->breakpoint_resume_armed = 0u;
    if (nova_heap_init(vm) != NOVA_OK) {
        return nova_fail(vm, NOVA_ERR_HEAP_CORRUPT);
    }
    return NOVA_OK;
}
int32_t nova_vm_load(NovaVm *vm, const uint8_t *bytecode, uint32_t size, uint32_t load_address) {
    if (vm == NULL || bytecode == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    if (!nova_range_valid(load_address, size) || load_address > NOVA_HEAP_BASE ||
        size > NOVA_HEAP_BASE - load_address) {
        return nova_fail(vm, NOVA_ERR_BOUNDS);
    }
    nova_copy_bytes(vm->memory + load_address, bytecode, size);
    vm->pc = load_address;
    vm->halted = 0u;
    vm->last_status = NOVA_OK;
    return NOVA_OK;
}
int32_t nova_vm_step(NovaVm *vm) {
    uint8_t opcode;
    int32_t status;
    if (vm == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    if (vm->halted) {
        vm->last_status = NOVA_HALTED;
        return NOVA_HALTED;
    }
    if (vm->breakpoint_resume_armed && vm->breakpoint_resume_pc == vm->pc) {
        vm->breakpoint_resume_armed = 0u;
    }
    status = nova_fetch_u8(vm, &opcode);
    if (status != NOVA_OK) {
        return nova_fail(vm, status);
    }
    vm->last_opcode = opcode;
    vm->instruction_count++;

    switch ((NovaOpcode)opcode) {
    case NOVA_OP_NOP:
        break;
    case NOVA_OP_HALT:
        vm->halted = 1u;
        vm->last_status = NOVA_HALTED;
        return NOVA_HALTED;
    case NOVA_OP_MOV_RI: {
        uint8_t dst;
        uint32_t imm;
        if (nova_fetch_u8(vm, &dst) != NOVA_OK || nova_fetch_u32(vm, &imm) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(dst)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        vm->registers[dst] = imm;
        nova_set_zn_flags(vm, imm);
        break;
    }
    case NOVA_OP_MOV_RR: {
        uint8_t dst, src;
        if (nova_fetch_u8(vm, &dst) != NOVA_OK || nova_fetch_u8(vm, &src) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(dst) || !nova_register_valid(src)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        vm->registers[dst] = vm->registers[src];
        nova_set_zn_flags(vm, vm->registers[dst]);
        break;
    }
    case NOVA_OP_ADD_RR:
    case NOVA_OP_SUB_RR:
    case NOVA_OP_MUL_RR:
    case NOVA_OP_DIV_RR:
    case NOVA_OP_AND_RR:
    case NOVA_OP_OR_RR:
    case NOVA_OP_XOR_RR:
    case NOVA_OP_SHL_RR:
    case NOVA_OP_SHR_RR: {
        uint8_t dst, lhs, rhs;
        uint32_t a, b, result;
        if (nova_fetch_u8(vm, &dst) != NOVA_OK || nova_fetch_u8(vm, &lhs) != NOVA_OK ||
            nova_fetch_u8(vm, &rhs) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(dst) || !nova_register_valid(lhs) || !nova_register_valid(rhs)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        a = vm->registers[lhs];
        b = vm->registers[rhs];
        if (opcode == NOVA_OP_ADD_RR) {
            uint64_t wide = (uint64_t)a + (uint64_t)b;

            result = (uint32_t)wide;
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
            if ((wide >> 32u) != 0u) {
                vm->flags |= NOVA_FLAG_CARRY;
            }
            if ((((a ^ result) & (b ^ result)) & 0x80000000u) != 0u) {
                vm->flags |= NOVA_FLAG_OVERFLOW;
            }
        } else if (opcode == NOVA_OP_SUB_RR) {
            result = a - b;
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
            if (a >= b) {
                vm->flags |= NOVA_FLAG_CARRY;
            }
            if ((((a ^ b) & (a ^ result)) & 0x80000000u) != 0u) {
                vm->flags |= NOVA_FLAG_OVERFLOW;
            }
        } else if (opcode == NOVA_OP_MUL_RR) {
            result = a * b;
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
        } else if (opcode == NOVA_OP_DIV_RR) {
            if (b == 0u) {
                return nova_fail(vm, NOVA_ERR_DIVIDE_BY_ZERO);
            }
            result = a / b;
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
        } else if (opcode == NOVA_OP_AND_RR) {
            result = a & b;
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
        } else if (opcode == NOVA_OP_OR_RR) {
            result = a | b;
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
        } else if (opcode == NOVA_OP_XOR_RR) {
            result = a ^ b;
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
        } else if (opcode == NOVA_OP_SHL_RR) {
            result = a << (b & 31u);
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
        } else {
            result = a >> (b & 31u);
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
        }
        vm->registers[dst] = result;
        nova_set_zn_flags(vm, result);
        break;
    }
    case NOVA_OP_CMP_RR: {
        uint8_t lhs, rhs;
        uint32_t result;
        if (nova_fetch_u8(vm, &lhs) != NOVA_OK || nova_fetch_u8(vm, &rhs) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(lhs) || !nova_register_valid(rhs)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        {
            uint32_t a = vm->registers[lhs];
            uint32_t b = vm->registers[rhs];

            result = a - b;
            vm->flags &= ~(NOVA_FLAG_CARRY | NOVA_FLAG_OVERFLOW);
            if (a >= b) {
                vm->flags |= NOVA_FLAG_CARRY;
            }
            if ((((a ^ b) & (a ^ result)) & 0x80000000u) != 0u) {
                vm->flags |= NOVA_FLAG_OVERFLOW;
            }
        }
        nova_set_zn_flags(vm, result);
        break;
    }
    case NOVA_OP_JMP:
    case NOVA_OP_JE:
    case NOVA_OP_JNE:
    case NOVA_OP_JL:
    case NOVA_OP_JLE:
    case NOVA_OP_JG:
    case NOVA_OP_JGE: {
        uint32_t target;
        int should_jump = 1;
        int zero;
        int negative;
        int overflow;
        if (nova_fetch_u32(vm, &target) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        zero = (vm->flags & NOVA_FLAG_ZERO) != 0u;
        negative = (vm->flags & NOVA_FLAG_NEGATIVE) != 0u;
        overflow = (vm->flags & NOVA_FLAG_OVERFLOW) != 0u;
        if (opcode == NOVA_OP_JE) {
            should_jump = zero;
        } else if (opcode == NOVA_OP_JNE) {
            should_jump = !zero;
        } else if (opcode == NOVA_OP_JL) {
            should_jump = negative != overflow;
        } else if (opcode == NOVA_OP_JLE) {
            should_jump = zero || (negative != overflow);
        } else if (opcode == NOVA_OP_JG) {
            should_jump = !zero && (negative == overflow);
        } else if (opcode == NOVA_OP_JGE) {
            should_jump = negative == overflow;
        }
        if (should_jump) {
            if (!nova_range_valid(target, 1u)) {
                return nova_fail(vm, NOVA_ERR_BOUNDS);
            }
            vm->pc = target;
        }
        break;
    }
    case NOVA_OP_PUSH: {
        uint8_t src;
        if (nova_fetch_u8(vm, &src) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(src)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        status = nova_push_u32(vm, vm->registers[src]);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        break;
    }
    case NOVA_OP_POP: {
        uint8_t dst;
        uint32_t value;
        if (nova_fetch_u8(vm, &dst) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(dst)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        status = nova_pop_u32(vm, &value);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        vm->registers[dst] = value;
        nova_set_zn_flags(vm, value);
        break;
    }
    case NOVA_OP_CALL: {
        uint32_t target;
        NovaCallFrame *frame;
        if (nova_fetch_u32(vm, &target) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_range_valid(target, 1u)) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (vm->call_frame_count >= NOVA_CALL_FRAME_CAPACITY) {
            return nova_fail(vm, NOVA_ERR_STACK);
        }
        status = nova_push_u32(vm, vm->pc);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        frame = &vm->call_frames[vm->call_frame_count];
        frame->target_address = target;
        frame->return_address = vm->pc;
        frame->stack_pointer = vm->sp;
        frame->depth = vm->call_frame_count;
        vm->call_frame_count++;
        vm->pc = target;
        break;
    }
    case NOVA_OP_RET: {
        uint32_t return_address;
        if (vm->call_frame_count == 0u) {
            return nova_fail(vm, NOVA_ERR_STACK);
        }
        status = nova_pop_u32(vm, &return_address);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        if (!nova_range_valid(return_address, 1u)) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (vm->call_frames[vm->call_frame_count - 1u].return_address != return_address) {
            return nova_fail(vm, NOVA_ERR_STACK);
        }
        vm->call_frame_count--;
        vm->pc = return_address;
        break;
    }
    case NOVA_OP_STORE8: {
        uint8_t address_reg, value_reg;
        if (nova_fetch_u8(vm, &address_reg) != NOVA_OK ||
            nova_fetch_u8(vm, &value_reg) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(address_reg) || !nova_register_valid(value_reg)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        status = nova_write_u8(vm, vm->registers[address_reg], (uint8_t)vm->registers[value_reg]);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        break;
    }
    case NOVA_OP_LOAD8: {
        uint8_t dst, address_reg;
        uint32_t address;
        if (nova_fetch_u8(vm, &dst) != NOVA_OK || nova_fetch_u8(vm, &address_reg) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(dst) || !nova_register_valid(address_reg)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        address = vm->registers[address_reg];
        if (!nova_range_valid(address, 1u)) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        vm->registers[dst] = vm->memory[address];
        nova_set_zn_flags(vm, vm->registers[dst]);
        break;
    }
    case NOVA_OP_STORE32: {
        uint8_t address_reg, value_reg;
        if (nova_fetch_u8(vm, &address_reg) != NOVA_OK ||
            nova_fetch_u8(vm, &value_reg) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(address_reg) || !nova_register_valid(value_reg)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        status = nova_write_u32(vm, vm->registers[address_reg], vm->registers[value_reg]);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        break;
    }

    case NOVA_OP_LOAD32: {
        uint8_t dst, address_reg;
        uint32_t value;
        if (nova_fetch_u8(vm, &dst) != NOVA_OK || nova_fetch_u8(vm, &address_reg) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(dst) || !nova_register_valid(address_reg)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        status = nova_read_u32(vm, vm->registers[address_reg], &value);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        vm->registers[dst] = value;
        nova_set_zn_flags(vm, value);
        break;
    }
    case NOVA_OP_ALLOC: {
        uint8_t dst, size_reg;
        uint32_t address;
        if (nova_fetch_u8(vm, &dst) != NOVA_OK || nova_fetch_u8(vm, &size_reg) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(dst) || !nova_register_valid(size_reg)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        status = nova_vm_heap_alloc(vm, vm->registers[size_reg], &address);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        vm->registers[dst] = address;
        nova_set_zn_flags(vm, address);
        break;
    }
    case NOVA_OP_FREE: {
        uint8_t address_reg;
        if (nova_fetch_u8(vm, &address_reg) != NOVA_OK) {
            return nova_fail(vm, NOVA_ERR_BOUNDS);
        }
        if (!nova_register_valid(address_reg)) {
            return nova_fail(vm, NOVA_ERR_BAD_REGISTER);
        }
        status = nova_vm_heap_free(vm, vm->registers[address_reg]);
        if (status != NOVA_OK) {
            return nova_fail(vm, status);
        }
        break;
    }
    default:
        return nova_fail(vm, NOVA_ERR_BAD_OPCODE);
    }
    vm->last_status = NOVA_OK;
    return NOVA_OK;
}
int32_t nova_vm_run(NovaVm *vm, uint64_t instruction_budget) {

    uint64_t executed = 0u;
    if (vm == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    while (!vm->halted && executed < instruction_budget) {
        if (nova_breakpoint_index(vm, vm->pc) >= 0) {
            if (vm->breakpoint_resume_armed && vm->breakpoint_resume_pc == vm->pc) {
                vm->breakpoint_resume_armed = 0u;
            } else {
                vm->breakpoint_resume_pc = vm->pc;
                vm->breakpoint_resume_armed = 1u;
                vm->last_status = NOVA_BREAKPOINT;
                return NOVA_BREAKPOINT;
            }
        }
        {
            int32_t status = nova_vm_step(vm);
            if (status < 0) {
                return status;
            }
            if (status == NOVA_HALTED) {
                return NOVA_HALTED;
            }
        }
        executed++;
    }
    return vm->halted ? NOVA_HALTED : NOVA_OK;
}
int32_t nova_vm_add_breakpoint(NovaVm *vm, uint32_t address) {
    if (vm == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    if (!nova_range_valid(address, 1u)) {
        return nova_fail(vm, NOVA_ERR_BOUNDS);
    }
    if (nova_breakpoint_index(vm, address) >= 0) {
        return NOVA_OK;
    }
    if (vm->breakpoint_count >= NOVA_BREAKPOINT_CAPACITY) {
        return nova_fail(vm, NOVA_ERR_ALLOCATION);
    }
    vm->breakpoints[vm->breakpoint_count++] = address;
    return NOVA_OK;
}
int32_t nova_vm_remove_breakpoint(NovaVm *vm, uint32_t address) {
    int index;
    uint32_t i;
    if (vm == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    index = nova_breakpoint_index(vm, address);
    if (index < 0) {
        return NOVA_OK;
    }
    for (i = (uint32_t)index; i + 1u < vm->breakpoint_count; ++i) {
        vm->breakpoints[i] = vm->breakpoints[i + 1u];
    }

    vm->breakpoint_count--;
    if (vm->breakpoint_resume_armed && vm->breakpoint_resume_pc == address) {
        vm->breakpoint_resume_armed = 0u;
    }
    return NOVA_OK;
}
void nova_vm_clear_breakpoints(NovaVm *vm) {
    if (vm == NULL) {
        return;
    }
    vm->breakpoint_count = 0u;
    vm->breakpoint_resume_pc = 0u;
    vm->breakpoint_resume_armed = 0u;
}
int32_t nova_vm_has_breakpoint(const NovaVm *vm, uint32_t address) {
    if (vm == NULL) {
        return 0;
    }
    return nova_breakpoint_index(vm, address) >= 0 ? 1 : 0;
}
uint32_t nova_vm_get_breakpoint_count(const NovaVm *vm) {
    return vm == NULL ? 0u : vm->breakpoint_count;
}
uint32_t nova_vm_get_register(const NovaVm *vm, uint32_t index) {
    if (vm == NULL || index >= NOVA_REGISTER_COUNT) {
        return 0u;
    }
    return vm->registers[index];
}
uint32_t nova_vm_get_pc(const NovaVm *vm) {
    return vm == NULL ? 0u : vm->pc;
}
uint32_t nova_vm_get_sp(const NovaVm *vm) {
    return vm == NULL ? 0u : vm->sp;
}
uint32_t nova_vm_get_flags(const NovaVm *vm) {
    return vm == NULL ? 0u : vm->flags;
}
uint8_t nova_vm_get_last_opcode(const NovaVm *vm) {
    return vm == NULL ? 0u : vm->last_opcode;
}
uint64_t nova_vm_get_instruction_count(const NovaVm *vm) {
    return vm == NULL ? 0u : vm->instruction_count;
}
int32_t nova_vm_is_halted(const NovaVm *vm) {
    return vm == NULL ? 1 : (int32_t)vm->halted;
}
int32_t nova_vm_get_last_status(const NovaVm *vm) {
    return vm == NULL ? NOVA_ERR_ARGUMENT : vm->last_status;
}
int32_t nova_vm_read_memory(const NovaVm *vm, uint32_t address, uint8_t *destination,
                            uint32_t size) {
    if (vm == NULL || destination == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    if (!nova_range_valid(address, size)) {
        return NOVA_ERR_BOUNDS;
    }
    nova_copy_bytes(destination, vm->memory + address, size);
    return NOVA_OK;
}
int32_t nova_vm_poll_event(NovaVm *vm, NovaEvent *event_out) {
    if (vm == NULL || event_out == NULL) {
        return NOVA_ERR_ARGUMENT;
    }
    if (vm->event_read == vm->event_write) {
        event_out->type = NOVA_EVENT_NONE;
        event_out->value = 0u;
        event_out->address = 0u;
        event_out->instruction_count = vm->instruction_count;
        return 0;
    }
    *event_out = vm->events[vm->event_read];
    vm->event_read = (vm->event_read + 1u) % NOVA_EVENT_CAPACITY;
    return 1;
}
