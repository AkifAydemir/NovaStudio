using System.Runtime.InteropServices;
namespace NovaStudio.Interop;

internal static class NovaCompilerNative
{
    private const string LibraryName = "nova_compiler.dll";
    internal const int Ok = 0;
    internal const uint MaxBytecode = 256u * 1024u;
    internal const uint MaxSourceMap = 2048u;
    internal const uint MaxLocals = 8u;
    internal const uint MaxFunctions = 16u;
    internal const uint MaxDebugSymbols = 4096u;
    internal const uint MaxTranslationUnits = 16u;
    internal const uint MaxGlobals = 64u;
    internal const uint MaxReferences = 4096u;
    internal const uint MaxIncludeDependencies = 256u;
    internal const uint MaxDebugStrings = 64u;
    internal const uint MaxDeclarations = 512u;
    internal const uint MaxMacros = 128u;
    internal const uint MacroObject = 1u;
    internal const uint MacroFunction = 2u;
    internal const uint MacroFlagHeader = 1u;
    internal const uint RefFunctionCall = 1u;
    internal const uint RefGlobalRead = 2u;
    internal const uint RefGlobalWrite = 3u;
    internal const uint DebugInt = 0u;
    internal const uint DebugIntPtr = 1u;
    internal const uint DebugArray = 2u;
    internal const uint DebugStruct = 3u;
    internal const uint DebugStructPtr = 4u;
    internal const uint DebugChar = 5u;
    internal const uint DebugCharPtr = 6u;
    internal const uint DebugUnion = 7u;
    internal const uint DebugUnionPtr = 8u;
    internal const uint DebugStorageRegister = 0u;
    internal const uint DebugStorageMemory = 1u;
    internal const uint DeclFunction = 1u;
    internal const uint DeclGlobal = 2u;
    internal const uint DeclTypedef = 3u;
    internal const uint DeclStaticLocal = 4u;
    internal const uint LinkageNone = 0u;
    internal const uint LinkageExternal = 1u;
    internal const uint LinkageInternal = 2u;
    internal const uint DeclQualConst = 1u;
    internal const uint DeclQualVolatile = 2u;
    internal const uint DeclQualRestrict = 4u;
    internal const uint DeclFlagDefinition = 1u;
    internal const uint DeclFlagTentative = 2u;
    internal const uint DeclFlagBlockScope = 4u;
    internal const uint DeclFlagStaticStorage = 8u;
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct TranslationUnit
    {
        [MarshalAs(UnmanagedType.LPStr)]
        public string Name;
        [MarshalAs(UnmanagedType.LPStr)]
        public string Source;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct CompileResult
    {
        public uint BytecodeSize;
        public uint AstNodeCount;
        public uint LocalCount;
        public uint StatementCount;
        public uint ExpressionCount;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct CompilerDiagnostic
    {
        public int Status;
        public uint Line;

        public uint Column;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 192)]
        public string Message;
        public uint FileId;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct SourceMapEntry
    {
        public uint BytecodeStart;
        public uint BytecodeEnd;
        public uint Line;
        public uint Column;
        public uint StatementKind;
        public uint FunctionIndex;
        public uint FileId;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct DebugSymbol
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string Name;
        public uint RegisterIndex;
        public uint Type;
        public uint DeclarationLine;
        public uint DeclarationColumn;
        public uint FunctionIndex;
        public uint IsParameter;
        public uint StorageKind;
        public uint ByteOffset;
        public uint ByteSize;
        public uint ElementCount;
        public uint FileId;
        public uint LifetimeStart;
        public uint LifetimeEnd;
        public uint ScopeDepth;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct DebugGlobal
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string Name;
        public uint Address;
        public uint Type;
        public uint InitialValue;
        public uint DeclarationLine;
        public uint DeclarationColumn;
        public uint FileId;
        public uint ByteSize;
        public uint ElementCount;
        public uint StructIndex;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]

    internal struct SymbolReference
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string Name;
        public uint Kind;
        public uint FileId;
        public uint Line;
        public uint Column;
        public uint FunctionIndex;
        public uint TargetIndex;
        public uint TargetFileId;
        public uint BytecodeOffset;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct IncludeDependency
    {
        public uint FromFileId;
        public uint ToFileId;
        public uint Line;
        public uint Column;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct DebugString
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string Preview;
        public uint Address;
        public uint ByteSize;
        public uint FileId;
        public uint Line;
        public uint Column;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct DebugFunction
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string Name;
        public uint BytecodeStart;
        public uint BytecodeEnd;
        public uint DeclarationLine;
        public uint DeclarationColumn;
        public uint ParameterCount;
        public uint LocalCount;
        public uint FileId;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct DeclarationMetadata
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string Name;
        public uint Kind;
        public uint Linkage;
        public uint Qualifiers;
        public uint PointeeQualifiers;
        public uint Type;
        public uint StructIndex;
        public uint TargetIndex;
        public uint FileId;
        public uint Line;
        public uint Column;
        public uint Flags;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct MacroDefinition
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string Name;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 192)]
        public string Replacement;
        public uint Kind;
        public uint ParameterCount;
        public uint FileId;
        public uint Line;
        public uint Column;
        public uint Flags;
    }
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_project_debug_ex5(
    [In, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] TranslationUnit[] units,
    uint unitCount,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,
    out CompilerDiagnostic diagnostic,
    [Out] SourceMapEntry[] sourceMap,
    uint sourceMapCapacity,
    out uint sourceMapCount,
    [Out] DebugSymbol[] symbols,
    uint symbolCapacity,
    out uint symbolCount,
    [Out] DebugFunction[] functions,
    uint functionCapacity,
    out uint functionCount,
    [Out] DebugGlobal[] globals,
    uint globalCapacity,
    out uint globalCount,
    [Out] SymbolReference[] references,
    uint referenceCapacity,
    out uint referenceCount,
    [Out] IncludeDependency[] includes,
    uint includeCapacity,
    out uint includeCount,
    [Out] DebugString[] debugStrings,
    uint debugStringCapacity,
    out uint debugStringCount,
    [Out] DeclarationMetadata[] declarations,
    uint declarationCapacity,
    out uint declarationCount,
    [Out] MacroDefinition[] macros,
    uint macroCapacity,
    out uint macroCount);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_project_debug_ex4(
    [In, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] TranslationUnit[] units,
    uint unitCount,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,
    out CompilerDiagnostic diagnostic,
    [Out] SourceMapEntry[] sourceMap,
    uint sourceMapCapacity,
    out uint sourceMapCount,
    [Out] DebugSymbol[] symbols,
    uint symbolCapacity,
    out uint symbolCount,
    [Out] DebugFunction[] functions,
    uint functionCapacity,
    out uint functionCount,
    [Out] DebugGlobal[] globals,
    uint globalCapacity,
    out uint globalCount,
    [Out] SymbolReference[] references,
    uint referenceCapacity,
    out uint referenceCount,
    [Out] IncludeDependency[] includes,
    uint includeCapacity,
    out uint includeCount,
    [Out] DebugString[] debugStrings,
    uint debugStringCapacity,
    out uint debugStringCount,
    [Out] DeclarationMetadata[] declarations,
    uint declarationCapacity,
    out uint declarationCount);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_project_debug_ex3(
    [In, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] TranslationUnit[] units,
    uint unitCount,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,

    out CompilerDiagnostic diagnostic,
    [Out] SourceMapEntry[] sourceMap,
    uint sourceMapCapacity,
    out uint sourceMapCount,
    [Out] DebugSymbol[] symbols,
    uint symbolCapacity,
    out uint symbolCount,
    [Out] DebugFunction[] functions,
    uint functionCapacity,
    out uint functionCount,
    [Out] DebugGlobal[] globals,
    uint globalCapacity,
    out uint globalCount,
    [Out] SymbolReference[] references,
    uint referenceCapacity,
    out uint referenceCount,
    [Out] IncludeDependency[] includes,
    uint includeCapacity,
    out uint includeCount,
    [Out] DebugString[] debugStrings,
    uint debugStringCapacity,
    out uint debugStringCount);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_project_debug_ex2(
    [In, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] TranslationUnit[] units,
    uint unitCount,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,
    out CompilerDiagnostic diagnostic,
    [Out] SourceMapEntry[] sourceMap,
    uint sourceMapCapacity,
    out uint sourceMapCount,
    [Out] DebugSymbol[] symbols,
    uint symbolCapacity,
    out uint symbolCount,
    [Out] DebugFunction[] functions,
    uint functionCapacity,
    out uint functionCount,
    [Out] DebugGlobal[] globals,
    uint globalCapacity,
    out uint globalCount,
    [Out] SymbolReference[] references,
    uint referenceCapacity,
    out uint referenceCount,
    [Out] IncludeDependency[] includes,
    uint includeCapacity,
    out uint includeCount);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_project_debug_ex(
    [In, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] TranslationUnit[] units,
    uint unitCount,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,
    out CompilerDiagnostic diagnostic,
    [Out] SourceMapEntry[] sourceMap,
    uint sourceMapCapacity,

    out uint sourceMapCount,
    [Out] DebugSymbol[] symbols,
    uint symbolCapacity,
    out uint symbolCount,
    [Out] DebugFunction[] functions,
    uint functionCapacity,
    out uint functionCount,
    [Out] DebugGlobal[] globals,
    uint globalCapacity,
    out uint globalCount,
    [Out] SymbolReference[] references,
    uint referenceCapacity,
    out uint referenceCount);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_project(
    [In, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] TranslationUnit[] units,
    uint unitCount,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,
    out CompilerDiagnostic diagnostic);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_project_debug(
    [In, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] TranslationUnit[] units,
    uint unitCount,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,
    out CompilerDiagnostic diagnostic,
    [Out] SourceMapEntry[] sourceMap,
    uint sourceMapCapacity,
    out uint sourceMapCount,
    [Out] DebugSymbol[] symbols,
    uint symbolCapacity,
    out uint symbolCount,
    [Out] DebugFunction[] functions,
    uint functionCapacity,
    out uint functionCount);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_c(
    [MarshalAs(UnmanagedType.LPStr)] string source,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,
    out CompilerDiagnostic diagnostic);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int nova_compile_c_debug(
    [MarshalAs(UnmanagedType.LPStr)] string source,
    [Out] byte[] output,
    uint outputCapacity,
    out CompileResult result,
    out CompilerDiagnostic diagnostic,
    [Out] SourceMapEntry[] sourceMap,
    uint sourceMapCapacity,
    out uint sourceMapCount,
    [Out] DebugSymbol[] symbols,

    uint symbolCapacity,
    out uint symbolCount,
    [Out] DebugFunction[] functions,
    uint functionCapacity,
    out uint functionCount);
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nint nova_compiler_version();
    internal static string Version
    {
        get
        {
            var pointer = nova_compiler_version();
            return Marshal.PtrToStringAnsi(pointer) ?? "NovaC";
        }
    }
}
