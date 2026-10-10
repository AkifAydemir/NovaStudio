# First successful session

## Portable preview (no development tools on the recipient's PC)

The maintainer can run `powershell -NoProfile -File tools/package.ps1` to build,
test and create a new ZIP under `artifacts/` with a SHA-256 digest. Extract the
whole ZIP before launching the executable; do not run it inside the archive.
These are unsigned preview builds, not certified installers. Never disable OS
security checks to launch them. Native components may require Microsoft's
Visual C++ 2015–2022 x64 runtime. No clean-machine installation claim is made.

## Build from source

1. Install Visual Studio 2022 (Desktop development with C++), CMake 3.20+ and .NET 8 SDK on Windows x64.
2. From the repository root run: `powershell -NoProfile -File tools/check.ps1`.
3. Launch `studio/bin/Release/net8.0-windows/NovaStudio.exe`.
4. Open `examples/PersistentWorkspace/NovaDemo.novaproj`, build and run. Expected console output: `A`.
5. Set a breakpoint and step; inspect registers and source/disassembly together.

## If something goes wrong

- Missing native DLL: build native targets before WPF. Both nova_vm.dll and nova_compiler.dll must be beside the executable. Do not download DLLs from third-party sites.
- Compiler diagnostic: this is a bounded C subset, not a full ISO C compiler. Start from the included sample.
- No Windows desktop: the WPF frontend requires Windows; native tests alone do not verify the UI.

Automated checks and an interactive demo are separate evidence. See [validation](VALIDATION.md) and [architecture](ARCHITECTURE.md).
