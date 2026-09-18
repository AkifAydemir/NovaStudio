# Validation evidence

## Windows release gate

Validation date: **2026-09-16**

The source tree passed the following local release pipeline on Windows:

1. CMake configure with Visual Studio 2022 / MSVC x64.
2. Native and compiler Release clean build.
3. CTest: **22/22 passed**.
4. .NET WPF Release build: **0 warnings, 0 errors**.
5. Runtime payload check for `nova_vm.dll`, `nova_compiler.dll`, `novac.exe`,
   and the WPF output directory.
6. `novac` compiled `examples/PersistentWorkspace` into bytecode.

Produced Studio executable:

```text
Size:    152,064 bytes
SHA-256: 278BD37A7659202455545CCB99F09EFDD0B6C288408AEF81A49255C1FBA8E509
```

Sample bytecode smoke output:

```text
NovaC 0.21.0: 5 units, 417 bytes, 46 AST nodes, 2 locals, 10 statements, 36 expressions
```

The WPF executable launches and the runtime payload is present, but a fresh
recorded interactive UI walkthrough is still outside the current evidence
boundary.

The README image is a `RenderTargetBitmap` capture of the real Release-build
WPF `MainWindow` with the included sample workspace loaded. It demonstrates the
rendered interface, not an automated end-to-end user interaction.

## Reproduce locally

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
dotnet build studio/NovaStudio.csproj -c Release
```

Expected CTest summary:

```text
100% tests passed, 0 tests failed out of 22
```

## Validation boundary

This evidence establishes a clean Windows build, native/compiler regression
suite, WPF compilation, native payload resolution, and a representative CLI
compile workflow. It is not a claim of full ISO C conformance, exhaustive UI
automation, performance certification, or safety-critical qualification.
