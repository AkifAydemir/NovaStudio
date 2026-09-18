# Contributing

Nova Studio is a bounded systems-programming workbench. Changes should preserve
the native ABI, deterministic compiler/VM behavior, and the distinction between
the supported NovaC subset and full ISO C conformance.

## Local quality gate

From an x64 Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
dotnet build studio/NovaStudio.csproj -c Release
```

Before opening a pull request:

- keep changes focused and document any ABI or bytecode-format change;
- add a regression test for compiler, VM, debugger, or source-map behavior;
- run `clang-format` using the repository `.clang-format` file for changed C/C++
  sources;
- do not commit `build`, `bin`, `obj`, IDE state, or generated packages.
