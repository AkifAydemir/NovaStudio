using System.Runtime.InteropServices;
namespace NovaStudio.Interop;

internal static class NovaNative
{
    private const string LibraryName = "nova_vm.dll";
    internal const int Ok = 0;
    internal const int Halted = 1;
    internal const int Breakpoint = 2;
    internal const uint EventConsoleChar = 1;
    [StructLayout(LayoutKind.Sequential)]
    internal struct NovaEvent
    {
        public uint Type;
        public uint Value;
        public uint Address;
        public ulong InstructionCount;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NovaCallFrame
    {
        public uint TargetAddress;
        public uint ReturnAddress;
        public uint StackPointer;
        public uint Depth;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NovaHeapStats
    {
        public uint HeapBase;
        public uint HeapLimit;
        public uint TotalPayloadBytes;
        public uint UsedPayloadBytes;
        public uint FreePayloadBytes;
        public uint LargestFreeBlock;
        public uint BlockCount;
        public uint AllocationCount;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NovaHeapBlock
    {
        public uint HeaderAddress;
        public uint PayloadAddress;
        public uint PayloadSize;
        public uint TotalSize;
        public uint AllocationId;
        public uint IsUsed;
    }
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint nova_vm_create();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void nova_vm_destroy(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_reset(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_load(nint vm, byte[] bytecode, uint size, uint loadAddress);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_step(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_run(nint vm, ulong instructionBudget);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_add_breakpoint(nint vm, uint address);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_remove_breakpoint(nint vm, uint address);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void nova_vm_clear_breakpoints(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_has_breakpoint(nint vm, uint address);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint nova_vm_get_breakpoint_count(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint nova_vm_get_call_frame_count(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_get_call_frame(nint vm, uint index, out NovaCallFrame frame);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_heap_alloc(nint vm, uint size, out uint address);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_heap_free(nint vm, uint address);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_get_heap_stats(nint vm, out NovaHeapStats stats);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_get_heap_block(nint vm, uint index, out NovaHeapBlock block);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint nova_vm_get_register(nint vm, uint index);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint nova_vm_get_pc(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint nova_vm_get_sp(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint nova_vm_get_flags(nint vm);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte nova_vm_get_last_opcode(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong nova_vm_get_instruction_count(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_is_halted(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_get_last_status(nint vm);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_read_memory(nint vm, uint address, [Out] byte[] destination, uint size);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int nova_vm_poll_event(nint vm, out NovaEvent eventData);
}
