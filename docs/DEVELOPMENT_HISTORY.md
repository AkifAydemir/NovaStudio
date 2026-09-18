# Nova Vol.21 Development History

> Historical engineering handoff. The pending external checks recorded below
> were subsequently completed successfully. See [VALIDATION.md](VALIDATION.md)
> for the current verified status.

This handoff describes the current canonical NovaVM + NovaC + Nova Studio 0.21 source tree after the bounded Windows portability repairs and the first real .NET 8 WPF compile repair. It does **not** claim a complete Windows Studio validation pass. External Windows verification now confirms the native/compiler path end-to-end: MSVC Release build PASS, `nova_vm.dll` PASS, `nova_compiler.dll` PASS, `novac.exe` PASS and Windows CTest 22/22 PASS. The earlier private `NovaVm` flexible-array, root compiler-workspace stack and recursive-preprocessor stack defects are therefore CLOSED. The pipeline then reached the first real WPF build and Roslyn reported CS0246 at the existing `InvalidDataException` uses in `studio/Services/BytecodeDisassembler.cs` because the file lacked the `System.IO` namespace import. This candidate adds only that required import and leaves WPF build/runtime pending external re-verification.

## Repair scope

This is a bounded Vol.21 build-candidate repair only. It does not open Vol.22 and does not add language/compiler features.

### Retained first portability repair

- `native/src/nova_vm.c`: the private `NovaVm` trailing flexible-array member remains replaced by `uint8_t* memory`. `nova_vm_create()` still reserves one contiguous platform allocation of `sizeof(NovaVm) + NOVA_MEMORY_SIZE`, zeros that allocation, points `vm->memory` at the immediately following memory region, then calls `nova_vm_reset(vm)`. `nova_vm_destroy()` still releases the same total allocation size. No public NovaVM C ABI/P/Invoke function or record changes.
- Root `CMakeLists.txt`: runtime outputs remain normalized to `${CMAKE_BINARY_DIR}` for Debug, Release, RelWithDebInfo, and MinSizeRel. On Windows this applies to shared-library DLLs and executable runtime targets, including `nova_vm.dll`, `nova_compiler.dll`, `novac.exe`, and registered CTest executables.
- `compiler/CMakeLists.txt`: NovaC explicitly requires C11, requires the standard, and disables compiler extensions, matching the native project. `/W4 /WX` remains enabled for MSVC in both native and compiler targets.

### Retained root compiler-workspace repair

`compiler/src/nova_compiler.c` retains one private internal `NcCompileWorkspace` allocated per compile call by the internal platform allocator. Windows uses `VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)` / `VirtualFree(..., MEM_RELEASE)` and POSIX uses `mmap(..., PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, ...)` / `munmap`. Allocation failure returns the existing `NOVAC_ERR_INTERNAL` diagnostic path. No static/global mutable compile workspace exists, so concurrent compile calls remain independent and reentrant. The locked no-direct-`malloc`/`calloc`/`realloc`/`free` compiler contract remains intact.

### Current recursive-preprocessor stack repair

The per-call `NcCompileWorkspace` now also owns fixed-capacity preprocessing scratch that previously lived in recursive thread-stack frames:

- `NcPpExpansionScratch` preallocates one `NcPpExpansionFrame` for each allowed macro expansion depth (`NOVAC_PP_MAX_EXPANSION_DEPTH + 1`). Each frame owns the bounded `raw_args`, `expanded_args`, `substituted`, and `expanded` buffers used by `nc_pp_expand_text_depth()`.
- `NcPpScanScratch` owns one `NcMacroEnv` and conditional stack per translation unit for recursive include scanning, plus a bounded line buffer used by preprocessing condition evaluation.
- `NcPpFileScratch` owns the non-recursive macro environment, conditional stack, and line buffer used by `nc_pp_make_source()`.
- Internal preprocessing calls receive the private scratch through `NcPpProject`; recursion indexes the expansion frame by `depth`. No process-global/static scratch and no test/linker stack-size override is used.
- `NOVAC_PP_MAX_EXPANSION_DEPTH` remains **32** and the exact `A -> B -> A` recursion regression remains unchanged. Under a constrained 1 MiB process stack, it now reaches the intended `NOVAC_ERR_MACRO_RECURSION` (`-135`) diagnostic instead of exhausting the process stack.

This is a storage-placement repair only. Macro/include behavior, recursion bounds, fixed capacities, public exports, debug/analysis record layouts and language semantics are unchanged.

GCC `-fstack-usage` on this candidate reports approximately **400 bytes** for `nova_compile_project_debug_ex5`, **320 bytes** per bounded `nc_pp_expand_text_depth` frame, **896 bytes** for `nc_pp_scan_file`, and **896 bytes** for `nc_pp_make_source`; the large macro environments and 8 KiB expansion buffers are now in the private OS-backed compile workspace. Normal Release CTest and full 1 MiB-stack CTest both pass 22/22.

### Current bounded WPF compile repair

`studio/Services/BytecodeDisassembler.cs` now imports `System.IO` so its existing `InvalidDataException` references resolve during the .NET 8 WPF build. This is the only product-source change in this repair. Disassembler behavior, bytecode decoding, public contracts, native/compiler code, WPF UX, P/Invoke declarations, project schema and runtime semantics are unchanged.

## Product identity

Nova Studio is a C development studio for viewing the same program as source, compiler semantic metadata, NovaVM execution state, registers/memory/stack, debugger state, and a living architecture/runtime graph. It is not a generic Visual Studio replacement.

## Exact projects and targets

Root CMake project: `CMakeLists.txt` (`project(Nova LANGUAGES C)`) adds `native/` and `compiler/` and establishes the shared runtime-output policy before either subtree creates targets.

Native project: `native/CMakeLists.txt` (`project(NovaVM C)`) explicitly uses C11 with extensions disabled and builds shared library target `nova_vm`. Native CTest executable targets are `nova_vm_smoke`, `nova_vm_debugger_test`, `nova_vm_compare_test`, `nova_vm_bitwise_test`, and `nova_vm_allocator_test`; their registered CTest names are `nova_vm_smoke`, `nova_vm_debugger`, `nova_vm_compare`, `nova_vm_bitwise`, and `nova_vm_allocator`.

Compiler project: `compiler/CMakeLists.txt` explicitly uses C11 with extensions disabled and builds shared library target `nova_compiler` linked to `nova_vm`, plus CLI target `novac`. Compiler CTest executable targets are created by the same project and are registered with the root CTest configuration.

WPF project: `studio/NovaStudio.csproj`, SDK `Microsoft.NET.Sdk`, `OutputType=WinExe`, `TargetFramework=net8.0-windows`, `UseWPF=true`, `PlatformTarget=x64`, executable assembly name `NovaStudio`. The project conditionally copies `../build/nova_vm.dll` and `../build/nova_compiler.dll` beside the WPF executable when those files exist.

## Windows configure/build/test sequence

From the repository root in an x64 Visual Studio Developer PowerShell / terminal with CMake, MSVC, and .NET 8 SDK installed:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
dotnet build studio/NovaStudio.csproj -c Release
```

The same root runtime-output policy is defined for `Debug`, `Release`, `RelWithDebInfo`, and `MinSizeRel`; multi-config generators should therefore place Windows runtime outputs directly in `build/` rather than `build/<Config>/`.

Expected Release native/compiler runtime outputs after the CMake build:

- `build/nova_vm.dll`
- `build/nova_compiler.dll`
- `build/novac.exe`
- CTest executable targets directly under `build/` as well (for example `build/nova_vm_smoke.exe` and `build/nova_compiler_test.exe`)

Expected WPF executable directory from the SDK-style project:

- `studio/bin/Release/net8.0-windows/`
- executable assembly: `NovaStudio.exe`
- copied native dependencies, when the root CMake outputs exist: `nova_vm.dll`, `nova_compiler.dll` in the same directory as `NovaStudio.exe`

The P/Invoke declarations use the exact library names `nova_vm.dll` and `nova_compiler.dll`; Windows must therefore be able to resolve the two DLLs from the application directory or another normal DLL search location. The canonical `.csproj` copy rules are the intended candidate path.

## First launch / sample workspace

Launch either:

```powershell
dotnet run --project studio/NovaStudio.csproj -c Release
```

or run `studio/bin/Release/net8.0-windows/NovaStudio.exe` after a successful build.

For an on-disk workspace smoke test, open:

`examples/PersistentWorkspace/NovaDemo.novaproj`

## Minimum real-user smoke sequence

1. Launch Nova Studio and confirm both native DLLs load without a dependency error.
2. Use **New Project** once; save the workspace and confirm `.novaproj` plus source/header files are created.
3. Use **Open Project** and open `examples/PersistentWorkspace/NovaDemo.novaproj`.
4. Edit and save a source file; confirm dirty-state and save behavior.
5. **Compile**; confirm Diagnostics reports success and the architecture graph rebuilds from compiler metadata. This step specifically verifies both the off-stack root compiler state and the off-stack recursive preprocessing scratch through the real WPF P/Invoke path.
6. **Run / Continue**; confirm console/runtime inspector update from NovaVM.
7. Toggle a source breakpoint from **SOURCE MAP** or an instruction breakpoint from **DISASSEMBLY**; confirm breakpoint badges appear on relevant graph source/function nodes.
8. Exercise **Step Into**, **Step Over**, and **Step Out**; confirm current source, selected frame WATCH values, registers, PC/SP/flags, call stack, heap, and disassembly stay coherent.
9. In the graph, confirm the active function/current source context and NovaVM activity glow are driven by live compiler/VM state; live call edges should appear only for actual call-stack relations already represented by semantic call edges.
10. Double-click source/function/global/typedef/macro nodes and confirm source navigation; double-click runtime/memory nodes and confirm the runtime inspector remains the source of current VM-backed state.
11. Close/reopen the project and confirm persisted graph positions and workspace paths remain intact.

## Current validation boundary

Available validation on this exact WPF-import-repaired source candidate is green for every executable native/compiler path available in this environment:

- fresh GCC Release CMake configure/build: PASS;
- normal GCC CTest: 22/22 PASS;
- constrained-stack GCC CTest under a 1 MiB process stack (`ulimit -s 1024`): 22/22 PASS;
- the unchanged `#define A B` / `#define B A` regression returns `NOVAC_ERR_MACRO_RECURSION` (`-135`) under the same 1 MiB stack rather than crashing;
- Clang Release configure/build and CTest: 22/22 PASS; the existing warning-as-error `nova_vm` / `nova_compiler` library gates remain green;
- GCC `-fstack-usage`: `nova_compile_project_debug_ex5` ~400 B, `nc_pp_expand_text_depth` ~320 B/frame, `nc_pp_scan_file` ~896 B, `nc_pp_make_source` ~896 B;
- compiler direct malloc/calloc/realloc/free calls: 0;
- compiler ABI/PInvoke: 10/10 PASS; native ABI/PInvoke: 27/27 PASS;
- Studio XAML/C# structural audits, workspace schema/include/dependency checks, and explicit `using System.IO;` import check: PASS;
- embedded Studio source fixture and `PersistentWorkspace` runtime expectations remain unchanged; this repair touches only the missing C# namespace import.

External Windows evidence for the immediately previous source state now completes the native/compiler gate: MSVC Release build PASS, `nova_vm.dll` PASS, `nova_compiler.dll` PASS, `novac.exe` PASS and Windows CTest **22/22 PASS**. The previously isolated NovaVM/compiler/preprocessor Windows portability defects are CLOSED. The first real .NET 8 WPF build then reached Roslyn/MSBuild and failed only with CS0246 at the existing `InvalidDataException` uses in `studio/Services/BytecodeDisassembler.cs`; the current source repairs that missing `System.IO` import. **This exact new ZIP has not yet been rebuilt externally, so WPF build success and WPF runtime/PInvoke remain NOT VERIFIED.** Required external checks are the repaired WPF XAML/BAML build through MSBuild, native DLL copy/load behavior, real P/Invoke compiler/runtime invocation, first-window runtime, input/mouse graph interaction, file dialogs, and the complete user workflow smoke sequence above. Any defect found there must be repaired in the same Vol.21 candidate and recanonicalized; do not advance to Vol.22 as a side effect of build repair.
