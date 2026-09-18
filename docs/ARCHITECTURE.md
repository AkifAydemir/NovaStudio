# Architecture

Nova separates compilation, execution, and presentation behind explicit native
interfaces. The WPF process owns project files and visualization state; the
compiler owns source analysis and bytecode generation; the VM owns runtime state.

## Component boundaries

### NovaC

`compiler/` implements a bounded multi-translation-unit C frontend. Its public C
ABI accepts caller-owned source buffers and caller-owned output/metadata buffers.
The latest entry point emits bytecode together with source maps, symbols,
functions, globals, references, include relationships, declarations, strings,
and macro definitions.

The implementation uses fixed-capacity tables for deterministic failure modes.
Large per-compilation scratch storage is allocated as one private OS-backed
workspace. No mutable global compiler workspace is shared between calls.

### NovaVM

`native/` implements the bytecode runtime. The public ABI exposes:

- sixteen 32-bit registers and explicit PC/SP/flags state;
- a one MiB address space with heap, stack, globals, and MMIO regions;
- arithmetic, bitwise, comparison, branching, call, memory, and allocation
  instructions;
- breakpoints, call frames, heap inspection, memory reads, and runtime events.

Console output is modeled as an MMIO event instead of being written directly by
the VM. This keeps runtime behavior observable by the Studio.

### Nova Studio

`studio/` is a .NET 8 WPF application. It loads `nova_compiler.dll` and
`nova_vm.dll` through P/Invoke and translates native metadata into UI models.
The same compilation result drives diagnostics, the source map, disassembly, and
the architecture graph. VM state drives the runtime inspector and active graph
path, avoiding a second simulated source of truth in the UI.

### Workspace format

A `.novaproj` manifest identifies project-local C and header files and persists
graph positions. The JSON schema is stored at `docs/nova-project.schema.json`.
`examples/PersistentWorkspace/` is the reference workspace.

## Execution flow

1. The Studio loads and validates a `.novaproj` workspace.
2. Project-local source and header files are passed to NovaC.
3. NovaC emits bytecode, diagnostics, and semantic/debug metadata.
4. The Studio builds the graph and source/disassembly views from that metadata.
5. Bytecode is loaded into a fresh NovaVM instance.
6. Run and step commands advance the VM.
7. Registers, memory, heap, call frames, events, and the active source location
   are read back through the VM ABI and projected into the UI.

## Test strategy

The native suite covers VM smoke behavior, debugger state, comparisons, bitwise
instructions, and allocation. Compiler tests are organized by language increment
and include architecture metadata, project compilation, headers, debug records,
control flow, declarations, memory types, preprocessing, and aggregates.

Both native libraries compile with high warning levels and warnings-as-errors on
MSVC. CTest registers 22 executable regression tests.
