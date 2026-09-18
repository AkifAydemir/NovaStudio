#ifndef NOVA_COMPILER_H
#define NOVA_COMPILER_H
#include <stdint.h>
#ifdef _WIN32
#ifdef NOVA_COMPILER_BUILD
#define NOVAC_API __declspec(dllexport)
#else
#define NOVAC_API __declspec(dllimport)
#endif
#else
#define NOVAC_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define NOVA_COMPILER_MAX_SOURCE_BYTES (256u * 1024u)
#define NOVA_COMPILER_MAX_PROJECT_SOURCE_BYTES (1024u * 1024u)
#define NOVA_COMPILER_MAX_TRANSLATION_UNITS 16u
#define NOVA_COMPILER_TRANSLATION_UNIT_NAME 96u
#define NOVA_COMPILER_MAX_AST_NODES 2048u
#define NOVA_COMPILER_MAX_LOCALS 8u
#define NOVA_COMPILER_MAX_PARAMETERS 4u
#define NOVA_COMPILER_MAX_BYTECODE (256u * 1024u)
#define NOVA_COMPILER_DIAGNOSTIC_MESSAGE 192u
#define NOVA_COMPILER_DEBUG_NAME 48u
#define NOVA_COMPILER_MAX_SOURCE_MAP 2048u
#define NOVA_COMPILER_MAX_DEBUG_FUNCTIONS 16u
#define NOVA_COMPILER_MAX_STRUCTS 16u
#define NOVA_COMPILER_MAX_STRUCT_FIELDS 16u
#define NOVA_COMPILER_MAX_ARRAY_LENGTH 32u
#define NOVA_COMPILER_MAX_DEBUG_SYMBOLS 4096u
#define NOVA_COMPILER_MAX_GLOBALS 64u
#define NOVA_COMPILER_MAX_REFERENCES 4096u
#define NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES 256u
#define NOVA_COMPILER_MAX_DEBUG_STRINGS 64u
#define NOVA_COMPILER_MAX_ENUM_CONSTANTS 256u
#define NOVA_COMPILER_MAX_DECLARATIONS 512u
#define NOVA_COMPILER_MAX_MACROS 128u
#define NOVA_COMPILER_MACRO_REPLACEMENT 192u
#define NOVA_COMPILER_GLOBAL_BASE 0x000E0000u
#define NOVA_COMPILER_GLOBAL_LIMIT 0x000F0000u
typedef enum NovaCompileStatus {
    NOVAC_OK = 0,
    NOVAC_ERR_ARGUMENT = -100,
    NOVAC_ERR_LEX = -101,
    NOVAC_ERR_PARSE = -102,
    NOVAC_ERR_AST_CAPACITY = -103,
    NOVAC_ERR_SYMBOL_CAPACITY = -104,
    NOVAC_ERR_UNKNOWN_SYMBOL = -105,
    NOVAC_ERR_REGISTER_PRESSURE = -106,
    NOVAC_ERR_OUTPUT_CAPACITY = -107,
    NOVAC_ERR_UNSUPPORTED = -108,
    NOVAC_ERR_INTERNAL = -109,
    NOVAC_ERR_DEBUG_CAPACITY = -110,
    NOVAC_ERR_FUNCTION_CAPACITY = -111,
    NOVAC_ERR_ARGUMENT_COUNT = -112,
    NOVAC_ERR_DUPLICATE_FUNCTION = -113,
    NOVAC_ERR_TYPE_CAPACITY = -114,

    NOVAC_ERR_UNKNOWN_TYPE = -115,
    NOVAC_ERR_DUPLICATE_MEMBER = -116,
    NOVAC_ERR_ARRAY_BOUNDS = -117,
    NOVAC_ERR_INVALID_MEMBER = -118,
    NOVAC_ERR_TRANSLATION_UNIT_CAPACITY = -119,
    NOVAC_ERR_DUPLICATE_TRANSLATION_UNIT = -120,
    NOVAC_ERR_SOURCE_CAPACITY = -121,
    NOVAC_ERR_CONFLICTING_DECLARATION = -122,
    NOVAC_ERR_GLOBAL_CAPACITY = -123,
    NOVAC_ERR_DUPLICATE_GLOBAL = -124,
    NOVAC_ERR_PREPROCESSOR = -125,
    NOVAC_ERR_INCLUDE_NOT_FOUND = -126,
    NOVAC_ERR_HEADER_DEFINITION = -127,
    NOVAC_ERR_INCLUDE_CAPACITY = -128,
    NOVAC_ERR_INCLUDE_CYCLE = -129,
    NOVAC_ERR_INVALID_POINTER = -130,
    NOVAC_ERR_SIZEOF = -131,
    NOVAC_ERR_CONSTANT_EXPRESSION = -132,
    NOVAC_ERR_CONST_WRITE = -133,
    NOVAC_ERR_MACRO_CAPACITY = -134,
    NOVAC_ERR_MACRO_RECURSION = -135
} NovaCompileStatus;
typedef struct NovaTranslationUnit {
    const char *name;
    const char *source;
} NovaTranslationUnit;
typedef struct NovaCompilerDiagnostic {
    int32_t status;
    uint32_t line;
    uint32_t column;
    char message[NOVA_COMPILER_DIAGNOSTIC_MESSAGE];
    uint32_t file_id;
} NovaCompilerDiagnostic;
typedef struct NovaCompileResult {
    uint32_t bytecode_size;
    uint32_t ast_node_count;
    uint32_t local_count;
    uint32_t statement_count;
    uint32_t expression_count;
} NovaCompileResult;
typedef enum NovaSourceStatementKind {
    NOVAC_SOURCE_VAR_DECL = 1,
    NOVAC_SOURCE_ASSIGN = 2,
    NOVAC_SOURCE_IF = 3,
    NOVAC_SOURCE_WHILE = 4,
    NOVAC_SOURCE_RETURN = 5,
    NOVAC_SOURCE_EXPR = 6,
    NOVAC_SOURCE_FOR = 7,
    NOVAC_SOURCE_BREAK = 8,
    NOVAC_SOURCE_CONTINUE = 9,
    NOVAC_SOURCE_SWITCH = 10,
    NOVAC_SOURCE_CASE = 11,
    NOVAC_SOURCE_DEFAULT = 12,
    NOVAC_SOURCE_DO_WHILE = 13,
    NOVAC_SOURCE_LABEL = 14,
    NOVAC_SOURCE_GOTO = 15
} NovaSourceStatementKind;
typedef enum NovaDebugSymbolType {
    NOVAC_DEBUG_INT = 0,
    NOVAC_DEBUG_INT_PTR = 1,
    NOVAC_DEBUG_ARRAY = 2,

    NOVAC_DEBUG_STRUCT = 3,
    NOVAC_DEBUG_STRUCT_PTR = 4,
    NOVAC_DEBUG_CHAR = 5,
    NOVAC_DEBUG_CHAR_PTR = 6,
    NOVAC_DEBUG_UNION = 7,
    NOVAC_DEBUG_UNION_PTR = 8
} NovaDebugSymbolType;
typedef enum NovaDebugStorageKind {
    NOVAC_DEBUG_STORAGE_REGISTER = 0,
    NOVAC_DEBUG_STORAGE_MEMORY = 1
} NovaDebugStorageKind;
typedef struct NovaSourceMapEntry {
    uint32_t bytecode_start;
    uint32_t bytecode_end;
    uint32_t line;
    uint32_t column;
    uint32_t statement_kind;
    uint32_t function_index;
    uint32_t file_id;
} NovaSourceMapEntry;
typedef struct NovaDebugSymbol {
    char name[NOVA_COMPILER_DEBUG_NAME];
    uint32_t register_index;
    uint32_t type;
    uint32_t declaration_line;
    uint32_t declaration_column;
    uint32_t function_index;
    uint32_t is_parameter;
    uint32_t storage_kind;
    uint32_t byte_offset;
    uint32_t byte_size;
    uint32_t element_count;
    uint32_t file_id;
    uint32_t lifetime_start;
    uint32_t lifetime_end;
    uint32_t scope_depth;
} NovaDebugSymbol;
typedef enum NovaSymbolReferenceKind {
    NOVAC_REF_FUNCTION_CALL = 1,
    NOVAC_REF_GLOBAL_READ = 2,
    NOVAC_REF_GLOBAL_WRITE = 3
} NovaSymbolReferenceKind;
typedef struct NovaDebugGlobal {
    char name[NOVA_COMPILER_DEBUG_NAME];
    uint32_t address;
    uint32_t type;
    uint32_t initial_value;
    uint32_t declaration_line;
    uint32_t declaration_column;
    uint32_t file_id;
    uint32_t byte_size;
    uint32_t element_count;
    uint32_t struct_index;
} NovaDebugGlobal;

typedef struct NovaSymbolReference {
    char name[NOVA_COMPILER_DEBUG_NAME];
    uint32_t kind;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    uint32_t function_index;
    uint32_t target_index;
    uint32_t target_file_id;
    uint32_t bytecode_offset;
} NovaSymbolReference;
typedef struct NovaIncludeDependency {
    uint32_t from_file_id;
    uint32_t to_file_id;
    uint32_t line;
    uint32_t column;
} NovaIncludeDependency;
typedef enum NovaMacroKind { NOVAC_MACRO_OBJECT = 1, NOVAC_MACRO_FUNCTION = 2 } NovaMacroKind;
typedef enum NovaMacroFlags {
    NOVAC_MACRO_FLAG_NONE = 0,
    NOVAC_MACRO_FLAG_HEADER = 1
} NovaMacroFlags;
typedef struct NovaMacroDefinition {
    char name[NOVA_COMPILER_DEBUG_NAME];
    char replacement[NOVA_COMPILER_MACRO_REPLACEMENT];
    uint32_t kind;
    uint32_t parameter_count;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    uint32_t flags;
} NovaMacroDefinition;
typedef struct NovaDebugString {
    char preview[NOVA_COMPILER_DEBUG_NAME];
    uint32_t address;
    uint32_t byte_size;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
} NovaDebugString;
typedef enum NovaDeclarationKind {
    NOVAC_DECL_FUNCTION = 1,
    NOVAC_DECL_GLOBAL = 2,
    NOVAC_DECL_TYPEDEF = 3,
    NOVAC_DECL_STATIC_LOCAL = 4
} NovaDeclarationKind;
typedef enum NovaDeclarationLinkage {
    NOVAC_LINKAGE_NONE = 0,
    NOVAC_LINKAGE_EXTERNAL = 1,
    NOVAC_LINKAGE_INTERNAL = 2
} NovaDeclarationLinkage;
typedef enum NovaDeclarationQualifierFlags {
    NOVAC_DECL_QUAL_NONE = 0,
    NOVAC_DECL_QUAL_CONST = 1,
    NOVAC_DECL_QUAL_VOLATILE = 2,
    NOVAC_DECL_QUAL_RESTRICT = 4
} NovaDeclarationQualifierFlags;
typedef enum NovaDeclarationFlags {
    NOVAC_DECL_FLAG_NONE = 0,
    NOVAC_DECL_FLAG_DEFINITION = 1,
    NOVAC_DECL_FLAG_TENTATIVE = 2,
    NOVAC_DECL_FLAG_BLOCK_SCOPE = 4,
    NOVAC_DECL_FLAG_STATIC_STORAGE = 8
} NovaDeclarationFlags;
typedef struct NovaDeclarationMetadata {
    char name[NOVA_COMPILER_DEBUG_NAME];
    uint32_t kind;
    uint32_t linkage;
    uint32_t qualifiers;
    uint32_t pointee_qualifiers;
    uint32_t type;
    uint32_t struct_index;
    uint32_t target_index;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    uint32_t flags;
} NovaDeclarationMetadata;
typedef struct NovaDebugFunction {
    char name[NOVA_COMPILER_DEBUG_NAME];
    uint32_t bytecode_start;
    uint32_t bytecode_end;
    uint32_t declaration_line;
    uint32_t declaration_column;
    uint32_t parameter_count;
    uint32_t local_count;
    uint32_t file_id;
} NovaDebugFunction;
/*
 * Vol.10 project compiler. Vol.9 entry points remain ABI-compatible wrappers. The translation-unit
 * array is caller-owned and fixed-capacity; NovaC performs no host heap allocation. file_id in all
 * diagnostics/debug records is the zero-based index into this array.
 *
 * Top-level function and struct namespaces are project-wide. Function calls
 * resolve after every translation unit is parsed. Struct references may appear
 * before the defining translation unit; NovaC creates a fixed placeholder and
 * requires a matching definition before code generation.
 */
/*
 * Vol.10 analysis entry point. It preserves the Vol.9 debug ABI and adds
 * deterministic global-data metadata plus compiler-emitted semantic references
 * used by Nova Studio's architecture graph.
 */
/*
* Vol.12 header/include-aware analysis entry point. The older Vol.10 ex API
* remains ABI-compatible and forwards here with no include-dependency buffer.
* Project files may be .c or workspace-local .h units. Only quoted includes
* are supported; headers are declaration-only (structs, prototypes, extern

* int/int* globals). Vol.20 adds a bounded macro/conditional preprocessing layer while system
headers and advanced macro operators remain deferred.
*
* Vol.13 extends the source type model with address-of, scaled pointer
* arithmetic, struct pointers/->, sizeof and project-scope aggregate storage.
* Vol.14 adds lexical block scopes/shadowing, char/string/static-data metadata
* and fixed local aggregate initializers. Vol.15 adds for/break/continue,
* short-circuit &&/||, prefix/postfix ++/-- and +=/-= while preserving the
* same debug ABI; source-map statement kinds append loop-control values.
* Vol.16 adds switch/case/default, ?:, bitwise/shift operators, project enum
* constants and deterministic integer constant-expression evaluation. The VM
* gains language-neutral AND/OR/XOR/SHL/SHR instructions; compiler debug ABI
* remains unchanged. ex3 appends NovaDebugString output while ex2 remains a
* compatible wrapper.
* Vol.17 adds declaration/linkage metadata through ex4 while preserving the
* ex3/ex2/ex ABI entry points byte-for-byte. Vol.20 adds bounded preprocessing
* metadata through ex5; ex4 remains an ABI-compatible wrapper.
*/
NOVAC_API int32_t nova_compile_project_debug_ex5(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out,
    NovaDebugGlobal *globals, uint32_t global_capacity, uint32_t *global_count_out,
    NovaSymbolReference *references, uint32_t reference_capacity, uint32_t *reference_count_out,
    NovaIncludeDependency *includes, uint32_t include_capacity, uint32_t *include_count_out,
    NovaDebugString *debug_strings, uint32_t debug_string_capacity,
    uint32_t *debug_string_count_out, NovaDeclarationMetadata *declarations,
    uint32_t declaration_capacity, uint32_t *declaration_count_out, NovaMacroDefinition *macros,
    uint32_t macro_capacity, uint32_t *macro_count_out);
NOVAC_API int32_t nova_compile_project_debug_ex4(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out,
    NovaDebugGlobal *globals, uint32_t global_capacity, uint32_t *global_count_out,
    NovaSymbolReference *references, uint32_t reference_capacity, uint32_t *reference_count_out,
    NovaIncludeDependency *includes, uint32_t include_capacity, uint32_t *include_count_out,
    NovaDebugString *debug_strings, uint32_t debug_string_capacity,
    uint32_t *debug_string_count_out, NovaDeclarationMetadata *declarations,
    uint32_t declaration_capacity, uint32_t *declaration_count_out);
NOVAC_API int32_t nova_compile_project_debug_ex3(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out,
    NovaDebugGlobal *globals, uint32_t global_capacity, uint32_t *global_count_out,
    NovaSymbolReference *references, uint32_t reference_capacity, uint32_t *reference_count_out,
    NovaIncludeDependency *includes, uint32_t include_capacity, uint32_t *include_count_out,
    NovaDebugString *debug_strings, uint32_t debug_string_capacity,
    uint32_t *debug_string_count_out);
NOVAC_API int32_t nova_compile_project_debug_ex2(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out,
    NovaDebugGlobal *globals,

    uint32_t global_capacity, uint32_t *global_count_out, NovaSymbolReference *references,
    uint32_t reference_capacity, uint32_t *reference_count_out, NovaIncludeDependency *includes,
    uint32_t include_capacity, uint32_t *include_count_out);
NOVAC_API int32_t nova_compile_project_debug_ex(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out,
    NovaDebugGlobal *globals, uint32_t global_capacity, uint32_t *global_count_out,
    NovaSymbolReference *references, uint32_t reference_capacity, uint32_t *reference_count_out);
NOVAC_API int32_t nova_compile_project(const NovaTranslationUnit *units, uint32_t unit_count,
                                       uint8_t *output, uint32_t output_capacity,
                                       NovaCompileResult *result_out,
                                       NovaCompilerDiagnostic *diagnostic_out);
NOVAC_API int32_t nova_compile_project_debug(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out);
/*
* Backward-compatible single-source convenience wrappers. Metadata emitted by
* these functions always carries file_id = 0 and behaves like a project with

* one translation unit named "main.c".
*/
NOVAC_API int32_t nova_compile_c(const char *source, uint8_t *output, uint32_t output_capacity,
                                 NovaCompileResult *result_out,
                                 NovaCompilerDiagnostic *diagnostic_out);
NOVAC_API int32_t nova_compile_c_debug(const char *source, uint8_t *output,
                                       uint32_t output_capacity, NovaCompileResult *result_out,
                                       NovaCompilerDiagnostic *diagnostic_out,
                                       NovaSourceMapEntry *source_map, uint32_t source_map_capacity,
                                       uint32_t *source_map_count_out, NovaDebugSymbol *symbols,
                                       uint32_t symbol_capacity, uint32_t *symbol_count_out,
                                       NovaDebugFunction *functions, uint32_t function_capacity,
                                       uint32_t *function_count_out);
NOVAC_API const char *nova_compiler_version(void);
#ifdef __cplusplus
}
#endif
#endif
