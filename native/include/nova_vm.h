#ifndef NOVA_VM_H
#define NOVA_VM_H
#include <stdint.h>
#ifdef _WIN32
#ifdef NOVA_VM_BUILD
#define NOVA_API __declspec(dllexport)
#else
#define NOVA_API __declspec(dllimport)
#endif
#else
#define NOVA_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define NOVA_REGISTER_COUNT 16u
#define NOVA_MEMORY_SIZE (1024u * 1024u)
#define NOVA_HEAP_BASE 0x00010000u
#define NOVA_HEAP_LIMIT 0x000C0000u
#define NOVA_STACK_BASE 0x000C0000u
#define NOVA_STACK_TOP 0x000E0000u
#define NOVA_MMIO_BASE 0x000F0000u
#define NOVA_MMIO_CONSOLE (NOVA_MMIO_BASE + 0x0000u)
#define NOVA_EVENT_CAPACITY 256u
#define NOVA_BREAKPOINT_CAPACITY 128u
#define NOVA_CALL_FRAME_CAPACITY 64u
#define NOVA_FLAG_ZERO (1u << 0)
#define NOVA_FLAG_NEGATIVE (1u << 1)
#define NOVA_FLAG_CARRY (1u << 2)
#define NOVA_FLAG_OVERFLOW (1u << 3)
typedef struct NovaVm NovaVm;
typedef enum NovaStatus {
    NOVA_OK = 0,
    NOVA_HALTED = 1,
    NOVA_BREAKPOINT = 2,
    NOVA_ERR_ARGUMENT = -1,
    NOVA_ERR_ALLOCATION = -2,
    NOVA_ERR_BOUNDS = -3,
    NOVA_ERR_BAD_OPCODE = -4,
    NOVA_ERR_BAD_REGISTER = -5,
    NOVA_ERR_DIVIDE_BY_ZERO = -6,
    NOVA_ERR_STACK = -7,
    NOVA_ERR_OUT_OF_MEMORY = -8,
    NOVA_ERR_INVALID_FREE = -9,
    NOVA_ERR_HEAP_CORRUPT = -10
} NovaStatus;
typedef enum NovaOpcode {
    NOVA_OP_NOP = 0x00,
    NOVA_OP_HALT = 0x01,
    NOVA_OP_MOV_RI = 0x10,
    NOVA_OP_MOV_RR = 0x11,

    NOVA_OP_ADD_RR = 0x20,
    NOVA_OP_SUB_RR = 0x21,
    NOVA_OP_MUL_RR = 0x22,
    NOVA_OP_DIV_RR = 0x23,
    NOVA_OP_AND_RR = 0x24,
    NOVA_OP_OR_RR = 0x25,
    NOVA_OP_XOR_RR = 0x26,
    NOVA_OP_SHL_RR = 0x27,
    NOVA_OP_SHR_RR = 0x28,
    NOVA_OP_CMP_RR = 0x30,
    NOVA_OP_JMP = 0x40,
    NOVA_OP_JE = 0x41,
    NOVA_OP_JNE = 0x42,
    NOVA_OP_JL = 0x43,
    NOVA_OP_JLE = 0x44,
    NOVA_OP_JG = 0x45,
    NOVA_OP_JGE = 0x46,
    NOVA_OP_PUSH = 0x50,
    NOVA_OP_POP = 0x51,
    NOVA_OP_CALL = 0x52,
    NOVA_OP_RET = 0x53,
    NOVA_OP_STORE8 = 0x60,
    NOVA_OP_LOAD8 = 0x61,
    NOVA_OP_STORE32 = 0x62,
    NOVA_OP_LOAD32 = 0x63,
    NOVA_OP_ALLOC = 0x70,
    NOVA_OP_FREE = 0x71
} NovaOpcode;
typedef enum NovaEventType { NOVA_EVENT_NONE = 0, NOVA_EVENT_CONSOLE_CHAR = 1 } NovaEventType;
typedef struct NovaEvent {
    uint32_t type;
    uint32_t value;
    uint32_t address;
    uint64_t instruction_count;
} NovaEvent;
typedef struct NovaHeapStats {
    uint32_t heap_base;
    uint32_t heap_limit;
    uint32_t total_payload_bytes;
    uint32_t used_payload_bytes;
    uint32_t free_payload_bytes;
    uint32_t largest_free_block;
    uint32_t block_count;
    uint32_t allocation_count;
} NovaHeapStats;
typedef struct NovaHeapBlock {
    uint32_t header_address;

    uint32_t payload_address;
    uint32_t payload_size;
    uint32_t total_size;
    uint32_t allocation_id;
    uint32_t is_used;
} NovaHeapBlock;
typedef struct NovaCallFrame {
    uint32_t target_address;
    uint32_t return_address;
    uint32_t stack_pointer;
    uint32_t depth;
} NovaCallFrame;
NOVA_API NovaVm *nova_vm_create(void);
NOVA_API void nova_vm_destroy(NovaVm *vm);
NOVA_API int32_t nova_vm_reset(NovaVm *vm);
NOVA_API int32_t nova_vm_load(NovaVm *vm, const uint8_t *bytecode, uint32_t size,
                              uint32_t load_address);
NOVA_API int32_t nova_vm_step(NovaVm *vm);
NOVA_API int32_t nova_vm_run(NovaVm *vm, uint64_t instruction_budget);
NOVA_API int32_t nova_vm_add_breakpoint(NovaVm *vm, uint32_t address);
NOVA_API int32_t nova_vm_remove_breakpoint(NovaVm *vm, uint32_t address);
NOVA_API void nova_vm_clear_breakpoints(NovaVm *vm);
NOVA_API int32_t nova_vm_has_breakpoint(const NovaVm *vm, uint32_t address);
NOVA_API uint32_t nova_vm_get_breakpoint_count(const NovaVm *vm);
NOVA_API uint32_t nova_vm_get_call_frame_count(const NovaVm *vm);
NOVA_API int32_t nova_vm_get_call_frame(const NovaVm *vm, uint32_t index, NovaCallFrame *frame_out);
NOVA_API int32_t nova_vm_heap_alloc(NovaVm *vm, uint32_t size, uint32_t *address_out);
NOVA_API int32_t nova_vm_heap_free(NovaVm *vm, uint32_t address);
NOVA_API int32_t nova_vm_get_heap_stats(const NovaVm *vm, NovaHeapStats *stats_out);
NOVA_API int32_t nova_vm_get_heap_block(const NovaVm *vm, uint32_t index, NovaHeapBlock *block_out);
NOVA_API uint32_t nova_vm_get_register(const NovaVm *vm, uint32_t index);
NOVA_API uint32_t nova_vm_get_pc(const NovaVm *vm);
NOVA_API uint32_t nova_vm_get_sp(const NovaVm *vm);
NOVA_API uint32_t nova_vm_get_flags(const NovaVm *vm);
NOVA_API uint8_t nova_vm_get_last_opcode(const NovaVm *vm);
NOVA_API uint64_t nova_vm_get_instruction_count(const NovaVm *vm);
NOVA_API int32_t nova_vm_is_halted(const NovaVm *vm);
NOVA_API int32_t nova_vm_get_last_status(const NovaVm *vm);
NOVA_API int32_t nova_vm_read_memory(const NovaVm *vm, uint32_t address, uint8_t *destination,
                                     uint32_t size);
NOVA_API int32_t nova_vm_poll_event(NovaVm *vm, NovaEvent *event_out);
#ifdef __cplusplus
}
#endif
#endif
