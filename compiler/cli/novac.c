#include "nova_compiler.h"
#include <stdio.h>
#include <string.h>
static char source_buffers[NOVA_COMPILER_MAX_TRANSLATION_UNITS][NOVA_COMPILER_MAX_SOURCE_BYTES];
static uint8_t bytecode_buffer[NOVA_COMPILER_MAX_BYTECODE];
static NovaTranslationUnit units[NOVA_COMPILER_MAX_TRANSLATION_UNITS];
static int read_source(uint32_t unit_index, const char *path) {
    FILE *file;
    size_t size;
    if (unit_index >= NOVA_COMPILER_MAX_TRANSLATION_UNITS)
        return 0;
    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    size = fread(source_buffers[unit_index], 1u, NOVA_COMPILER_MAX_SOURCE_BYTES - 1u, file);
    if (ferror(file)) {
        fclose(file);
        return 0;
    }
    if (!feof(file)) {
        fclose(file);
        return 0;
    }
    source_buffers[unit_index][size] = '\0';
    fclose(file);
    units[unit_index].name = path;
    units[unit_index].source = source_buffers[unit_index];
    return 1;
}
static int write_bytecode(const char *path, uint32_t size) {
    FILE *file = fopen(path, "wb");
    size_t written;
    if (file == NULL)
        return 0;
    written = fwrite(bytecode_buffer, 1u, size, file);
    fclose(file);
    return written == size;
}
static void print_usage(void) {
    fprintf(stderr, "usage:\n"
                    " novac <input.c> <output.nvc>\n"
                    " novac -o <output.nvc> <file1.c> [file2.c/file.h ...]\n");
}
int main(int argc, char **argv) {
    NovaCompileResult result;
    NovaCompilerDiagnostic diagnostic;
    const char *output_path;
    uint32_t unit_count;
    int first_input;
    uint32_t i;
    int32_t status;
    if (argc == 3 && strcmp(argv[1], "-o") != 0) {
        output_path = argv[2];
        first_input = 1;
        unit_count = 1u;

    } else if (argc >= 4 && strcmp(argv[1], "-o") == 0) {
        output_path = argv[2];
        first_input = 3;
        unit_count = (uint32_t)(argc - first_input);
    } else {
        print_usage();
        return 2;
    }
    if (unit_count == 0u || unit_count > NOVA_COMPILER_MAX_TRANSLATION_UNITS) {
        fprintf(stderr, "novac: project supports 1-%u .c/.h project files\n",
                NOVA_COMPILER_MAX_TRANSLATION_UNITS);
        return 2;
    }
    for (i = 0u; i < unit_count; i++) {
        const char *path = argv[first_input + (int)i];
        if (!read_source(i, path)) {
            fprintf(stderr, "novac: failed to read '%s' or source exceeds %u bytes\n", path,
                    NOVA_COMPILER_MAX_SOURCE_BYTES - 1u);
            return 3;
        }
    }
    status = nova_compile_project(units, unit_count, bytecode_buffer,
                                  (uint32_t)sizeof(bytecode_buffer), &result, &diagnostic);
    if (status != NOVAC_OK) {
        const char *name =
            diagnostic.file_id < unit_count ? units[diagnostic.file_id].name : "<project>";
        fprintf(stderr, "%s:%u:%u: error %d: %s\n", name, diagnostic.line, diagnostic.column,
                status, diagnostic.message);
        return 4;
    }
    if (!write_bytecode(output_path, result.bytecode_size)) {
        fprintf(stderr, "novac: failed to write '%s'\n", output_path);
        return 5;
    }
    printf("%s: %u units, %u bytes, %u AST nodes, %u locals, %u statements, %u expressions\n",
           nova_compiler_version(), unit_count, result.bytecode_size, result.ast_node_count,
           result.local_count, result.statement_count, result.expression_count);
    return 0;
}
