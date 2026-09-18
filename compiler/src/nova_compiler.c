#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#define NOVA_COMPILER_BUILD
#include "nova_compiler.h"
#include "nova_vm.h"
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#define NOVAC_INVALID_NODE 0xFFFFFFFFu
#define NOVAC_NAME_CAPACITY 48u
#define NOVAC_TEMP_FIRST 9u
#define NOVAC_TEMP_LAST 14u
#define NOVAC_SCRATCH_REGISTER 15u
#define NOVAC_RETURN_REGISTER 0u
#define NOVAC_LOCAL_FIRST 1u
#define NOVAC_LOCAL_LAST 8u
#define NOVAC_MAX_STRING_LITERALS 64u
#define NOVAC_MAX_STRING_BYTES 128u
static void *nc_platform_allocate(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return memory == MAP_FAILED ? NULL : memory;
#endif
}
static void nc_platform_release(void *memory, size_t size) {
    if (memory == NULL)
        return;
#ifdef _WIN32
    (void)size;
    (void)VirtualFree(memory, 0u, MEM_RELEASE);
#else
    (void)munmap(memory, size);
#endif
}
/* ------------------------------ small utilities --------------------------- */
static size_t nc_strlen(const char *text) {
    size_t n = 0u;
    if (text == NULL)
        return 0u;
    while (text[n] != '\0')
        n++;
    return n;
}
static int nc_streq(const char *a, const char *b) {
    size_t i = 0u;
    if (a == NULL || b == NULL)
        return 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i];
}
static void nc_copy_text(char *dst, uint32_t capacity, const char *src, uint32_t length) {
    uint32_t i;
    if (dst == NULL || capacity == 0u)
        return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    if (length >= capacity)
        length = capacity - 1u;
    for (i = 0u; i < length; i++)
        dst[i] = src[i];
    dst[length] = '\0';
}
static char nc_ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}
static int nc_ends_with_ci(const char *text, const char *suffix) {
    size_t text_len = nc_strlen(text);
    size_t suffix_len = nc_strlen(suffix);
    size_t i;
    if (suffix_len > text_len)
        return 0;
    for (i = 0u; i < suffix_len; i++) {

        if (nc_ascii_lower(text[text_len - suffix_len + i]) != nc_ascii_lower(suffix[i]))
            return 0;
    }
    return 1;
}
static int nc_is_header_name(const char *name) {
    return nc_ends_with_ci(name, ".h");
}
static int nc_is_source_name(const char *name) {
    return nc_ends_with_ci(name, ".c");
}
static int nc_path_is_separator(char c) {
    return c == '/' || c == '\\';
}
static int nc_path_is_absolute(const char *path) {
    if (path == NULL || path[0] == '\0')
        return 0;
    if (nc_path_is_separator(path[0]))
        return 1;
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
}
static int nc_normalize_path(const char *input, char *output, uint32_t capacity) {
    char segments[48][NOVA_COMPILER_TRANSLATION_UNIT_NAME];
    uint32_t segment_count = 0u;
    uint32_t i = 0u;
    uint32_t out = 0u;
    int absolute = 0;
    int drive = 0;
    char drive_letter = '\0';
    if (input == NULL || output == NULL || capacity == 0u)
        return 0;
    if (((input[0] >= 'A' && input[0] <= 'Z') || (input[0] >= 'a' && input[0] <= 'z')) &&
        input[1] == ':') {
        drive = 1;
        drive_letter = nc_ascii_lower(input[0]);
        i = 2u;
        if (nc_path_is_separator(input[i])) {
            absolute = 1;
            i++;
        }
    } else if (nc_path_is_separator(input[0])) {
        absolute = 1;
        i = 1u;
    }
    while (input[i] != '\0') {
        uint32_t start;
        uint32_t len;
        while (nc_path_is_separator(input[i]))
            i++;
        if (input[i] == '\0')
            break;
        start = i;
        while (input[i] != '\0' && !nc_path_is_separator(input[i]))
            i++;
        len = i - start;
        if (len == 1u && input[start] == '.')
            continue;
        if (len == 2u && input[start] == '.' && input[start + 1u] == '.') {
            if (segment_count == 0u)
                return 0;
            segment_count--;
            continue;
        }
        if (segment_count >= 48u || len == 0u || len >= NOVA_COMPILER_TRANSLATION_UNIT_NAME)
            return 0;
        nc_copy_text(segments[segment_count], NOVA_COMPILER_TRANSLATION_UNIT_NAME, input + start,
                     len);
        segment_count++;
    }
    if (drive) {
        if (capacity < 3u)
            return 0;
        output[out++] = drive_letter;
        output[out++] = ':';
        if (absolute)
            output[out++] = '/';
    } else if (absolute) {
        if (capacity < 2u)
            return 0;
        output[out++] = '/';
    }

    for (i = 0u; i < segment_count; i++) {
        uint32_t j;
        uint32_t len = (uint32_t)nc_strlen(segments[i]);
        if (out > 0u && output[out - 1u] != '/' && output[out - 1u] != ':') {
            if (out + 1u >= capacity)
                return 0;
            output[out++] = '/';
        }
        if (out + len >= capacity)
            return 0;
        for (j = 0u; j < len; j++)
            output[out++] = segments[i][j];
    }
    if (out == 0u || out >= capacity)
        return 0;
    output[out] = '\0';
    return 1;
}
static int nc_resolve_include_path(const char *including_name, const char *include_path,
                                   char *output, uint32_t capacity) {
    char raw[NOVA_COMPILER_TRANSLATION_UNIT_NAME * 2u];
    uint32_t i = 0u;
    uint32_t slash = NOVAC_INVALID_NODE;
    uint32_t out = 0u;
    if (including_name == NULL || include_path == NULL || output == NULL)
        return 0;
    if (nc_path_is_absolute(include_path))
        return 0;
    while (including_name[i] != '\0') {
        if (nc_path_is_separator(including_name[i]))
            slash = i;
        i++;
    }
    if (slash != NOVAC_INVALID_NODE) {
        uint32_t j;
        if (slash + 1u >= (uint32_t)sizeof(raw))
            return 0;
        for (j = 0u; j <= slash; j++)
            raw[out++] = including_name[j];
    }
    i = 0u;
    while (include_path[i] != '\0') {
        if (out + 1u >= (uint32_t)sizeof(raw))
            return 0;
        raw[out++] = include_path[i++];
    }
    raw[out] = '\0';
    return nc_normalize_path(raw, output, capacity);
}
static void nc_diag_clear(NovaCompilerDiagnostic *diagnostic) {
    if (diagnostic == NULL)
        return;
    diagnostic->status = NOVAC_OK;
    diagnostic->line = 0u;
    diagnostic->column = 0u;
    diagnostic->message[0] = '\0';
    diagnostic->file_id = 0u;
}
static int32_t nc_diag(NovaCompilerDiagnostic *diagnostic, int32_t status, uint32_t line,
                       uint32_t column, const char *message) {
    if (diagnostic != NULL) {
        diagnostic->status = status;
        diagnostic->line = line;
        diagnostic->column = column;
        nc_copy_text(

            diagnostic->message, NOVA_COMPILER_DIAGNOSTIC_MESSAGE, message,
            (uint32_t)nc_strlen(message));
    }
    return status;
}
/* -------------------------------- lexer ---------------------------------- */
typedef enum NcTokenKind {
    NC_TOK_EOF = 0,
    NC_TOK_IDENTIFIER,
    NC_TOK_INTEGER,
    NC_TOK_CHAR,
    NC_TOK_STRING,
    NC_TOK_KW_INT,
    NC_TOK_KW_CHAR,
    NC_TOK_KW_VOID,
    NC_TOK_KW_RETURN,
    NC_TOK_KW_IF,
    NC_TOK_KW_ELSE,
    NC_TOK_KW_WHILE,
    NC_TOK_KW_DO,
    NC_TOK_KW_FOR,
    NC_TOK_KW_BREAK,
    NC_TOK_KW_CONTINUE,
    NC_TOK_KW_GOTO,
    NC_TOK_KW_SWITCH,
    NC_TOK_KW_CASE,
    NC_TOK_KW_DEFAULT,
    NC_TOK_KW_ENUM,
    NC_TOK_KW_STRUCT,
    NC_TOK_KW_UNION,
    NC_TOK_KW_EXTERN,
    NC_TOK_KW_TYPEDEF,
    NC_TOK_KW_CONST,
    NC_TOK_KW_STATIC,
    NC_TOK_KW_VOLATILE,
    NC_TOK_KW_RESTRICT,
    NC_TOK_KW_SIZEOF,
    NC_TOK_LPAREN,
    NC_TOK_RPAREN,
    NC_TOK_LBRACE,
    NC_TOK_RBRACE,
    NC_TOK_LBRACKET,
    NC_TOK_RBRACKET,
    NC_TOK_DOT,
    NC_TOK_SEMICOLON,
    NC_TOK_COMMA,
    NC_TOK_PLUS,
    NC_TOK_MINUS,
    NC_TOK_PLUS_PLUS,
    NC_TOK_MINUS_MINUS,
    NC_TOK_PLUS_ASSIGN,
    NC_TOK_MINUS_ASSIGN,
    NC_TOK_STAR,
    NC_TOK_SLASH,
    NC_TOK_AMP,
    NC_TOK_PIPE,
    NC_TOK_CARET,
    NC_TOK_TILDE,
    NC_TOK_AND_AND,
    NC_TOK_OR_OR,
    NC_TOK_ARROW,
    NC_TOK_QUESTION,

    NC_TOK_COLON,
    NC_TOK_BANG,
    NC_TOK_ASSIGN,
    NC_TOK_EQ,
    NC_TOK_NE,
    NC_TOK_LT,
    NC_TOK_LE,
    NC_TOK_GT,
    NC_TOK_GE,
    NC_TOK_SHIFT_LEFT,
    NC_TOK_SHIFT_RIGHT
} NcTokenKind;
typedef struct NcToken {
    NcTokenKind kind;
    const char *start;
    uint32_t length;
    uint32_t line;
    uint32_t column;
    uint32_t value;
} NcToken;
typedef struct NcLexer {
    const char *source;
    uint32_t offset;
    uint32_t line;
    uint32_t column;
    NovaCompilerDiagnostic *diagnostic;
    int32_t status;
} NcLexer;
static int nc_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int nc_is_digit(char c) {
    return c >= '0' && c <= '9';
}
static char nc_peek(const NcLexer *lexer) {
    return lexer->source[lexer->offset];
}
static char nc_peek_next(const NcLexer *lexer) {
    char current = lexer->source[lexer->offset];
    if (current == '\0')
        return '\0';
    return lexer->source[lexer->offset + 1u];
}
static char nc_advance(NcLexer *lexer) {
    char c = lexer->source[lexer->offset];
    if (c == '\0')
        return c;
    lexer->offset++;
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1u;
    } else {
        lexer->column++;
    }
    return c;
}
static int nc_token_equals(const NcToken *token, const char *text) {
    uint32_t i = 0u;
    while (text[i] != '\0') {
        if (i >= token->length || token->start[i] != text[i])
            return 0;
        i++;
    }
    return i == token->length;
}
static void nc_skip_space_and_comments(NcLexer *lexer) {
    for (;;) {
        char c = nc_peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            nc_advance(lexer);
            continue;
        }
        if (c == '/' && nc_peek_next(lexer) == '/') {
            while (nc_peek(lexer) != '\0' && nc_peek(lexer) != '\n')
                nc_advance(lexer);
            continue;
        }
        if (c == '#') {
            uint32_t p = lexer->offset;
            int directive_line = 1;
            while (p > 0u && lexer->source[p - 1u] != '\n') {
                char before = lexer->source[p - 1u];
                if (before != ' ' && before != '\t' && before != '\r') {
                    directive_line = 0;
                    break;
                }
                p--;
            }
            if (directive_line) {
                while (nc_peek(lexer) != '\0' && nc_peek(lexer) != '\n')
                    nc_advance(lexer);
                continue;
            }
        }
        if (c == '/' && nc_peek_next(lexer) == '*') {
            uint32_t line = lexer->line;
            uint32_t column = lexer->column;
            nc_advance(lexer);
            nc_advance(lexer);
            while (!(nc_peek(lexer) == '*' && nc_peek_next(lexer) == '/')) {
                if (nc_peek(lexer) == '\0') {
                    lexer->status = nc_diag(lexer->diagnostic, NOVAC_ERR_LEX, line, column,
                                            "unterminated block comment");
                    return;
                }
                nc_advance(lexer);
            }
            nc_advance(lexer);
            nc_advance(lexer);
            continue;
        }
        break;
    }
}

static NcToken nc_make_token(NcTokenKind kind, const char *start, uint32_t length, uint32_t line,
                             uint32_t column, uint32_t value) {
    NcToken token;
    token.kind = kind;
    token.start = start;
    token.length = length;
    token.line = line;
    token.column = column;
    token.value = value;
    return token;
}
static NcToken nc_lex_identifier(NcLexer *lexer, uint32_t start_offset, uint32_t line,
                                 uint32_t column) {
    NcToken token;
    while (nc_is_alpha(nc_peek(lexer)) || nc_is_digit(nc_peek(lexer)))
        nc_advance(lexer);
    token = nc_make_token(NC_TOK_IDENTIFIER, lexer->source + start_offset,
                          lexer->offset - start_offset, line, column, 0u);
    if (nc_token_equals(&token, "int"))
        token.kind = NC_TOK_KW_INT;
    else if (nc_token_equals(&token, "char"))
        token.kind = NC_TOK_KW_CHAR;
    else if (nc_token_equals(&token, "void"))
        token.kind = NC_TOK_KW_VOID;
    else if (nc_token_equals(&token, "return"))
        token.kind = NC_TOK_KW_RETURN;
    else if (nc_token_equals(&token, "if"))
        token.kind = NC_TOK_KW_IF;
    else if (nc_token_equals(&token, "else"))
        token.kind = NC_TOK_KW_ELSE;
    else if (nc_token_equals(&token, "while"))
        token.kind = NC_TOK_KW_WHILE;
    else if (nc_token_equals(&token, "do"))
        token.kind = NC_TOK_KW_DO;
    else if (nc_token_equals(&token, "for"))
        token.kind = NC_TOK_KW_FOR;
    else if (nc_token_equals(&token, "break"))
        token.kind = NC_TOK_KW_BREAK;
    else if (nc_token_equals(&token, "continue"))
        token.kind = NC_TOK_KW_CONTINUE;
    else if (nc_token_equals(&token, "goto"))
        token.kind = NC_TOK_KW_GOTO;
    else if (nc_token_equals(&token, "switch"))
        token.kind = NC_TOK_KW_SWITCH;
    else if (nc_token_equals(&token, "case"))
        token.kind = NC_TOK_KW_CASE;
    else if (nc_token_equals(&token, "default"))
        token.kind = NC_TOK_KW_DEFAULT;
    else if (nc_token_equals(&token, "enum"))
        token.kind = NC_TOK_KW_ENUM;
    else if (nc_token_equals(&token, "struct"))
        token.kind = NC_TOK_KW_STRUCT;
    else if (nc_token_equals(&token, "union"))
        token.kind = NC_TOK_KW_UNION;
    else if (nc_token_equals(&token, "extern"))
        token.kind = NC_TOK_KW_EXTERN;
    else if (nc_token_equals(&token, "typedef"))
        token.kind = NC_TOK_KW_TYPEDEF;
    else if (nc_token_equals(&token, "const"))
        token.kind = NC_TOK_KW_CONST;
    else if (nc_token_equals(&token, "static"))
        token.kind = NC_TOK_KW_STATIC;
    else if (nc_token_equals(&token, "volatile"))
        token.kind = NC_TOK_KW_VOLATILE;
    else if (nc_token_equals(&token, "restrict"))
        token.kind = NC_TOK_KW_RESTRICT;
    else if (nc_token_equals(&token, "sizeof"))
        token.kind = NC_TOK_KW_SIZEOF;
    return token;
}
static NcToken nc_lex_number(NcLexer *lexer, uint32_t start_offset, uint32_t line,
                             uint32_t column) {
    uint64_t value = 0u;
    int base = 10;
    if (nc_peek(lexer) == '0' && (nc_peek_next(lexer) == 'x' || nc_peek_next(lexer) == 'X')) {
        base = 16;
        nc_advance(lexer);
        nc_advance(lexer);
        while (1) {
            char c = nc_peek(lexer);
            uint32_t digit;
            if (c >= '0' && c <= '9')
                digit = (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = 10u + (uint32_t)(c - 'a');
            else if (c >= 'A' && c <= 'F')
                digit = 10u + (uint32_t)(c - 'A');

            else
                break;
            value = value * 16u + digit;
            nc_advance(lexer);
        }
    } else {
        while (nc_is_digit(nc_peek(lexer))) {
            value = value * 10u + (uint32_t)(nc_peek(lexer) - '0');
            nc_advance(lexer);
        }
    }
    (void)base;
    if (value > 0xFFFFFFFFu) {
        lexer->status = nc_diag(lexer->diagnostic, NOVAC_ERR_LEX, line, column,
                                "integer literal exceeds 32-bit range");
    }
    return nc_make_token(NC_TOK_INTEGER, lexer->source + start_offset, lexer->offset - start_offset,
                         line, column, (uint32_t)value);
}
static uint32_t nc_escape_value(char c) {
    switch (c) {
    case 'n':
        return (uint32_t)'\n';
    case 'r':
        return (uint32_t)'\r';
    case 't':
        return (uint32_t)'\t';
    case '0':
        return 0u;
    case '\\':
        return (uint32_t)'\\';
    case '\'':
        return (uint32_t)'\'';
    case '"':
        return (uint32_t)'"';
    default:
        return (uint32_t)(uint8_t)c;
    }
}
static NcToken nc_lex_char(NcLexer *lexer, uint32_t start_offset, uint32_t line, uint32_t column) {
    uint32_t value;
    char c;
    nc_advance(lexer); /* opening quote */
    c = nc_peek(lexer);
    if (c == '\0' || c == '\n' || c == '\'') {
        lexer->status =
            nc_diag(lexer->diagnostic, NOVAC_ERR_LEX, line, column, "invalid character literal");
        return nc_make_token(NC_TOK_CHAR, lexer->source + start_offset, 1u, line, column, 0u);
    }
    if (c == '\\') {
        nc_advance(lexer);
        c = nc_peek(lexer);
        if (c == '\0') {
            lexer->status = nc_diag(lexer->diagnostic, NOVAC_ERR_LEX, line, column,
                                    "unterminated escape sequence");
            return nc_make_token(NC_TOK_CHAR, lexer->source + start_offset, 1u, line, column, 0u);
        }
        value = nc_escape_value(c);
        nc_advance(lexer);
    } else {

        value = (uint32_t)(uint8_t)c;
        nc_advance(lexer);
    }
    if (nc_peek(lexer) != '\'') {
        lexer->status = nc_diag(lexer->diagnostic, NOVAC_ERR_LEX, line, column,
                                "character literal must contain one character");
        return nc_make_token(NC_TOK_CHAR, lexer->source + start_offset,
                             lexer->offset - start_offset, line, column, value);
    }
    nc_advance(lexer);
    return nc_make_token(NC_TOK_CHAR, lexer->source + start_offset, lexer->offset - start_offset,
                         line, column, value);
}
static NcToken nc_lex_string(NcLexer *lexer, uint32_t start_offset, uint32_t line,
                             uint32_t column) {
    uint32_t content_start;
    (void)start_offset;
    nc_advance(lexer); /* opening quote */
    content_start = lexer->offset;
    while (nc_peek(lexer) != '\0' && nc_peek(lexer) != '\n' && nc_peek(lexer) != '"') {
        if (nc_peek(lexer) == '\\') {
            nc_advance(lexer);
            if (nc_peek(lexer) == '\0' || nc_peek(lexer) == '\n')
                break;
        }
        nc_advance(lexer);
    }
    if (nc_peek(lexer) != '"') {
        lexer->status =
            nc_diag(lexer->diagnostic, NOVAC_ERR_LEX, line, column, "unterminated string literal");
        return nc_make_token(NC_TOK_STRING, lexer->source + content_start,
                             lexer->offset - content_start, line, column, 0u);
    }
    {
        uint32_t length = lexer->offset - content_start;
        nc_advance(lexer);
        return nc_make_token(NC_TOK_STRING, lexer->source + content_start, length, line, column,
                             0u);
    }
}
static NcToken nc_next_token(NcLexer *lexer) {
    uint32_t start;
    uint32_t line;
    uint32_t column;
    char c;
    nc_skip_space_and_comments(lexer);
    if (lexer->status != NOVAC_OK)
        return nc_make_token(NC_TOK_EOF, lexer->source + lexer->offset, 0u, lexer->line,
                             lexer->column, 0u);
    start = lexer->offset;
    line = lexer->line;
    column = lexer->column;
    c = nc_peek(lexer);
    if (c == '\0')
        return nc_make_token(NC_TOK_EOF, lexer->source + start, 0u, line, column, 0u);
    if (nc_is_alpha(c)) {
        nc_advance(lexer);
        return nc_lex_identifier(lexer, start, line, column);
    }
    if (nc_is_digit(c))
        return nc_lex_number(lexer, start, line, column);
    if (c == '\'')
        return nc_lex_char(lexer, start, line, column);
    if (c == '"')
        return nc_lex_string(lexer, start, line, column);
    nc_advance(lexer);
    switch (c) {
    case '(':
        return nc_make_token(NC_TOK_LPAREN, lexer->source + start, 1u, line, column, 0u);
    case ')':
        return nc_make_token(NC_TOK_RPAREN, lexer->source + start, 1u, line, column, 0u);
    case '{':
        return nc_make_token(NC_TOK_LBRACE, lexer->source + start, 1u, line, column, 0u);
    case '}':
        return nc_make_token(NC_TOK_RBRACE, lexer->source + start, 1u, line, column, 0u);
    case '[':
        return nc_make_token(NC_TOK_LBRACKET, lexer->source + start, 1u, line, column, 0u);

    case ']':
        return nc_make_token(NC_TOK_RBRACKET, lexer->source + start, 1u, line, column, 0u);
    case '.':
        return nc_make_token(NC_TOK_DOT, lexer->source + start, 1u, line, column, 0u);
    case ';':
        return nc_make_token(NC_TOK_SEMICOLON, lexer->source + start, 1u, line, column, 0u);
    case ',':
        return nc_make_token(NC_TOK_COMMA, lexer->source + start, 1u, line, column, 0u);
    case '+':
        if (nc_peek(lexer) == '+') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_PLUS_PLUS, lexer->source + start, 2u, line, column, 0u);
        }
        if (nc_peek(lexer) == '=') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_PLUS_ASSIGN, lexer->source + start, 2u, line, column, 0u);
        }
        return nc_make_token(NC_TOK_PLUS, lexer->source + start, 1u, line, column, 0u);
    case '-':
        if (nc_peek(lexer) == '>') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_ARROW, lexer->source + start, 2u, line, column, 0u);
        }
        if (nc_peek(lexer) == '-') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_MINUS_MINUS, lexer->source + start, 2u, line, column, 0u);
        }
        if (nc_peek(lexer) == '=') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_MINUS_ASSIGN, lexer->source + start, 2u, line, column, 0u);
        }
        return nc_make_token(NC_TOK_MINUS, lexer->source + start, 1u, line, column, 0u);
    case '*':
        return nc_make_token(NC_TOK_STAR, lexer->source + start, 1u, line, column, 0u);
    case '/':
        return nc_make_token(NC_TOK_SLASH, lexer->source + start, 1u, line, column, 0u);
    case '&':
        if (nc_peek(lexer) == '&') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_AND_AND, lexer->source + start, 2u, line, column, 0u);
        }
        return nc_make_token(NC_TOK_AMP, lexer->source + start, 1u, line, column, 0u);
    case '|':
        if (nc_peek(lexer) == '|') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_OR_OR, lexer->source + start, 2u, line, column, 0u);
        }
        return nc_make_token(NC_TOK_PIPE, lexer->source + start, 1u, line, column, 0u);
    case '^':
        return nc_make_token(NC_TOK_CARET, lexer->source + start, 1u, line, column, 0u);
    case '~':
        return nc_make_token(NC_TOK_TILDE, lexer->source + start, 1u, line, column, 0u);
    case '?':
        return nc_make_token(NC_TOK_QUESTION, lexer->source + start, 1u, line, column, 0u);
    case ':':
        return nc_make_token(NC_TOK_COLON, lexer->source + start, 1u, line, column, 0u);
    case '!':
        if (nc_peek(lexer) == '=') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_NE, lexer->source + start, 2u, line, column, 0u);
        }
        return nc_make_token(NC_TOK_BANG, lexer->source + start, 1u, line, column, 0u);
    case '=':
        if (nc_peek(lexer) == '=') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_EQ, lexer->source + start, 2u, line, column, 0u);
        }
        return nc_make_token(NC_TOK_ASSIGN, lexer->source + start, 1u, line, column, 0u);
    case '<':
        if (nc_peek(lexer) == '<') {

            nc_advance(lexer);
            return nc_make_token(NC_TOK_SHIFT_LEFT, lexer->source + start, 2u, line, column, 0u);
        }
        if (nc_peek(lexer) == '=') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_LE, lexer->source + start, 2u, line, column, 0u);
        }
        return nc_make_token(NC_TOK_LT, lexer->source + start, 1u, line, column, 0u);
    case '>':
        if (nc_peek(lexer) == '>') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_SHIFT_RIGHT, lexer->source + start, 2u, line, column, 0u);
        }
        if (nc_peek(lexer) == '=') {
            nc_advance(lexer);
            return nc_make_token(NC_TOK_GE, lexer->source + start, 2u, line, column, 0u);
        }
        return nc_make_token(NC_TOK_GT, lexer->source + start, 1u, line, column, 0u);
    default:
        lexer->status = nc_diag(lexer->diagnostic, NOVAC_ERR_LEX, line, column,
                                "unexpected character in source");
        return nc_make_token(NC_TOK_EOF, lexer->source + start, 1u, line, column, 0u);
    }
}
/* --------------------------------- AST ----------------------------------- */
typedef enum NcTypeKind {
    NC_TYPE_INT = 0,
    NC_TYPE_INT_PTR = 1,
    NC_TYPE_STRUCT = 2,
    NC_TYPE_ARRAY = 3,
    NC_TYPE_STRUCT_PTR = 4,
    NC_TYPE_CHAR = 5,
    NC_TYPE_CHAR_PTR = 6,
    NC_TYPE_UNION = 7,
    NC_TYPE_UNION_PTR = 8
} NcTypeKind;
#define NC_TYPE_QUAL_CONST 0x1u
#define NC_TYPE_QUAL_VOLATILE 0x2u
#define NC_TYPE_QUAL_RESTRICT 0x4u
#define NC_LINKAGE_EXTERNAL 1u
#define NC_LINKAGE_INTERNAL 2u
#define NOVAC_MAX_TYPEDEFS 256u
#define NOVAC_MAX_STATIC_LOCALS 128u
#define NOVAC_MAX_SCOPE_DEPTH 64u
typedef struct NcTypeSpec {
    NcTypeKind kind;
    NcTypeKind element_kind;
    uint32_t struct_index;
    uint32_t array_length;
    uint32_t qualifiers;
    uint32_t element_qualifiers;
} NcTypeSpec;
static NcTypeSpec nc_type_int(void) {
    NcTypeSpec type;
    type.kind = NC_TYPE_INT;
    type.element_kind = NC_TYPE_INT;
    type.struct_index = NOVAC_INVALID_NODE;
    type.array_length = 0u;
    type.qualifiers = 0u;
    type.element_qualifiers = 0u;
    return type;
}
static NcTypeSpec nc_type_int_ptr(void) {
    NcTypeSpec type = nc_type_int();
    type.kind = NC_TYPE_INT_PTR;
    return type;
}
static NcTypeSpec nc_type_char(void) {
    NcTypeSpec type = nc_type_int();

    type.kind = NC_TYPE_CHAR;
    type.element_kind = NC_TYPE_CHAR;
    return type;
}
static NcTypeSpec nc_type_char_ptr(void) {
    NcTypeSpec type = nc_type_char();
    type.kind = NC_TYPE_CHAR_PTR;
    return type;
}
static NcTypeSpec nc_type_struct(uint32_t struct_index) {
    NcTypeSpec type = nc_type_int();
    type.kind = NC_TYPE_STRUCT;
    type.struct_index = struct_index;
    return type;
}
static NcTypeSpec nc_type_struct_ptr(uint32_t struct_index) {
    NcTypeSpec type = nc_type_int();
    type.kind = NC_TYPE_STRUCT_PTR;
    type.struct_index = struct_index;
    return type;
}
static NcTypeSpec nc_type_union(uint32_t struct_index) {
    NcTypeSpec type = nc_type_int();
    type.kind = NC_TYPE_UNION;
    type.struct_index = struct_index;
    return type;
}
static int nc_type_is_pointer(NcTypeSpec type) {
    return type.kind == NC_TYPE_INT_PTR || type.kind == NC_TYPE_STRUCT_PTR ||
           type.kind == NC_TYPE_CHAR_PTR || type.kind == NC_TYPE_UNION_PTR;
}
static int nc_type_is_integer_scalar(NcTypeSpec type) {
    return type.kind == NC_TYPE_INT || type.kind == NC_TYPE_CHAR;
}
static NcTypeSpec nc_type_array(NcTypeSpec element, uint32_t length) {
    NcTypeSpec type = nc_type_int();
    type.kind = NC_TYPE_ARRAY;
    type.element_kind = element.kind;
    type.struct_index = element.struct_index;
    type.array_length = length;
    type.element_qualifiers = element.qualifiers;
    return type;
}
static NcTypeSpec nc_type_pointer_to(NcTypeSpec base) {
    NcTypeSpec pointer = nc_type_int();
    if (base.kind == NC_TYPE_INT)
        pointer.kind = NC_TYPE_INT_PTR;
    else if (base.kind == NC_TYPE_CHAR)
        pointer.kind = NC_TYPE_CHAR_PTR;
    else if (base.kind == NC_TYPE_STRUCT) {
        pointer.kind = NC_TYPE_STRUCT_PTR;
        pointer.struct_index = base.struct_index;
    } else if (base.kind == NC_TYPE_UNION) {
        pointer.kind = NC_TYPE_UNION_PTR;
        pointer.struct_index = base.struct_index;
    } else
        return base;
    pointer.element_kind = base.kind;
    pointer.struct_index = base.struct_index;
    pointer.element_qualifiers = base.qualifiers;
    return pointer;
}
static int nc_type_is_aggregate(NcTypeSpec type) {
    return type.kind == NC_TYPE_ARRAY || type.kind == NC_TYPE_STRUCT || type.kind == NC_TYPE_UNION;
}
typedef enum NcNodeKind {
    NC_NODE_BLOCK = 1,
    NC_NODE_VAR_DECL,
    NC_NODE_TYPEDEF_DECL,
    NC_NODE_ASSIGN,
    NC_NODE_COMPOUND_ASSIGN,
    NC_NODE_IF,
    NC_NODE_WHILE,
    NC_NODE_DO_WHILE,
    NC_NODE_FOR,
    NC_NODE_SWITCH,
    NC_NODE_CASE,
    NC_NODE_BREAK,
    NC_NODE_CONTINUE,
    NC_NODE_LABEL,
    NC_NODE_GOTO,
    NC_NODE_RETURN,
    NC_NODE_EXPR_STMT,

    NC_NODE_LITERAL,
    NC_NODE_STRING,
    NC_NODE_INIT_LIST,
    NC_NODE_INIT_ITEM,
    NC_NODE_VARIABLE,
    NC_NODE_BINARY,
    NC_NODE_TERNARY,
    NC_NODE_COMMA,
    NC_NODE_CAST,
    NC_NODE_UNARY,
    NC_NODE_UPDATE,
    NC_NODE_CALL,
    NC_NODE_DEREF,
    NC_NODE_ADDRESS,
    NC_NODE_SIZEOF,
    NC_NODE_INDEX,
    NC_NODE_MEMBER,
    NC_NODE_PTR_MEMBER
} NcNodeKind;
typedef enum NcBinaryOp {
    NC_BIN_ADD = 1,
    NC_BIN_SUB,
    NC_BIN_MUL,
    NC_BIN_DIV,
    NC_BIN_SHIFT_LEFT,
    NC_BIN_SHIFT_RIGHT,
    NC_BIN_BIT_AND,
    NC_BIN_BIT_XOR,
    NC_BIN_BIT_OR,
    NC_BIN_EQ,
    NC_BIN_NE,
    NC_BIN_LT,
    NC_BIN_LE,
    NC_BIN_GT,
    NC_BIN_GE,
    NC_BIN_LOGICAL_AND,
    NC_BIN_LOGICAL_OR
} NcBinaryOp;
typedef enum NcUnaryOp { NC_UN_NEGATE = 1, NC_UN_NOT, NC_UN_BIT_NOT } NcUnaryOp;
typedef enum NcUpdateOp { NC_UPDATE_INCREMENT = 1, NC_UPDATE_DECREMENT = 2 } NcUpdateOp;
typedef struct NcNode {
    NcNodeKind kind;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    uint32_t next;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t value;
    uint32_t op;
    uint32_t scope_depth;
    uint32_t epoch;

    NcTypeSpec type;
    char name[NOVAC_NAME_CAPACITY];
} NcNode;
typedef struct NcStructField {
    char name[NOVAC_NAME_CAPACITY];
    NcTypeSpec type;
    uint32_t byte_offset;
    uint32_t line;
    uint32_t column;
} NcStructField;
typedef struct NcStructDef {
    char name[NOVAC_NAME_CAPACITY];
    NcStructField fields[NOVA_COMPILER_MAX_STRUCT_FIELDS];
    uint32_t field_count;
    uint32_t byte_size;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    uint32_t is_defined;
    uint32_t is_union;
} NcStructDef;
typedef struct NcParameter {
    char name[NOVAC_NAME_CAPACITY];
    NcTypeSpec type;
    uint32_t line;
    uint32_t column;
} NcParameter;
typedef struct NcFunction {
    char name[NOVAC_NAME_CAPACITY];
    NcTypeSpec return_type;
    NcParameter parameters[NOVA_COMPILER_MAX_PARAMETERS];
    uint32_t parameter_count;
    uint32_t body;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    uint32_t is_defined;
    uint32_t linkage;
    uint32_t visibility_mask;
    uint32_t declaration_file_id;
    uint32_t declaration_line;
    uint32_t declaration_column;
} NcFunction;
typedef struct NcGlobal {
    char name[NOVAC_NAME_CAPACITY];
    NcTypeSpec type;
    uint32_t address;
    uint32_t initial_value;
    uint32_t has_initializer;
    uint32_t initializer_node;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    uint32_t is_defined;
    uint32_t has_strong_definition;
    uint32_t has_tentative_definition;
    uint32_t linkage;
    uint32_t visibility_mask;
    uint32_t declaration_file_id;
    uint32_t declaration_line;
    uint32_t declaration_column;
} NcGlobal;
typedef struct NcTypedef {
    char name[NOVAC_NAME_CAPACITY];
    NcTypeSpec type;
    uint32_t declaration_file_id;
    uint32_t line;
    uint32_t column;
    uint32_t visibility_mask;
    uint32_t function_index;
    uint32_t scope_id;
    uint32_t scope_depth;
    uint32_t is_block_scope;
} NcTypedef;
typedef struct NcStaticLocal {
    char name[NOVAC_NAME_CAPACITY];
    NcTypeSpec type;
    uint32_t address;
    uint32_t initial_value;
    uint32_t has_initializer;
    uint32_t initializer_node;
    uint32_t function_index;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
    uint32_t scope_depth;
} NcStaticLocal;
typedef struct NcEnumConstant {
    char name[NOVAC_NAME_CAPACITY];
    uint32_t expression_node;
    uint32_t previous_index;
    uint32_t value;

    uint32_t has_value;
    uint32_t resolving;
    uint32_t auto_increment;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
} NcEnumConstant;
typedef struct NcStringLiteral {
    uint8_t bytes[NOVAC_MAX_STRING_BYTES];
    uint32_t length;
    uint32_t address;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
} NcStringLiteral;
typedef struct NcParser {
    NcLexer lexer;
    NcToken current;
    NcToken previous;
    NcNode nodes[NOVA_COMPILER_MAX_AST_NODES];
    uint32_t node_count;
    uint32_t statement_count;
    uint32_t expression_count;
    NcStructDef structs[NOVA_COMPILER_MAX_STRUCTS];
    uint32_t struct_count;
    NcFunction functions[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
    uint32_t function_count;
    NcGlobal globals[NOVA_COMPILER_MAX_GLOBALS];
    uint32_t global_count;
    NcEnumConstant constants[NOVA_COMPILER_MAX_ENUM_CONSTANTS];
    uint32_t constant_count;
    NcTypedef typedefs[NOVAC_MAX_TYPEDEFS];
    uint32_t typedef_count;
    NcStaticLocal static_locals[NOVAC_MAX_STATIC_LOCALS];
    uint32_t static_local_count;
    NcStringLiteral strings[NOVAC_MAX_STRING_LITERALS];
    uint32_t string_count;
    uint32_t include_visibility[NOVA_COMPILER_MAX_TRANSLATION_UNITS];
    uint32_t unit_count;
    uint32_t block_depth;
    uint32_t loop_depth;
    uint32_t switch_depth;
    uint32_t declaration_epoch;
    uint32_t current_function_index;
    uint32_t scope_serial;
    uint32_t scope_stack[NOVAC_MAX_SCOPE_DEPTH];
    uint32_t current_file_id;
    uint32_t current_is_header;
    NovaCompilerDiagnostic *diagnostic;
    int32_t status;
} NcParser;
static void nc_parser_advance(NcParser *parser) {
    parser->previous = parser->current;
    parser->current = nc_next_token(&parser->lexer);
    if (parser->lexer.status != NOVAC_OK && parser->status == NOVAC_OK)
        parser->status = parser->lexer.status;
}
static int nc_match(NcParser *parser, NcTokenKind kind) {
    if (parser->current.kind != kind)
        return 0;
    nc_parser_advance(parser);
    return 1;
}
static int nc_expect(NcParser *parser, NcTokenKind kind, const char *message) {
    if (parser->status != NOVAC_OK)
        return 0;
    if (parser->current.kind == kind) {
        nc_parser_advance(parser);

        return 1;
    }
    parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                             parser->current.column, message);
    return 0;
}
static NcToken nc_parser_peek_token(const NcParser *parser) {
    NcLexer lexer = parser->lexer;
    return nc_next_token(&lexer);
}
static uint32_t nc_new_node(NcParser *parser, NcNodeKind kind, uint32_t line, uint32_t column) {
    NcNode *node;
    uint32_t index;
    if (parser->node_count >= NOVA_COMPILER_MAX_AST_NODES) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_AST_CAPACITY, line, column,
                                 "AST capacity exceeded");
        return NOVAC_INVALID_NODE;
    }
    index = parser->node_count++;
    node = &parser->nodes[index];
    node->kind = kind;
    node->file_id = parser->current_file_id;
    node->line = line;
    node->column = column;
    node->next = NOVAC_INVALID_NODE;
    node->a = NOVAC_INVALID_NODE;
    node->b = NOVAC_INVALID_NODE;
    node->c = NOVAC_INVALID_NODE;
    node->value = 0u;
    node->op = 0u;
    node->scope_depth = parser->block_depth;
    node->epoch = parser->declaration_epoch;
    node->type = nc_type_int();
    node->name[0] = '\0';
    if (kind >= NC_NODE_LITERAL)
        parser->expression_count++;
    else
        parser->statement_count++;
    return index;
}
static void nc_node_set_name(NcNode *node, const NcToken *token) {
    nc_copy_text(node->name, NOVAC_NAME_CAPACITY, token->start, token->length);
}
static int nc_parser_struct_index(const NcParser *parser, const char *name) {
    uint32_t i;
    for (i = 0u; i < parser->struct_count; i++) {
        if (nc_streq(parser->structs[i].name, name))
            return (int)i;
    }
    return -1;
}
static uint32_t nc_file_bit(uint32_t file_id) {
    return file_id < 32u ? ((uint32_t)1u << file_id) : 0u;
}
static uint32_t nc_declaration_visibility(const NcParser *parser, uint32_t declaration_file_id) {
    uint32_t i;
    uint32_t mask = 0u;
    uint32_t bit = nc_file_bit(declaration_file_id);
    if (parser == NULL || declaration_file_id >= parser->unit_count || bit == 0u)
        return 0u;
    for (i = 0u; i < parser->unit_count; i++) {
        if ((parser->include_visibility[i] & bit) != 0u)
            mask |= nc_file_bit(i);
    }
    return mask;
}
static int nc_parser_scope_contains(const NcParser *parser, uint32_t scope_id) {
    uint32_t depth;
    if (parser == NULL || scope_id == 0u)
        return 0;
    for (depth = 1u; depth <= parser->block_depth && depth < NOVAC_MAX_SCOPE_DEPTH; depth++)
        if (parser->scope_stack[depth] == scope_id)
            return 1;
    return 0;
}
static int nc_parser_typedef_index_visible(const NcParser *parser, const char *name,
                                           uint32_t use_file_id) {
    uint32_t i;
    uint32_t bit = nc_file_bit(use_file_id);
    if (parser == NULL || name == NULL || bit == 0u)
        return -1;
    for (i = parser->typedef_count; i > 0u; i--) {
        const NcTypedef *alias = &parser->typedefs[i - 1u];
        if (!nc_streq(alias->name, name))
            continue;
        if (alias->is_block_scope != 0u) {
            if (alias->function_index == parser->current_function_index &&
                alias->declaration_file_id == use_file_id &&
                nc_parser_scope_contains(parser, alias->scope_id))
                return (int)(i - 1u);
            continue;
        }
        if ((alias->visibility_mask & bit) != 0u)
            return (int)(i - 1u);
    }
    return -1;
}
static int nc_token_visible_typedef(const NcParser *parser, const NcToken *token,
                                    uint32_t use_file_id, NcTypeSpec *type_out) {
    char name[NOVAC_NAME_CAPACITY];
    int index;
    if (parser == NULL || token == NULL || token->kind != NC_TOK_IDENTIFIER)
        return 0;
    nc_copy_text(name, NOVAC_NAME_CAPACITY, token->start, token->length);
    index = nc_parser_typedef_index_visible(parser, name, use_file_id);
    if (index < 0)
        return 0;
    if (type_out != NULL)
        *type_out = parser->typedefs[(uint32_t)index].type;
    return 1;
}
static int nc_struct_field_index(const NcStructDef *def, const char *name) {
    uint32_t i;
    if (def == NULL)
        return -1;
    for (i = 0u; i < def->field_count; i++) {
        if (nc_streq(def->fields[i].name, name))
            return (int)i;
    }
    return -1;
}
static uint32_t nc_parse_expression(NcParser *parser);
static uint32_t nc_parse_expression_no_comma(NcParser *parser);
static uint32_t nc_parse_statement(NcParser *parser);
static int nc_parse_struct_type_name(NcParser *parser, NcTypeSpec *type_out);
static int nc_parse_union_type_name(NcParser *parser, NcTypeSpec *type_out);
static int nc_parse_decl_type(NcParser *parser, NcTypeSpec *type_out);
static int nc_add_block_typedef(NcParser *parser, const NcToken *name, NcTypeSpec type);
static uint32_t nc_parse_unary(NcParser *parser);
static uint32_t nc_parse_initializer_list(NcParser *parser);
static uint32_t nc_parser_type_size(const NcParser *parser, NcTypeSpec type);
static NcTypeSpec nc_array_element_type(NcTypeSpec type);
static int nc_node_is_assignable(const NcNode *node);
static uint32_t nc_parse_call_after_name(NcParser *parser, const NcToken *name) {
    uint32_t call_index;

    uint32_t first_arg = NOVAC_INVALID_NODE;
    uint32_t last_arg = NOVAC_INVALID_NODE;
    uint32_t arg_count = 0u;
    if (!nc_expect(parser, NC_TOK_LPAREN, "expected '(' after function name"))
        return NOVAC_INVALID_NODE;
    while (parser->status == NOVAC_OK && parser->current.kind != NC_TOK_RPAREN) {
        uint32_t arg;
        if (arg_count >= NOVA_COMPILER_MAX_PARAMETERS) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_ARGUMENT_COUNT, parser->current.line,
                        parser->current.column, "NovaC supports at most four function arguments");
            return NOVAC_INVALID_NODE;
        }
        arg = nc_parse_expression_no_comma(parser);
        if (arg == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
        if (first_arg == NOVAC_INVALID_NODE)
            first_arg = arg;
        else
            parser->nodes[last_arg].next = arg;
        last_arg = arg;
        arg_count++;
        if (!nc_match(parser, NC_TOK_COMMA))
            break;
    }
    if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after function arguments"))
        return NOVAC_INVALID_NODE;
    call_index = nc_new_node(parser, NC_NODE_CALL, name->line, name->column);
    if (call_index != NOVAC_INVALID_NODE) {
        NcNode *call = &parser->nodes[call_index];
        nc_node_set_name(call, name);
        call->a = first_arg;
        call->value = arg_count;
        call->type = nc_token_equals(name, "nova_alloc") ? nc_type_int_ptr() : nc_type_int();
    }
    return call_index;
}
static int nc_add_string_literal(NcParser *parser, const NcToken *token) {
    NcStringLiteral *literal;
    uint32_t i = 0u;
    uint32_t out = 0u;
    if (parser->string_count >= NOVAC_MAX_STRING_LITERALS) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_SYMBOL_CAPACITY, token->line,
                                 token->column, "string literal capacity exceeded");
        return -1;
    }
    literal = &parser->strings[parser->string_count];
    while (i < token->length) {
        uint32_t value;
        char c = token->start[i++];
        if (c == '\\') {
            if (i >= token->length) {
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_LEX, token->line,
                                         token->column, "invalid trailing string escape");
                return -1;
            }
            value = nc_escape_value(token->start[i++]);
        } else
            value = (uint32_t)(uint8_t)c;
        if (out + 1u >= NOVAC_MAX_STRING_BYTES) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_SOURCE_CAPACITY, token->line,
                                     token->column, "string literal exceeds fixed byte capacity");
            return -1;
        }
        literal->bytes[out++] = (uint8_t)value;
    }
    literal->bytes[out++] = 0u;
    literal->length = out;
    literal->address = 0u;
    literal->file_id = parser->current_file_id;
    literal->line = token->line;

    literal->column = token->column;
    parser->string_count++;
    return (int)(parser->string_count - 1u);
}
static uint32_t nc_parse_primary(NcParser *parser) {
    uint32_t node_index;
    NcToken token = parser->current;
    if (nc_match(parser, NC_TOK_INTEGER) || nc_match(parser, NC_TOK_CHAR)) {
        node_index = nc_new_node(parser, NC_NODE_LITERAL, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE)
            parser->nodes[node_index].value = token.value;
        return node_index;
    }
    if (nc_match(parser, NC_TOK_STRING)) {
        int string_index = nc_add_string_literal(parser, &token);
        if (string_index < 0)
            return NOVAC_INVALID_NODE;
        node_index = nc_new_node(parser, NC_NODE_STRING, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].value = (uint32_t)string_index;
            parser->nodes[node_index].type = nc_type_char_ptr();
        }
        return node_index;
    }
    if (parser->current.kind == NC_TOK_IDENTIFIER) {
        NcToken name = parser->current;
        nc_parser_advance(parser);
        if (parser->current.kind == NC_TOK_LPAREN)
            return nc_parse_call_after_name(parser, &name);
        node_index = nc_new_node(parser, NC_NODE_VARIABLE, name.line, name.column);
        if (node_index != NOVAC_INVALID_NODE)
            nc_node_set_name(&parser->nodes[node_index], &name);
        return node_index;
    }
    if (nc_match(parser, NC_TOK_LPAREN)) {
        NcTypeSpec cast_type;
        int starts_type =
            parser->current.kind == NC_TOK_KW_INT || parser->current.kind == NC_TOK_KW_CHAR ||
            parser->current.kind == NC_TOK_KW_STRUCT || parser->current.kind == NC_TOK_KW_UNION ||
            parser->current.kind == NC_TOK_KW_CONST || parser->current.kind == NC_TOK_KW_VOLATILE ||
            parser->current.kind == NC_TOK_KW_RESTRICT ||
            (parser->current.kind == NC_TOK_IDENTIFIER &&
             nc_token_visible_typedef(parser, &parser->current, parser->current_file_id, NULL));
        if (starts_type) {
            uint32_t operand;
            if (!nc_parse_decl_type(parser, &cast_type))
                return NOVAC_INVALID_NODE;
            if (cast_type.kind == NC_TYPE_STRUCT || cast_type.kind == NC_TYPE_UNION ||
                cast_type.kind == NC_TYPE_ARRAY) {
                parser->status = nc_diag(
                    parser->diagnostic, NOVAC_ERR_UNSUPPORTED, token.line, token.column,
                    "explicit casts support integer/char/pointer targets, not aggregate values");
                return NOVAC_INVALID_NODE;
            }
            if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after cast type"))
                return NOVAC_INVALID_NODE;
            operand = nc_parse_unary(parser);
            if (operand == NOVAC_INVALID_NODE)
                return NOVAC_INVALID_NODE;
            node_index = nc_new_node(parser, NC_NODE_CAST, token.line, token.column);
            if (node_index != NOVAC_INVALID_NODE) {
                parser->nodes[node_index].a = operand;
                parser->nodes[node_index].type = cast_type;
            }
            return node_index;
        }
        node_index = nc_parse_expression(parser);
        if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after expression"))
            return NOVAC_INVALID_NODE;
        return node_index;
    }
    parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, token.line, token.column,
                             "expected expression");
    return NOVAC_INVALID_NODE;
}
static uint32_t nc_parse_postfix(NcParser *parser) {
    uint32_t base = nc_parse_primary(parser);
    while (base != NOVAC_INVALID_NODE && parser->status == NOVAC_OK) {
        if (parser->current.kind == NC_TOK_LBRACKET) {
            NcToken bracket = parser->current;
            uint32_t index_expr;
            uint32_t node_index;
            nc_parser_advance(parser);
            index_expr = nc_parse_expression(parser);
            if (!nc_expect(parser, NC_TOK_RBRACKET, "expected ']' after array index"))
                return NOVAC_INVALID_NODE;
            node_index = nc_new_node(parser, NC_NODE_INDEX, bracket.line, bracket.column);
            if (node_index != NOVAC_INVALID_NODE) {
                parser->nodes[node_index].a = base;
                parser->nodes[node_index].b = index_expr;
            }
            base = node_index;
            continue;
        }
        if (parser->current.kind == NC_TOK_PLUS_PLUS ||
            parser->current.kind == NC_TOK_MINUS_MINUS) {
            NcToken update = parser->current;

            uint32_t node_index;
            nc_parser_advance(parser);
            if (!nc_node_is_assignable(&parser->nodes[base])) {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, update.line, update.column,
                            "postfix ++/-- requires an assignable lvalue");
                return NOVAC_INVALID_NODE;
            }
            node_index = nc_new_node(parser, NC_NODE_UPDATE, update.line, update.column);
            if (node_index != NOVAC_INVALID_NODE) {
                parser->nodes[node_index].a = base;
                parser->nodes[node_index].op =
                    update.kind == NC_TOK_PLUS_PLUS ? NC_UPDATE_INCREMENT : NC_UPDATE_DECREMENT;
                parser->nodes[node_index].value = 1u; /* postfix */
            }
            base = node_index;
            continue;
        }
        if (parser->current.kind == NC_TOK_DOT || parser->current.kind == NC_TOK_ARROW) {
            NcToken dot = parser->current;
            NcTokenKind member_kind = parser->current.kind;
            NcToken member;
            uint32_t node_index;
            nc_parser_advance(parser);
            if (parser->current.kind != NC_TOK_IDENTIFIER) {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                            parser->current.column, "expected member name after '.' or '->'");
                return NOVAC_INVALID_NODE;
            }
            member = parser->current;
            nc_parser_advance(parser);
            node_index = nc_new_node(
                parser, member_kind == NC_TOK_ARROW ? NC_NODE_PTR_MEMBER : NC_NODE_MEMBER, dot.line,
                dot.column);
            if (node_index != NOVAC_INVALID_NODE) {
                parser->nodes[node_index].a = base;
                nc_node_set_name(&parser->nodes[node_index], &member);
            }
            base = node_index;
            continue;
        }
        break;
    }
    return base;
}
static uint32_t nc_parse_unary(NcParser *parser) {
    NcToken token = parser->current;
    if (nc_match(parser, NC_TOK_PLUS_PLUS) || nc_match(parser, NC_TOK_MINUS_MINUS)) {
        uint32_t operand = nc_parse_unary(parser);
        uint32_t node_index;
        if (operand == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
        if (!nc_node_is_assignable(&parser->nodes[operand])) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, token.line, token.column,
                                     "prefix ++/-- requires an assignable lvalue");
            return NOVAC_INVALID_NODE;
        }
        node_index = nc_new_node(parser, NC_NODE_UPDATE, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].a = operand;
            parser->nodes[node_index].op =
                token.kind == NC_TOK_PLUS_PLUS ? NC_UPDATE_INCREMENT : NC_UPDATE_DECREMENT;
            parser->nodes[node_index].value = 0u; /* prefix */
        }
        return node_index;
    }
    if (nc_match(parser, NC_TOK_KW_SIZEOF)) {
        uint32_t node_index = nc_new_node(parser, NC_NODE_SIZEOF, token.line, token.column);

        if (node_index == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
        if (!nc_expect(parser, NC_TOK_LPAREN, "expected '(' after sizeof"))
            return NOVAC_INVALID_NODE;
        if (parser->current.kind == NC_TOK_KW_INT) {
            NcTypeSpec type = nc_type_int();
            nc_parser_advance(parser);
            if (nc_match(parser, NC_TOK_STAR))
                type = nc_type_int_ptr();
            parser->nodes[node_index].type = type;
            parser->nodes[node_index].value = 1u;
        } else if (parser->current.kind == NC_TOK_KW_CHAR) {
            NcTypeSpec type = nc_type_char();
            nc_parser_advance(parser);
            if (nc_match(parser, NC_TOK_STAR))
                type = nc_type_char_ptr();
            parser->nodes[node_index].type = type;
            parser->nodes[node_index].value = 1u;
        } else if (parser->current.kind == NC_TOK_KW_STRUCT) {
            NcTypeSpec type;
            nc_parser_advance(parser);
            if (!nc_parse_struct_type_name(parser, &type))
                return NOVAC_INVALID_NODE;
            if (nc_match(parser, NC_TOK_STAR))
                type = nc_type_struct_ptr(type.struct_index);
            parser->nodes[node_index].type = type;
            parser->nodes[node_index].value = 1u;
        } else {
            parser->nodes[node_index].a = nc_parse_expression(parser);
        }
        if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after sizeof operand"))
            return NOVAC_INVALID_NODE;
        return node_index;
    }
    if (nc_match(parser, NC_TOK_MINUS) || nc_match(parser, NC_TOK_BANG) ||
        nc_match(parser, NC_TOK_TILDE)) {
        uint32_t operand = nc_parse_unary(parser);
        uint32_t node_index = nc_new_node(parser, NC_NODE_UNARY, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].op = token.kind == NC_TOK_MINUS  ? NC_UN_NEGATE
                                           : token.kind == NC_TOK_BANG ? NC_UN_NOT
                                                                       : NC_UN_BIT_NOT;
            parser->nodes[node_index].a = operand;
        }
        return node_index;
    }
    if (nc_match(parser, NC_TOK_STAR)) {
        uint32_t operand = nc_parse_unary(parser);
        uint32_t node_index = nc_new_node(parser, NC_NODE_DEREF, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE)
            parser->nodes[node_index].a = operand;
        return node_index;
    }
    if (nc_match(parser, NC_TOK_AMP)) {
        uint32_t operand = nc_parse_unary(parser);
        uint32_t node_index = nc_new_node(parser, NC_NODE_ADDRESS, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE)
            parser->nodes[node_index].a = operand;
        return node_index;
    }
    return nc_parse_postfix(parser);
}
static uint32_t nc_parse_binary_level(NcParser *parser, uint32_t (*next_level)(NcParser *),
                                      const NcTokenKind *kinds, const NcBinaryOp *ops,
                                      uint32_t count) {
    uint32_t left = next_level(parser);
    while (parser->status == NOVAC_OK) {

        uint32_t i;
        int matched = 0;
        for (i = 0u; i < count; i++) {
            if (parser->current.kind == kinds[i]) {
                NcToken token = parser->current;
                uint32_t right;
                uint32_t node_index;
                nc_parser_advance(parser);
                right = next_level(parser);
                node_index = nc_new_node(parser, NC_NODE_BINARY, token.line, token.column);
                if (node_index != NOVAC_INVALID_NODE) {
                    parser->nodes[node_index].op = (uint32_t)ops[i];
                    parser->nodes[node_index].a = left;
                    parser->nodes[node_index].b = right;
                }
                left = node_index;
                matched = 1;
                break;
            }
        }
        if (!matched)
            break;
    }
    return left;
}
static uint32_t nc_parse_multiplicative(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_STAR, NC_TOK_SLASH};
    static const NcBinaryOp ops[] = {NC_BIN_MUL, NC_BIN_DIV};
    return nc_parse_binary_level(parser, nc_parse_unary, kinds, ops, 2u);
}
static uint32_t nc_parse_additive(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_PLUS, NC_TOK_MINUS};
    static const NcBinaryOp ops[] = {NC_BIN_ADD, NC_BIN_SUB};
    return nc_parse_binary_level(parser, nc_parse_multiplicative, kinds, ops, 2u);
}
static uint32_t nc_parse_shift(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_SHIFT_LEFT, NC_TOK_SHIFT_RIGHT};
    static const NcBinaryOp ops[] = {NC_BIN_SHIFT_LEFT, NC_BIN_SHIFT_RIGHT};
    return nc_parse_binary_level(parser, nc_parse_additive, kinds, ops, 2u);
}
static uint32_t nc_parse_relational(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_LT, NC_TOK_LE, NC_TOK_GT, NC_TOK_GE};
    static const NcBinaryOp ops[] = {NC_BIN_LT, NC_BIN_LE, NC_BIN_GT, NC_BIN_GE};
    return nc_parse_binary_level(parser, nc_parse_shift, kinds, ops, 4u);
}
static uint32_t nc_parse_equality(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_EQ, NC_TOK_NE};
    static const NcBinaryOp ops[] = {NC_BIN_EQ, NC_BIN_NE};
    return nc_parse_binary_level(parser, nc_parse_relational, kinds, ops, 2u);
}
static uint32_t nc_parse_bitwise_and(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_AMP};
    static const NcBinaryOp ops[] = {NC_BIN_BIT_AND};
    return nc_parse_binary_level(parser, nc_parse_equality, kinds, ops, 1u);
}

static uint32_t nc_parse_bitwise_xor(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_CARET};
    static const NcBinaryOp ops[] = {NC_BIN_BIT_XOR};
    return nc_parse_binary_level(parser, nc_parse_bitwise_and, kinds, ops, 1u);
}
static uint32_t nc_parse_bitwise_or(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_PIPE};
    static const NcBinaryOp ops[] = {NC_BIN_BIT_OR};
    return nc_parse_binary_level(parser, nc_parse_bitwise_xor, kinds, ops, 1u);
}
static uint32_t nc_parse_logical_and(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_AND_AND};
    static const NcBinaryOp ops[] = {NC_BIN_LOGICAL_AND};
    return nc_parse_binary_level(parser, nc_parse_bitwise_or, kinds, ops, 1u);
}
static uint32_t nc_parse_logical_or(NcParser *parser) {
    static const NcTokenKind kinds[] = {NC_TOK_OR_OR};
    static const NcBinaryOp ops[] = {NC_BIN_LOGICAL_OR};
    return nc_parse_binary_level(parser, nc_parse_logical_and, kinds, ops, 1u);
}
static uint32_t nc_parse_conditional(NcParser *parser) {
    uint32_t condition = nc_parse_logical_or(parser);
    if (parser->status != NOVAC_OK || parser->current.kind != NC_TOK_QUESTION)
        return condition;
    {
        NcToken token = parser->current;
        uint32_t when_true;
        uint32_t when_false;
        uint32_t node_index;
        nc_parser_advance(parser);
        when_true = nc_parse_expression(parser);
        if (!nc_expect(parser, NC_TOK_COLON, "expected ':' in conditional expression"))
            return NOVAC_INVALID_NODE;
        when_false = nc_parse_conditional(parser);
        node_index = nc_new_node(parser, NC_NODE_TERNARY, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].a = condition;
            parser->nodes[node_index].b = when_true;
            parser->nodes[node_index].c = when_false;
        }
        return node_index;
    }
}
static uint32_t nc_parse_expression_no_comma(NcParser *parser) {
    return nc_parse_conditional(parser);
}
static uint32_t nc_parse_expression(NcParser *parser) {
    uint32_t left = nc_parse_expression_no_comma(parser);
    while (left != NOVAC_INVALID_NODE && parser->status == NOVAC_OK &&
           parser->current.kind == NC_TOK_COMMA) {
        NcToken token = parser->current;
        uint32_t right;
        uint32_t node_index;
        nc_parser_advance(parser);
        right = nc_parse_expression_no_comma(parser);
        if (right == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
        node_index = nc_new_node(parser, NC_NODE_COMMA, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].a = left;
            parser->nodes[node_index].b = right;
        }
        left = node_index;
    }
    return left;
}
static uint32_t nc_parse_block(NcParser *parser) {
    uint32_t block;
    uint32_t first = NOVAC_INVALID_NODE;
    uint32_t last = NOVAC_INVALID_NODE;
    NcToken opening = parser->previous;
    block = nc_new_node(parser, NC_NODE_BLOCK, opening.line, opening.column);
    if (parser->block_depth + 1u >= NOVAC_MAX_SCOPE_DEPTH) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_SYMBOL_CAPACITY, opening.line,
                                 opening.column, "lexical scope depth exceeds fixed capacity");
        return NOVAC_INVALID_NODE;
    }
    parser->block_depth++;
    parser->scope_serial++;
    if (parser->scope_serial == 0u)
        parser->scope_serial++;
    parser->scope_stack[parser->block_depth] = parser->scope_serial;
    while (parser->status == NOVAC_OK && parser->current.kind != NC_TOK_RBRACE &&
           parser->current.kind != NC_TOK_EOF) {
        uint32_t statement = nc_parse_statement(parser);

        if (statement == NOVAC_INVALID_NODE)
            break;
        if (first == NOVAC_INVALID_NODE)
            first = statement;
        else
            parser->nodes[last].next = statement;
        last = statement;
    }
    if (!nc_expect(parser, NC_TOK_RBRACE, "expected '}' after block")) {
        if (parser->block_depth > 0u) {
            parser->scope_stack[parser->block_depth] = 0u;
            parser->block_depth--;
        }
        return NOVAC_INVALID_NODE;
    }
    if (parser->block_depth > 0u) {
        parser->scope_stack[parser->block_depth] = 0u;
        parser->block_depth--;
    }
    if (block != NOVAC_INVALID_NODE)
        parser->nodes[block].a = first;
    return block;
}
static int nc_parse_aggregate_type_name(NcParser *parser, NcTypeSpec *type_out, uint32_t is_union) {
    NcToken name;
    char type_name[NOVAC_NAME_CAPACITY];
    int index;
    if (parser->current.kind != NC_TOK_IDENTIFIER) {
        parser->status = nc_diag(
            parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line, parser->current.column,
            is_union ? "expected union type name" : "expected struct type name");
        return 0;
    }
    name = parser->current;
    nc_parser_advance(parser);
    nc_copy_text(type_name, NOVAC_NAME_CAPACITY, name.start, name.length);
    index = nc_parser_struct_index(parser, type_name);
    if (index < 0) {
        NcStructDef *placeholder;
        if (parser->struct_count >= NOVA_COMPILER_MAX_STRUCTS) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_TYPE_CAPACITY, name.line,
                                     name.column, "aggregate type capacity exceeded");
            return 0;
        }
        index = (int)parser->struct_count++;
        placeholder = &parser->structs[(uint32_t)index];
        nc_copy_text(placeholder->name, NOVAC_NAME_CAPACITY, name.start, name.length);
        placeholder->field_count = 0u;
        placeholder->byte_size = 0u;
        placeholder->file_id = parser->current_file_id;
        placeholder->line = name.line;
        placeholder->column = name.column;
        placeholder->is_defined = 0u;
        placeholder->is_union = is_union;
    } else if (parser->structs[(uint32_t)index].is_union != is_union) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name.line, name.column,
                    "aggregate tag was previously declared with a different kind");
        return 0;
    }
    *type_out = is_union ? nc_type_union((uint32_t)index) : nc_type_struct((uint32_t)index);
    return 1;
}
static int nc_parse_struct_type_name(NcParser *parser, NcTypeSpec *type_out) {
    return nc_parse_aggregate_type_name(parser, type_out, 0u);
}
static int nc_parse_union_type_name(NcParser *parser, NcTypeSpec *type_out) {
    return nc_parse_aggregate_type_name(parser, type_out, 1u);
}
static int nc_parse_type_qualifiers(NcParser *parser, uint32_t *qualifiers_out,
                                    int allow_restrict) {
    uint32_t qualifiers = 0u;
    int matched = 1;
    if (parser == NULL || qualifiers_out == NULL)
        return 0;
    while (matched) {
        matched = 0;
        if (nc_match(parser, NC_TOK_KW_CONST)) {
            if ((qualifiers & NC_TYPE_QUAL_CONST) != 0u) {
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION,
                                         parser->previous.line, parser->previous.column,
                                         "duplicate const qualifier");
                return 0;
            }
            qualifiers |= NC_TYPE_QUAL_CONST;
            matched = 1;
        } else if (nc_match(parser, NC_TOK_KW_VOLATILE)) {
            if ((qualifiers & NC_TYPE_QUAL_VOLATILE) != 0u) {
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION,
                                         parser->previous.line, parser->previous.column,
                                         "duplicate volatile qualifier");
                return 0;
            }
            qualifiers |= NC_TYPE_QUAL_VOLATILE;
            matched = 1;
        } else if (allow_restrict && nc_match(parser, NC_TOK_KW_RESTRICT)) {
            if ((qualifiers & NC_TYPE_QUAL_RESTRICT) != 0u) {
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION,
                                         parser->previous.line, parser->previous.column,
                                         "duplicate restrict qualifier");
                return 0;
            }
            qualifiers |= NC_TYPE_QUAL_RESTRICT;
            matched = 1;
        }
    }
    *qualifiers_out = qualifiers;
    return 1;
}
static int nc_parse_decl_type(NcParser *parser, NcTypeSpec *type_out) {
    NcTypeSpec type;
    uint32_t leading = 0u;
    uint32_t trailing = 0u;
    uint32_t pointer_qualifiers = 0u;
    int base_from_typedef = 0;
    if (parser == NULL || type_out == NULL)
        return 0;
    /* restrict in declaration specifiers is accepted only when a typedef already
       denotes a pointer type. Ordinary pointer declarators use `* restrict`. */
    while (parser->current.kind == NC_TOK_KW_CONST || parser->current.kind == NC_TOK_KW_VOLATILE ||
           parser->current.kind == NC_TOK_KW_RESTRICT) {
        if (parser->current.kind == NC_TOK_KW_RESTRICT) {
            if ((leading & NC_TYPE_QUAL_RESTRICT) != 0u) {
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION,
                                         parser->current.line, parser->current.column,
                                         "duplicate restrict qualifier");
                return 0;
            }
            leading |= NC_TYPE_QUAL_RESTRICT;
            nc_parser_advance(parser);
        } else {
            uint32_t q = 0u;
            if (!nc_parse_type_qualifiers(parser, &q, 0))
                return 0;
            if ((leading & q) != 0u) {
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION,
                                         parser->previous.line, parser->previous.column,
                                         "duplicate type qualifier");
                return 0;
            }
            leading |= q;
        }
    }
    if (nc_match(parser, NC_TOK_KW_INT)) {
        type = nc_type_int();
    } else if (nc_match(parser, NC_TOK_KW_CHAR)) {
        type = nc_type_char();
    } else if (nc_match(parser, NC_TOK_KW_STRUCT)) {
        if (!nc_parse_struct_type_name(parser, &type))
            return 0;
    } else if (nc_match(parser, NC_TOK_KW_UNION)) {
        if (!nc_parse_union_type_name(parser, &type))
            return 0;
    } else if (parser->current.kind == NC_TOK_IDENTIFIER &&
               nc_token_visible_typedef(parser, &parser->current, parser->current_file_id, &type)) {
        base_from_typedef = 1;
        nc_parser_advance(parser);
    } else {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                 parser->current.column,
                                 "expected a supported type specifier or visible typedef name");
        return 0;
    }
    if (!nc_parse_type_qualifiers(parser, &trailing, 1))
        return 0;
    if ((leading & trailing) != 0u) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, parser->previous.line,
                    parser->previous.column, "duplicate type qualifier");
        return 0;
    }
    type.qualifiers |= leading | trailing;
    if ((type.qualifiers & NC_TYPE_QUAL_RESTRICT) != 0u && !nc_type_is_pointer(type)) {
        /* A restrict qualifier in decl-specifier position cannot qualify the later `*`. */
        parser->status = nc_diag(
            parser->diagnostic, NOVAC_ERR_UNSUPPORTED, parser->previous.line,
            parser->previous.column,
            "restrict qualifier requires an already-pointer typedef or a `* restrict` declarator");
        return 0;
    }
    (void)base_from_typedef;
    if (nc_match(parser, NC_TOK_STAR)) {
        NcTypeSpec pointer;
        if (nc_type_is_pointer(type)) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_INVALID_POINTER,
                                     parser->previous.line, parser->previous.column,
                                     "pointer nesting beyond one level is not supported");
            return 0;
        }
        pointer = nc_type_pointer_to(type);
        if (!nc_type_is_pointer(pointer)) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_INVALID_POINTER, parser->previous.line,
                        parser->previous.column, "unsupported pointer target type");
            return 0;
        }
        if (!nc_parse_type_qualifiers(parser, &pointer_qualifiers, 1))
            return 0;
        pointer.qualifiers |= pointer_qualifiers;
        type = pointer;
    }
    if ((type.qualifiers & NC_TYPE_QUAL_RESTRICT) != 0u && !nc_type_is_pointer(type)) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED, parser->previous.line,
                    parser->previous.column, "restrict qualifier requires a pointer object type");
        return 0;
    }
    *type_out = type;
    return 1;
}
static uint32_t nc_parse_initializer_value(NcParser *parser) {
    if (parser->current.kind == NC_TOK_LBRACE)
        return nc_parse_initializer_list(parser);
    return nc_parse_expression_no_comma(parser);
}
static uint32_t nc_parse_initializer_list(NcParser *parser) {
    NcToken opening = parser->current;
    uint32_t list;
    uint32_t first = NOVAC_INVALID_NODE;
    uint32_t last = NOVAC_INVALID_NODE;
    uint32_t count = 0u;
    if (!nc_expect(parser, NC_TOK_LBRACE, "expected '{' for aggregate initializer"))
        return NOVAC_INVALID_NODE;
    list = nc_new_node(parser, NC_NODE_INIT_LIST, opening.line, opening.column);
    while (parser->status == NOVAC_OK && parser->current.kind != NC_TOK_RBRACE) {
        uint32_t value;
        uint32_t item;
        uint32_t designator = 0u; /* 0 positional, 1 .field, 2 [index] */
        NcToken designator_token = parser->current;
        char designator_name[NOVAC_NAME_CAPACITY];
        uint32_t designator_index = 0u;
        designator_name[0] = '\0';
        if (count >= NOVA_COMPILER_MAX_ARRAY_LENGTH) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_ARRAY_BOUNDS,
                                     parser->current.line, parser->current.column,
                                     "initializer list exceeds fixed aggregate capacity");
            return NOVAC_INVALID_NODE;
        }
        if (nc_match(parser, NC_TOK_DOT)) {
            NcToken field;
            designator = 1u;
            if (parser->current.kind != NC_TOK_IDENTIFIER) {
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                         parser->current.column,
                                         "expected field name after initializer '.' designator");
                return NOVAC_INVALID_NODE;
            }
            field = parser->current;
            nc_parser_advance(parser);
            nc_copy_text(designator_name, NOVAC_NAME_CAPACITY, field.start, field.length);
            if (!nc_expect(parser, NC_TOK_ASSIGN, "expected '=' after field designator"))
                return NOVAC_INVALID_NODE;
        } else if (nc_match(parser, NC_TOK_LBRACKET)) {
            NcToken index_token = parser->current;
            designator = 2u;
            if (parser->current.kind != NC_TOK_INTEGER) {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                            parser->current.column,
                            "array initializer designator requires an integer constant index");
                return NOVAC_INVALID_NODE;
            }
            designator_index = index_token.value;
            nc_parser_advance(parser);
            if (!nc_expect(parser, NC_TOK_RBRACKET,
                           "expected ']' after array initializer designator"))
                return NOVAC_INVALID_NODE;
            if (!nc_expect(parser, NC_TOK_ASSIGN,
                           "expected '=' after array initializer designator"))
                return NOVAC_INVALID_NODE;
        }
        value = nc_parse_initializer_value(parser);
        if (value == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
        item =
            nc_new_node(parser, NC_NODE_INIT_ITEM, designator_token.line, designator_token.column);
        if (item == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
        parser->nodes[item].a = value;
        parser->nodes[item].op = designator;
        parser->nodes[item].value = designator_index;
        if (designator == 1u)
            nc_copy_text(parser->nodes[item].name, NOVAC_NAME_CAPACITY, designator_name,
                         (uint32_t)nc_strlen(designator_name));
        if (first == NOVAC_INVALID_NODE)
            first = item;
        else
            parser->nodes[last].next = item;
        last = item;
        count++;
        if (!nc_match(parser, NC_TOK_COMMA))
            break;
        if (parser->current.kind == NC_TOK_RBRACE)
            break;
    }
    if (!nc_expect(parser, NC_TOK_RBRACE, "expected '}' after aggregate initializer"))
        return NOVAC_INVALID_NODE;
    if (list != NOVAC_INVALID_NODE) {
        parser->nodes[list].a = first;
        parser->nodes[list].value = count;
    }
    return list;
}
static uint32_t nc_parse_declaration_after_type(NcParser *parser, NcTypeSpec type) {
    NcToken name;
    uint32_t initializer = NOVAC_INVALID_NODE;
    uint32_t node_index;
    if (parser->current.kind != NC_TOK_IDENTIFIER) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                 parser->current.column, "expected variable name after type");
        return NOVAC_INVALID_NODE;
    }
    name = parser->current;
    nc_parser_advance(parser);
    if (nc_match(parser, NC_TOK_LBRACKET)) {
        uint32_t length;
        NcToken length_token = parser->current;
        if (parser->current.kind != NC_TOK_INTEGER) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                        parser->current.column, "array length must be an integer constant");
            return NOVAC_INVALID_NODE;
        }
        length = length_token.value;
        nc_parser_advance(parser);
        if (length == 0u || length > NOVA_COMPILER_MAX_ARRAY_LENGTH) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_ARRAY_BOUNDS, length_token.line,
                        length_token.column, "array length is outside the Vol.10 fixed-size limit");
            return NOVAC_INVALID_NODE;
        }
        if (!nc_expect(parser, NC_TOK_RBRACKET, "expected ']' after array length"))
            return NOVAC_INVALID_NODE;
        type = nc_type_array(type, length);
    }
    if (nc_match(parser, NC_TOK_ASSIGN)) {
        if (nc_type_is_aggregate(type)) {

            if (type.kind == NC_TYPE_ARRAY && type.element_kind == NC_TYPE_CHAR &&
                parser->current.kind == NC_TOK_STRING)
                initializer = nc_parse_expression_no_comma(parser);
            else if (parser->current.kind == NC_TOK_LBRACE)
                initializer = nc_parse_initializer_list(parser);
            else {
                parser->status = nc_diag(
                    parser->diagnostic, NOVAC_ERR_UNSUPPORTED, name.line, name.column,
                    "aggregate initializer requires a brace list (or a string for char arrays)");
                return NOVAC_INVALID_NODE;
            }
        } else
            initializer = nc_parse_expression_no_comma(parser);
    }
    if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after variable declaration"))
        return NOVAC_INVALID_NODE;
    parser->declaration_epoch++;
    node_index = nc_new_node(parser, NC_NODE_VAR_DECL, name.line, name.column);
    if (node_index != NOVAC_INVALID_NODE) {
        NcNode *node = &parser->nodes[node_index];
        nc_node_set_name(node, &name);
        node->type = type;
        node->a = initializer;
    }
    return node_index;
}
static uint32_t nc_parse_declaration(NcParser *parser) {
    NcTypeSpec type;
    if (!nc_parse_decl_type(parser, &type))
        return NOVAC_INVALID_NODE;
    return nc_parse_declaration_after_type(parser, type);
}
static int nc_node_is_assignable(const NcNode *node) {
    if (node == NULL)
        return 0;
    return node->kind == NC_NODE_VARIABLE || node->kind == NC_NODE_DEREF ||
           node->kind == NC_NODE_INDEX || node->kind == NC_NODE_MEMBER ||
           node->kind == NC_NODE_PTR_MEMBER;
}
static uint32_t nc_parse_assignment_or_expression_clause(NcParser *parser, int consume_semicolon) {
    uint32_t lhs;
    uint32_t node_index;
    uint32_t line = parser->current.line;
    uint32_t column = parser->current.column;
    lhs = nc_parse_expression(parser);
    if (lhs == NOVAC_INVALID_NODE)
        return NOVAC_INVALID_NODE;
    if (parser->current.kind == NC_TOK_ASSIGN || parser->current.kind == NC_TOK_PLUS_ASSIGN ||
        parser->current.kind == NC_TOK_MINUS_ASSIGN) {
        NcTokenKind assign_kind = parser->current.kind;
        uint32_t rhs;
        nc_parser_advance(parser);
        if (!nc_node_is_assignable(&parser->nodes[lhs])) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, line, column,
                                     "left side of assignment is not assignable");
            return NOVAC_INVALID_NODE;
        }
        rhs = nc_parse_expression(parser);
        if (consume_semicolon &&
            !nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after assignment"))
            return NOVAC_INVALID_NODE;
        node_index = nc_new_node(
            parser, assign_kind == NC_TOK_ASSIGN ? NC_NODE_ASSIGN : NC_NODE_COMPOUND_ASSIGN, line,
            column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].a = lhs;
            parser->nodes[node_index].b = rhs;
            parser->nodes[node_index].op =
                assign_kind == NC_TOK_MINUS_ASSIGN ? NC_BIN_SUB : NC_BIN_ADD;
        }
        return node_index;
    }
    if (consume_semicolon && !nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after expression"))
        return NOVAC_INVALID_NODE;
    node_index = nc_new_node(parser, NC_NODE_EXPR_STMT, line, column);
    if (node_index != NOVAC_INVALID_NODE)
        parser->nodes[node_index].a = lhs;
    return node_index;
}
static uint32_t nc_parse_assignment_or_expression_statement(NcParser *parser) {
    return nc_parse_assignment_or_expression_clause(parser, 1);
}
static uint32_t nc_parse_switch_statement(NcParser *parser, NcToken token) {
    uint32_t condition;
    uint32_t switch_node;
    uint32_t first_case = NOVAC_INVALID_NODE;
    uint32_t last_case = NOVAC_INVALID_NODE;
    if (!nc_expect(parser, NC_TOK_LPAREN, "expected '(' after switch"))
        return NOVAC_INVALID_NODE;
    condition = nc_parse_expression(parser);
    if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after switch expression"))
        return NOVAC_INVALID_NODE;
    if (!nc_expect(parser, NC_TOK_LBRACE, "expected '{' after switch expression"))
        return NOVAC_INVALID_NODE;
    switch_node = nc_new_node(parser, NC_NODE_SWITCH, token.line, token.column);
    parser->switch_depth++;
    while (parser->status == NOVAC_OK && parser->current.kind != NC_TOK_RBRACE &&
           parser->current.kind != NC_TOK_EOF) {
        NcToken label = parser->current;
        uint32_t case_node;
        uint32_t case_expr = NOVAC_INVALID_NODE;
        uint32_t first_stmt = NOVAC_INVALID_NODE;
        uint32_t last_stmt = NOVAC_INVALID_NODE;
        int is_default = 0;
        if (nc_match(parser, NC_TOK_KW_CASE)) {
            case_expr = nc_parse_expression_no_comma(parser);
            if (case_expr == NOVAC_INVALID_NODE)
                break;
            if (!nc_expect(parser, NC_TOK_COLON, "expected ':' after case constant expression"))
                break;
        } else if (nc_match(parser, NC_TOK_KW_DEFAULT)) {
            is_default = 1;
            if (!nc_expect(parser, NC_TOK_COLON, "expected ':' after default"))
                break;
        } else {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                        parser->current.column, "switch body must begin with case/default labels");
            break;
        }
        case_node = nc_new_node(parser, NC_NODE_CASE, label.line, label.column);
        if (case_node == NOVAC_INVALID_NODE)
            break;
        parser->nodes[case_node].a = case_expr;
        parser->nodes[case_node].op = is_default ? 1u : 0u;
        while (parser->status == NOVAC_OK && parser->current.kind != NC_TOK_KW_CASE &&
               parser->current.kind != NC_TOK_KW_DEFAULT && parser->current.kind != NC_TOK_RBRACE &&
               parser->current.kind != NC_TOK_EOF) {
            uint32_t stmt = nc_parse_statement(parser);
            if (stmt == NOVAC_INVALID_NODE)
                break;
            if (first_stmt == NOVAC_INVALID_NODE)
                first_stmt = stmt;
            else
                parser->nodes[last_stmt].next = stmt;
            last_stmt = stmt;
        }
        parser->nodes[case_node].b = first_stmt;
        if (first_case == NOVAC_INVALID_NODE)
            first_case = case_node;
        else
            parser->nodes[last_case].next = case_node;
        last_case = case_node;
    }
    if (parser->switch_depth > 0u)
        parser->switch_depth--;
    if (!nc_expect(parser, NC_TOK_RBRACE, "expected '}' after switch body"))
        return NOVAC_INVALID_NODE;
    if (first_case == NOVAC_INVALID_NODE) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, token.line, token.column,
                                 "switch requires at least one case/default label");
        return NOVAC_INVALID_NODE;
    }
    parser->nodes[switch_node].a = condition;
    parser->nodes[switch_node].b = first_case;

    return switch_node;
}
static uint32_t nc_parse_block_typedef_statement(NcParser *parser, NcToken token) {
    NcTypeSpec type;
    NcToken name;
    uint32_t node_index;
    if (!nc_parse_decl_type(parser, &type))
        return NOVAC_INVALID_NODE;
    if (parser->current.kind != NC_TOK_IDENTIFIER) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                    parser->current.column, "expected typedef name after block-scope type");
        return NOVAC_INVALID_NODE;
    }
    name = parser->current;
    nc_parser_advance(parser);
    if (parser->current.kind != NC_TOK_SEMICOLON) {
        parser->status = nc_diag(
            parser->diagnostic, NOVAC_ERR_UNSUPPORTED, name.line, name.column,
            "block-scope typedef aliases currently name supported object/pointer types only");
        return NOVAC_INVALID_NODE;
    }
    nc_parser_advance(parser);
    if (!nc_add_block_typedef(parser, &name, type))
        return NOVAC_INVALID_NODE;
    parser->declaration_epoch++;
    node_index = nc_new_node(parser, NC_NODE_TYPEDEF_DECL, token.line, token.column);
    if (node_index != NOVAC_INVALID_NODE) {
        nc_node_set_name(&parser->nodes[node_index], &name);
        parser->nodes[node_index].type = type;
    }
    return node_index;
}
static uint32_t nc_parse_static_local_statement(NcParser *parser, NcToken token) {
    NcTypeSpec type;
    NcToken name;
    uint32_t initializer = NOVAC_INVALID_NODE;
    uint32_t node_index;
    uint32_t static_index;
    if (parser->current_function_index == NOVAC_INVALID_NODE || parser->block_depth == 0u) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, token.line, token.column,
                                 "local static declaration requires function block scope");
        return NOVAC_INVALID_NODE;
    }
    if (!nc_parse_decl_type(parser, &type))
        return NOVAC_INVALID_NODE;
    if (parser->current.kind != NC_TOK_IDENTIFIER) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                 parser->current.column, "expected local static object name");
        return NOVAC_INVALID_NODE;
    }
    name = parser->current;
    nc_parser_advance(parser);
    if (nc_match(parser, NC_TOK_LBRACKET)) {
        NcToken length_token = parser->current;
        uint32_t length;
        if (parser->current.kind != NC_TOK_INTEGER) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                     parser->current.column,
                                     "local static array length must be an integer constant");
            return NOVAC_INVALID_NODE;
        }
        length = length_token.value;
        nc_parser_advance(parser);
        if (length == 0u || length > NOVA_COMPILER_MAX_ARRAY_LENGTH) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_ARRAY_BOUNDS, length_token.line,
                                     length_token.column,
                                     "local static array length is outside the fixed-size limit");
            return NOVAC_INVALID_NODE;
        }
        if (!nc_expect(parser, NC_TOK_RBRACKET, "expected ']' after local static array length"))
            return NOVAC_INVALID_NODE;
        type = nc_type_array(type, length);
    }
    if (nc_match(parser, NC_TOK_ASSIGN)) {
        if (nc_type_is_aggregate(type)) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED, name.line, name.column,
                        "local static aggregate initializers are deferred; aggregate static locals "
                        "are zero-initialized");
            return NOVAC_INVALID_NODE;
        }
        initializer = nc_parse_expression_no_comma(parser);
        if (initializer == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
    }
    if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after local static declaration"))
        return NOVAC_INVALID_NODE;
    if (parser->static_local_count >= NOVAC_MAX_STATIC_LOCALS) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_GLOBAL_CAPACITY, name.line,
                                 name.column, "local static object capacity exceeded");
        return NOVAC_INVALID_NODE;
    }
    static_index = parser->static_local_count++;
    {
        NcStaticLocal *local = &parser->static_locals[static_index];
        nc_copy_text(local->name, NOVAC_NAME_CAPACITY, name.start, name.length);
        local->type = type;
        local->address = 0u;
        local->initial_value = 0u;
        local->has_initializer = initializer != NOVAC_INVALID_NODE ? 1u : 0u;
        local->initializer_node = initializer;
        local->function_index = parser->current_function_index;
        local->file_id = parser->current_file_id;
        local->line = name.line;
        local->column = name.column;
        local->scope_depth = parser->block_depth;
    }
    parser->declaration_epoch++;
    node_index = nc_new_node(parser, NC_NODE_VAR_DECL, token.line, token.column);
    if (node_index != NOVAC_INVALID_NODE) {
        NcNode *node = &parser->nodes[node_index];
        nc_node_set_name(node, &name);
        node->type = type;
        node->a = initializer;
        node->op = 1u; /* local static storage */
        node->value = static_index;
    }
    return node_index;
}
static uint32_t nc_parse_statement(NcParser *parser) {
    NcToken token = parser->current;
    if (nc_match(parser, NC_TOK_LBRACE))
        return nc_parse_block(parser);
    if (nc_match(parser, NC_TOK_KW_TYPEDEF))
        return nc_parse_block_typedef_statement(parser, token);
    if (nc_match(parser, NC_TOK_KW_STATIC))
        return nc_parse_static_local_statement(parser, token);
    if (parser->current.kind == NC_TOK_IDENTIFIER &&
        nc_parser_peek_token(parser).kind == NC_TOK_COLON) {
        NcToken name = parser->current;
        uint32_t statement;
        uint32_t node_index;
        uint32_t label_scope = parser->block_depth;
        uint32_t label_epoch = parser->declaration_epoch;
        nc_parser_advance(parser);
        if (!nc_expect(parser, NC_TOK_COLON, "expected ':' after label"))
            return NOVAC_INVALID_NODE;
        statement = nc_parse_statement(parser);
        if (statement == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
        node_index = nc_new_node(parser, NC_NODE_LABEL, name.line, name.column);
        if (node_index != NOVAC_INVALID_NODE) {
            nc_node_set_name(&parser->nodes[node_index], &name);
            parser->nodes[node_index].a = statement;
            parser->nodes[node_index].scope_depth = label_scope;
            parser->nodes[node_index].epoch = label_epoch;
        }
        return node_index;
    }
    if (nc_match(parser, NC_TOK_KW_GOTO)) {
        NcToken name;
        uint32_t node_index;
        if (parser->current.kind != NC_TOK_IDENTIFIER) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                     parser->current.column, "expected label name after goto");
            return NOVAC_INVALID_NODE;
        }
        name = parser->current;
        nc_parser_advance(parser);
        if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after goto"))
            return NOVAC_INVALID_NODE;
        node_index = nc_new_node(parser, NC_NODE_GOTO, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE)
            nc_node_set_name(&parser->nodes[node_index], &name);
        return node_index;
    }
    if (parser->current.kind == NC_TOK_KW_INT || parser->current.kind == NC_TOK_KW_CHAR ||
        parser->current.kind == NC_TOK_KW_STRUCT || parser->current.kind == NC_TOK_KW_UNION ||
        parser->current.kind == NC_TOK_KW_CONST || parser->current.kind == NC_TOK_KW_VOLATILE ||
        parser->current.kind == NC_TOK_KW_RESTRICT ||
        (parser->current.kind == NC_TOK_IDENTIFIER &&
         nc_token_visible_typedef(parser, &parser->current, parser->current_file_id, NULL)))
        return nc_parse_declaration(parser);
    if (nc_match(parser, NC_TOK_KW_RETURN)) {
        uint32_t expr = nc_parse_expression(parser);
        uint32_t node_index;
        if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after return"))
            return NOVAC_INVALID_NODE;
        node_index = nc_new_node(parser, NC_NODE_RETURN, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE)
            parser->nodes[node_index].a = expr;
        return node_index;
    }
    if (nc_match(parser, NC_TOK_KW_IF)) {
        uint32_t condition;
        uint32_t then_branch;
        uint32_t else_branch = NOVAC_INVALID_NODE;
        uint32_t node_index;
        if (!nc_expect(parser, NC_TOK_LPAREN, "expected '(' after if"))
            return NOVAC_INVALID_NODE;
        condition = nc_parse_expression(parser);
        if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after if condition"))
            return NOVAC_INVALID_NODE;
        then_branch = nc_parse_statement(parser);
        if (nc_match(parser, NC_TOK_KW_ELSE))
            else_branch = nc_parse_statement(parser);
        node_index = nc_new_node(parser, NC_NODE_IF, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].a = condition;
            parser->nodes[node_index].b = then_branch;
            parser->nodes[node_index].c = else_branch;
        }
        return node_index;
    }
    if (nc_match(parser, NC_TOK_KW_SWITCH))
        return nc_parse_switch_statement(parser, token);
    if (nc_match(parser, NC_TOK_KW_DO)) {
        uint32_t body;
        uint32_t condition;
        uint32_t node_index;
        parser->loop_depth++;
        body = nc_parse_statement(parser);
        if (parser->loop_depth > 0u)
            parser->loop_depth--;
        if (body == NOVAC_INVALID_NODE)
            return NOVAC_INVALID_NODE;
        if (!nc_expect(parser, NC_TOK_KW_WHILE, "expected while after do body"))
            return NOVAC_INVALID_NODE;
        if (!nc_expect(parser, NC_TOK_LPAREN, "expected '(' after do-while while"))
            return NOVAC_INVALID_NODE;
        condition = nc_parse_expression(parser);
        if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after do-while condition"))
            return NOVAC_INVALID_NODE;
        if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after do-while"))
            return NOVAC_INVALID_NODE;
        node_index = nc_new_node(parser, NC_NODE_DO_WHILE, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].a = body;
            parser->nodes[node_index].b = condition;
        }
        return node_index;
    }
    if (nc_match(parser, NC_TOK_KW_WHILE)) {
        uint32_t condition;
        uint32_t body;
        uint32_t node_index;
        if (!nc_expect(parser, NC_TOK_LPAREN, "expected '(' after while"))
            return NOVAC_INVALID_NODE;
        condition = nc_parse_expression(parser);
        if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after while condition"))
            return NOVAC_INVALID_NODE;
        parser->loop_depth++;
        body = nc_parse_statement(parser);
        if (parser->loop_depth > 0u)
            parser->loop_depth--;
        node_index = nc_new_node(parser, NC_NODE_WHILE, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].a = condition;
            parser->nodes[node_index].b = body;
        }
        return node_index;
    }
    if (nc_match(parser, NC_TOK_KW_FOR)) {
        uint32_t init = NOVAC_INVALID_NODE;
        uint32_t condition = NOVAC_INVALID_NODE;
        uint32_t post = NOVAC_INVALID_NODE;
        uint32_t body;
        uint32_t node_index;
        if (!nc_expect(parser, NC_TOK_LPAREN, "expected '(' after for"))
            return NOVAC_INVALID_NODE;

        if (parser->current.kind == NC_TOK_SEMICOLON)
            nc_parser_advance(parser);
        else if (parser->current.kind == NC_TOK_KW_INT || parser->current.kind == NC_TOK_KW_CHAR ||
                 parser->current.kind == NC_TOK_KW_STRUCT ||
                 parser->current.kind == NC_TOK_KW_UNION ||
                 parser->current.kind == NC_TOK_KW_CONST ||
                 (parser->current.kind == NC_TOK_IDENTIFIER &&
                  nc_token_visible_typedef(parser, &parser->current, parser->current_file_id,
                                           NULL)))
            init = nc_parse_declaration(parser);
        else
            init = nc_parse_assignment_or_expression_clause(parser, 1);
        if (parser->status != NOVAC_OK)
            return NOVAC_INVALID_NODE;
        if (parser->current.kind != NC_TOK_SEMICOLON)
            condition = nc_parse_expression(parser);
        if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after for condition"))
            return NOVAC_INVALID_NODE;
        if (parser->current.kind != NC_TOK_RPAREN)
            post = nc_parse_assignment_or_expression_clause(parser, 0);
        if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after for clauses"))
            return NOVAC_INVALID_NODE;
        parser->loop_depth++;
        body = nc_parse_statement(parser);
        if (parser->loop_depth > 0u)
            parser->loop_depth--;
        if (body != NOVAC_INVALID_NODE && parser->nodes[body].kind == NC_NODE_VAR_DECL) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->nodes[body].line,
                        parser->nodes[body].column, "for-loop declaration body requires braces");
            return NOVAC_INVALID_NODE;
        }
        node_index = nc_new_node(parser, NC_NODE_FOR, token.line, token.column);
        if (node_index != NOVAC_INVALID_NODE) {
            parser->nodes[node_index].a = init;
            parser->nodes[node_index].b = condition;
            parser->nodes[node_index].c = body;
            parser->nodes[node_index].value = post;
        }
        return node_index;
    }
    if (nc_match(parser, NC_TOK_KW_BREAK) || nc_match(parser, NC_TOK_KW_CONTINUE)) {
        NcTokenKind kind = token.kind;
        uint32_t node_index;
        if ((kind == NC_TOK_KW_BREAK && parser->loop_depth == 0u && parser->switch_depth == 0u) ||
            (kind == NC_TOK_KW_CONTINUE && parser->loop_depth == 0u)) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, token.line, token.column,
                        kind == NC_TOK_KW_BREAK ? "break is only valid inside a loop or switch"
                                                : "continue is only valid inside a loop");
            return NOVAC_INVALID_NODE;
        }
        if (!nc_expect(parser, NC_TOK_SEMICOLON,
                       kind == NC_TOK_KW_BREAK ? "expected ';' after break"
                                               : "expected ';' after continue"))
            return NOVAC_INVALID_NODE;
        node_index = nc_new_node(parser, kind == NC_TOK_KW_BREAK ? NC_NODE_BREAK : NC_NODE_CONTINUE,
                                 token.line, token.column);
        return node_index;
    }
    if (parser->current.kind == NC_TOK_KW_CASE || parser->current.kind == NC_TOK_KW_DEFAULT) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                 parser->current.column,
                                 "case/default label is only valid directly inside a switch body");
        return NOVAC_INVALID_NODE;
    }
    return nc_parse_assignment_or_expression_statement(parser);
}
static int nc_parser_function_index_external(const NcParser *parser, const char *name) {
    uint32_t i;
    for (i = 0u; i < parser->function_count; i++) {
        if (parser->functions[i].linkage == NC_LINKAGE_EXTERNAL &&
            nc_streq(parser->functions[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_parser_function_index_internal(const NcParser *parser, const char *name,
                                             uint32_t file_id) {
    uint32_t i;
    for (i = 0u; i < parser->function_count; i++) {
        if (parser->functions[i].linkage == NC_LINKAGE_INTERNAL &&
            parser->functions[i].file_id == file_id && nc_streq(parser->functions[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_parser_function_index_visible(const NcParser *parser, const char *name,
                                            uint32_t use_file_id) {
    uint32_t i;
    uint32_t bit = nc_file_bit(use_file_id);
    int internal = nc_parser_function_index_internal(parser, name, use_file_id);
    if (internal >= 0)
        return internal;
    for (i = 0u; i < parser->function_count; i++) {
        if (parser->functions[i].linkage == NC_LINKAGE_EXTERNAL &&
            nc_streq(parser->functions[i].name, name) &&
            (parser->functions[i].visibility_mask & bit) != 0u)
            return (int)i;
    }
    return -1;
}
static int nc_parser_global_index_external(const NcParser *parser, const char *name) {
    uint32_t i;
    for (i = 0u; i < parser->global_count; i++) {
        if (parser->globals[i].linkage == NC_LINKAGE_EXTERNAL &&
            nc_streq(parser->globals[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_parser_global_index_internal(const NcParser *parser, const char *name,
                                           uint32_t file_id) {
    uint32_t i;
    for (i = 0u; i < parser->global_count; i++) {
        if (parser->globals[i].linkage == NC_LINKAGE_INTERNAL &&
            parser->globals[i].file_id == file_id && nc_streq(parser->globals[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_parser_global_index_visible(const NcParser *parser, const char *name,
                                          uint32_t use_file_id) {
    uint32_t i;
    uint32_t bit = nc_file_bit(use_file_id);
    int internal = nc_parser_global_index_internal(parser, name, use_file_id);
    if (internal >= 0)
        return internal;
    for (i = 0u; i < parser->global_count; i++) {
        if (parser->globals[i].linkage == NC_LINKAGE_EXTERNAL &&
            nc_streq(parser->globals[i].name, name) &&
            (parser->globals[i].visibility_mask & bit) != 0u)
            return (int)i;
    }
    return -1;
}
static int nc_parser_constant_index(const NcParser *parser, const char *name) {
    uint32_t i;
    for (i = 0u; i < parser->constant_count; i++) {
        if (nc_streq(parser->constants[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_type_equal(NcTypeSpec a, NcTypeSpec b) {
    return a.kind == b.kind && a.element_kind == b.element_kind &&
           a.struct_index == b.struct_index && a.array_length == b.array_length &&
           a.qualifiers == b.qualifiers && a.element_qualifiers == b.element_qualifiers;
}
static int nc_add_typedef(NcParser *parser, const NcToken *name, NcTypeSpec type) {
    char temp[NOVAC_NAME_CAPACITY];
    uint32_t visibility;
    uint32_t i;
    if (parser == NULL || name == NULL)
        return 0;
    nc_copy_text(temp, NOVAC_NAME_CAPACITY, name->start, name->length);
    visibility = nc_declaration_visibility(parser, parser->current_file_id);
    if (nc_parser_constant_index(parser, temp) >= 0 ||
        nc_parser_function_index_visible(parser, temp, parser->current_file_id) >= 0 ||
        nc_parser_global_index_visible(parser, temp, parser->current_file_id) >= 0) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                    "typedef name conflicts with a visible ordinary identifier");
        return 0;
    }
    for (i = 0u; i < parser->typedef_count; i++) {
        NcTypedef *alias = &parser->typedefs[i];
        if (!nc_streq(alias->name, temp) || (alias->visibility_mask & visibility) == 0u)
            continue;
        if (!nc_type_equal(alias->type, type)) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION,
                                     name->line, name->column,
                                     "typedef declaration conflicts with an earlier visible alias");
            return 0;
        }
        alias->visibility_mask |= visibility;
        return 1;
    }
    if (parser->typedef_count >= NOVAC_MAX_TYPEDEFS) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_SYMBOL_CAPACITY, name->line,
                                 name->column, "typedef alias capacity exceeded");
        return 0;
    }
    {
        NcTypedef *alias = &parser->typedefs[parser->typedef_count++];
        nc_copy_text(alias->name, NOVAC_NAME_CAPACITY, name->start, name->length);
        alias->type = type;
        alias->declaration_file_id = parser->current_file_id;
        alias->line = name->line;
        alias->column = name->column;
        alias->visibility_mask = visibility;
        alias->function_index = NOVAC_INVALID_NODE;
        alias->scope_id = 0u;
        alias->scope_depth = 0u;
        alias->is_block_scope = 0u;
    }
    return 1;
}
static int nc_add_block_typedef(NcParser *parser, const NcToken *name, NcTypeSpec type) {
    char temp[NOVAC_NAME_CAPACITY];
    uint32_t i;
    uint32_t scope_id;
    if (parser == NULL || name == NULL || parser->current_function_index == NOVAC_INVALID_NODE ||
        parser->block_depth == 0u)
        return 0;
    scope_id = parser->scope_stack[parser->block_depth];
    nc_copy_text(temp, NOVAC_NAME_CAPACITY, name->start, name->length);
    for (i = parser->typedef_count; i > 0u; i--) {
        NcTypedef *alias = &parser->typedefs[i - 1u];
        if (alias->is_block_scope == 0u ||
            alias->function_index != parser->current_function_index ||
            alias->scope_id != scope_id || !nc_streq(alias->name, temp))
            continue;
        if (!nc_type_equal(alias->type, type)) {
            parser->status = nc_diag(
                parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                "block-scope typedef conflicts with an earlier alias in the same lexical scope");
            return 0;
        }
        return 1;
    }
    if (parser->typedef_count >= NOVAC_MAX_TYPEDEFS) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_SYMBOL_CAPACITY, name->line,
                                 name->column, "typedef alias capacity exceeded");
        return 0;
    }
    {
        NcTypedef *alias = &parser->typedefs[parser->typedef_count++];
        nc_copy_text(alias->name, NOVAC_NAME_CAPACITY, name->start, name->length);
        alias->type = type;
        alias->declaration_file_id = parser->current_file_id;
        alias->line = name->line;
        alias->column = name->column;
        alias->visibility_mask = 0u;
        alias->function_index = parser->current_function_index;
        alias->scope_id = scope_id;
        alias->scope_depth = parser->block_depth;
        alias->is_block_scope = 1u;
    }
    return 1;
}
static int nc_function_signature_matches(const NcFunction *function, const NcParameter *parameters,
                                         uint32_t parameter_count) {
    uint32_t i;
    if (function == NULL || function->parameter_count != parameter_count)
        return 0;
    for (i = 0u; i < parameter_count; i++) {
        if (!nc_type_equal(function->parameters[i].type, parameters[i].type))
            return 0;
    }
    return 1;
}
static int nc_parse_global_after_name(NcParser *parser, const NcToken *name,
                                      NcTypeSpec declared_type, int is_extern, uint32_t linkage);
static int nc_parse_aggregate_definition(NcParser *parser, int is_extern, uint32_t is_union) {
    NcStructDef *def;
    NcToken name;
    uint32_t struct_index;
    char temp_name[NOVAC_NAME_CAPACITY];
    int existing_index;
    if (parser->current.kind != NC_TOK_IDENTIFIER) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                 parser->current.column,
                                 is_union ? "expected union name" : "expected struct name");
        return 0;
    }
    name = parser->current;
    nc_parser_advance(parser);
    nc_copy_text(temp_name, NOVAC_NAME_CAPACITY, name.start, name.length);
    existing_index = nc_parser_struct_index(parser, temp_name);
    if (existing_index >= 0) {
        def = &parser->structs[(uint32_t)existing_index];
        struct_index = (uint32_t)existing_index;
        if (def->is_union != is_union) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name.line,
                        name.column, "aggregate tag was previously declared with a different kind");
            return 0;
        }
    } else {
        if (parser->struct_count >= NOVA_COMPILER_MAX_STRUCTS) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_TYPE_CAPACITY, parser->current.line,
                        parser->current.column, "aggregate type capacity exceeded");
            return 0;
        }
        struct_index = parser->struct_count++;
        def = &parser->structs[struct_index];
        nc_copy_text(def->name, NOVAC_NAME_CAPACITY, name.start, name.length);
        def->field_count = 0u;
        def->byte_size = 0u;
        def->file_id = parser->current_file_id;
        def->line = name.line;
        def->column = name.column;
        def->is_defined = 0u;
        def->is_union = is_union;
    }
    if (parser->current.kind != NC_TOK_LBRACE) {
        NcTypeSpec type = is_union ? nc_type_union(struct_index) : nc_type_struct(struct_index);
        NcToken variable_name;
        if (nc_match(parser, NC_TOK_SEMICOLON))
            return 1; /* forward tag declaration */
        if (nc_match(parser, NC_TOK_STAR))
            type = nc_type_pointer_to(type);
        if (parser->current.kind != NC_TOK_IDENTIFIER) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                        parser->current.column, "expected aggregate global name or '{'");
            return 0;
        }
        variable_name = parser->current;
        nc_parser_advance(parser);
        return nc_parse_global_after_name(parser, &variable_name, type, is_extern,
                                          NC_LINKAGE_EXTERNAL);
    }
    if (def->is_defined != 0u) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_DUPLICATE_MEMBER, name.line,
                                 name.column, "duplicate aggregate definition");
        return 0;
    }
    if (is_extern) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED, name.line, name.column,
                                 "extern aggregate definitions are not supported");
        return 0;
    }
    def->field_count = 0u;
    def->byte_size = 0u;
    def->file_id = parser->current_file_id;
    def->line = name.line;
    def->column = name.column;
    def->is_union = is_union;
    /* Keep the definition incomplete while fields are parsed so recursive
       by-value self references are rejected, while pointer self references remain valid. */
    def->is_defined = 0u;
    if (!nc_expect(parser, NC_TOK_LBRACE, "expected '{' after aggregate name"))
        return 0;
    while (parser->status == NOVAC_OK && parser->current.kind != NC_TOK_RBRACE) {
        NcStructField *field;
        NcTypeSpec field_type;
        NcToken field_name;
        char field_temp[NOVAC_NAME_CAPACITY];
        uint32_t field_size;
        if (def->field_count >= NOVA_COMPILER_MAX_STRUCT_FIELDS) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_TYPE_CAPACITY, parser->current.line,
                        parser->current.column, "aggregate field capacity exceeded");
            return 0;
        }
        if (!nc_parse_decl_type(parser, &field_type))
            return 0;
        if (parser->current.kind != NC_TOK_IDENTIFIER) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                     parser->current.column, "expected aggregate field name");
            return 0;
        }
        field_name = parser->current;
        nc_parser_advance(parser);
        if (nc_match(parser, NC_TOK_LBRACKET)) {
            NcToken length_token = parser->current;
            uint32_t length;
            if (parser->current.kind != NC_TOK_INTEGER) {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                            parser->current.column,
                            "aggregate field array length must be an integer constant");
                return 0;
            }
            length = length_token.value;
            nc_parser_advance(parser);
            if (length == 0u || length > NOVA_COMPILER_MAX_ARRAY_LENGTH) {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_ARRAY_BOUNDS, length_token.line,
                            length_token.column,
                            "aggregate field array length is outside the fixed-size limit");
                return 0;
            }
            if (!nc_expect(parser, NC_TOK_RBRACKET,
                           "expected ']' after aggregate field array length"))
                return 0;
            field_type = nc_type_array(field_type, length);
        }
        nc_copy_text(field_temp, NOVAC_NAME_CAPACITY, field_name.start, field_name.length);
        if (nc_struct_field_index(def, field_temp) >= 0) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_DUPLICATE_MEMBER, field_name.line,
                        field_name.column, "duplicate aggregate field");
            return 0;
        }
        if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after aggregate field"))
            return 0;
        field_size = nc_parser_type_size(parser, field_type);
        if (field_size == 0u) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_UNKNOWN_TYPE, field_name.line,
                        field_name.column, "incomplete or recursively-sized aggregate field type");
            return 0;
        }
        field = &def->fields[def->field_count++];
        nc_copy_text(field->name, NOVAC_NAME_CAPACITY, field_name.start, field_name.length);
        field->type = field_type;
        field->byte_offset = is_union ? 0u : def->byte_size;
        field->line = field_name.line;
        field->column = field_name.column;
        if (is_union) {
            if (field_size > def->byte_size)
                def->byte_size = field_size;
        } else {
            if (def->byte_size > 0xFFFFFFFFu - field_size) {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_TYPE_CAPACITY, field_name.line,
                            field_name.column, "aggregate layout size overflow");
                return 0;
            }
            def->byte_size += field_size;
        }
    }
    if (!nc_expect(parser, NC_TOK_RBRACE, "expected '}' after aggregate fields"))
        return 0;
    if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after aggregate declaration"))
        return 0;
    if (def->field_count == 0u) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED, name.line, name.column,
                                 "empty aggregates are not supported");
        return 0;
    }
    def->is_defined = 1u;
    return 1;
}
static int nc_parse_struct_definition(NcParser *parser, int is_extern) {
    return nc_parse_aggregate_definition(parser, is_extern, 0u);
}
static int nc_parse_union_definition(NcParser *parser, int is_extern) {
    return nc_parse_aggregate_definition(parser, is_extern, 1u);
}
static int nc_parse_function_after_name(NcParser *parser, const NcToken *name,
                                        NcTypeSpec return_type, uint32_t linkage) {
    NcParameter parsed_parameters[NOVA_COMPILER_MAX_PARAMETERS];
    uint32_t parameter_count = 0u;
    char temp_name[NOVAC_NAME_CAPACITY];
    int existing_index;
    int is_definition;
    uint32_t i;
    uint32_t visibility;
    if (return_type.kind != NC_TYPE_INT || return_type.qualifiers != 0u) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED, name->line,
                                 name->column, "user functions currently return unqualified int");
        return 0;
    }
    if (!nc_expect(parser, NC_TOK_LPAREN, "expected '(' after function name"))
        return 0;
    if (parser->current.kind == NC_TOK_KW_VOID) {
        nc_parser_advance(parser);
    } else if (parser->current.kind != NC_TOK_RPAREN) {
        for (;;) {
            NcTypeSpec type;
            NcParameter *parameter;
            if (parameter_count >= NOVA_COMPILER_MAX_PARAMETERS) {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_ARGUMENT_COUNT, parser->current.line,
                            parser->current.column, "NovaC supports at most four parameters");
                return 0;
            }
            if (!nc_parse_decl_type(parser, &type))
                return 0;
            if (type.kind == NC_TYPE_STRUCT || type.kind == NC_TYPE_UNION ||
                type.kind == NC_TYPE_ARRAY) {
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED,
                                         parser->current.line, parser->current.column,
                                         "NovaC function parameters support scalar types and "
                                         "aggregate pointers, not aggregates by value");
                return 0;
            }
            parameter = &parsed_parameters[parameter_count++];
            parameter->name[0] = '\0';
            parameter->type = type;
            parameter->line = parser->current.line;
            parameter->column = parser->current.column;
            if (parser->current.kind == NC_TOK_IDENTIFIER) {
                NcToken parameter_name = parser->current;
                nc_parser_advance(parser);
                nc_copy_text(parameter->name, NOVAC_NAME_CAPACITY, parameter_name.start,
                             parameter_name.length);
                parameter->line = parameter_name.line;
                parameter->column = parameter_name.column;
            }
            if (!nc_match(parser, NC_TOK_COMMA))
                break;
        }
    }
    if (!nc_expect(parser, NC_TOK_RPAREN, "expected ')' after function parameters"))
        return 0;
    is_definition = parser->current.kind == NC_TOK_LBRACE;
    if (is_definition && parser->current_is_header != 0u) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_HEADER_DEFINITION, name->line, name->column,
                    "header files may declare function prototypes but may not define functions");
        return 0;
    }
    if (!is_definition && parser->current.kind != NC_TOK_SEMICOLON) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                    parser->current.column, "expected ';' prototype or '{' function definition");
        return 0;
    }
    if (is_definition) {
        for (i = 0u; i < parameter_count; i++) {
            if (parsed_parameters[i].name[0] == '\0') {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, name->line, name->column,
                            "function definition parameters require names");
                return 0;
            }
        }
    }
    nc_copy_text(temp_name, NOVAC_NAME_CAPACITY, name->start, name->length);
    if (nc_parser_constant_index(parser, temp_name) >= 0 ||
        nc_parser_typedef_index_visible(parser, temp_name, parser->current_file_id) >= 0) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                    "function name conflicts with a visible ordinary identifier");
        return 0;
    }
    if (nc_parser_global_index_visible(parser, temp_name, parser->current_file_id) >= 0) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                    "function name conflicts with a visible project-scope global");
        return 0;
    }
    if (linkage == NC_LINKAGE_INTERNAL) {
        if (nc_parser_function_index_external(parser, temp_name) >= 0 &&
            nc_parser_function_index_visible(parser, temp_name, parser->current_file_id) >= 0) {
            parser->status = nc_diag(
                parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                "static function conflicts with a visible external declaration");
            return 0;
        }
        existing_index =
            nc_parser_function_index_internal(parser, temp_name, parser->current_file_id);
        visibility = nc_file_bit(parser->current_file_id);
    } else {
        if (nc_parser_function_index_internal(parser, temp_name, parser->current_file_id) >= 0) {
            parser->status = nc_diag(
                parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                "external function conflicts with a static declaration in this translation unit");
            return 0;
        }
        existing_index = nc_parser_function_index_external(parser, temp_name);
        visibility = nc_declaration_visibility(parser, parser->current_file_id);
    }
    if (existing_index >= 0) {
        NcFunction *existing = &parser->functions[(uint32_t)existing_index];
        if (!nc_type_equal(existing->return_type, return_type) ||
            !nc_function_signature_matches(existing, parsed_parameters, parameter_count)) {
            parser->status = nc_diag(
                parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                "function declaration conflicts with an earlier prototype/definition");
            return 0;
        }
        existing->visibility_mask |= visibility;
        if (!is_definition) {
            nc_parser_advance(parser); /* ';' */
            return 1;
        }
        if (existing->is_defined != 0u) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_DUPLICATE_FUNCTION, name->line,
                                     name->column, "duplicate function definition");
            return 0;
        }
        existing->is_defined = 1u;
        existing->file_id = parser->current_file_id;
        existing->line = name->line;
        existing->column = name->column;
        existing->return_type = return_type;
        for (i = 0u; i < parameter_count; i++)
            existing->parameters[i] = parsed_parameters[i];
        nc_parser_advance(parser); /* '{' */
        parser->declaration_epoch = 0u;
        parser->current_function_index = (uint32_t)existing_index;
        existing->body = nc_parse_block(parser);
        parser->current_function_index = NOVAC_INVALID_NODE;
        return parser->status == NOVAC_OK;
    }
    if (parser->function_count >= NOVA_COMPILER_MAX_DEBUG_FUNCTIONS) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_FUNCTION_CAPACITY, name->line,
                                 name->column, "function capacity exceeded");
        return 0;
    }
    {
        NcFunction *function = &parser->functions[parser->function_count++];
        nc_copy_text(function->name, NOVAC_NAME_CAPACITY, name->start, name->length);
        function->return_type = return_type;
        function->parameter_count = parameter_count;
        function->body = NOVAC_INVALID_NODE;
        function->file_id = parser->current_file_id;
        function->line = name->line;
        function->column = name->column;
        function->is_defined = is_definition ? 1u : 0u;
        function->linkage = linkage;
        function->visibility_mask = visibility;
        function->declaration_file_id = parser->current_file_id;
        function->declaration_line = name->line;
        function->declaration_column = name->column;
        for (i = 0u; i < parameter_count; i++)
            function->parameters[i] = parsed_parameters[i];
        if (!is_definition) {
            nc_parser_advance(parser); /* ';' */
            return 1;
        }
        nc_parser_advance(parser); /* '{' */
        parser->declaration_epoch = 0u;
        parser->current_function_index = parser->function_count - 1u;
        function->body = nc_parse_block(parser);
        parser->current_function_index = NOVAC_INVALID_NODE;
    }
    return parser->status == NOVAC_OK;
}
static int nc_parse_global_after_name(NcParser *parser, const NcToken *name,
                                      NcTypeSpec declared_type, int is_extern, uint32_t linkage) {
    NcGlobal *global;
    char temp_name[NOVAC_NAME_CAPACITY];
    uint32_t initial_value = 0u;
    uint32_t has_initializer = 0u;
    uint32_t initializer_node = NOVAC_INVALID_NODE;
    uint32_t visibility;
    int existing_index;
    if (nc_match(parser, NC_TOK_LBRACKET)) {
        NcToken length_token = parser->current;
        uint32_t length;
        if (parser->current.kind != NC_TOK_INTEGER) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                        parser->current.column, "global array length must be an integer constant");
            return 0;
        }
        length = length_token.value;
        nc_parser_advance(parser);
        if (length == 0u || length > NOVA_COMPILER_MAX_ARRAY_LENGTH) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_ARRAY_BOUNDS, length_token.line,
                        length_token.column, "global array length is outside the fixed-size limit");
            return 0;
        }
        if (!nc_expect(parser, NC_TOK_RBRACKET, "expected ']' after global array length"))
            return 0;
        declared_type = nc_type_array(declared_type, length);
    }
    nc_copy_text(temp_name, NOVAC_NAME_CAPACITY, name->start, name->length);
    if (nc_parser_constant_index(parser, temp_name) >= 0 ||
        nc_parser_typedef_index_visible(parser, temp_name, parser->current_file_id) >= 0) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                    "global name conflicts with a visible ordinary identifier");
        return 0;
    }
    if (nc_parser_function_index_visible(parser, temp_name, parser->current_file_id) >= 0) {
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line,
                                 name->column, "global name conflicts with a visible function");
        return 0;
    }
    if (parser->current_is_header != 0u && !is_extern) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_HEADER_DEFINITION, name->line, name->column,
                    "header globals must use extern and may not allocate storage");
        return 0;
    }
    if (is_extern && parser->current.kind == NC_TOK_ASSIGN) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_HEADER_DEFINITION, name->line, name->column,
                    "extern global declarations may not have initializers");
        return 0;
    }
    if (!is_extern && nc_match(parser, NC_TOK_ASSIGN)) {
        if (nc_type_is_aggregate(declared_type)) {
            if (declared_type.kind == NC_TYPE_ARRAY && declared_type.element_kind == NC_TYPE_CHAR &&
                parser->current.kind == NC_TOK_STRING) {
                initializer_node = nc_parse_expression_no_comma(parser);
            } else {
                if (parser->current.kind != NC_TOK_LBRACE) {
                    parser->status =
                        nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED, name->line, name->column,
                                "aggregate global initializer requires a brace list");
                    return 0;
                }
                initializer_node = nc_parse_initializer_list(parser);
            }
        } else {
            initializer_node = nc_parse_expression_no_comma(parser);
        }
        if (initializer_node == NOVAC_INVALID_NODE)
            return 0;
        has_initializer = 1u;
    }
    if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after global declaration"))
        return 0;
    {
        int is_tentative = linkage == NC_LINKAGE_EXTERNAL && !is_extern && has_initializer == 0u;
        int is_strong = !is_extern && !is_tentative;
        if (linkage == NC_LINKAGE_INTERNAL && !is_extern) {
            is_strong = 1;
            is_tentative = 0;
        }
        if (linkage == NC_LINKAGE_INTERNAL) {
            if (nc_parser_global_index_external(parser, temp_name) >= 0 &&
                nc_parser_global_index_visible(parser, temp_name, parser->current_file_id) >= 0) {
                parser->status = nc_diag(
                    parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                    "static global conflicts with a visible external declaration");
                return 0;
            }
            existing_index =
                nc_parser_global_index_internal(parser, temp_name, parser->current_file_id);
            visibility = nc_file_bit(parser->current_file_id);
        } else {
            if (nc_parser_global_index_internal(parser, temp_name, parser->current_file_id) >= 0) {
                parser->status = nc_diag(
                    parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                    "external global conflicts with a static declaration in this translation unit");
                return 0;
            }
            existing_index = nc_parser_global_index_external(parser, temp_name);
            visibility = nc_declaration_visibility(parser, parser->current_file_id);
        }
        if (existing_index >= 0) {
            global = &parser->globals[(uint32_t)existing_index];
            if (!nc_type_equal(global->type, declared_type)) {
                parser->status = nc_diag(
                    parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name->line, name->column,
                    "global declaration conflicts with an earlier declaration/definition");
                return 0;
            }
            global->visibility_mask |= visibility;
            if (is_extern)
                return 1;
            if (is_tentative) {
                global->has_tentative_definition = 1u;
                global->is_defined = 1u;
                if (global->has_strong_definition == 0u) {
                    global->file_id = parser->current_file_id;
                    global->line = name->line;
                    global->column = name->column;
                }
                return 1;
            }
            if (is_strong) {
                if (global->has_strong_definition != 0u) {
                    parser->status =
                        nc_diag(parser->diagnostic, NOVAC_ERR_DUPLICATE_GLOBAL, name->line,
                                name->column, "duplicate strong project-scope global definition");
                    return 0;
                }
                global->initial_value = initial_value;
                global->has_initializer = has_initializer;
                global->initializer_node = initializer_node;
                global->file_id = parser->current_file_id;
                global->line = name->line;
                global->column = name->column;
                global->is_defined = 1u;
                global->has_strong_definition = 1u;
                return 1;
            }
        }
        if (parser->global_count >= NOVA_COMPILER_MAX_GLOBALS) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_GLOBAL_CAPACITY, name->line,
                                     name->column, "global variable capacity exceeded");
            return 0;
        }
        global = &parser->globals[parser->global_count];
        nc_copy_text(global->name, NOVAC_NAME_CAPACITY, name->start, name->length);
        global->type = declared_type;
        global->address = 0u;
        global->initial_value = initial_value;
        global->has_initializer = has_initializer;
        global->initializer_node = initializer_node;
        global->file_id = parser->current_file_id;
        global->line = name->line;
        global->column = name->column;
        global->is_defined = is_extern ? 0u : 1u;
        global->has_strong_definition = is_strong ? 1u : 0u;
        global->has_tentative_definition = is_tentative ? 1u : 0u;
        global->linkage = linkage;
        global->visibility_mask = visibility;
        global->declaration_file_id = parser->current_file_id;
        global->declaration_line = name->line;
        global->declaration_column = name->column;
        parser->global_count++;
        return 1;
    }
}
static int nc_parse_enum_definition(NcParser *parser) {

    uint32_t previous_index = NOVAC_INVALID_NODE;
    if (parser->current.kind == NC_TOK_IDENTIFIER) {
        /* Named enum tags are accepted for source readability. Vol.16 exposes
        enumerators as project-wide integer constants but does not yet allow
        variables declared with an enum type. */
        nc_parser_advance(parser);
    }
    if (!nc_expect(parser, NC_TOK_LBRACE, "expected '{' after enum name"))
        return 0;
    if (parser->current.kind == NC_TOK_RBRACE) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED, parser->current.line,
                    parser->current.column, "empty enum definitions are not supported");
        return 0;
    }
    while (parser->status == NOVAC_OK && parser->current.kind != NC_TOK_RBRACE) {
        NcToken name;
        NcEnumConstant *constant;
        char temp[NOVAC_NAME_CAPACITY];
        uint32_t expression_node = NOVAC_INVALID_NODE;
        if (parser->current.kind != NC_TOK_IDENTIFIER) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                                     parser->current.column, "expected enum constant name");
            return 0;
        }
        name = parser->current;
        nc_parser_advance(parser);
        nc_copy_text(temp, NOVAC_NAME_CAPACITY, name.start, name.length);
        if (nc_parser_constant_index(parser, temp) >= 0 ||
            nc_parser_global_index_visible(parser, temp, parser->current_file_id) >= 0 ||
            nc_parser_function_index_visible(parser, temp, parser->current_file_id) >= 0 ||
            nc_parser_typedef_index_visible(parser, temp, parser->current_file_id) >= 0) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, name.line,
                        name.column, "enum constant conflicts with an existing project identifier");
            return 0;
        }
        if (parser->constant_count >= NOVA_COMPILER_MAX_ENUM_CONSTANTS) {
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_SYMBOL_CAPACITY, name.line,
                                     name.column, "enum constant capacity exceeded");
            return 0;
        }
        if (nc_match(parser, NC_TOK_ASSIGN)) {
            expression_node = nc_parse_expression_no_comma(parser);
            if (expression_node == NOVAC_INVALID_NODE)
                return 0;
        }
        constant = &parser->constants[parser->constant_count];
        nc_copy_text(constant->name, NOVAC_NAME_CAPACITY, name.start, name.length);
        constant->expression_node = expression_node;
        constant->previous_index = previous_index;
        constant->value = 0u;
        constant->has_value = 0u;
        constant->resolving = 0u;
        constant->auto_increment = expression_node == NOVAC_INVALID_NODE ? 1u : 0u;
        constant->file_id = parser->current_file_id;
        constant->line = name.line;
        constant->column = name.column;
        previous_index = parser->constant_count;
        parser->constant_count++;
        if (!nc_match(parser, NC_TOK_COMMA))
            break;
        if (parser->current.kind == NC_TOK_RBRACE)
            break;
    }
    if (!nc_expect(parser, NC_TOK_RBRACE, "expected '}' after enum constants"))
        return 0;
    if (!nc_expect(parser, NC_TOK_SEMICOLON, "expected ';' after enum definition"))
        return 0;

    return 1;
}
static int nc_parse_translation_unit(NcParser *parser, const char *source, uint32_t file_id,
                                     uint32_t is_header) {
    if (parser == NULL || source == NULL)
        return 0;
    parser->current_file_id = file_id;
    parser->current_is_header = is_header;
    if (parser->diagnostic != NULL)
        parser->diagnostic->file_id = file_id;
    parser->lexer.source = source;
    parser->lexer.offset = 0u;
    parser->lexer.line = 1u;
    parser->lexer.column = 1u;
    parser->lexer.diagnostic = parser->diagnostic;
    parser->lexer.status = NOVAC_OK;
    parser->block_depth = 0u;
    parser->declaration_epoch = 0u;
    parser->current = nc_make_token(NC_TOK_EOF, source, 0u, 1u, 1u, 0u);
    parser->previous = parser->current;
    nc_parser_advance(parser);
    while (parser->status == NOVAC_OK && parser->current.kind != NC_TOK_EOF) {
        uint32_t storage = 0u; /* 0 none, 1 typedef, 2 extern, 3 static */
        NcTypeSpec type;
        NcToken name;
        uint32_t linkage;
        int is_extern;
        if (nc_match(parser, NC_TOK_KW_ENUM)) {
            if (!nc_parse_enum_definition(parser))
                return 0;
            continue;
        }
        /* Preserve the Vol.13 struct-definition/forward-declaration path. Qualified,
           storage-class and typedef-based struct object declarations use the generic
           declaration path below. */
        if (parser->current.kind == NC_TOK_KW_STRUCT || parser->current.kind == NC_TOK_KW_UNION) {
            uint32_t is_union = parser->current.kind == NC_TOK_KW_UNION ? 1u : 0u;
            /* A naked aggregate declaration is delegated to the tag parser. It handles
               definitions, forward declarations and ordinary external-linkage globals. */
            nc_parser_advance(parser);
            if (is_union) {
                if (!nc_parse_union_definition(parser, 0))
                    return 0;
            } else if (!nc_parse_struct_definition(parser, 0))
                return 0;
            continue;
        }
        if (nc_match(parser, NC_TOK_KW_TYPEDEF))
            storage = 1u;
        else if (nc_match(parser, NC_TOK_KW_EXTERN))
            storage = 2u;
        else if (nc_match(parser, NC_TOK_KW_STATIC))
            storage = 3u;
        if (parser->current.kind == NC_TOK_KW_TYPEDEF || parser->current.kind == NC_TOK_KW_EXTERN ||
            parser->current.kind == NC_TOK_KW_STATIC) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, parser->current.line,
                        parser->current.column, "multiple storage classes are not supported");
            return 0;
        }
        if (storage == 3u && parser->current_is_header != 0u) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_HEADER_DEFINITION, parser->current.line,
                        parser->current.column,
                        "static file-scope declarations are restricted to .c translation units");
            return 0;
        }
        if (!nc_parse_decl_type(parser, &type))
            return 0;
        if (parser->current.kind != NC_TOK_IDENTIFIER) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, parser->current.line,
                        parser->current.column, "expected declaration name after type");
            return 0;
        }
        name = parser->current;
        nc_parser_advance(parser);
        if (storage == 1u) {
            if (parser->current.kind != NC_TOK_SEMICOLON) {
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_UNSUPPORTED, name.line, name.column,
                            "Vol.17 typedef aliases name supported object types; function/array "
                            "declarator aliases are deferred");
                return 0;
            }
            nc_parser_advance(parser);
            if (!nc_add_typedef(parser, &name, type))
                return 0;
            continue;
        }
        linkage = storage == 3u ? NC_LINKAGE_INTERNAL : NC_LINKAGE_EXTERNAL;
        is_extern = storage == 2u ? 1 : 0;
        if (parser->current.kind == NC_TOK_LPAREN) {
            if (!nc_parse_function_after_name(parser, &name, type, linkage))
                return 0;
        } else {
            if (!nc_parse_global_after_name(parser, &name, type, is_extern, linkage))
                return 0;
        }
    }
    parser->current_is_header = 0u;
    return parser->status == NOVAC_OK;
}
static uint32_t nc_parser_type_size(const NcParser *parser, NcTypeSpec type) {
    if (type.kind == NC_TYPE_CHAR)
        return 1u;
    if (type.kind == NC_TYPE_INT || type.kind == NC_TYPE_INT_PTR ||
        type.kind == NC_TYPE_STRUCT_PTR || type.kind == NC_TYPE_CHAR_PTR ||
        type.kind == NC_TYPE_UNION_PTR)
        return 4u;
    if (type.kind == NC_TYPE_STRUCT || type.kind == NC_TYPE_UNION) {
        if (type.struct_index >= parser->struct_count ||
            parser->structs[type.struct_index].is_defined == 0u)
            return 0u;
        return parser->structs[type.struct_index].byte_size;
    }
    if (type.kind == NC_TYPE_ARRAY) {
        NcTypeSpec element = nc_type_int();
        uint32_t element_size;
        element.kind = type.element_kind;
        element.struct_index = type.struct_index;
        element_size = nc_parser_type_size(parser, element);
        if (element_size == 0u || type.array_length > 0xFFFFFFFFu / element_size)
            return 0u;
        return type.array_length * element_size;
    }
    return 0u;
}
static int nc_eval_constant_node(NcParser *parser, uint32_t node_index, uint32_t *value_out);
static int nc_resolve_enum_constant(NcParser *parser, uint32_t index, uint32_t *value_out) {
    NcEnumConstant *constant;
    uint32_t value = 0u;
    if (index >= parser->constant_count)
        return 0;
    constant = &parser->constants[index];
    if (constant->has_value != 0u) {
        *value_out = constant->value;
        return 1;
    }
    if (constant->resolving != 0u) {
        if (parser->diagnostic != NULL)
            parser->diagnostic->file_id = constant->file_id;
        parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, constant->line,
                                 constant->column, "cyclic enum constant expression");
        return 0;
    }
    constant->resolving = 1u;
    if (constant->auto_increment != 0u) {
        if (constant->previous_index == NOVAC_INVALID_NODE)
            value = 0u;
        else {
            uint32_t previous;
            if (!nc_resolve_enum_constant(parser, constant->previous_index, &previous)) {
                constant->resolving = 0u;
                return 0;
            }
            value = previous + 1u;
        }
    } else if (!nc_eval_constant_node(parser, constant->expression_node, &value)) {
        constant->resolving = 0u;
        return 0;
    }
    constant->value = value;
    constant->has_value = 1u;
    constant->resolving = 0u;
    *value_out = value;
    return 1;
}

static int nc_eval_constant_node(NcParser *parser, uint32_t node_index, uint32_t *value_out) {
    const NcNode *node;
    uint32_t a, b;
    int constant_index;
    if (node_index == NOVAC_INVALID_NODE || node_index >= parser->node_count || value_out == NULL)
        return 0;
    node = &parser->nodes[node_index];
    switch (node->kind) {
    case NC_NODE_LITERAL:
        *value_out = node->value;
        return 1;
    case NC_NODE_VARIABLE:
        constant_index = nc_parser_constant_index(parser, node->name);
        if (constant_index < 0)
            break;
        return nc_resolve_enum_constant(parser, (uint32_t)constant_index, value_out);
    case NC_NODE_SIZEOF:
        if (node->value == 0u)
            break; /* expression-form sizeof is not part of Vol.16 constant evaluation */
        a = nc_parser_type_size(parser, node->type);
        if (a == 0u)
            break;
        *value_out = a;
        return 1;
    case NC_NODE_UNARY:
        if (!nc_eval_constant_node(parser, node->a, &a))
            return 0;
        if (node->op == NC_UN_NEGATE)
            *value_out = 0u - a;
        else if (node->op == NC_UN_NOT)
            *value_out = a == 0u ? 1u : 0u;
        else if (node->op == NC_UN_BIT_NOT)
            *value_out = ~a;
        else
            break;
        return 1;
    case NC_NODE_TERNARY:
        if (!nc_eval_constant_node(parser, node->a, &a))
            return 0;
        return nc_eval_constant_node(parser, a != 0u ? node->b : node->c, value_out);
    case NC_NODE_CAST:
        if (!nc_eval_constant_node(parser, node->a, &a))
            return 0;
        if (node->type.kind == NC_TYPE_CHAR)
            a &= 0xFFu;
        else if (node->type.kind != NC_TYPE_INT) {
            parser->status = nc_diag(
                parser->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, node->line, node->column,
                "pointer casts are not integer constant expressions in the current NovaC model");
            return 0;
        }
        *value_out = a;
        return 1;
    case NC_NODE_BINARY:
        if (node->op == NC_BIN_LOGICAL_AND) {
            if (!nc_eval_constant_node(parser, node->a, &a))
                return 0;
            if (a == 0u) {
                *value_out = 0u;
                return 1;
            }
            if (!nc_eval_constant_node(parser, node->b, &b))
                return 0;
            *value_out = b != 0u ? 1u : 0u;
            return 1;
        }
        if (node->op == NC_BIN_LOGICAL_OR) {
            if (!nc_eval_constant_node(parser, node->a, &a))
                return 0;
            if (a != 0u) {
                *value_out = 1u;
                return 1;
            }
            if (!nc_eval_constant_node(parser, node->b, &b))
                return 0;
            *value_out = b != 0u ? 1u : 0u;
            return 1;
        }
        if (!nc_eval_constant_node(parser, node->a, &a) ||
            !nc_eval_constant_node(parser, node->b, &b))
            return 0;
        switch ((NcBinaryOp)node->op) {
        case NC_BIN_ADD:
            *value_out = a + b;
            return 1;
        case NC_BIN_SUB:
            *value_out = a - b;
            return 1;
        case NC_BIN_MUL:
            *value_out = a * b;
            return 1;
        case NC_BIN_DIV:
            if (b == 0u)
                break;
            *value_out = a / b;
            return 1;
        case NC_BIN_SHIFT_LEFT:
            *value_out = a << (b & 31u);
            return 1;
        case NC_BIN_SHIFT_RIGHT:
            *value_out = a >> (b & 31u);
            return 1;
        case NC_BIN_BIT_AND:
            *value_out = a & b;
            return 1;
        case NC_BIN_BIT_XOR:
            *value_out = a ^ b;
            return 1;
        case NC_BIN_BIT_OR:
            *value_out = a | b;
            return 1;
        case NC_BIN_EQ:
            *value_out = a == b ? 1u : 0u;
            return 1;

        case NC_BIN_NE:
            *value_out = a != b ? 1u : 0u;
            return 1;
        case NC_BIN_LT:
            *value_out = (int32_t)a < (int32_t)b ? 1u : 0u;
            return 1;
        case NC_BIN_LE:
            *value_out = (int32_t)a <= (int32_t)b ? 1u : 0u;
            return 1;
        case NC_BIN_GT:
            *value_out = (int32_t)a > (int32_t)b ? 1u : 0u;
            return 1;
        case NC_BIN_GE:
            *value_out = (int32_t)a >= (int32_t)b ? 1u : 0u;
            return 1;
        default:
            break;
        }
        break;
    default:
        break;
    }
    if (parser->status == NOVAC_OK) {
        if (parser->diagnostic != NULL)
            parser->diagnostic->file_id = node->file_id;
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, node->line, node->column,
                    "expression is not a supported deterministic integer constant expression");
    }
    return 0;
}
static int nc_validate_static_initializer(NcParser *parser, NcTypeSpec target_type,
                                          uint32_t initializer_node) {
    const NcNode *init;
    if (initializer_node == NOVAC_INVALID_NODE)
        return 1;
    init = &parser->nodes[initializer_node];
    if (init->kind == NC_NODE_INIT_ITEM) {
        initializer_node = init->a;
        init = &parser->nodes[initializer_node];
    }
    if (nc_type_is_aggregate(target_type)) {
        if (target_type.kind == NC_TYPE_ARRAY && target_type.element_kind == NC_TYPE_CHAR &&
            init->kind == NC_NODE_STRING) {
            if (init->value >= parser->string_count)
                return 0;
            if (parser->strings[init->value].length > target_type.array_length + 1u) {
                if (parser->diagnostic != NULL)
                    parser->diagnostic->file_id = init->file_id;
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_ARRAY_BOUNDS, init->line, init->column,
                            "string initializer does not fit destination char array");
                return 0;
            }
            return 1;
        }
        if (init->kind != NC_NODE_INIT_LIST) {
            if (parser->diagnostic != NULL)
                parser->diagnostic->file_id = init->file_id;
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, init->line, init->column,
                        "static aggregate initializer requires a brace list");
            return 0;
        }
        if (target_type.kind == NC_TYPE_ARRAY) {
            NcTypeSpec element = nc_array_element_type(target_type);
            uint32_t next_index = 0u;
            uint32_t used_mask = 0u;
            uint32_t item_index = init->a;
            while (item_index != NOVAC_INVALID_NODE) {
                const NcNode *item = &parser->nodes[item_index];
                uint32_t index;
                if (item->kind != NC_NODE_INIT_ITEM)
                    return 0;
                if (item->op == 1u) {
                    parser->status =
                        nc_diag(parser->diagnostic, NOVAC_ERR_INVALID_MEMBER, item->line,
                                item->column, "field designator cannot initialize an array");
                    return 0;
                }
                if (item->op == 2u) {
                    index = item->value;
                    next_index = index + 1u;
                } else
                    index = next_index++;
                if (index >= target_type.array_length) {
                    parser->status = nc_diag(
                        parser->diagnostic, NOVAC_ERR_ARRAY_BOUNDS, item->line, item->column,
                        "array initializer index is outside the declared bound");
                    return 0;
                }
                if ((used_mask & ((uint32_t)1u << index)) != 0u) {
                    parser->status =
                        nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, item->line,
                                item->column, "duplicate array initializer target");
                    return 0;
                }
                used_mask |= (uint32_t)1u << index;
                if (!nc_validate_static_initializer(parser, element, item->a))
                    return 0;
                item_index = item->next;
            }
            return 1;
        }
        if (target_type.kind == NC_TYPE_STRUCT || target_type.kind == NC_TYPE_UNION) {
            const NcStructDef *def;
            uint32_t next_field = 0u;
            uint32_t used_mask = 0u;
            uint32_t item_index = init->a;
            uint32_t union_items = 0u;
            if (target_type.struct_index >= parser->struct_count)
                return 0;
            def = &parser->structs[target_type.struct_index];
            while (item_index != NOVAC_INVALID_NODE) {
                const NcNode *item = &parser->nodes[item_index];
                uint32_t field_index;
                if (item->kind != NC_NODE_INIT_ITEM)
                    return 0;
                if (item->op == 2u) {
                    parser->status = nc_diag(
                        parser->diagnostic, NOVAC_ERR_INVALID_MEMBER, item->line, item->column,
                        "array index designator cannot initialize a struct/union");
                    return 0;
                }
                if (item->op == 1u) {
                    int found = nc_struct_field_index(def, item->name);
                    if (found < 0) {
                        parser->status = nc_diag(
                            parser->diagnostic, NOVAC_ERR_INVALID_MEMBER, item->line, item->column,
                            "initializer designates an unknown aggregate field");
                        return 0;
                    }
                    field_index = (uint32_t)found;
                    next_field = field_index + 1u;
                } else
                    field_index = target_type.kind == NC_TYPE_UNION ? 0u : next_field++;
                if (field_index >= def->field_count) {
                    parser->status =
                        nc_diag(parser->diagnostic, NOVAC_ERR_INVALID_MEMBER, item->line,
                                item->column, "too many aggregate initializer elements");
                    return 0;
                }
                if (target_type.kind == NC_TYPE_UNION && union_items++ != 0u) {
                    parser->status = nc_diag(
                        parser->diagnostic, NOVAC_ERR_INVALID_MEMBER, item->line, item->column,
                        "bounded union initializer accepts exactly one active member");
                    return 0;
                }
                if ((used_mask & ((uint32_t)1u << field_index)) != 0u) {
                    parser->status =
                        nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, item->line,
                                item->column, "duplicate aggregate initializer target");
                    return 0;
                }
                used_mask |= (uint32_t)1u << field_index;
                if (!nc_validate_static_initializer(parser, def->fields[field_index].type, item->a))
                    return 0;
                item_index = item->next;
            }
            return 1;
        }
    }
    if (init->kind == NC_NODE_INIT_LIST) {
        uint32_t item = init->a;
        if (item == NOVAC_INVALID_NODE || parser->nodes[item].next != NOVAC_INVALID_NODE ||
            parser->nodes[item].kind != NC_NODE_INIT_ITEM || parser->nodes[item].op != 0u) {
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, init->line, init->column,
                        "scalar static initializer brace list must contain one positional value");
            return 0;
        }
        return nc_validate_static_initializer(parser, target_type, parser->nodes[item].a);
    }
    {
        uint32_t value;
        return nc_eval_constant_node(parser, initializer_node, &value);
    }
}
static int nc_validate_switch_constants(NcParser *parser) {
    uint32_t i;
    for (i = 0u; i < parser->node_count; i++) {
        NcNode *switch_node = &parser->nodes[i];
        uint32_t case_index;
        uint32_t default_count = 0u;
        if (switch_node->kind != NC_NODE_SWITCH)
            continue;
        case_index = switch_node->b;
        while (case_index != NOVAC_INVALID_NODE) {
            NcNode *current = &parser->nodes[case_index];
            uint32_t scan;
            if (current->op != 0u) {
                default_count++;
                if (default_count > 1u) {
                    if (parser->diagnostic != NULL)
                        parser->diagnostic->file_id = current->file_id;
                    parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION,
                                             current->line, current->column,
                                             "switch contains more than one default label");
                    return 0;
                }
            } else {
                uint32_t value;
                if (!nc_eval_constant_node(parser, current->a, &value))
                    return 0;
                current->value = value;
                scan = switch_node->b;
                while (scan != case_index) {
                    NcNode *earlier = &parser->nodes[scan];
                    if (earlier->op == 0u && earlier->value == value) {
                        if (parser->diagnostic != NULL)
                            parser->diagnostic->file_id = current->file_id;
                        parser->status = nc_diag(parser->diagnostic,
                                                 NOVAC_ERR_CONFLICTING_DECLARATION, current->line,
                                                 current->column, "duplicate switch case constant");
                        return 0;
                    }
                    scan = earlier->next;
                }
            }
            case_index = current->next;
        }
    }
    return 1;
}

static int nc_validate_project_program(NcParser *parser) {
    uint32_t i;
    for (i = 0u; i < parser->constant_count; i++) {
        uint32_t ignored;
        if (!nc_resolve_enum_constant(parser, i, &ignored))
            return 0;
    }
    for (i = 0u; i < parser->global_count; i++) {
        if (parser->globals[i].has_initializer != 0u &&
            parser->globals[i].initializer_node != NOVAC_INVALID_NODE) {
            if (nc_type_is_aggregate(parser->globals[i].type)) {
                if (!nc_validate_static_initializer(parser, parser->globals[i].type,
                                                    parser->globals[i].initializer_node))
                    return 0;
                parser->globals[i].initial_value = 0u;
            } else {
                uint32_t value;
                if (!nc_eval_constant_node(parser, parser->globals[i].initializer_node, &value))
                    return 0;
                if (parser->globals[i].type.kind == NC_TYPE_CHAR)
                    value &= 0xFFu;
                parser->globals[i].initial_value = value;
            }
        }
    }
    for (i = 0u; i < parser->static_local_count; i++) {
        NcStaticLocal *local = &parser->static_locals[i];
        if (local->has_initializer != 0u && local->initializer_node != NOVAC_INVALID_NODE) {
            uint32_t value;
            if (!nc_eval_constant_node(parser, local->initializer_node, &value))
                return 0;
            if (local->type.kind == NC_TYPE_CHAR)
                value &= 0xFFu;
            if (nc_type_is_pointer(local->type) && value != 0u) {
                if (parser->diagnostic != NULL)
                    parser->diagnostic->file_id = local->file_id;
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION,
                                         local->line, local->column,
                                         "local static pointer initializer currently supports only "
                                         "the null integer constant");
                return 0;
            }
            local->initial_value = value;
        }
    }
    if (!nc_validate_switch_constants(parser))
        return 0;
    if (parser->function_count == 0u) {
        parser->status =
            nc_diag(parser->diagnostic, NOVAC_ERR_PARSE, 1u, 1u, "project contains no functions");
        return 0;
    }
    for (i = 0u; i < parser->struct_count; i++) {
        if (parser->structs[i].is_defined == 0u) {
            if (parser->diagnostic != NULL)
                parser->diagnostic->file_id = parser->structs[i].file_id;
            parser->status =
                nc_diag(parser->diagnostic, NOVAC_ERR_UNKNOWN_TYPE, parser->structs[i].line,
                        parser->structs[i].column,
                        "aggregate type is referenced but never defined in the project");
            return 0;
        }
    }
    for (i = 0u; i < parser->function_count; i++) {
        if (parser->functions[i].is_defined == 0u) {
            if (parser->diagnostic != NULL)
                parser->diagnostic->file_id = parser->functions[i].file_id;
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_UNKNOWN_SYMBOL,
                                     parser->functions[i].line, parser->functions[i].column,
                                     "function prototype has no definition in the project");
            return 0;
        }
    }
    for (i = 0u; i < parser->global_count; i++) {
        if (parser->globals[i].has_strong_definition == 0u &&
            parser->globals[i].has_tentative_definition == 0u) {
            if (parser->diagnostic != NULL)
                parser->diagnostic->file_id = parser->globals[i].file_id;
            parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_UNKNOWN_SYMBOL,
                                     parser->globals[i].line, parser->globals[i].column,
                                     "extern global declaration has no definition in the project");
            return 0;
        }
    }
    {
        uint32_t cursor = NOVA_COMPILER_GLOBAL_BASE;
        for (i = 0u; i < parser->global_count; i++) {
            NcTypeSpec type = parser->globals[i].type;
            uint32_t size = 0u;
            if (type.kind == NC_TYPE_CHAR)
                size = 1u;
            else if (type.kind == NC_TYPE_INT || type.kind == NC_TYPE_INT_PTR ||
                     type.kind == NC_TYPE_STRUCT_PTR || type.kind == NC_TYPE_CHAR_PTR ||
                     type.kind == NC_TYPE_UNION_PTR)
                size = 4u;
            else if ((type.kind == NC_TYPE_STRUCT || type.kind == NC_TYPE_UNION) &&
                     type.struct_index < parser->struct_count)
                size = parser->structs[type.struct_index].byte_size;

            else if (type.kind == NC_TYPE_ARRAY) {
                uint32_t elem = 0u;
                if (type.element_kind == NC_TYPE_CHAR)
                    elem = 1u;
                else if (type.element_kind == NC_TYPE_INT || type.element_kind == NC_TYPE_INT_PTR ||
                         type.element_kind == NC_TYPE_STRUCT_PTR ||
                         type.element_kind == NC_TYPE_CHAR_PTR ||
                         type.element_kind == NC_TYPE_UNION_PTR)
                    elem = 4u;
                else if ((type.element_kind == NC_TYPE_STRUCT ||
                          type.element_kind == NC_TYPE_UNION) &&
                         type.struct_index < parser->struct_count)
                    elem = parser->structs[type.struct_index].byte_size;
                if (elem != 0u && type.array_length <= 0xFFFFFFFFu / elem)
                    size = elem * type.array_length;
            }
            if (size == 0u || cursor > NOVA_COMPILER_GLOBAL_LIMIT ||
                size > NOVA_COMPILER_GLOBAL_LIMIT - cursor) {
                if (parser->diagnostic != NULL)
                    parser->diagnostic->file_id = parser->globals[i].file_id;
                parser->status = nc_diag(parser->diagnostic, NOVAC_ERR_GLOBAL_CAPACITY,
                                         parser->globals[i].line, parser->globals[i].column,
                                         "global static-data segment exceeds reserved NovaVM range "
                                         "or has invalid aggregate size");
                return 0;
            }
            parser->globals[i].address = cursor;
            cursor += size;
        }
        for (i = 0u; i < parser->static_local_count; i++) {
            uint32_t size = nc_parser_type_size(parser, parser->static_locals[i].type);
            if (size == 0u || cursor > NOVA_COMPILER_GLOBAL_LIMIT ||
                size > NOVA_COMPILER_GLOBAL_LIMIT - cursor) {
                if (parser->diagnostic != NULL)
                    parser->diagnostic->file_id = parser->static_locals[i].file_id;
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_GLOBAL_CAPACITY,
                            parser->static_locals[i].line, parser->static_locals[i].column,
                            "local static-data segment exceeds reserved NovaVM range or has "
                            "invalid object size");
                return 0;
            }
            parser->static_locals[i].address = cursor;
            cursor += size;
        }
        for (i = 0u; i < parser->string_count; i++) {
            uint32_t size = parser->strings[i].length;
            if (size == 0u || cursor > NOVA_COMPILER_GLOBAL_LIMIT ||
                size > NOVA_COMPILER_GLOBAL_LIMIT - cursor) {
                if (parser->diagnostic != NULL)
                    parser->diagnostic->file_id = parser->strings[i].file_id;
                parser->status =
                    nc_diag(parser->diagnostic, NOVAC_ERR_GLOBAL_CAPACITY, parser->strings[i].line,
                            parser->strings[i].column,
                            "string-literal static data exceeds reserved NovaVM range");
                return 0;
            }
            parser->strings[i].address = cursor;
            cursor += size;
        }
    }
    return 1;
}
/* ------------------------------- codegen --------------------------------- */
typedef struct NcSymbol {
    char name[NOVAC_NAME_CAPACITY];
    NcTypeSpec type;
    uint8_t reg;
    uint32_t declaration_line;
    uint32_t declaration_column;
    uint32_t function_index;
    uint32_t is_parameter;
    uint32_t memory_backed;
    uint32_t owns_storage;
    uint32_t scope_depth;
    uint32_t debug_entry_index;
    uint32_t debug_entry_count;
    uint32_t declaration_epoch;
    uint32_t source_scope_depth;
} NcSymbol;
typedef struct NcCallPatch {
    uint32_t target_offset;
    uint32_t function_index;
} NcCallPatch;
#define NOVAC_MAX_CALL_PATCHES 512u
#define NOVAC_MAX_LOOP_DEPTH 16u
#define NOVAC_MAX_LOOP_PATCHES 256u
#define NOVAC_MAX_BREAK_DEPTH 16u
#define NOVAC_MAX_SWITCH_CASES 64u
#define NOVAC_MAX_LABELS 64u
#define NOVAC_MAX_GOTO_PATCHES 128u
typedef struct NcLabelInfo {
    char name[NOVAC_NAME_CAPACITY];
    uint32_t node_index;
    uint32_t scope_depth;
    uint32_t epoch;
    uint32_t address;
    uint32_t is_emitted;
} NcLabelInfo;
typedef struct NcGotoPatch {
    uint32_t target_offset;
    uint32_t label_index;
} NcGotoPatch;

typedef struct NcBreakContext {
    uint32_t break_patch_start;
    uint32_t preserve_symbol_count;
} NcBreakContext;
typedef struct NcLoopContext {
    uint32_t continue_patch_start;
    uint32_t preserve_symbol_count;
} NcLoopContext;
typedef struct NcCodegen {
    const NcNode *nodes;
    const NcStructDef *structs;
    uint32_t struct_count;
    const NcFunction *functions;
    uint32_t function_count;
    const NcGlobal *globals;
    uint32_t global_count;
    const NcStaticLocal *static_locals;
    uint32_t static_local_count;
    const NcStringLiteral *strings;
    uint32_t string_count;
    const NcEnumConstant *constants;
    uint32_t constant_count;
    uint8_t *output;
    uint32_t capacity;
    uint32_t size;
    NcSymbol symbols[NOVA_COMPILER_MAX_LOCALS];
    uint32_t symbol_count;
    uint32_t total_symbol_count;
    uint32_t function_decl_count;
    uint32_t scope_depth;
    uint8_t temp_used[16];
    uint32_t current_function_index;
    uint32_t function_starts[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
    uint32_t function_ends[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
    uint32_t function_local_counts[NOVA_COMPILER_MAX_DEBUG_FUNCTIONS];
    NcCallPatch call_patches[NOVAC_MAX_CALL_PATCHES];
    uint32_t call_patch_count;
    NcLoopContext loop_stack[NOVAC_MAX_LOOP_DEPTH];
    uint32_t loop_depth;
    NcBreakContext break_stack[NOVAC_MAX_BREAK_DEPTH];
    uint32_t break_depth;
    uint32_t break_patches[NOVAC_MAX_LOOP_PATCHES];
    uint32_t break_patch_count;
    uint32_t continue_patches[NOVAC_MAX_LOOP_PATCHES];
    uint32_t continue_patch_count;
    NcLabelInfo labels[NOVAC_MAX_LABELS];
    uint32_t label_count;
    NcGotoPatch goto_patches[NOVAC_MAX_GOTO_PATCHES];
    uint32_t goto_patch_count;
    NovaSourceMapEntry *source_map;
    uint32_t source_map_capacity;
    uint32_t source_map_count;
    NovaDebugSymbol *debug_symbols;
    uint32_t debug_symbol_capacity;
    uint32_t debug_symbol_count;
    NovaDebugFunction *debug_functions;
    uint32_t debug_function_capacity;
    NovaDebugGlobal *debug_globals;
    uint32_t debug_global_capacity;
    uint32_t debug_global_count;
    NovaSymbolReference *references;
    uint32_t reference_capacity;
    uint32_t reference_count;

    NovaCompilerDiagnostic *diagnostic;
    int32_t status;
} NcCodegen;
static int32_t nc_codegen_fail(NcCodegen *cg, const NcNode *node, int32_t status,
                               const char *message) {
    if (cg->status == NOVAC_OK) {
        if (cg->diagnostic != NULL) {
            if (node != NULL)
                cg->diagnostic->file_id = node->file_id;
            else if (cg->current_function_index < cg->function_count)
                cg->diagnostic->file_id = cg->functions[cg->current_function_index].file_id;
        }
        cg->status = nc_diag(cg->diagnostic, status, node != NULL ? node->line : 0u,
                             node != NULL ? node->column : 0u, message);
    }
    return cg->status;
}
static int nc_emit_u8(NcCodegen *cg, uint8_t value) {
    if (cg->size >= cg->capacity) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_OUTPUT_CAPACITY, "bytecode output buffer is full");
        return 0;
    }
    cg->output[cg->size++] = value;
    return 1;
}
static int nc_emit_u32(NcCodegen *cg, uint32_t value) {
    return nc_emit_u8(cg, (uint8_t)(value & 0xFFu)) &&
           nc_emit_u8(cg, (uint8_t)((value >> 8u) & 0xFFu)) &&
           nc_emit_u8(cg, (uint8_t)((value >> 16u) & 0xFFu)) &&
           nc_emit_u8(cg, (uint8_t)((value >> 24u) & 0xFFu));
}
static void nc_patch_u32(NcCodegen *cg, uint32_t offset, uint32_t value) {
    if (offset > cg->size || cg->size - offset < 4u) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_INTERNAL, "invalid bytecode patch offset");
        return;
    }
    cg->output[offset] = (uint8_t)(value & 0xFFu);
    cg->output[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
    cg->output[offset + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
    cg->output[offset + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
}
static void nc_emit_mov_ri(NcCodegen *cg, uint8_t reg, uint32_t value) {
    nc_emit_u8(cg, NOVA_OP_MOV_RI);
    nc_emit_u8(cg, reg);
    nc_emit_u32(cg, value);
}
static void nc_emit_mov_rr(NcCodegen *cg, uint8_t dst, uint8_t src) {
    nc_emit_u8(cg, NOVA_OP_MOV_RR);
    nc_emit_u8(cg, dst);
    nc_emit_u8(cg, src);
}
static void nc_emit_scalar_load(NcCodegen *cg, int address_reg, NcTypeSpec type) {
    nc_emit_u8(cg, type.kind == NC_TYPE_CHAR ? NOVA_OP_LOAD8 : NOVA_OP_LOAD32);
    nc_emit_u8(cg, (uint8_t)address_reg);
    nc_emit_u8(cg, (uint8_t)address_reg);
}

static void nc_emit_scalar_store(NcCodegen *cg, int address_reg, int value_reg, NcTypeSpec type) {
    nc_emit_u8(cg, type.kind == NC_TYPE_CHAR ? NOVA_OP_STORE8 : NOVA_OP_STORE32);
    nc_emit_u8(cg, (uint8_t)address_reg);
    nc_emit_u8(cg, (uint8_t)value_reg);
}
static int nc_emit_initializer_at_base(NcCodegen *cg, const NcNode *declaration, uint8_t base_reg,
                                       uint32_t byte_offset, NcTypeSpec target_type,
                                       uint32_t initializer_node);
static void nc_emit_global_initializers(NcCodegen *cg) {
    uint32_t i;
    for (i = 0u; i < cg->global_count && cg->status == NOVAC_OK; i++) {
        const NcGlobal *global = &cg->globals[i];
        if (global->has_initializer == 0u)
            continue;
        if (nc_type_is_aggregate(global->type)) {
            const NcNode *init = &cg->nodes[global->initializer_node];
            nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, global->address);
            if (!nc_emit_initializer_at_base(cg, init, NOVAC_SCRATCH_REGISTER, 0u, global->type,
                                             global->initializer_node))
                return;
            continue;
        }
        if (global->initial_value == 0u)
            continue;
        nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, global->address);
        nc_emit_mov_ri(cg, NOVAC_TEMP_LAST, global->initial_value);
        nc_emit_scalar_store(cg, NOVAC_SCRATCH_REGISTER, NOVAC_TEMP_LAST, global->type);
    }
}
static void nc_emit_static_local_initializers(NcCodegen *cg) {
    uint32_t i;
    for (i = 0u; i < cg->static_local_count && cg->status == NOVAC_OK; i++) {
        const NcStaticLocal *local = &cg->static_locals[i];
        if (local->has_initializer == 0u || local->initial_value == 0u)
            continue;
        nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, local->address);
        nc_emit_mov_ri(cg, NOVAC_TEMP_LAST, local->initial_value);
        nc_emit_scalar_store(cg, NOVAC_SCRATCH_REGISTER, NOVAC_TEMP_LAST, local->type);
    }
}
static uint32_t nc_emit_jump_placeholder(NcCodegen *cg, uint8_t opcode) {
    uint32_t patch;
    nc_emit_u8(cg, opcode);
    patch = cg->size;
    nc_emit_u32(cg, 0u);
    return patch;
}
static void nc_emit_jump_target(NcCodegen *cg, uint8_t opcode, uint32_t target) {
    nc_emit_u8(cg, opcode);
    nc_emit_u32(cg, target);
}
static void nc_emit_string_initializers(NcCodegen *cg) {
    uint32_t i;
    for (i = 0u; i < cg->string_count && cg->status == NOVAC_OK; i++) {
        uint32_t j;
        for (j = 0u; j < cg->strings[i].length; j++) {
            nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, cg->strings[i].address + j);
            nc_emit_mov_ri(cg, NOVAC_TEMP_LAST, cg->strings[i].bytes[j]);
            nc_emit_scalar_store(cg, NOVAC_SCRATCH_REGISTER, NOVAC_TEMP_LAST, nc_type_char());
        }
    }
}
static uint32_t nc_debug_type(NcTypeSpec type);
static uint32_t nc_codegen_use_file(const NcCodegen *cg) {
    if (cg == NULL || cg->current_function_index >= cg->function_count)
        return 0u;
    return cg->functions[cg->current_function_index].file_id;
}
static int nc_find_function(const NcCodegen *cg, const char *name) {
    uint32_t i;
    uint32_t use_file = nc_codegen_use_file(cg);
    uint32_t bit = nc_file_bit(use_file);
    for (i = 0u; i < cg->function_count; i++) {
        if (cg->functions[i].linkage == NC_LINKAGE_INTERNAL &&
            cg->functions[i].file_id == use_file && nc_streq(cg->functions[i].name, name))
            return (int)i;
    }
    for (i = 0u; i < cg->function_count; i++) {
        if (cg->functions[i].linkage == NC_LINKAGE_EXTERNAL &&
            nc_streq(cg->functions[i].name, name) && (cg->functions[i].visibility_mask & bit) != 0u)
            return (int)i;
    }
    return -1;
}
static int nc_find_symbol(const NcCodegen *cg, const char *name) {
    uint32_t i = cg->symbol_count;
    while (i > 0u) {
        i--;
        if (nc_streq(cg->symbols[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_find_symbol_in_scope(const NcCodegen *cg, const char *name, uint32_t scope_depth) {
    uint32_t i = cg->symbol_count;
    while (i > 0u) {
        i--;
        if (cg->symbols[i].scope_depth < scope_depth)
            break;
        if (cg->symbols[i].scope_depth == scope_depth && nc_streq(cg->symbols[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_find_global(const NcCodegen *cg, const char *name) {
    uint32_t i;
    uint32_t use_file = nc_codegen_use_file(cg);
    uint32_t bit = nc_file_bit(use_file);
    for (i = 0u; i < cg->global_count; i++) {
        if (cg->globals[i].linkage == NC_LINKAGE_INTERNAL && cg->globals[i].file_id == use_file &&
            nc_streq(cg->globals[i].name, name))
            return (int)i;
    }
    for (i = 0u; i < cg->global_count; i++) {
        if (cg->globals[i].linkage == NC_LINKAGE_EXTERNAL && nc_streq(cg->globals[i].name, name) &&
            (cg->globals[i].visibility_mask & bit) != 0u)
            return (int)i;
    }
    return -1;
}
static int nc_find_constant(const NcCodegen *cg, const char *name) {
    uint32_t i;
    for (i = 0u; i < cg->constant_count; i++) {
        if (nc_streq(cg->constants[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_lvalue_root_global(const NcCodegen *cg, uint32_t node_index) {
    const NcNode *node;
    if (node_index == NOVAC_INVALID_NODE)
        return -1;
    node = &cg->nodes[node_index];
    if (node->kind == NC_NODE_VARIABLE) {
        if (nc_find_symbol(cg, node->name) >= 0)
            return -1;
        return nc_find_global(cg, node->name);
    }
    if (node->kind == NC_NODE_INDEX || node->kind == NC_NODE_MEMBER)
        return nc_lvalue_root_global(cg, node->a);
    return -1;
}
static int nc_publish_reference(NcCodegen *cg, const NcNode *node, uint32_t kind, const char *name,
                                uint32_t target_index, uint32_t target_file_id,
                                uint32_t bytecode_offset) {
    NovaSymbolReference *out;
    if (cg->references == NULL)
        return 1;
    if (cg->reference_count >= cg->reference_capacity) {
        nc_codegen_fail(cg, node, NOVAC_ERR_DEBUG_CAPACITY, "symbol-reference buffer is full");
        return 0;
    }
    out = &cg->references[cg->reference_count++];
    nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, name, (uint32_t)nc_strlen(name));
    out->kind = kind;
    out->file_id = node != NULL ? node->file_id : 0u;

    out->line = node != NULL ? node->line : 0u;
    out->column = node != NULL ? node->column : 0u;
    out->function_index = cg->current_function_index;
    out->target_index = target_index;
    out->target_file_id = target_file_id;
    out->bytecode_offset = bytecode_offset;
    return 1;
}
static uint32_t nc_type_size(const NcCodegen *cg, NcTypeSpec type);
static int nc_publish_global_metadata(NcCodegen *cg) {
    uint32_t i;
    if (cg->debug_globals == NULL)
        return 1;
    if (cg->global_count > cg->debug_global_capacity) {
        return nc_codegen_fail(cg, NULL, NOVAC_ERR_DEBUG_CAPACITY,
                               "global metadata buffer is too small") == NOVAC_OK;
    }
    for (i = 0u; i < cg->global_count; i++) {
        const NcGlobal *global = &cg->globals[i];
        NovaDebugGlobal *out = &cg->debug_globals[i];
        nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, global->name,
                     (uint32_t)nc_strlen(global->name));
        out->address = global->address;
        out->type = nc_debug_type(global->type);
        out->initial_value = global->initial_value;
        out->declaration_line = global->line;
        out->declaration_column = global->column;
        out->file_id = global->file_id;
        out->byte_size = nc_type_size(cg, global->type);
        out->element_count =
            global->type.kind == NC_TYPE_ARRAY
                ? global->type.array_length
                : ((global->type.kind == NC_TYPE_STRUCT || global->type.kind == NC_TYPE_UNION) &&
                           global->type.struct_index < cg->struct_count
                       ? cg->structs[global->type.struct_index].field_count
                       : 0u);
        out->struct_index =
            (global->type.kind == NC_TYPE_STRUCT || global->type.kind == NC_TYPE_UNION ||
             global->type.kind == NC_TYPE_STRUCT_PTR || global->type.kind == NC_TYPE_UNION_PTR ||
             (global->type.kind == NC_TYPE_ARRAY &&
              (global->type.element_kind == NC_TYPE_STRUCT ||
               global->type.element_kind == NC_TYPE_UNION ||
               global->type.element_kind == NC_TYPE_STRUCT_PTR ||
               global->type.element_kind == NC_TYPE_UNION_PTR)))
                ? global->type.struct_index
                : NOVAC_INVALID_NODE;
    }
    cg->debug_global_count = cg->global_count;
    return 1;
}
static uint32_t nc_type_size(const NcCodegen *cg, NcTypeSpec type) {
    if (type.kind == NC_TYPE_CHAR)
        return 1u;
    if (type.kind == NC_TYPE_INT || type.kind == NC_TYPE_INT_PTR ||
        type.kind == NC_TYPE_STRUCT_PTR || type.kind == NC_TYPE_CHAR_PTR ||
        type.kind == NC_TYPE_UNION_PTR)
        return 4u;
    if (type.kind == NC_TYPE_STRUCT || type.kind == NC_TYPE_UNION) {
        if (type.struct_index >= cg->struct_count)
            return 0u;
        return cg->structs[type.struct_index].byte_size;
    }
    if (type.kind == NC_TYPE_ARRAY) {
        NcTypeSpec element = nc_type_int();
        uint32_t element_size;
        element.kind = type.element_kind;
        element.struct_index = type.struct_index;
        element_size = nc_type_size(cg, element);
        if (element_size == 0u || type.array_length > 0xFFFFFFFFu / element_size)
            return 0u;
        return type.array_length * element_size;
    }
    return 0u;
}
static NcTypeSpec nc_array_element_type(NcTypeSpec type) {
    NcTypeSpec element = nc_type_int();
    element.kind = type.element_kind;

    element.struct_index = type.struct_index;
    element.qualifiers = type.element_qualifiers;
    return element;
}
static uint32_t nc_debug_type(NcTypeSpec type) {
    if (type.kind == NC_TYPE_CHAR)
        return NOVAC_DEBUG_CHAR;
    if (type.kind == NC_TYPE_CHAR_PTR)
        return NOVAC_DEBUG_CHAR_PTR;
    if (type.kind == NC_TYPE_INT_PTR)
        return NOVAC_DEBUG_INT_PTR;
    if (type.kind == NC_TYPE_STRUCT_PTR)
        return NOVAC_DEBUG_STRUCT_PTR;
    if (type.kind == NC_TYPE_UNION_PTR)
        return NOVAC_DEBUG_UNION_PTR;
    if (type.kind == NC_TYPE_ARRAY)
        return NOVAC_DEBUG_ARRAY;
    if (type.kind == NC_TYPE_STRUCT)
        return NOVAC_DEBUG_STRUCT;
    if (type.kind == NC_TYPE_UNION)
        return NOVAC_DEBUG_UNION;
    return NOVAC_DEBUG_INT;
}
static uint32_t nc_name_append_text(char *dst, uint32_t used, const char *text) {
    uint32_t i = 0u;
    while (text != NULL && text[i] != '\0' && used + 1u < NOVA_COMPILER_DEBUG_NAME) {
        dst[used++] = text[i++];
    }
    dst[used] = '\0';
    return used;
}
static uint32_t nc_name_append_u32(char *dst, uint32_t used, uint32_t value) {
    char digits[10];
    uint32_t count = 0u;
    uint32_t i;
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < 10u);
    for (i = 0u; i < count && used + 1u < NOVA_COMPILER_DEBUG_NAME; i++) {
        dst[used++] = digits[count - 1u - i];
    }
    dst[used] = '\0';
    return used;
}
static void nc_make_child_name(char *out, const char *base, int has_index, uint32_t index,
                               const char *member) {
    uint32_t used = 0u;
    out[0] = '\0';
    used = nc_name_append_text(out, used, base);
    if (has_index) {
        if (used + 1u < NOVA_COMPILER_DEBUG_NAME)
            out[used++] = '[';
        out[used] = '\0';
        used = nc_name_append_u32(out, used, index);
        if (used + 1u < NOVA_COMPILER_DEBUG_NAME)
            out[used++] = ']';
        out[used] = '\0';
    }
    if (member != NULL) {
        if (used + 1u < NOVA_COMPILER_DEBUG_NAME)
            out[used++] = '.';
        out[used] = '\0';
        (void)nc_name_append_text(out, used, member);
    }
}
static int nc_publish_debug_entry(NcCodegen *cg, const char *name, uint8_t reg,

                                  NcTypeSpec type, uint32_t declaration_line,
                                  uint32_t declaration_column, uint32_t function_index,
                                  uint32_t is_parameter, uint32_t storage_kind,
                                  uint32_t byte_offset, uint32_t byte_size,
                                  uint32_t element_count) {
    NovaDebugSymbol *out;
    if (cg->debug_symbols == NULL)
        return 1;
    if (cg->debug_symbol_count >= cg->debug_symbol_capacity) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_DEBUG_CAPACITY, "debug symbol buffer is full");
        return 0;
    }
    out = &cg->debug_symbols[cg->debug_symbol_count++];
    nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, name, (uint32_t)nc_strlen(name));
    out->register_index = reg;
    out->type = nc_debug_type(type);
    out->declaration_line = declaration_line;
    out->declaration_column = declaration_column;
    out->function_index = function_index;
    out->is_parameter = is_parameter;
    out->storage_kind = storage_kind;
    out->byte_offset = byte_offset;
    out->byte_size = byte_size;
    out->element_count = element_count;
    out->file_id = function_index < cg->function_count ? cg->functions[function_index].file_id : 0u;
    out->lifetime_start = cg->size;
    out->lifetime_end = 0xFFFFFFFFu;
    out->scope_depth = cg->scope_depth;
    return 1;
}
static uint32_t nc_debug_element_count(const NcCodegen *cg, NcTypeSpec type) {
    if (type.kind == NC_TYPE_ARRAY)
        return type.array_length;
    if ((type.kind == NC_TYPE_STRUCT || type.kind == NC_TYPE_UNION) &&
        type.struct_index < cg->struct_count)
        return cg->structs[type.struct_index].field_count;
    return 0u;
}
static int nc_publish_debug_children(NcCodegen *cg, const NcSymbol *symbol, const char *base_name,
                                     NcTypeSpec type, uint32_t base_offset, uint32_t depth) {
    uint32_t i;
    if (!nc_type_is_aggregate(type))
        return 1;
    if (depth > 16u) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_DEBUG_CAPACITY,
                        "nested aggregate debug depth exceeds bounded capacity");
        return 0;
    }
    if (type.kind == NC_TYPE_STRUCT || type.kind == NC_TYPE_UNION) {
        const NcStructDef *def;
        if (type.struct_index >= cg->struct_count)
            return 0;
        def = &cg->structs[type.struct_index];
        for (i = 0u; i < def->field_count; i++) {
            char child[NOVA_COMPILER_DEBUG_NAME];
            const NcStructField *field = &def->fields[i];
            uint32_t offset = base_offset + field->byte_offset;
            nc_make_child_name(child, base_name, 0, 0u, field->name);
            if (!nc_publish_debug_entry(
                    cg, child, symbol->reg, field->type, symbol->declaration_line,
                    symbol->declaration_column, symbol->function_index, symbol->is_parameter,
                    NOVAC_DEBUG_STORAGE_MEMORY, offset, nc_type_size(cg, field->type),
                    nc_debug_element_count(cg, field->type)))
                return 0;
            if (nc_type_is_aggregate(field->type) &&
                !nc_publish_debug_children(cg, symbol, child, field->type, offset, depth + 1u))
                return 0;
        }
        return 1;
    }
    if (type.kind == NC_TYPE_ARRAY) {
        NcTypeSpec element = nc_array_element_type(type);
        uint32_t element_size = nc_type_size(cg, element);
        for (i = 0u; i < type.array_length; i++) {
            char child[NOVA_COMPILER_DEBUG_NAME];
            uint32_t offset = base_offset + i * element_size;
            nc_make_child_name(child, base_name, 1, i, NULL);
            if (!nc_publish_debug_entry(cg, child, symbol->reg, element, symbol->declaration_line,
                                        symbol->declaration_column, symbol->function_index,
                                        symbol->is_parameter, NOVAC_DEBUG_STORAGE_MEMORY, offset,
                                        element_size, nc_debug_element_count(cg, element)))
                return 0;
            if (nc_type_is_aggregate(element) &&
                !nc_publish_debug_children(cg, symbol, child, element, offset, depth + 1u))
                return 0;
        }
    }
    return 1;
}
static int nc_publish_debug_symbol(NcCodegen *cg, const NcSymbol *symbol) {
    uint32_t byte_size = nc_type_size(cg, symbol->type);
    uint32_t element_count = nc_debug_element_count(cg, symbol->type);
    if (!nc_publish_debug_entry(
            cg, symbol->name, symbol->reg, symbol->type, symbol->declaration_line,
            symbol->declaration_column, symbol->function_index, symbol->is_parameter,
            symbol->memory_backed ? NOVAC_DEBUG_STORAGE_MEMORY : NOVAC_DEBUG_STORAGE_REGISTER, 0u,
            byte_size, element_count))
        return 0;
    return nc_publish_debug_children(cg, symbol, symbol->name, symbol->type, 0u, 0u);
}
static int nc_declare_named_symbol(NcCodegen *cg, const char *name, NcTypeSpec type, uint32_t line,
                                   uint32_t column, uint32_t is_parameter,
                                   uint32_t declaration_epoch, uint32_t memory_backed) {
    NcSymbol *symbol;
    uint32_t debug_start;
    if (nc_find_symbol_in_scope(cg, name, cg->scope_depth) >= 0) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_UNKNOWN_SYMBOL,
                        "duplicate local or parameter name in the same lexical scope");
        return -1;
    }

    if (cg->symbol_count >= NOVA_COMPILER_MAX_LOCALS) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_SYMBOL_CAPACITY,
                        "function local register capacity exceeded");
        return -1;
    }
    symbol = &cg->symbols[cg->symbol_count];
    nc_copy_text(symbol->name, NOVAC_NAME_CAPACITY, name, (uint32_t)nc_strlen(name));
    symbol->type = type;
    symbol->reg = (uint8_t)(NOVAC_LOCAL_FIRST + cg->symbol_count);
    symbol->declaration_line = line;
    symbol->declaration_column = column;
    symbol->function_index = cg->current_function_index;
    symbol->is_parameter = is_parameter;
    symbol->memory_backed = memory_backed;
    symbol->owns_storage = 0u;
    symbol->scope_depth = cg->scope_depth;
    debug_start = cg->debug_symbol_count;
    symbol->debug_entry_index = debug_start;
    symbol->debug_entry_count = 0u;
    symbol->declaration_epoch = declaration_epoch;
    symbol->source_scope_depth = 0u;
    cg->symbol_count++;
    cg->total_symbol_count++;
    cg->function_decl_count++;
    if (!nc_publish_debug_symbol(cg, symbol))
        return -1;
    symbol->debug_entry_count = cg->debug_symbol_count - debug_start;
    return (int)(cg->symbol_count - 1u);
}
static int nc_declare_symbol(NcCodegen *cg, const NcNode *node) {
    int is_static = node->op == 1u;
    int index = nc_declare_named_symbol(cg, node->name, node->type, node->line, node->column, 0u,
                                        node->epoch, is_static ? 1u : 0u);
    if (index >= 0) {
        NcSymbol *symbol = &cg->symbols[(uint32_t)index];
        symbol->source_scope_depth = node->scope_depth;
        if (is_static) {
            if (node->value >= cg->static_local_count) {
                nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL,
                                "local static declaration has invalid storage index");
                return -1;
            }
            symbol->owns_storage = 0u;
            nc_emit_mov_ri(cg, symbol->reg, cg->static_locals[node->value].address);
        }
    }
    return index;
}
static int nc_alloc_temp(NcCodegen *cg, const NcNode *node) {
    uint32_t reg;
    for (reg = NOVAC_TEMP_FIRST; reg <= NOVAC_TEMP_LAST; reg++) {
        if (!cg->temp_used[reg]) {
            cg->temp_used[reg] = 1u;
            return (int)reg;
        }
    }
    nc_codegen_fail(cg, node, NOVAC_ERR_REGISTER_PRESSURE,
                    "expression requires more temporary registers than available");
    return -1;
}
static void nc_free_temp(NcCodegen *cg, int reg) {
    if (reg >= (int)NOVAC_TEMP_FIRST && reg <= (int)NOVAC_TEMP_LAST)
        cg->temp_used[reg] = 0u;
}
static int nc_add_call_patch(NcCodegen *cg, uint32_t target_offset, uint32_t function_index) {
    if (cg->call_patch_count >= NOVAC_MAX_CALL_PATCHES) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_OUTPUT_CAPACITY, "call patch capacity exceeded");
        return 0;
    }
    cg->call_patches[cg->call_patch_count].target_offset = target_offset;
    cg->call_patches[cg->call_patch_count].function_index = function_index;
    cg->call_patch_count++;
    return 1;
}
static void nc_emit_call_placeholder(NcCodegen *cg, uint32_t function_index) {
    uint32_t patch;
    nc_emit_u8(cg, NOVA_OP_CALL);

    patch = cg->size;
    nc_emit_u32(cg, 0u);
    nc_add_call_patch(cg, patch, function_index);
}
static int nc_emit_expr(NcCodegen *cg, uint32_t node_index);
static int nc_emit_comparison(NcCodegen *cg, const NcNode *node, int left, int right,
                              NcBinaryOp op) {
    (void)node;
    uint8_t branch;
    uint32_t true_patch;
    uint32_t end_patch;
    nc_emit_u8(cg, NOVA_OP_CMP_RR);
    nc_emit_u8(cg, (uint8_t)left);
    nc_emit_u8(cg, (uint8_t)right);
    nc_free_temp(cg, right);
    branch = op == NC_BIN_EQ   ? NOVA_OP_JE
             : op == NC_BIN_NE ? NOVA_OP_JNE
             : op == NC_BIN_LT ? NOVA_OP_JL
             : op == NC_BIN_LE ? NOVA_OP_JLE
             : op == NC_BIN_GT ? NOVA_OP_JG
                               : NOVA_OP_JGE;
    true_patch = nc_emit_jump_placeholder(cg, branch);
    nc_emit_mov_ri(cg, (uint8_t)left, 0u);
    end_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
    nc_patch_u32(cg, true_patch, cg->size);
    nc_emit_mov_ri(cg, (uint8_t)left, 1u);
    nc_patch_u32(cg, end_patch, cg->size);
    return left;
}
static int nc_emit_intrinsic_call(NcCodegen *cg, const NcNode *node) {
    int argument;
    if (!nc_streq(node->name, "putchar") && !nc_streq(node->name, "nova_alloc") &&
        !nc_streq(node->name, "nova_free"))
        return -2;
    if (node->value != 1u || node->a == NOVAC_INVALID_NODE) {
        nc_codegen_fail(cg, node, NOVAC_ERR_ARGUMENT_COUNT,
                        "intrinsic requires exactly one argument");
        return -1;
    }
    argument = nc_emit_expr(cg, node->a);
    if (argument < 0)
        return -1;
    if (nc_streq(node->name, "putchar")) {
        nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, NOVA_MMIO_CONSOLE);
        nc_emit_u8(cg, NOVA_OP_STORE8);
        nc_emit_u8(cg, NOVAC_SCRATCH_REGISTER);
        nc_emit_u8(cg, (uint8_t)argument);
        return argument;
    }
    if (nc_streq(node->name, "nova_alloc")) {
        nc_emit_u8(cg, NOVA_OP_ALLOC);
        nc_emit_u8(cg, (uint8_t)argument);
        nc_emit_u8(cg, (uint8_t)argument);
        return argument;
    }
    if (nc_streq(node->name, "nova_free")) {
        nc_emit_u8(cg, NOVA_OP_FREE);
        nc_emit_u8(cg, (uint8_t)argument);
        nc_emit_mov_ri(cg, (uint8_t)argument, 0u);
        return argument;
    }
    return -2;
}

static int nc_emit_user_call(NcCodegen *cg, const NcNode *node, uint32_t function_index) {
    const NcFunction *function = &cg->functions[function_index];
    uint8_t saved_temp_mask[16];
    uint32_t arg_node;
    uint32_t arg_index = 0u;
    int result_reg;
    int reg;
    if (node->value != function->parameter_count) {
        nc_codegen_fail(cg, node, NOVAC_ERR_ARGUMENT_COUNT,
                        "function call argument count does not match definition");
        return -1;
    }
    /* NovaC calling convention: caller saves R1-R14. This creates a deterministic
    56-byte register save area directly above the VM CALL return address. */
    for (reg = 1; reg <= 14; reg++) {
        nc_emit_u8(cg, NOVA_OP_PUSH);
        nc_emit_u8(cg, (uint8_t)reg);
    }
    for (reg = 0; reg < 16; reg++)
        saved_temp_mask[reg] = cg->temp_used[reg];
    for (reg = (int)NOVAC_TEMP_FIRST; reg <= (int)NOVAC_TEMP_LAST; reg++)
        cg->temp_used[reg] = 0u;
    arg_node = node->a;
    while (arg_node != NOVAC_INVALID_NODE && arg_index < node->value && cg->status == NOVAC_OK) {
        int arg_reg = nc_emit_expr(cg, arg_node);
        if (arg_reg < 0)
            return -1;
        nc_emit_u8(cg, NOVA_OP_PUSH);
        nc_emit_u8(cg, (uint8_t)arg_reg);
        nc_free_temp(cg, arg_reg);
        arg_node = cg->nodes[arg_node].next;
        arg_index++;
    }
    if (arg_index != node->value) {
        nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "call argument chain is corrupt");
        return -1;
    }
    while (arg_index > 0u) {
        nc_emit_u8(cg, NOVA_OP_POP);
        nc_emit_u8(cg, (uint8_t)arg_index);
        arg_index--;
    }
    (void)nc_publish_reference(cg, node, NOVAC_REF_FUNCTION_CALL, function->name, function_index,
                               function->file_id, cg->size);
    nc_emit_call_placeholder(cg, function_index);
    for (reg = 14; reg >= 1; reg--) {
        nc_emit_u8(cg, NOVA_OP_POP);
        nc_emit_u8(cg, (uint8_t)reg);
    }
    for (reg = 0; reg < 16; reg++)
        cg->temp_used[reg] = saved_temp_mask[reg];
    result_reg = nc_alloc_temp(cg, node);
    if (result_reg >= 0)
        nc_emit_mov_rr(cg, (uint8_t)result_reg, NOVAC_RETURN_REGISTER);
    return result_reg;
}
static int nc_pointer_target_type(NcTypeSpec pointer, NcTypeSpec *target_out) {
    if (target_out == NULL)
        return 0;
    if (pointer.kind == NC_TYPE_INT_PTR) {
        *target_out = nc_type_int();
        target_out->qualifiers = pointer.element_qualifiers;
        return 1;
    }
    if (pointer.kind == NC_TYPE_CHAR_PTR) {
        *target_out = nc_type_char();
        target_out->qualifiers = pointer.element_qualifiers;
        return 1;
    }
    if (pointer.kind == NC_TYPE_STRUCT_PTR) {
        *target_out = nc_type_struct(pointer.struct_index);
        target_out->qualifiers = pointer.element_qualifiers;
        return 1;
    }
    if (pointer.kind == NC_TYPE_UNION_PTR) {
        *target_out = nc_type_union(pointer.struct_index);
        target_out->qualifiers = pointer.element_qualifiers;
        return 1;
    }
    return 0;
}
static int nc_address_type(NcTypeSpec value, NcTypeSpec *pointer_out) {
    if (pointer_out == NULL)
        return 0;
    if (value.kind == NC_TYPE_INT) {
        *pointer_out = nc_type_pointer_to(value);
        return 1;
    }
    if (value.kind == NC_TYPE_CHAR) {
        *pointer_out = nc_type_pointer_to(value);
        return 1;
    }
    if (value.kind == NC_TYPE_STRUCT || value.kind == NC_TYPE_UNION) {
        *pointer_out = nc_type_pointer_to(value);
        return 1;
    }
    return 0;
}
static uint32_t nc_pointer_element_size(const NcCodegen *cg, NcTypeSpec pointer) {
    NcTypeSpec target;
    if (!nc_pointer_target_type(pointer, &target))
        return 0u;
    return nc_type_size(cg, target);
}
static int nc_promote_scalar_symbol(NcCodegen *cg, const NcNode *node, uint32_t symbol_index) {
    NcSymbol *symbol;
    int temp;
    if (symbol_index >= cg->symbol_count)
        return 0;
    symbol = &cg->symbols[symbol_index];
    if (symbol->memory_backed != 0u)
        return 1;
    if (symbol->type.kind != NC_TYPE_INT) {
        nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                        "address-of for register locals currently requires int; pointer-to-pointer "
                        "types are not supported");
        return 0;
    }
    temp = nc_alloc_temp(cg, node);
    if (temp < 0)
        return 0;
    nc_emit_mov_ri(cg, (uint8_t)temp, 4u);
    nc_emit_u8(cg, NOVA_OP_ALLOC);
    nc_emit_u8(cg, (uint8_t)temp);
    nc_emit_u8(cg, (uint8_t)temp);
    nc_emit_scalar_store(cg, temp, symbol->reg, symbol->type);

    nc_emit_mov_rr(cg, symbol->reg, (uint8_t)temp);
    symbol->memory_backed = 1u;
    symbol->owns_storage = 1u;
    if (cg->debug_symbols != NULL && symbol->debug_entry_index < cg->debug_symbol_count) {
        cg->debug_symbols[symbol->debug_entry_index].storage_kind = NOVAC_DEBUG_STORAGE_MEMORY;
        cg->debug_symbols[symbol->debug_entry_index].byte_offset = 0u;
        cg->debug_symbols[symbol->debug_entry_index].byte_size = 4u;
    }
    nc_free_temp(cg, temp);
    return cg->status == NOVAC_OK;
}
static int nc_decay_pointer_type(NcTypeSpec type, NcTypeSpec *pointer_out) {
    NcTypeSpec element;
    if (pointer_out == NULL)
        return 0;
    if (nc_type_is_pointer(type)) {
        *pointer_out = type;
        return 1;
    }
    if (type.kind != NC_TYPE_ARRAY)
        return 0;
    element = nc_array_element_type(type);
    if (element.kind == NC_TYPE_INT || element.kind == NC_TYPE_CHAR ||
        element.kind == NC_TYPE_STRUCT || element.kind == NC_TYPE_UNION) {
        *pointer_out = nc_type_pointer_to(element);
        return 1;
    }
    return 0;
}
static int nc_resolve_expr_type(NcCodegen *cg, uint32_t node_index, NcTypeSpec *type_out) {
    const NcNode *node;
    if (node_index == NOVAC_INVALID_NODE || type_out == NULL)
        return 0;
    node = &cg->nodes[node_index];
    switch (node->kind) {
    case NC_NODE_LITERAL:
    case NC_NODE_UNARY:
    case NC_NODE_SIZEOF:
        *type_out = nc_type_int();
        return 1;
    case NC_NODE_CAST: {
        NcTypeSpec source_type;
        if (!nc_resolve_expr_type(cg, node->a, &source_type))
            return 0;
        if ((!nc_type_is_integer_scalar(source_type) && !nc_type_is_pointer(source_type)) ||
            (!nc_type_is_integer_scalar(node->type) && !nc_type_is_pointer(node->type))) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED,
                            "explicit cast requires supported integer/char/pointer scalar types");
            return 0;
        }
        *type_out = node->type;
        return 1;
    }
    case NC_NODE_COMMA:
        if (!nc_resolve_expr_type(cg, node->a, type_out))
            return 0;
        return nc_resolve_expr_type(cg, node->b, type_out);
    case NC_NODE_UPDATE: {
        NcTypeSpec target_type;
        if (!nc_resolve_expr_type(cg, node->a, &target_type))
            return 0;
        if (!nc_type_is_integer_scalar(target_type) && !nc_type_is_pointer(target_type)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED,
                            "++/-- requires an integer scalar or supported pointer lvalue");
            return 0;
        }
        *type_out = target_type.kind == NC_TYPE_CHAR ? nc_type_int() : target_type;
        return 1;
    }
    case NC_NODE_STRING:
        *type_out = nc_type_char_ptr();
        return 1;
    case NC_NODE_BINARY: {

        NcTypeSpec left_type;
        NcTypeSpec right_type;
        NcTypeSpec left_ptr;
        NcTypeSpec right_ptr;
        if (!nc_resolve_expr_type(cg, node->a, &left_type) ||
            !nc_resolve_expr_type(cg, node->b, &right_type))
            return 0;
        if ((node->op >= NC_BIN_EQ && node->op <= NC_BIN_GE) || node->op == NC_BIN_LOGICAL_AND ||
            node->op == NC_BIN_LOGICAL_OR) {
            *type_out = nc_type_int();
            return 1;
        }
        if (node->op == NC_BIN_SHIFT_LEFT || node->op == NC_BIN_SHIFT_RIGHT ||
            node->op == NC_BIN_BIT_AND || node->op == NC_BIN_BIT_XOR || node->op == NC_BIN_BIT_OR) {
            if (!nc_type_is_integer_scalar(left_type) || !nc_type_is_integer_scalar(right_type)) {
                nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED,
                                "bitwise/shift operators require integer operands");
                return 0;
            }
            *type_out = nc_type_int();
            return 1;
        }
        if (node->op == NC_BIN_MUL || node->op == NC_BIN_DIV) {
            if (!nc_type_is_integer_scalar(left_type) || !nc_type_is_integer_scalar(right_type)) {
                nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                                "multiplication/division require integer operands");
                return 0;
            }
            *type_out = nc_type_int();
            return 1;
        }
        if ((node->op == NC_BIN_ADD || node->op == NC_BIN_SUB) &&
            nc_decay_pointer_type(left_type, &left_ptr) && nc_type_is_integer_scalar(right_type)) {
            *type_out = left_ptr;
            return 1;
        }
        if (node->op == NC_BIN_ADD && nc_type_is_integer_scalar(left_type) &&
            nc_decay_pointer_type(right_type, &right_ptr)) {
            *type_out = right_ptr;
            return 1;
        }
        if (nc_type_is_integer_scalar(left_type) && nc_type_is_integer_scalar(right_type)) {
            *type_out = nc_type_int();
            return 1;
        }
        nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                        "unsupported pointer arithmetic expression");
        return 0;
    }
    case NC_NODE_TERNARY: {
        NcTypeSpec condition_type;
        NcTypeSpec true_type;
        NcTypeSpec false_type;
        NcTypeSpec true_ptr;
        NcTypeSpec false_ptr;
        if (!nc_resolve_expr_type(cg, node->a, &condition_type) ||
            !nc_resolve_expr_type(cg, node->b, &true_type) ||
            !nc_resolve_expr_type(cg, node->c, &false_type))
            return 0;
        if (!nc_type_is_integer_scalar(condition_type) && !nc_type_is_pointer(condition_type)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED, "ternary condition must be scalar");
            return 0;
        }
        if (nc_type_is_integer_scalar(true_type) && nc_type_is_integer_scalar(false_type)) {
            *type_out = nc_type_int();
            return 1;
        }
        if (nc_decay_pointer_type(true_type, &true_ptr) &&
            nc_decay_pointer_type(false_type, &false_ptr) && nc_type_equal(true_ptr, false_ptr)) {
            *type_out = true_ptr;
            return 1;
        }
        nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED,
                        "ternary branches must have compatible scalar types");
        return 0;
    }
    case NC_NODE_CALL:
        *type_out = node->type;
        return 1;
    case NC_NODE_VARIABLE: {
        int symbol_index = nc_find_symbol(cg, node->name);
        if (symbol_index >= 0) {
            *type_out = cg->symbols[symbol_index].type;
            return 1;
        }
        {
            int global_index = nc_find_global(cg, node->name);
            if (global_index >= 0) {
                *type_out = cg->globals[(uint32_t)global_index].type;
                return 1;
            }
        }
        if (nc_find_constant(cg, node->name) >= 0) {
            *type_out = nc_type_int();
            return 1;
        }
        nc_codegen_fail(cg, node, NOVAC_ERR_UNKNOWN_SYMBOL,
                        "unknown local/parameter/global/enum symbol");
        return 0;
    }
    case NC_NODE_ADDRESS: {
        NcTypeSpec value_type;
        const NcNode *target = node->a != NOVAC_INVALID_NODE ? &cg->nodes[node->a] : NULL;
        if (target == NULL || !nc_node_is_assignable(target)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "address-of requires an addressable lvalue");
            return 0;
        }
        if (!nc_resolve_expr_type(cg, node->a, &value_type))
            return 0;
        if (!nc_address_type(value_type, type_out)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "address-of currently supports int and struct objects; "
                            "pointer-to-pointer/array-pointer types are deferred");
            return 0;
        }
        return 1;
    }
    case NC_NODE_DEREF: {
        NcTypeSpec pointer_type;
        if (!nc_resolve_expr_type(cg, node->a, &pointer_type))
            return 0;
        if (!nc_pointer_target_type(pointer_type, type_out)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "dereference requires a supported scalar/aggregate pointer");
            return 0;
        }
        return 1;
    }
    case NC_NODE_INDEX: {
        NcTypeSpec base_type;
        if (!nc_resolve_expr_type(cg, node->a, &base_type))
            return 0;
        if (base_type.kind == NC_TYPE_ARRAY) {
            *type_out = nc_array_element_type(base_type);
            return 1;
        }
        if (nc_pointer_target_type(base_type, type_out))
            return 1;
        nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                        "index operator requires an array or supported pointer");
        return 0;
    }
    case NC_NODE_MEMBER:
    case NC_NODE_PTR_MEMBER: {
        NcTypeSpec base_type;
        NcTypeSpec object_type;
        const NcStructDef *def;
        uint32_t struct_index;
        int field_index;
        if (!nc_resolve_expr_type(cg, node->a, &base_type))
            return 0;
        if (node->kind == NC_NODE_MEMBER) {
            if (base_type.kind != NC_TYPE_STRUCT && base_type.kind != NC_TYPE_UNION) {
                nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_MEMBER,
                                "'.' requires a struct/union value");
                return 0;
            }
            struct_index = base_type.struct_index;
            object_type = base_type;
        } else {
            if (base_type.kind != NC_TYPE_STRUCT_PTR && base_type.kind != NC_TYPE_UNION_PTR) {
                nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_MEMBER,
                                "'->' requires a struct/union pointer");
                return 0;
            }
            struct_index = base_type.struct_index;
            if (!nc_pointer_target_type(base_type, &object_type)) {
                nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_MEMBER,
                                "cannot resolve aggregate pointer target type");
                return 0;
            }
        }
        if (struct_index >= cg->struct_count) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_MEMBER, "aggregate type index is invalid");
            return 0;
        }
        def = &cg->structs[struct_index];
        field_index = nc_struct_field_index(def, node->name);
        if (field_index < 0) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_MEMBER, "unknown aggregate member");
            return 0;
        }
        *type_out = def->fields[field_index].type;
        if ((object_type.qualifiers & NC_TYPE_QUAL_CONST) != 0u)
            type_out->qualifiers |= NC_TYPE_QUAL_CONST;
        return 1;
    }
    default:
        nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "cannot resolve expression type");
        return 0;
    }
}
static int nc_type_is_const_object(NcTypeSpec type) {
    return (type.qualifiers & NC_TYPE_QUAL_CONST) != 0u;
}
static int nc_require_writable_lvalue(NcCodegen *cg, const NcNode *diagnostic_node,
                                      uint32_t target_index, NcTypeSpec *type_out) {
    NcTypeSpec type;
    if (!nc_resolve_expr_type(cg, target_index, &type))
        return 0;
    if (nc_type_is_const_object(type)) {
        nc_codegen_fail(cg, diagnostic_node, NOVAC_ERR_CONST_WRITE,
                        "cannot modify a const-qualified object");
        return 0;
    }
    if (type_out != NULL)
        *type_out = type;
    return 1;
}
static int nc_emit_add_immediate(NcCodegen *cg, const NcNode *node, int base_reg, uint32_t value) {
    int offset_reg;
    if (value == 0u)
        return base_reg;
    offset_reg = nc_alloc_temp(cg, node);
    if (offset_reg < 0) {
        nc_free_temp(cg, base_reg);
        return -1;
    }
    nc_emit_mov_ri(cg, (uint8_t)offset_reg, value);

    nc_emit_u8(cg, NOVA_OP_ADD_RR);
    nc_emit_u8(cg, (uint8_t)base_reg);
    nc_emit_u8(cg, (uint8_t)base_reg);
    nc_emit_u8(cg, (uint8_t)offset_reg);
    nc_free_temp(cg, offset_reg);
    return base_reg;
}
static int nc_emit_lvalue_address(NcCodegen *cg, uint32_t node_index, NcTypeSpec *type_out) {
    const NcNode *node;
    if (node_index == NOVAC_INVALID_NODE || cg->status != NOVAC_OK)
        return -1;
    node = &cg->nodes[node_index];
    if (node->kind == NC_NODE_VARIABLE) {
        int symbol_index = nc_find_symbol(cg, node->name);
        int reg;
        if (symbol_index >= 0) {
            NcSymbol *symbol = &cg->symbols[(uint32_t)symbol_index];
            if (!nc_type_is_aggregate(symbol->type) && symbol->memory_backed == 0u) {
                if (!nc_promote_scalar_symbol(cg, node, (uint32_t)symbol_index))
                    return -1;
            }
            reg = nc_alloc_temp(cg, node);
            if (reg >= 0)
                nc_emit_mov_rr(cg, (uint8_t)reg, symbol->reg);
            if (type_out != NULL)
                *type_out = symbol->type;
            return reg;
        }
        {
            int global_index = nc_find_global(cg, node->name);
            if (global_index >= 0) {
                const NcGlobal *global = &cg->globals[(uint32_t)global_index];
                reg = nc_alloc_temp(cg, node);
                if (reg >= 0)
                    nc_emit_mov_ri(cg, (uint8_t)reg, global->address);
                if (type_out != NULL)
                    *type_out = global->type;
                return reg;
            }
        }
        nc_codegen_fail(cg, node, NOVAC_ERR_UNKNOWN_SYMBOL,
                        "unknown local/parameter/global symbol");
        return -1;
    }
    if (node->kind == NC_NODE_DEREF) {
        NcTypeSpec pointer_type;
        NcTypeSpec target_type;
        int address_reg;
        if (!nc_resolve_expr_type(cg, node->a, &pointer_type) ||
            !nc_pointer_target_type(pointer_type, &target_type)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "dereference requires a supported scalar/aggregate pointer");
            return -1;
        }
        address_reg = nc_emit_expr(cg, node->a);
        if (type_out != NULL)
            *type_out = target_type;
        return address_reg;
    }
    if (node->kind == NC_NODE_INDEX) {
        NcTypeSpec base_type;
        NcTypeSpec element_type;
        int base_reg;
        int index_reg;
        uint32_t element_size;
        if (!nc_resolve_expr_type(cg, node->a, &base_type))
            return -1;
        if (base_type.kind == NC_TYPE_ARRAY) {
            element_type = nc_array_element_type(base_type);
            if (cg->nodes[node->b].kind == NC_NODE_LITERAL &&
                cg->nodes[node->b].value >= base_type.array_length) {

                nc_codegen_fail(cg, node, NOVAC_ERR_ARRAY_BOUNDS,
                                "constant array index is outside the declared bound");
                return -1;
            }
        } else if (!nc_pointer_target_type(base_type, &element_type)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "index operator requires an array or supported pointer");
            return -1;
        }
        element_size = nc_type_size(cg, element_type);
        if (element_size == 0u) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "indexed element has invalid size");
            return -1;
        }
        if (base_type.kind == NC_TYPE_ARRAY)
            base_reg = nc_emit_lvalue_address(cg, node->a, NULL);
        else
            base_reg = nc_emit_expr(cg, node->a);
        if (base_reg < 0)
            return -1;
        index_reg = nc_emit_expr(cg, node->b);
        if (index_reg < 0) {
            nc_free_temp(cg, base_reg);
            return -1;
        }
        if (element_size != 1u) {
            int size_reg = nc_alloc_temp(cg, node);
            if (size_reg < 0) {
                nc_free_temp(cg, index_reg);
                nc_free_temp(cg, base_reg);
                return -1;
            }
            nc_emit_mov_ri(cg, (uint8_t)size_reg, element_size);
            nc_emit_u8(cg, NOVA_OP_MUL_RR);
            nc_emit_u8(cg, (uint8_t)index_reg);
            nc_emit_u8(cg, (uint8_t)index_reg);
            nc_emit_u8(cg, (uint8_t)size_reg);
            nc_free_temp(cg, size_reg);
        }
        nc_emit_u8(cg, NOVA_OP_ADD_RR);
        nc_emit_u8(cg, (uint8_t)base_reg);
        nc_emit_u8(cg, (uint8_t)base_reg);
        nc_emit_u8(cg, (uint8_t)index_reg);
        nc_free_temp(cg, index_reg);
        if (type_out != NULL)
            *type_out = element_type;
        return base_reg;
    }
    if (node->kind == NC_NODE_MEMBER || node->kind == NC_NODE_PTR_MEMBER) {
        NcTypeSpec base_type;
        const NcStructDef *def;
        uint32_t struct_index;
        int field_index;
        int base_reg;
        if (!nc_resolve_expr_type(cg, node->a, &base_type))
            return -1;
        if (node->kind == NC_NODE_MEMBER) {
            if (base_type.kind != NC_TYPE_STRUCT && base_type.kind != NC_TYPE_UNION) {
                nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_MEMBER,
                                "'.' requires a struct/union value");
                return -1;
            }
            struct_index = base_type.struct_index;
            base_reg = nc_emit_lvalue_address(cg, node->a, NULL);
        } else {
            if (base_type.kind != NC_TYPE_STRUCT_PTR) {

                nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_MEMBER,
                                "'->' requires a struct/union pointer");
                return -1;
            }
            struct_index = base_type.struct_index;
            base_reg = nc_emit_expr(cg, node->a);
        }
        if (struct_index >= cg->struct_count || base_reg < 0)
            return -1;
        def = &cg->structs[struct_index];
        field_index = nc_struct_field_index(def, node->name);
        if (field_index < 0) {
            nc_free_temp(cg, base_reg);
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_MEMBER, "unknown aggregate member");
            return -1;
        }
        base_reg = nc_emit_add_immediate(cg, node, base_reg, def->fields[field_index].byte_offset);
        if (base_reg < 0)
            return -1;
        if (type_out != NULL)
            *type_out = def->fields[field_index].type;
        return base_reg;
    }
    nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER, "expression is not an addressable lvalue");
    return -1;
}
static void nc_close_symbol_lifetime(NcCodegen *cg, const NcSymbol *symbol) {
    uint32_t i;
    if (cg->debug_symbols == NULL)
        return;
    for (i = 0u; i < symbol->debug_entry_count; i++) {
        uint32_t index = symbol->debug_entry_index + i;
        if (index < cg->debug_symbol_count)
            cg->debug_symbols[index].lifetime_end = cg->size;
    }
}
static void nc_emit_runtime_cleanup_from(NcCodegen *cg, uint32_t from_symbol) {
    uint32_t i = cg->symbol_count;
    while (i > from_symbol) {
        NcSymbol *symbol = &cg->symbols[i - 1u];
        if (symbol->owns_storage != 0u) {
            nc_emit_u8(cg, NOVA_OP_FREE);
            nc_emit_u8(cg, symbol->reg);
        }
        i--;
    }
}
static NcBreakContext *nc_push_break_context(NcCodegen *cg, uint32_t preserve_symbol_count) {
    NcBreakContext *context;
    if (cg->break_depth >= NOVAC_MAX_BREAK_DEPTH) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_SYMBOL_CAPACITY,
                        "breakable nesting exceeds fixed compiler capacity");
        return NULL;
    }
    context = &cg->break_stack[cg->break_depth++];
    context->break_patch_start = cg->break_patch_count;
    context->preserve_symbol_count = preserve_symbol_count;
    return context;
}
static void nc_finish_break_context(NcCodegen *cg, NcBreakContext *context, uint32_t target) {
    uint32_t i;
    if (context == NULL)
        return;
    for (i = context->break_patch_start; i < cg->break_patch_count; i++)

        nc_patch_u32(cg, cg->break_patches[i], target);
    cg->break_patch_count = context->break_patch_start;
    if (cg->break_depth > 0u)
        cg->break_depth--;
}
static NcLoopContext *nc_push_loop(NcCodegen *cg, uint32_t preserve_symbol_count) {
    NcLoopContext *loop;
    if (cg->loop_depth >= NOVAC_MAX_LOOP_DEPTH) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_SYMBOL_CAPACITY,
                        "loop nesting exceeds fixed compiler capacity");
        return NULL;
    }
    if (nc_push_break_context(cg, preserve_symbol_count) == NULL)
        return NULL;
    loop = &cg->loop_stack[cg->loop_depth++];
    loop->continue_patch_start = cg->continue_patch_count;
    loop->preserve_symbol_count = preserve_symbol_count;
    return loop;
}
static int nc_add_break_jump_patch(NcCodegen *cg, uint32_t patch) {
    if (cg->break_patch_count >= NOVAC_MAX_LOOP_PATCHES) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_OUTPUT_CAPACITY, "break patch capacity exceeded");
        return 0;
    }
    cg->break_patches[cg->break_patch_count++] = patch;
    return 1;
}
static int nc_add_continue_jump_patch(NcCodegen *cg, uint32_t patch) {
    if (cg->continue_patch_count >= NOVAC_MAX_LOOP_PATCHES) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_OUTPUT_CAPACITY, "continue patch capacity exceeded");
        return 0;
    }
    cg->continue_patches[cg->continue_patch_count++] = patch;
    return 1;
}
static void nc_finish_loop(NcCodegen *cg, NcLoopContext *loop, uint32_t break_target,
                           uint32_t continue_target) {
    uint32_t i;
    NcBreakContext *break_context =
        cg->break_depth > 0u ? &cg->break_stack[cg->break_depth - 1u] : NULL;
    if (loop == NULL)
        return;
    for (i = loop->continue_patch_start; i < cg->continue_patch_count; i++)
        nc_patch_u32(cg, cg->continue_patches[i], continue_target);
    cg->continue_patch_count = loop->continue_patch_start;
    nc_finish_break_context(cg, break_context, break_target);
    if (cg->loop_depth > 0u)
        cg->loop_depth--;
}
static void nc_emit_scope_cleanup(NcCodegen *cg, uint32_t from_symbol) {
    uint32_t i = cg->symbol_count;
    while (i > from_symbol) {
        NcSymbol *symbol = &cg->symbols[i - 1u];
        if (symbol->owns_storage != 0u) {
            nc_emit_u8(cg, NOVA_OP_FREE);
            nc_emit_u8(cg, symbol->reg);
        }
        nc_close_symbol_lifetime(cg, symbol);
        i--;
    }
}

static void nc_emit_aggregate_cleanup(NcCodegen *cg) {
    uint32_t i;
    for (i = 0u; i < cg->symbol_count; i++) {
        if (cg->symbols[i].owns_storage != 0u) {
            nc_emit_u8(cg, NOVA_OP_FREE);
            nc_emit_u8(cg, cg->symbols[i].reg);
        }
    }
}
static int nc_emit_allocate_aggregate(NcCodegen *cg, const NcNode *node, NcSymbol *symbol) {
    uint32_t byte_size = nc_type_size(cg, symbol->type);
    int temp;
    if (byte_size == 0u) {
        nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "aggregate has invalid byte size");
        return 0;
    }
    temp = nc_alloc_temp(cg, node);
    if (temp < 0)
        return 0;
    nc_emit_mov_ri(cg, (uint8_t)temp, byte_size);
    nc_emit_u8(cg, NOVA_OP_ALLOC);
    nc_emit_u8(cg, (uint8_t)temp);
    nc_emit_u8(cg, (uint8_t)temp);
    nc_emit_mov_rr(cg, symbol->reg, (uint8_t)temp);
    symbol->owns_storage = 1u;
    nc_free_temp(cg, temp);
    return cg->status == NOVAC_OK;
}
static int nc_prepare_scaled_delta(NcCodegen *cg, const NcNode *node, NcTypeSpec target_type,
                                   int delta_reg) {
    if (nc_type_is_pointer(target_type)) {
        uint32_t scale = nc_pointer_element_size(cg, target_type);
        if (scale == 0u) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "pointer update has invalid element size");
            return 0;
        }
        if (scale != 1u) {
            int scale_reg = nc_alloc_temp(cg, node);
            if (scale_reg < 0)
                return 0;
            nc_emit_mov_ri(cg, (uint8_t)scale_reg, scale);
            nc_emit_u8(cg, NOVA_OP_MUL_RR);
            nc_emit_u8(cg, (uint8_t)delta_reg);
            nc_emit_u8(cg, (uint8_t)delta_reg);
            nc_emit_u8(cg, (uint8_t)scale_reg);
            nc_free_temp(cg, scale_reg);
        }
    }
    return cg->status == NOVAC_OK;
}
static int nc_emit_rmw(NcCodegen *cg, const NcNode *node, uint32_t target_index, uint32_t op,
                       int rhs_reg, int postfix) {
    const NcNode *target = &cg->nodes[target_index];
    NcTypeSpec target_type;
    int root_global;
    if (!nc_require_writable_lvalue(cg, node, target_index, &target_type))
        return -1;
    if (!nc_type_is_integer_scalar(target_type) && !nc_type_is_pointer(target_type)) {
        nc_codegen_fail(
            cg, node, NOVAC_ERR_UNSUPPORTED,
            "update/compound assignment requires scalar integer or supported pointer target");
        return -1;
    }
    if (nc_type_is_pointer(target_type) && !nc_prepare_scaled_delta(cg, node, target_type, rhs_reg))
        return -1;

    if (!nc_type_is_pointer(target_type) && !nc_type_is_integer_scalar(target_type))
        return -1;
    root_global = nc_lvalue_root_global(cg, target_index);
    if (root_global >= 0) {
        const NcGlobal *global = &cg->globals[(uint32_t)root_global];
        (void)nc_publish_reference(cg, target, NOVAC_REF_GLOBAL_READ, global->name,
                                   (uint32_t)root_global, global->file_id, cg->size);
        (void)nc_publish_reference(cg, target, NOVAC_REF_GLOBAL_WRITE, global->name,
                                   (uint32_t)root_global, global->file_id, cg->size);
    }
    if (target->kind == NC_NODE_VARIABLE) {
        int symbol_index = nc_find_symbol(cg, target->name);
        if (symbol_index >= 0 && cg->symbols[(uint32_t)symbol_index].memory_backed == 0u) {
            NcSymbol *symbol = &cg->symbols[(uint32_t)symbol_index];
            int result_reg = nc_alloc_temp(cg, node);
            if (result_reg < 0)
                return -1;
            if (postfix)
                nc_emit_mov_rr(cg, (uint8_t)result_reg, symbol->reg);
            nc_emit_u8(cg, op == NC_BIN_SUB ? NOVA_OP_SUB_RR : NOVA_OP_ADD_RR);
            nc_emit_u8(cg, symbol->reg);
            nc_emit_u8(cg, symbol->reg);
            nc_emit_u8(cg, (uint8_t)rhs_reg);
            if (!postfix)
                nc_emit_mov_rr(cg, (uint8_t)result_reg, symbol->reg);
            return result_reg;
        }
    }
    {
        NcTypeSpec address_type;
        int address_reg = nc_emit_lvalue_address(cg, target_index, &address_type);
        int value_reg;
        int result_reg;
        if (address_reg < 0)
            return -1;
        value_reg = nc_alloc_temp(cg, node);
        result_reg = nc_alloc_temp(cg, node);
        if (value_reg < 0 || result_reg < 0) {
            if (value_reg >= 0)
                nc_free_temp(cg, value_reg);
            if (result_reg >= 0)
                nc_free_temp(cg, result_reg);
            nc_free_temp(cg, address_reg);
            return -1;
        }
        nc_emit_mov_rr(cg, (uint8_t)value_reg, (uint8_t)address_reg);
        nc_emit_scalar_load(cg, value_reg, address_type);
        if (postfix)
            nc_emit_mov_rr(cg, (uint8_t)result_reg, (uint8_t)value_reg);
        nc_emit_u8(cg, op == NC_BIN_SUB ? NOVA_OP_SUB_RR : NOVA_OP_ADD_RR);
        nc_emit_u8(cg, (uint8_t)value_reg);
        nc_emit_u8(cg, (uint8_t)value_reg);
        nc_emit_u8(cg, (uint8_t)rhs_reg);
        nc_emit_scalar_store(cg, address_reg, value_reg, address_type);
        if (!postfix)
            nc_emit_mov_rr(cg, (uint8_t)result_reg, (uint8_t)value_reg);
        nc_free_temp(cg, value_reg);
        nc_free_temp(cg, address_reg);
        return result_reg;
    }
}
static int nc_emit_update_expr(NcCodegen *cg, const NcNode *node) {
    int delta_reg = nc_alloc_temp(cg, node);
    int result;
    if (delta_reg < 0)
        return -1;

    nc_emit_mov_ri(cg, (uint8_t)delta_reg, 1u);
    result =
        nc_emit_rmw(cg, node, node->a, node->op == NC_UPDATE_DECREMENT ? NC_BIN_SUB : NC_BIN_ADD,
                    delta_reg, node->value != 0u);
    nc_free_temp(cg, delta_reg);
    return result;
}
static int nc_emit_logical_expr(NcCodegen *cg, const NcNode *node) {
    int result_reg = nc_alloc_temp(cg, node);
    int left;
    int right;
    uint32_t first_patch;
    uint32_t second_patch;
    uint32_t end_patch;
    if (result_reg < 0)
        return -1;
    left = nc_emit_expr(cg, node->a);
    if (left < 0) {
        nc_free_temp(cg, result_reg);
        return -1;
    }
    nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, 0u);
    nc_emit_u8(cg, NOVA_OP_CMP_RR);
    nc_emit_u8(cg, (uint8_t)left);
    nc_emit_u8(cg, NOVAC_SCRATCH_REGISTER);
    nc_free_temp(cg, left);
    first_patch =
        nc_emit_jump_placeholder(cg, node->op == NC_BIN_LOGICAL_AND ? NOVA_OP_JE : NOVA_OP_JNE);
    right = nc_emit_expr(cg, node->b);
    if (right < 0) {
        nc_free_temp(cg, result_reg);
        return -1;
    }
    nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, 0u);
    nc_emit_u8(cg, NOVA_OP_CMP_RR);
    nc_emit_u8(cg, (uint8_t)right);
    nc_emit_u8(cg, NOVAC_SCRATCH_REGISTER);
    nc_free_temp(cg, right);
    second_patch =
        nc_emit_jump_placeholder(cg, node->op == NC_BIN_LOGICAL_AND ? NOVA_OP_JE : NOVA_OP_JNE);
    if (node->op == NC_BIN_LOGICAL_AND) {
        nc_emit_mov_ri(cg, (uint8_t)result_reg, 1u);
        end_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
        nc_patch_u32(cg, first_patch, cg->size);
        nc_patch_u32(cg, second_patch, cg->size);
        nc_emit_mov_ri(cg, (uint8_t)result_reg, 0u);
    } else {
        nc_emit_mov_ri(cg, (uint8_t)result_reg, 0u);
        end_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
        nc_patch_u32(cg, first_patch, cg->size);
        nc_patch_u32(cg, second_patch, cg->size);
        nc_emit_mov_ri(cg, (uint8_t)result_reg, 1u);
    }
    nc_patch_u32(cg, end_patch, cg->size);
    return result_reg;
}
static int nc_emit_expr(NcCodegen *cg, uint32_t node_index) {
    const NcNode *node;
    int reg;
    if (node_index == NOVAC_INVALID_NODE || cg->status != NOVAC_OK)
        return -1;
    node = &cg->nodes[node_index];
    switch (node->kind) {
    case NC_NODE_LITERAL:
        reg = nc_alloc_temp(cg, node);
        if (reg >= 0)
            nc_emit_mov_ri(cg, (uint8_t)reg, node->value);

        return reg;
    case NC_NODE_STRING:
        if (node->value >= cg->string_count) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "string literal index is invalid");
            return -1;
        }
        reg = nc_alloc_temp(cg, node);
        if (reg >= 0)
            nc_emit_mov_ri(cg, (uint8_t)reg, cg->strings[node->value].address);
        return reg;
    case NC_NODE_VARIABLE: {
        int symbol_index = nc_find_symbol(cg, node->name);
        if (symbol_index >= 0) {
            const NcSymbol *symbol = &cg->symbols[(uint32_t)symbol_index];
            reg = nc_alloc_temp(cg, node);
            if (reg < 0)
                return -1;
            nc_emit_mov_rr(cg, (uint8_t)reg, symbol->reg);
            if (symbol->memory_backed != 0u)
                nc_emit_scalar_load(cg, reg, symbol->type);
            return reg;
        }
        {
            int global_index = nc_find_global(cg, node->name);
            if (global_index >= 0) {
                const NcGlobal *global = &cg->globals[(uint32_t)global_index];
                uint32_t reference_offset = cg->size;
                reg = nc_alloc_temp(cg, node);
                if (reg < 0)
                    return -1;
                nc_emit_mov_ri(cg, (uint8_t)reg, global->address);
                if (!nc_type_is_aggregate(global->type))
                    nc_emit_scalar_load(cg, reg, global->type);
                (void)nc_publish_reference(cg, node, NOVAC_REF_GLOBAL_READ, global->name,
                                           (uint32_t)global_index, global->file_id,
                                           reference_offset);
                return reg;
            }
        }
        {
            int constant_index = nc_find_constant(cg, node->name);
            if (constant_index >= 0) {
                reg = nc_alloc_temp(cg, node);
                if (reg >= 0)
                    nc_emit_mov_ri(cg, (uint8_t)reg, cg->constants[(uint32_t)constant_index].value);
                return reg;
            }
        }
        nc_codegen_fail(cg, node, NOVAC_ERR_UNKNOWN_SYMBOL,
                        "unknown local/parameter/global/enum symbol");
        return -1;
    }
    case NC_NODE_DEREF:
    case NC_NODE_INDEX:
    case NC_NODE_MEMBER:
    case NC_NODE_PTR_MEMBER: {
        NcTypeSpec value_type;
        int root_global = nc_lvalue_root_global(cg, node_index);
        if (root_global >= 0) {
            const NcGlobal *global = &cg->globals[(uint32_t)root_global];

            (void)nc_publish_reference(cg, node, NOVAC_REF_GLOBAL_READ, global->name,
                                       (uint32_t)root_global, global->file_id, cg->size);
        }
        int address_reg = nc_emit_lvalue_address(cg, node_index, &value_type);
        if (address_reg < 0)
            return -1;
        if (nc_type_is_aggregate(value_type))
            return address_reg;
        nc_emit_scalar_load(cg, address_reg, value_type);
        return address_reg;
    }
    case NC_NODE_ADDRESS: {
        NcTypeSpec value_type;
        int address_reg = nc_emit_lvalue_address(cg, node->a, &value_type);
        NcTypeSpec pointer_type;
        if (address_reg < 0)
            return -1;
        if (!nc_address_type(value_type, &pointer_type)) {
            nc_free_temp(cg, address_reg);
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "address-of target type requires unsupported pointer nesting");
            return -1;
        }
        return address_reg;
    }
    case NC_NODE_SIZEOF: {
        NcTypeSpec operand_type;
        uint32_t byte_size;
        if (node->value != 0u)
            operand_type = node->type;
        else if (!nc_resolve_expr_type(cg, node->a, &operand_type))
            return -1;
        byte_size = nc_type_size(cg, operand_type);
        if (byte_size == 0u) {
            nc_codegen_fail(cg, node, NOVAC_ERR_SIZEOF,
                            "sizeof operand has incomplete/unsupported type");
            return -1;
        }
        reg = nc_alloc_temp(cg, node);
        if (reg >= 0)
            nc_emit_mov_ri(cg, (uint8_t)reg, byte_size);
        return reg;
    }
    case NC_NODE_UNARY: {
        int value_reg = nc_emit_expr(cg, node->a);
        int zero_reg;
        if (value_reg < 0)
            return -1;
        if (node->op == NC_UN_BIT_NOT) {
            int mask_reg = nc_alloc_temp(cg, node);
            if (mask_reg < 0) {
                nc_free_temp(cg, value_reg);
                return -1;
            }
            nc_emit_mov_ri(cg, (uint8_t)mask_reg, 0xFFFFFFFFu);
            nc_emit_u8(cg, NOVA_OP_XOR_RR);
            nc_emit_u8(cg, (uint8_t)value_reg);
            nc_emit_u8(cg, (uint8_t)value_reg);
            nc_emit_u8(cg, (uint8_t)mask_reg);
            nc_free_temp(cg, mask_reg);
            return value_reg;
        }
        zero_reg = nc_alloc_temp(cg, node);
        if (zero_reg < 0) {
            nc_free_temp(cg, value_reg);
            return -1;
        }
        nc_emit_mov_ri(cg, (uint8_t)zero_reg, 0u);

        if (node->op == NC_UN_NEGATE) {
            nc_emit_u8(cg, NOVA_OP_SUB_RR);
            nc_emit_u8(cg, (uint8_t)value_reg);
            nc_emit_u8(cg, (uint8_t)zero_reg);
            nc_emit_u8(cg, (uint8_t)value_reg);
            nc_free_temp(cg, zero_reg);
            return value_reg;
        }
        if (node->op == NC_UN_NOT) {
            uint32_t true_patch;
            uint32_t end_patch;
            nc_emit_u8(cg, NOVA_OP_CMP_RR);
            nc_emit_u8(cg, (uint8_t)value_reg);
            nc_emit_u8(cg, (uint8_t)zero_reg);
            nc_free_temp(cg, zero_reg);
            true_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JE);
            nc_emit_mov_ri(cg, (uint8_t)value_reg, 0u);
            end_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
            nc_patch_u32(cg, true_patch, cg->size);
            nc_emit_mov_ri(cg, (uint8_t)value_reg, 1u);
            nc_patch_u32(cg, end_patch, cg->size);
            return value_reg;
        }
        nc_free_temp(cg, zero_reg);
        nc_free_temp(cg, value_reg);
        nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "invalid unary operator");
        return -1;
    }
    case NC_NODE_BINARY: {
        NcTypeSpec left_type;
        NcTypeSpec right_type;
        NcTypeSpec left_ptr;
        NcTypeSpec right_ptr;
        int left;
        int right;
        if (node->op == NC_BIN_LOGICAL_AND || node->op == NC_BIN_LOGICAL_OR)
            return nc_emit_logical_expr(cg, node);
        if (!nc_resolve_expr_type(cg, node->a, &left_type) ||
            !nc_resolve_expr_type(cg, node->b, &right_type))
            return -1;
        left = nc_emit_expr(cg, node->a);
        if (left < 0)
            return -1;
        right = nc_emit_expr(cg, node->b);
        if (right < 0) {
            nc_free_temp(cg, left);
            return -1;
        }
        if (node->op >= NC_BIN_EQ && node->op <= NC_BIN_GE)
            return nc_emit_comparison(cg, node, left, right, (NcBinaryOp)node->op);
        if ((node->op == NC_BIN_ADD || node->op == NC_BIN_SUB) &&
            nc_decay_pointer_type(left_type, &left_ptr) && nc_type_is_integer_scalar(right_type)) {
            uint32_t scale = nc_pointer_element_size(cg, left_ptr);
            if (scale == 0u) {
                nc_free_temp(cg, right);
                nc_free_temp(cg, left);
                nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                                "pointer arithmetic has invalid element size");
                return -1;
            }

            if (scale != 1u) {
                int scale_reg = nc_alloc_temp(cg, node);
                if (scale_reg < 0) {
                    nc_free_temp(cg, right);
                    nc_free_temp(cg, left);
                    return -1;
                }
                nc_emit_mov_ri(cg, (uint8_t)scale_reg, scale);
                nc_emit_u8(cg, NOVA_OP_MUL_RR);
                nc_emit_u8(cg, (uint8_t)right);
                nc_emit_u8(cg, (uint8_t)right);
                nc_emit_u8(cg, (uint8_t)scale_reg);
                nc_free_temp(cg, scale_reg);
            }
            nc_emit_u8(cg, node->op == NC_BIN_ADD ? NOVA_OP_ADD_RR : NOVA_OP_SUB_RR);
            nc_emit_u8(cg, (uint8_t)left);
            nc_emit_u8(cg, (uint8_t)left);
            nc_emit_u8(cg, (uint8_t)right);
            nc_free_temp(cg, right);
            return left;
        }
        if (node->op == NC_BIN_ADD && nc_type_is_integer_scalar(left_type) &&
            nc_decay_pointer_type(right_type, &right_ptr)) {
            uint32_t scale = nc_pointer_element_size(cg, right_ptr);
            if (scale == 0u) {
                nc_free_temp(cg, right);
                nc_free_temp(cg, left);
                nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                                "pointer arithmetic has invalid element size");
                return -1;
            }
            if (scale != 1u) {
                int scale_reg = nc_alloc_temp(cg, node);
                if (scale_reg < 0) {
                    nc_free_temp(cg, right);
                    nc_free_temp(cg, left);
                    return -1;
                }
                nc_emit_mov_ri(cg, (uint8_t)scale_reg, scale);
                nc_emit_u8(cg, NOVA_OP_MUL_RR);
                nc_emit_u8(cg, (uint8_t)left);
                nc_emit_u8(cg, (uint8_t)left);
                nc_emit_u8(cg, (uint8_t)scale_reg);
                nc_free_temp(cg, scale_reg);
            }
            nc_emit_u8(cg, NOVA_OP_ADD_RR);
            nc_emit_u8(cg, (uint8_t)right);
            nc_emit_u8(cg, (uint8_t)right);
            nc_emit_u8(cg, (uint8_t)left);
            nc_free_temp(cg, left);
            return right;
        }
        if (!nc_type_is_integer_scalar(left_type) || !nc_type_is_integer_scalar(right_type)) {
            nc_free_temp(cg, right);
            nc_free_temp(cg, left);
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "unsupported pointer arithmetic expression");
            return -1;
        }
        nc_emit_u8(cg,

                   node->op == NC_BIN_ADD          ? NOVA_OP_ADD_RR
                   : node->op == NC_BIN_SUB        ? NOVA_OP_SUB_RR
                   : node->op == NC_BIN_MUL        ? NOVA_OP_MUL_RR
                   : node->op == NC_BIN_DIV        ? NOVA_OP_DIV_RR
                   : node->op == NC_BIN_BIT_AND    ? NOVA_OP_AND_RR
                   : node->op == NC_BIN_BIT_OR     ? NOVA_OP_OR_RR
                   : node->op == NC_BIN_BIT_XOR    ? NOVA_OP_XOR_RR
                   : node->op == NC_BIN_SHIFT_LEFT ? NOVA_OP_SHL_RR
                                                   : NOVA_OP_SHR_RR);
        nc_emit_u8(cg, (uint8_t)left);
        nc_emit_u8(cg, (uint8_t)left);
        nc_emit_u8(cg, (uint8_t)right);
        nc_free_temp(cg, right);
        return left;
    }
    case NC_NODE_TERNARY: {
        int result_reg;
        int condition_reg;
        int branch_reg;
        uint32_t false_patch;
        uint32_t end_patch;
        NcTypeSpec ignored_type;
        if (!nc_resolve_expr_type(cg, node_index, &ignored_type))
            return -1;
        result_reg = nc_alloc_temp(cg, node);
        if (result_reg < 0)
            return -1;
        condition_reg = nc_emit_expr(cg, node->a);
        if (condition_reg < 0) {
            nc_free_temp(cg, result_reg);
            return -1;
        }
        nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, 0u);
        nc_emit_u8(cg, NOVA_OP_CMP_RR);
        nc_emit_u8(cg, (uint8_t)condition_reg);
        nc_emit_u8(cg, NOVAC_SCRATCH_REGISTER);
        nc_free_temp(cg, condition_reg);
        false_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JE);
        branch_reg = nc_emit_expr(cg, node->b);
        if (branch_reg < 0) {
            nc_free_temp(cg, result_reg);
            return -1;
        }
        nc_emit_mov_rr(cg, (uint8_t)result_reg, (uint8_t)branch_reg);
        nc_free_temp(cg, branch_reg);
        end_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
        nc_patch_u32(cg, false_patch, cg->size);
        branch_reg = nc_emit_expr(cg, node->c);
        if (branch_reg < 0) {
            nc_free_temp(cg, result_reg);
            return -1;
        }
        nc_emit_mov_rr(cg, (uint8_t)result_reg, (uint8_t)branch_reg);
        nc_free_temp(cg, branch_reg);
        nc_patch_u32(cg, end_patch, cg->size);
        return result_reg;
    }
    case NC_NODE_COMMA: {
        int left_reg = nc_emit_expr(cg, node->a);
        if (left_reg < 0)
            return -1;
        nc_free_temp(cg, left_reg);
        return nc_emit_expr(cg, node->b);
    }
    case NC_NODE_CAST: {
        NcTypeSpec target_type;
        int value_reg;
        if (!nc_resolve_expr_type(cg, node_index, &target_type))
            return -1;
        value_reg = nc_emit_expr(cg, node->a);
        if (value_reg < 0)
            return -1;
        if (target_type.kind == NC_TYPE_CHAR) {
            int mask_reg = nc_alloc_temp(cg, node);
            if (mask_reg < 0) {
                nc_free_temp(cg, value_reg);
                return -1;
            }
            nc_emit_mov_ri(cg, (uint8_t)mask_reg, 0xFFu);
            nc_emit_u8(cg, NOVA_OP_AND_RR);
            nc_emit_u8(cg, (uint8_t)value_reg);
            nc_emit_u8(cg, (uint8_t)value_reg);
            nc_emit_u8(cg, (uint8_t)mask_reg);
            nc_free_temp(cg, mask_reg);
        }
        return value_reg;
    }
    case NC_NODE_UPDATE:
        return nc_emit_update_expr(cg, node);
    case NC_NODE_CALL: {
        int intrinsic = nc_emit_intrinsic_call(cg, node);
        int function_index;
        if (intrinsic != -2)
            return intrinsic;
        function_index = nc_find_function(cg, node->name);
        if (function_index < 0) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNKNOWN_SYMBOL, "call to unknown function");
            return -1;
        }
        return nc_emit_user_call(cg, node, (uint32_t)function_index);
    }
    default:
        nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "statement node used as expression");
        return -1;
    }
}
static void nc_emit_condition_jump_false(NcCodegen *cg, uint32_t condition, uint32_t *patch_out) {
    int reg = nc_emit_expr(cg, condition);
    if (reg < 0)
        return;
    nc_emit_mov_ri(cg, NOVAC_SCRATCH_REGISTER, 0u);
    nc_emit_u8(cg, NOVA_OP_CMP_RR);
    nc_emit_u8(cg, (uint8_t)reg);
    nc_emit_u8(cg, NOVAC_SCRATCH_REGISTER);
    nc_free_temp(cg, reg);
    *patch_out = nc_emit_jump_placeholder(cg, NOVA_OP_JE);
}
static uint32_t nc_source_kind(const NcNode *node) {
    if (node == NULL)
        return 0u;
    switch (node->kind) {
    case NC_NODE_VAR_DECL:
    case NC_NODE_TYPEDEF_DECL:
        return NOVAC_SOURCE_VAR_DECL;
    case NC_NODE_ASSIGN:
    case NC_NODE_COMPOUND_ASSIGN:
        return NOVAC_SOURCE_ASSIGN;
    case NC_NODE_IF:
        return NOVAC_SOURCE_IF;
    case NC_NODE_WHILE:
        return NOVAC_SOURCE_WHILE;
    case NC_NODE_DO_WHILE:
        return NOVAC_SOURCE_DO_WHILE;
    case NC_NODE_FOR:
        return NOVAC_SOURCE_FOR;
    case NC_NODE_BREAK:
        return NOVAC_SOURCE_BREAK;
    case NC_NODE_CONTINUE:
        return NOVAC_SOURCE_CONTINUE;
    case NC_NODE_LABEL:
        return NOVAC_SOURCE_LABEL;
    case NC_NODE_GOTO:
        return NOVAC_SOURCE_GOTO;
    case NC_NODE_SWITCH:
        return NOVAC_SOURCE_SWITCH;
    case NC_NODE_CASE:
        return node->op != 0u ? NOVAC_SOURCE_DEFAULT : NOVAC_SOURCE_CASE;
    case NC_NODE_RETURN:
        return NOVAC_SOURCE_RETURN;
    case NC_NODE_EXPR_STMT:
        return NOVAC_SOURCE_EXPR;
    default:
        return 0u;
    }
}
static uint32_t nc_debug_begin_statement(NcCodegen *cg, const NcNode *node) {
    NovaSourceMapEntry *entry;
    uint32_t kind = nc_source_kind(node);
    uint32_t index;
    if (kind == 0u || cg->source_map == NULL)
        return NOVAC_INVALID_NODE;
    if (cg->source_map_count >= cg->source_map_capacity) {
        nc_codegen_fail(cg, node, NOVAC_ERR_DEBUG_CAPACITY, "source-map buffer is full");
        return NOVAC_INVALID_NODE;
    }
    index = cg->source_map_count++;
    entry = &cg->source_map[index];
    entry->bytecode_start = cg->size;
    entry->bytecode_end = cg->size;
    entry->line = node->line;
    entry->column = node->column;
    entry->statement_kind = kind;
    entry->function_index = cg->current_function_index;
    entry->file_id = node->file_id;
    return index;
}
static void nc_debug_end_statement(NcCodegen *cg, uint32_t index) {

    if (index == NOVAC_INVALID_NODE || cg->source_map == NULL)
        return;
    if (index < cg->source_map_count)
        cg->source_map[index].bytecode_end = cg->size;
}
static int nc_emit_initializer_at_base(NcCodegen *cg, const NcNode *declaration, uint8_t base_reg,
                                       uint32_t byte_offset, NcTypeSpec target_type,
                                       uint32_t initializer_node) {
    const NcNode *init;
    if (initializer_node == NOVAC_INVALID_NODE)
        return 1;
    init = &cg->nodes[initializer_node];
    if (init->kind == NC_NODE_INIT_ITEM) {
        initializer_node = init->a;
        init = &cg->nodes[initializer_node];
    }
    if (nc_type_is_aggregate(target_type)) {
        if (target_type.kind == NC_TYPE_ARRAY && target_type.element_kind == NC_TYPE_CHAR &&
            init->kind == NC_NODE_STRING) {
            const NcStringLiteral *literal;
            uint32_t copy_count;
            uint32_t i;
            if (init->value >= cg->string_count)
                return 0;
            literal = &cg->strings[init->value];
            copy_count = literal->length <= target_type.array_length ? literal->length
                                                                     : target_type.array_length;
            if (literal->length > target_type.array_length + 1u) {
                nc_codegen_fail(cg, declaration, NOVAC_ERR_ARRAY_BOUNDS,
                                "string initializer does not fit destination char array");
                return 0;
            }
            for (i = 0u; i < copy_count; i++) {
                int address_reg = nc_alloc_temp(cg, declaration);
                int value_reg = nc_alloc_temp(cg, declaration);
                if (address_reg < 0 || value_reg < 0) {
                    if (address_reg >= 0)
                        nc_free_temp(cg, address_reg);
                    if (value_reg >= 0)
                        nc_free_temp(cg, value_reg);
                    return 0;
                }
                nc_emit_mov_rr(cg, (uint8_t)address_reg, base_reg);
                address_reg = nc_emit_add_immediate(cg, declaration, address_reg, byte_offset + i);
                if (address_reg < 0) {
                    nc_free_temp(cg, value_reg);
                    return 0;
                }
                nc_emit_mov_ri(cg, (uint8_t)value_reg, literal->bytes[i]);
                nc_emit_scalar_store(cg, address_reg, value_reg, nc_type_char());
                nc_free_temp(cg, value_reg);
                nc_free_temp(cg, address_reg);
            }
            return cg->status == NOVAC_OK;
        }
        if (init->kind != NC_NODE_INIT_LIST) {
            nc_codegen_fail(cg, declaration, NOVAC_ERR_UNSUPPORTED,
                            "aggregate initializer requires a brace list");
            return 0;
        }
        if (target_type.kind == NC_TYPE_ARRAY) {
            NcTypeSpec element = nc_array_element_type(target_type);
            uint32_t element_size = nc_type_size(cg, element);
            uint32_t next_index = 0u;
            uint32_t item_index = init->a;
            while (item_index != NOVAC_INVALID_NODE) {
                const NcNode *item = &cg->nodes[item_index];
                uint32_t index;
                if (item->kind != NC_NODE_INIT_ITEM) {
                    nc_codegen_fail(cg, declaration, NOVAC_ERR_INTERNAL,
                                    "initializer list item metadata is invalid");
                    return 0;
                }
                if (item->op == 1u) {
                    nc_codegen_fail(cg, item, NOVAC_ERR_INVALID_MEMBER,
                                    "field designator cannot initialize an array");
                    return 0;
                }
                if (item->op == 2u) {
                    index = item->value;
                    next_index = index + 1u;
                } else {
                    index = next_index++;
                }
                if (index >= target_type.array_length) {
                    nc_codegen_fail(cg, item, NOVAC_ERR_ARRAY_BOUNDS,
                                    "array initializer index is outside the declared bound");
                    return 0;
                }
                if (!nc_emit_initializer_at_base(cg, declaration, base_reg,
                                                 byte_offset + index * element_size, element,
                                                 item->a))
                    return 0;
                item_index = item->next;
            }
            return 1;
        }
        if (target_type.kind == NC_TYPE_STRUCT || target_type.kind == NC_TYPE_UNION) {
            const NcStructDef *def;
            uint32_t next_field = 0u;
            uint32_t item_index = init->a;
            uint32_t union_items = 0u;
            if (target_type.struct_index >= cg->struct_count)
                return 0;
            def = &cg->structs[target_type.struct_index];
            while (item_index != NOVAC_INVALID_NODE) {
                const NcNode *item = &cg->nodes[item_index];
                uint32_t field_index;
                const NcStructField *field;
                if (item->kind != NC_NODE_INIT_ITEM) {
                    nc_codegen_fail(cg, declaration, NOVAC_ERR_INTERNAL,
                                    "initializer list item metadata is invalid");
                    return 0;
                }
                if (item->op == 2u) {
                    nc_codegen_fail(cg, item, NOVAC_ERR_INVALID_MEMBER,
                                    "array index designator cannot initialize a struct/union");
                    return 0;
                }
                if (item->op == 1u) {
                    int found = nc_struct_field_index(def, item->name);
                    if (found < 0) {
                        nc_codegen_fail(cg, item, NOVAC_ERR_INVALID_MEMBER,
                                        "initializer designates an unknown aggregate field");
                        return 0;
                    }
                    field_index = (uint32_t)found;
                    next_field = field_index + 1u;
                } else {
                    field_index = target_type.kind == NC_TYPE_UNION ? 0u : next_field++;
                }
                if (field_index >= def->field_count) {
                    nc_codegen_fail(cg, item, NOVAC_ERR_INVALID_MEMBER,
                                    "too many aggregate initializer elements");
                    return 0;
                }
                if (target_type.kind == NC_TYPE_UNION && union_items++ != 0u) {
                    nc_codegen_fail(cg, item, NOVAC_ERR_INVALID_MEMBER,
                                    "bounded union initializer accepts exactly one active member");
                    return 0;
                }
                field = &def->fields[field_index];
                if (!nc_emit_initializer_at_base(cg, declaration, base_reg,
                                                 byte_offset + field->byte_offset, field->type,
                                                 item->a))
                    return 0;
                item_index = item->next;
            }
            return 1;
        }
    }
    if (init->kind == NC_NODE_INIT_LIST) {
        uint32_t item = init->a;
        if (item == NOVAC_INVALID_NODE || cg->nodes[item].next != NOVAC_INVALID_NODE ||
            cg->nodes[item].kind != NC_NODE_INIT_ITEM || cg->nodes[item].op != 0u) {
            nc_codegen_fail(cg, declaration, NOVAC_ERR_UNSUPPORTED,
                            "scalar brace initializer must contain exactly one positional value");
            return 0;
        }
        return nc_emit_initializer_at_base(cg, declaration, base_reg, byte_offset, target_type,
                                           cg->nodes[item].a);
    }
    {
        int address_reg = nc_alloc_temp(cg, declaration);
        int value_reg;
        if (address_reg < 0)
            return 0;
        nc_emit_mov_rr(cg, (uint8_t)address_reg, base_reg);
        address_reg = nc_emit_add_immediate(cg, declaration, address_reg, byte_offset);
        if (address_reg < 0)
            return 0;
        value_reg = nc_emit_expr(cg, initializer_node);
        if (value_reg < 0) {
            nc_free_temp(cg, address_reg);
            return 0;
        }
        nc_emit_scalar_store(cg, address_reg, value_reg, target_type);
        nc_free_temp(cg, value_reg);
        nc_free_temp(cg, address_reg);
        return cg->status == NOVAC_OK;
    }
}
static int nc_emit_aggregate_initializer(NcCodegen *cg, const NcNode *declaration, NcSymbol *symbol,
                                         uint32_t initializer_node) {
    return nc_emit_initializer_at_base(cg, declaration, symbol->reg, 0u, symbol->type,
                                       initializer_node);
}
static int nc_label_index(const NcCodegen *cg, const char *name) {
    uint32_t i;
    for (i = 0u; i < cg->label_count; i++) {
        if (nc_streq(cg->labels[i].name, name))
            return (int)i;
    }
    return -1;
}
static int nc_collect_labels(NcCodegen *cg, uint32_t node_index) {
    while (node_index != NOVAC_INVALID_NODE && cg->status == NOVAC_OK) {
        const NcNode *node = &cg->nodes[node_index];
        if (node->kind == NC_NODE_LABEL) {
            NcLabelInfo *info;
            if (nc_label_index(cg, node->name) >= 0) {
                nc_codegen_fail(cg, node, NOVAC_ERR_CONFLICTING_DECLARATION,
                                "duplicate label in function");
                return 0;
            }
            if (cg->label_count >= NOVAC_MAX_LABELS) {
                nc_codegen_fail(cg, node, NOVAC_ERR_SYMBOL_CAPACITY, "label capacity exceeded");
                return 0;
            }
            info = &cg->labels[cg->label_count++];
            nc_copy_text(info->name, NOVAC_NAME_CAPACITY, node->name,
                         (uint32_t)nc_strlen(node->name));
            info->node_index = node_index;
            info->scope_depth = node->scope_depth;
            info->epoch = node->epoch;
            info->address = 0u;
            info->is_emitted = 0u;
            if (!nc_collect_labels(cg, node->a))
                return 0;
        } else if (node->kind == NC_NODE_BLOCK) {
            if (!nc_collect_labels(cg, node->a))
                return 0;
        } else if (node->kind == NC_NODE_IF) {
            if (!nc_collect_labels(cg, node->b) || !nc_collect_labels(cg, node->c))
                return 0;
        } else if (node->kind == NC_NODE_WHILE) {
            if (!nc_collect_labels(cg, node->b))
                return 0;
        } else if (node->kind == NC_NODE_DO_WHILE) {
            if (!nc_collect_labels(cg, node->a))
                return 0;
        } else if (node->kind == NC_NODE_FOR) {
            if (!nc_collect_labels(cg, node->a) || !nc_collect_labels(cg, node->c) ||
                !nc_collect_labels(cg, node->value))
                return 0;
        } else if (node->kind == NC_NODE_SWITCH) {
            uint32_t case_index = node->b;
            while (case_index != NOVAC_INVALID_NODE) {
                if (!nc_collect_labels(cg, cg->nodes[case_index].b))
                    return 0;
                case_index = cg->nodes[case_index].next;
            }
        }
        node_index = node->next;
    }
    return cg->status == NOVAC_OK;
}
static void nc_emit_goto_cleanup(NcCodegen *cg, uint32_t target_scope, uint32_t target_epoch) {
    uint32_t i = cg->symbol_count;
    while (i > 0u) {
        NcSymbol *symbol = &cg->symbols[i - 1u];
        int leaves_scope = symbol->source_scope_depth > target_scope;
        int rewinds_same_scope =
            symbol->source_scope_depth == target_scope && symbol->declaration_epoch > target_epoch;
        if ((leaves_scope || rewinds_same_scope) && symbol->owns_storage != 0u) {
            nc_emit_u8(cg, NOVA_OP_FREE);
            nc_emit_u8(cg, symbol->reg);
        }
        i--;
    }
}
static int nc_add_goto_patch(NcCodegen *cg, uint32_t target_offset, uint32_t label_index) {
    if (cg->goto_patch_count >= NOVAC_MAX_GOTO_PATCHES) {
        nc_codegen_fail(cg, NULL, NOVAC_ERR_OUTPUT_CAPACITY, "goto patch capacity exceeded");
        return 0;
    }
    cg->goto_patches[cg->goto_patch_count].target_offset = target_offset;
    cg->goto_patches[cg->goto_patch_count].label_index = label_index;
    cg->goto_patch_count++;
    return 1;
}
static void nc_emit_label_target(NcCodegen *cg, const NcNode *node) {
    int index = nc_label_index(cg, node->name);
    uint32_t i;
    if (index < 0) {
        nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "label table is missing a parsed label");
        return;
    }
    cg->labels[(uint32_t)index].address = cg->size;
    cg->labels[(uint32_t)index].is_emitted = 1u;
    for (i = 0u; i < cg->goto_patch_count; i++) {
        if (cg->goto_patches[i].label_index == (uint32_t)index)
            nc_patch_u32(cg, cg->goto_patches[i].target_offset, cg->size);
    }
}
static void nc_emit_statement(NcCodegen *cg, uint32_t node_index) {
    const NcNode *node;
    uint32_t debug_index = NOVAC_INVALID_NODE;
    if (node_index == NOVAC_INVALID_NODE || cg->status != NOVAC_OK)
        return;
    node = &cg->nodes[node_index];
    if (node->kind != NC_NODE_BLOCK)
        debug_index = nc_debug_begin_statement(cg, node);
    if (cg->status != NOVAC_OK)
        return;
    switch (node->kind) {
    case NC_NODE_TYPEDEF_DECL:
        break;
    case NC_NODE_BLOCK: {
        uint32_t child = node->a;
        uint32_t saved_symbol_count = cg->symbol_count;
        cg->scope_depth++;
        while (child != NOVAC_INVALID_NODE && cg->status == NOVAC_OK) {

            uint32_t next = cg->nodes[child].next;
            nc_emit_statement(cg, child);
            child = next;
        }
        if (cg->status == NOVAC_OK)
            nc_emit_scope_cleanup(cg, saved_symbol_count);
        cg->symbol_count = saved_symbol_count;
        if (cg->scope_depth > 0u)
            cg->scope_depth--;
        break;
    }
    case NC_NODE_VAR_DECL: {
        int symbol_index = nc_declare_symbol(cg, node);
        if (symbol_index < 0)
            break;
        if (node->op == 1u)
            break;
        if (nc_type_is_aggregate(cg->symbols[symbol_index].type)) {
            if (nc_emit_allocate_aggregate(cg, node, &cg->symbols[symbol_index]) &&
                node->a != NOVAC_INVALID_NODE)
                (void)nc_emit_aggregate_initializer(cg, node, &cg->symbols[symbol_index], node->a);
        } else if (node->a == NOVAC_INVALID_NODE) {
            nc_emit_mov_ri(cg, cg->symbols[symbol_index].reg, 0u);
        } else {
            int value_reg = nc_emit_expr(cg, node->a);
            if (value_reg >= 0) {
                if (cg->symbols[symbol_index].memory_backed != 0u) {
                    nc_emit_scalar_store(cg, cg->symbols[symbol_index].reg, value_reg,
                                         cg->symbols[symbol_index].type);
                } else {
                    nc_emit_mov_rr(cg, cg->symbols[symbol_index].reg, (uint8_t)value_reg);
                }
                nc_free_temp(cg, value_reg);
            }
        }
        break;
    }
    case NC_NODE_ASSIGN: {
        const NcNode *target = &cg->nodes[node->a];
        int value_reg;
        if (!nc_require_writable_lvalue(cg, node, node->a, NULL))
            break;
        value_reg = nc_emit_expr(cg, node->b);
        if (value_reg < 0)
            break;
        if (target->kind == NC_NODE_VARIABLE) {
            int symbol_index = nc_find_symbol(cg, target->name);
            if (symbol_index >= 0) {
                if (nc_type_is_aggregate(cg->symbols[symbol_index].type)) {
                    nc_free_temp(cg, value_reg);
                    nc_codegen_fail(
                        cg, target, NOVAC_ERR_UNSUPPORTED,
                        "whole-array/whole-struct assignment is not implemented in Vol.10");
                    break;
                }
                if (cg->symbols[symbol_index].memory_backed != 0u) {
                    nc_emit_scalar_store(cg, cg->symbols[symbol_index].reg, value_reg,
                                         cg->symbols[symbol_index].type);
                } else {
                    nc_emit_mov_rr(cg, cg->symbols[symbol_index].reg, (uint8_t)value_reg);
                }
                nc_free_temp(cg, value_reg);
            } else {
                int global_index = nc_find_global(cg, target->name);
                if (global_index < 0) {
                    nc_free_temp(cg, value_reg);
                    nc_codegen_fail(cg, target, NOVAC_ERR_UNKNOWN_SYMBOL,
                                    "assignment to unknown local/global variable");
                    break;
                }
                {
                    const NcGlobal *global = &cg->globals[(uint32_t)global_index];
                    int address_reg;

                    if (nc_type_is_aggregate(global->type)) {
                        nc_free_temp(cg, value_reg);
                        nc_codegen_fail(
                            cg, target, NOVAC_ERR_UNSUPPORTED,
                            "whole-array/whole-struct global assignment is not implemented");
                        break;
                    }
                    address_reg = nc_alloc_temp(cg, target);
                    uint32_t reference_offset = cg->size;
                    if (address_reg < 0) {
                        nc_free_temp(cg, value_reg);
                        break;
                    }
                    nc_emit_mov_ri(cg, (uint8_t)address_reg, global->address);
                    nc_emit_scalar_store(cg, address_reg, value_reg, global->type);
                    nc_free_temp(cg, address_reg);
                    nc_free_temp(cg, value_reg);
                    (void)nc_publish_reference(cg, target, NOVAC_REF_GLOBAL_WRITE, global->name,
                                               (uint32_t)global_index, global->file_id,
                                               reference_offset);
                }
            }
        } else {
            NcTypeSpec target_type;
            int root_global = nc_lvalue_root_global(cg, node->a);
            if (root_global >= 0) {
                const NcGlobal *global = &cg->globals[(uint32_t)root_global];
                (void)nc_publish_reference(cg, target, NOVAC_REF_GLOBAL_WRITE, global->name,
                                           (uint32_t)root_global, global->file_id, cg->size);
            }
            int address_reg = nc_emit_lvalue_address(cg, node->a, &target_type);
            if (address_reg >= 0) {
                if (nc_type_is_aggregate(target_type)) {
                    nc_free_temp(cg, address_reg);
                    nc_free_temp(cg, value_reg);
                    nc_codegen_fail(cg, target, NOVAC_ERR_UNSUPPORTED,
                                    "aggregate assignment target requires a scalar element/member");
                    break;
                }
                nc_emit_scalar_store(cg, address_reg, value_reg, target_type);
                nc_free_temp(cg, address_reg);
            }
            nc_free_temp(cg, value_reg);
        }
        break;
    }
    case NC_NODE_COMPOUND_ASSIGN: {
        NcTypeSpec target_type;
        NcTypeSpec rhs_type;
        int rhs_reg;
        int result_reg;
        if (!nc_require_writable_lvalue(cg, node, node->a, &target_type) ||
            !nc_resolve_expr_type(cg, node->b, &rhs_type))
            break;
        if (nc_type_is_pointer(target_type) && !nc_type_is_integer_scalar(rhs_type)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INVALID_POINTER,
                            "pointer +=/-= requires an integer right operand");
            break;
        }

        if (!nc_type_is_pointer(target_type) &&
            (!nc_type_is_integer_scalar(target_type) || !nc_type_is_integer_scalar(rhs_type))) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED,
                            "compound +=/-= requires scalar integer operands");
            break;
        }
        rhs_reg = nc_emit_expr(cg, node->b);
        if (rhs_reg < 0)
            break;
        result_reg = nc_emit_rmw(cg, node, node->a, node->op, rhs_reg, 0);
        nc_free_temp(cg, rhs_reg);
        if (result_reg >= 0)
            nc_free_temp(cg, result_reg);
        break;
    }
    case NC_NODE_IF: {
        uint32_t false_patch = 0u;
        uint32_t end_patch = 0u;
        nc_emit_condition_jump_false(cg, node->a, &false_patch);
        nc_emit_statement(cg, node->b);
        if (node->c != NOVAC_INVALID_NODE) {
            end_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
            nc_patch_u32(cg, false_patch, cg->size);
            nc_emit_statement(cg, node->c);
            nc_patch_u32(cg, end_patch, cg->size);
        } else
            nc_patch_u32(cg, false_patch, cg->size);
        break;
    }
    case NC_NODE_SWITCH: {
        uint32_t case_nodes[NOVAC_MAX_SWITCH_CASES];
        uint32_t case_patches[NOVAC_MAX_SWITCH_CASES];
        uint32_t case_count = 0u;
        uint32_t case_index = node->b;
        uint32_t default_index = NOVAC_INVALID_NODE;
        uint32_t default_dispatch_patch;
        uint32_t end_target;
        NcTypeSpec selector_type;
        int selector_reg;
        NcBreakContext *break_context;
        if (!nc_resolve_expr_type(cg, node->a, &selector_type))
            break;
        if (!nc_type_is_integer_scalar(selector_type)) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED,
                            "switch expression requires an integer scalar");
            break;
        }
        selector_reg = nc_emit_expr(cg, node->a);
        if (selector_reg < 0)
            break;
        break_context = nc_push_break_context(cg, cg->symbol_count);
        if (break_context == NULL) {
            nc_free_temp(cg, selector_reg);
            break;
        }
        while (case_index != NOVAC_INVALID_NODE) {
            const NcNode *case_node = &cg->nodes[case_index];
            if (case_count >= NOVAC_MAX_SWITCH_CASES) {
                nc_free_temp(cg, selector_reg);
                nc_codegen_fail(cg, case_node, NOVAC_ERR_SYMBOL_CAPACITY,
                                "switch case count exceeds fixed compiler capacity");
                break;
            }
            case_nodes[case_count] = case_index;
            case_patches[case_count] = 0u;
            if (case_node->op != 0u)
                default_index = case_count;
            else {
                int value_reg = nc_alloc_temp(cg, case_node);
                if (value_reg < 0)
                    break;

                nc_emit_mov_ri(cg, (uint8_t)value_reg, case_node->value);
                nc_emit_u8(cg, NOVA_OP_CMP_RR);
                nc_emit_u8(cg, (uint8_t)selector_reg);
                nc_emit_u8(cg, (uint8_t)value_reg);
                nc_free_temp(cg, value_reg);
                case_patches[case_count] = nc_emit_jump_placeholder(cg, NOVA_OP_JE);
            }
            case_count++;
            case_index = case_node->next;
        }
        nc_free_temp(cg, selector_reg);
        if (cg->status != NOVAC_OK) {
            if (cg->break_depth > 0u)
                cg->break_depth--;
            break;
        }
        default_dispatch_patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
        {
            uint32_t i;
            for (i = 0u; i < case_count; i++) {
                const NcNode *case_node = &cg->nodes[case_nodes[i]];
                uint32_t saved_symbol_count = cg->symbol_count;
                uint32_t map_index = nc_debug_begin_statement(cg, case_node);
                uint32_t stmt = case_node->b;
                if (case_patches[i] != 0u)
                    nc_patch_u32(cg, case_patches[i], cg->size);
                if (default_index == i)
                    nc_patch_u32(cg, default_dispatch_patch, cg->size);
                cg->scope_depth++;
                while (stmt != NOVAC_INVALID_NODE && cg->status == NOVAC_OK) {
                    uint32_t next_stmt = cg->nodes[stmt].next;
                    nc_emit_statement(cg, stmt);
                    stmt = next_stmt;
                }
                nc_emit_scope_cleanup(cg, saved_symbol_count);
                cg->symbol_count = saved_symbol_count;
                if (cg->scope_depth > 0u)
                    cg->scope_depth--;
                nc_debug_end_statement(cg, map_index);
            }
        }
        end_target = cg->size;
        if (default_index == NOVAC_INVALID_NODE)
            nc_patch_u32(cg, default_dispatch_patch, end_target);
        nc_finish_break_context(cg, break_context, end_target);
        break;
    }
    case NC_NODE_WHILE: {
        uint32_t loop_start = cg->size;
        uint32_t exit_patch = 0u;
        NcLoopContext *loop = nc_push_loop(cg, cg->symbol_count);
        if (loop == NULL)
            break;
        nc_emit_condition_jump_false(cg, node->a, &exit_patch);
        nc_emit_statement(cg, node->b);
        nc_emit_jump_target(cg, NOVA_OP_JMP, loop_start);
        nc_patch_u32(cg, exit_patch, cg->size);
        nc_finish_loop(cg, loop, cg->size, loop_start);
        break;
    }
    case NC_NODE_DO_WHILE: {
        uint32_t body_start = cg->size;
        uint32_t condition_start;
        uint32_t exit_patch = 0u;
        NcLoopContext *loop = nc_push_loop(cg, cg->symbol_count);
        if (loop == NULL)
            break;
        nc_emit_statement(cg, node->a);
        condition_start = cg->size;
        nc_emit_condition_jump_false(cg, node->b, &exit_patch);
        nc_emit_jump_target(cg, NOVA_OP_JMP, body_start);
        nc_patch_u32(cg, exit_patch, cg->size);
        nc_finish_loop(cg, loop, cg->size, condition_start);
        break;
    }
    case NC_NODE_FOR: {
        uint32_t saved_symbol_count = cg->symbol_count;
        uint32_t condition_start;
        uint32_t post_start;

        uint32_t exit_patch = 0u;
        uint32_t exit_target;
        NcLoopContext *loop;
        cg->scope_depth++;
        if (node->a != NOVAC_INVALID_NODE)
            nc_emit_statement(cg, node->a);
        if (cg->status != NOVAC_OK) {
            if (cg->scope_depth > 0u)
                cg->scope_depth--;
            break;
        }
        loop = nc_push_loop(cg, cg->symbol_count);
        if (loop == NULL) {
            if (cg->scope_depth > 0u)
                cg->scope_depth--;
            break;
        }
        condition_start = cg->size;
        if (node->b != NOVAC_INVALID_NODE)
            nc_emit_condition_jump_false(cg, node->b, &exit_patch);
        nc_emit_statement(cg, node->c);
        post_start = cg->size;
        if (node->value != NOVAC_INVALID_NODE)
            nc_emit_statement(cg, node->value);
        nc_emit_jump_target(cg, NOVA_OP_JMP, condition_start);
        exit_target = cg->size;
        if (exit_patch != 0u)
            nc_patch_u32(cg, exit_patch, exit_target);
        nc_finish_loop(cg, loop, exit_target, post_start);
        nc_emit_scope_cleanup(cg, saved_symbol_count);
        cg->symbol_count = saved_symbol_count;
        if (cg->scope_depth > 0u)
            cg->scope_depth--;
        break;
    }
    case NC_NODE_BREAK: {
        NcBreakContext *context;
        uint32_t patch;
        if (cg->break_depth == 0u) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL,
                            "break reached codegen outside a breakable construct");
            break;
        }
        context = &cg->break_stack[cg->break_depth - 1u];
        nc_emit_runtime_cleanup_from(cg, context->preserve_symbol_count);
        patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
        (void)nc_add_break_jump_patch(cg, patch);
        break;
    }
    case NC_NODE_CONTINUE: {
        NcLoopContext *loop;
        uint32_t patch;
        if (cg->loop_depth == 0u) {
            nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL,
                            "continue reached codegen outside a loop");
            break;
        }
        loop = &cg->loop_stack[cg->loop_depth - 1u];
        nc_emit_runtime_cleanup_from(cg, loop->preserve_symbol_count);
        patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
        (void)nc_add_continue_jump_patch(cg, patch);
        break;
    }
    case NC_NODE_LABEL:
        nc_emit_label_target(cg, node);
        if (cg->status == NOVAC_OK)
            nc_emit_statement(cg, node->a);
        break;
    case NC_NODE_GOTO: {
        int label_index = nc_label_index(cg, node->name);
        NcLabelInfo *label;
        uint32_t patch;
        if (label_index < 0) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNKNOWN_SYMBOL,
                            "goto target label is not defined in this function");
            break;
        }
        label = &cg->labels[(uint32_t)label_index];
        if (label->scope_depth > node->scope_depth) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED,
                            "goto may not enter a deeper lexical scope");
            break;
        }
        if (label->epoch > node->epoch) {
            nc_codegen_fail(cg, node, NOVAC_ERR_UNSUPPORTED,
                            "forward goto may not skip declarations before its target label");
            break;
        }
        nc_emit_goto_cleanup(cg, label->scope_depth, label->epoch);
        patch = nc_emit_jump_placeholder(cg, NOVA_OP_JMP);
        if (label->is_emitted != 0u)
            nc_patch_u32(cg, patch, label->address);
        else
            (void)nc_add_goto_patch(cg, patch, (uint32_t)label_index);
        break;
    }
    case NC_NODE_RETURN: {
        int value_reg = nc_emit_expr(cg, node->a);
        if (value_reg >= 0) {
            nc_emit_mov_rr(cg, NOVAC_RETURN_REGISTER, (uint8_t)value_reg);
            nc_free_temp(cg, value_reg);
            nc_emit_aggregate_cleanup(cg);
            nc_emit_u8(cg, NOVA_OP_RET);
        }
        break;
    }
    case NC_NODE_EXPR_STMT: {
        int value_reg = nc_emit_expr(cg, node->a);
        if (value_reg >= 0)
            nc_free_temp(cg, value_reg);
        break;
    }
    default:
        nc_codegen_fail(cg, node, NOVAC_ERR_INTERNAL, "expression node used as statement");
        break;
    }
    nc_debug_end_statement(cg, debug_index);
}
static int nc_emit_function(NcCodegen *cg, uint32_t function_index) {
    const NcFunction *function = &cg->functions[function_index];
    uint32_t i;
    cg->current_function_index = function_index;
    cg->symbol_count = 0u;
    cg->function_decl_count = 0u;
    cg->scope_depth = 1u;
    cg->loop_depth = 0u;
    cg->break_depth = 0u;
    cg->break_patch_count = 0u;
    cg->continue_patch_count = 0u;
    cg->label_count = 0u;
    cg->goto_patch_count = 0u;
    for (i = 0u; i < 16u; i++)
        cg->temp_used[i] = 0u;
    cg->function_starts[function_index] = cg->size;
    if (!nc_collect_labels(cg, function->body))
        return 0;
    for (i = 0u; i < function->parameter_count; i++) {
        const NcParameter *parameter = &function->parameters[i];
        if (nc_declare_named_symbol(cg, parameter->name, parameter->type, parameter->line,
                                    parameter->column, 1u, 0u, 0u) < 0)
            return 0;
    }
    cg->scope_depth =
        0u; /* function-body block enters depth 1 with parameters already in that scope */
    nc_emit_statement(cg, function->body);
    if (cg->status != NOVAC_OK)
        return 0;
    for (i = 0u; i < cg->label_count; i++) {
        if (cg->labels[i].is_emitted == 0u) {
            nc_codegen_fail(cg, NULL, NOVAC_ERR_INTERNAL, "parsed label was not emitted");
            return 0;
        }
    }
    nc_emit_mov_ri(cg, NOVAC_RETURN_REGISTER, 0u);
    nc_emit_aggregate_cleanup(cg);
    nc_emit_u8(cg, NOVA_OP_RET);
    cg->function_ends[function_index] = cg->size;
    for (i = 0u; i < cg->symbol_count; i++)
        nc_close_symbol_lifetime(cg, &cg->symbols[i]);
    cg->function_local_counts[function_index] = cg->function_decl_count;
    if (cg->debug_functions != NULL) {
        NovaDebugFunction *out;
        if (function_index >= cg->debug_function_capacity) {
            nc_codegen_fail(cg, NULL, NOVAC_ERR_DEBUG_CAPACITY,
                            "debug function buffer is too small");
            return 0;
        }
        out = &cg->debug_functions[function_index];
        nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, function->name,
                     (uint32_t)nc_strlen(function->name));
        out->bytecode_start = cg->function_starts[function_index];
        out->bytecode_end = cg->function_ends[function_index];
        out->declaration_line = function->line;
        out->declaration_column = function->column;
        out->parameter_count = function->parameter_count;
        out->local_count = cg->function_decl_count;
        out->file_id = function->file_id;
    }
    return 1;
}
/* ----------------------- bounded preprocessing -------------------------- */
#define NOVAC_PP_MAX_MACRO_PARAMS 8u
#define NOVAC_PP_MAX_CONDITIONAL_DEPTH 32u
#define NOVAC_PP_MAX_EXPANSION_DEPTH 32u
#define NOVAC_PP_MAX_ARGUMENT_BYTES 512u
#define NOVAC_PP_MAX_LINE_BYTES 8192u

typedef struct NcMacro {
    char name[NOVA_COMPILER_DEBUG_NAME];
    char replacement[NOVA_COMPILER_MACRO_REPLACEMENT];
    char parameters[NOVAC_PP_MAX_MACRO_PARAMS][NOVA_COMPILER_DEBUG_NAME];
    uint32_t kind;
    uint32_t parameter_count;
    uint32_t file_id;
    uint32_t line;
    uint32_t column;
} NcMacro;

typedef struct NcMacroEnv {
    NcMacro macros[NOVA_COMPILER_MAX_MACROS];
    uint32_t count;
} NcMacroEnv;

typedef struct NcMacroEvent {
    NcMacro macro;
    uint32_t is_undef;
} NcMacroEvent;

typedef struct NcPpConditional {
    uint32_t parent_active;
    uint32_t branch_taken;
    uint32_t active;
    uint32_t saw_else;
} NcPpConditional;

typedef struct NcPpExpansionFrame {
    char raw_args[NOVAC_PP_MAX_MACRO_PARAMS][NOVAC_PP_MAX_ARGUMENT_BYTES];
    char expanded_args[NOVAC_PP_MAX_MACRO_PARAMS][NOVAC_PP_MAX_ARGUMENT_BYTES];
    char substituted[NOVAC_PP_MAX_LINE_BYTES];
    char expanded[NOVAC_PP_MAX_LINE_BYTES];
} NcPpExpansionFrame;

typedef struct NcPpExpansionScratch {
    NcPpExpansionFrame frames[NOVAC_PP_MAX_EXPANSION_DEPTH + 1u];
} NcPpExpansionScratch;

typedef struct NcPpScanScratch {
    NcMacroEnv env[NOVA_COMPILER_MAX_TRANSLATION_UNITS];
    NcPpConditional conditional[NOVA_COMPILER_MAX_TRANSLATION_UNITS]
                               [NOVAC_PP_MAX_CONDITIONAL_DEPTH];
    char line_buffer[NOVAC_PP_MAX_LINE_BYTES];
} NcPpScanScratch;

typedef struct NcPpFileScratch {
    NcMacroEnv env;
    NcPpConditional conditional[NOVAC_PP_MAX_CONDITIONAL_DEPTH];
    char line_buffer[NOVAC_PP_MAX_LINE_BYTES];
} NcPpFileScratch;

typedef struct NcPpProject {
    const NovaTranslationUnit *units;
    uint32_t unit_count;
    NovaIncludeDependency dependencies[NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES];
    uint32_t dependency_count;
    NcMacroEvent events[NOVA_COMPILER_MAX_MACROS];
    uint32_t event_count;
    uint8_t scan_state[NOVA_COMPILER_MAX_TRANSLATION_UNITS];
    uint8_t active_units[NOVA_COMPILER_MAX_TRANSLATION_UNITS];
    NovaCompilerDiagnostic *diagnostic;
    NcPpExpansionScratch *expansion_scratch;
    NcPpScanScratch *scan_scratch;
    NcPpFileScratch *make_scratch;
} NcPpProject;

typedef struct NcCompileWorkspace {
    NcParser parser;
    NcCodegen codegen;
    NcPpProject preprocessor;
    NcPpExpansionScratch pp_expansion;
    NcPpScanScratch pp_scan;
    NcPpFileScratch pp_make;
    char preprocessed_source[NOVA_COMPILER_MAX_SOURCE_BYTES + 1u];
} NcCompileWorkspace;

typedef struct NcPpExpr {
    const char *text;
    uint32_t length;
    uint32_t pos;
    uint32_t file_id;
    uint32_t line;
    uint32_t base_column;
    NovaCompilerDiagnostic *diagnostic;
    int32_t status;
} NcPpExpr;

static int nc_find_unit_by_normalized_name(const NovaTranslationUnit *units, uint32_t unit_count,
                                           const char *normalized_name) {
    uint32_t i;
    char candidate[NOVA_COMPILER_TRANSLATION_UNIT_NAME];
    for (i = 0u; i < unit_count; i++) {
        if (!nc_normalize_path(units[i].name, candidate, (uint32_t)sizeof(candidate)))
            continue;
        if (nc_streq(candidate, normalized_name))
            return (int)i;
    }
    return -1;
}

static void nc_update_block_comment_state(const char *source, uint32_t start, uint32_t end,
                                          int *in_block_comment) {
    uint32_t p = start;
    int quote = 0;
    while (p < end) {
        char c = source[p];
        char next = p + 1u < end ? source[p + 1u] : '\0';
        if (*in_block_comment) {
            if (c == '*' && next == '/') {
                *in_block_comment = 0;
                p += 2u;
                continue;
            }
            p++;
            continue;
        }
        if (quote != 0) {
            if (c == '\\') {
                p += p + 1u < end ? 2u : 1u;
                continue;
            }
            if (c == quote)
                quote = 0;
            p++;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            p++;
            continue;
        }
        if (c == '/' && next == '/')
            return;
        if (c == '/' && next == '*') {
            *in_block_comment = 1;
            p += 2u;
            continue;
        }
        p++;
    }
}

static uint32_t nc_pp_trim_left(const char *text, uint32_t p, uint32_t end) {
    while (p < end && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r'))
        p++;
    return p;
}

static uint32_t nc_pp_trim_right(const char *text, uint32_t start, uint32_t end) {
    while (end > start &&
           (text[end - 1u] == ' ' || text[end - 1u] == '\t' || text[end - 1u] == '\r'))
        end--;
    return end;
}

static int nc_pp_read_identifier(const char *text, uint32_t *pos, uint32_t end, char *output,
                                 uint32_t capacity) {
    uint32_t start = *pos;
    uint32_t length;
    if (start >= end || !nc_is_alpha(text[start]))
        return 0;
    (*pos)++;
    while (*pos < end && (nc_is_alpha(text[*pos]) || nc_is_digit(text[*pos])))
        (*pos)++;
    length = *pos - start;
    if (length + 1u > capacity)
        return 0;
    nc_copy_text(output, capacity, text + start, length);
    return 1;
}

static int nc_pp_macro_same(const NcMacro *a, const NcMacro *b) {
    uint32_t i;
    if (a->kind != b->kind || a->parameter_count != b->parameter_count ||
        !nc_streq(a->replacement, b->replacement))
        return 0;
    for (i = 0u; i < a->parameter_count; i++)
        if (!nc_streq(a->parameters[i], b->parameters[i]))
            return 0;
    return 1;
}

static int nc_pp_env_find(const NcMacroEnv *env, const char *name) {
    uint32_t i;
    for (i = 0u; i < env->count; i++)
        if (nc_streq(env->macros[i].name, name))
            return (int)i;
    return -1;
}

static int nc_pp_env_define(NcMacroEnv *env, const NcMacro *macro,
                            NovaCompilerDiagnostic *diagnostic) {
    int index = nc_pp_env_find(env, macro->name);
    if (index >= 0) {
        if (!nc_pp_macro_same(&env->macros[(uint32_t)index], macro)) {
            if (diagnostic != NULL)
                diagnostic->file_id = macro->file_id;
            (void)nc_diag(diagnostic, NOVAC_ERR_CONFLICTING_DECLARATION, macro->line, macro->column,
                          "conflicting macro redefinition");
            return 0;
        }
        env->macros[(uint32_t)index] = *macro;
        return 1;
    }
    if (env->count >= NOVA_COMPILER_MAX_MACROS) {
        if (diagnostic != NULL)
            diagnostic->file_id = macro->file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_CAPACITY, macro->line, macro->column,
                      "macro environment capacity exceeded");
        return 0;
    }
    env->macros[env->count++] = *macro;
    return 1;
}

static void nc_pp_env_undef(NcMacroEnv *env, const char *name) {
    int index = nc_pp_env_find(env, name);
    uint32_t i;
    if (index < 0)
        return;
    for (i = (uint32_t)index + 1u; i < env->count; i++)
        env->macros[i - 1u] = env->macros[i];
    env->count--;
}

static int nc_pp_record_event(NcPpProject *project, const NcMacro *macro, uint32_t is_undef) {
    if (project->event_count >= NOVA_COMPILER_MAX_MACROS) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = macro->file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_MACRO_CAPACITY, macro->line, macro->column,
                      "project macro directive capacity exceeded");
        return 0;
    }
    project->events[project->event_count].macro = *macro;
    project->events[project->event_count].is_undef = is_undef;
    project->event_count++;
    return 1;
}

static int nc_pp_apply_event(NcMacroEnv *env, const NcMacroEvent *event,
                             NovaCompilerDiagnostic *diagnostic) {
    if (event->is_undef != 0u) {
        nc_pp_env_undef(env, event->macro.name);
        return 1;
    }
    return nc_pp_env_define(env, &event->macro, diagnostic);
}

static int nc_pp_import_file_effects(const NcPpProject *project, uint32_t file_id, NcMacroEnv *env,
                                     uint32_t depth) {
    uint32_t cursor_line = 0u;
    if (depth > NOVA_COMPILER_MAX_TRANSLATION_UNITS) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_INCLUDE_CYCLE, 0u, 0u,
                      "macro import recursion exceeded translation-unit bound");
        return 0;
    }
    for (;;) {
        uint32_t i;
        uint32_t next_line = 0xFFFFFFFFu;
        uint32_t next_kind = 0u;
        uint32_t next_index = 0u;
        for (i = 0u; i < project->dependency_count; i++) {
            const NovaIncludeDependency *dep = &project->dependencies[i];
            if (dep->from_file_id != file_id || dep->line <= cursor_line)
                continue;
            if (dep->line < next_line) {
                next_line = dep->line;
                next_kind = 1u;
                next_index = i;
            }
        }
        for (i = 0u; i < project->event_count; i++) {
            const NcMacroEvent *event = &project->events[i];
            if (event->macro.file_id != file_id || event->macro.line <= cursor_line)
                continue;
            if (event->macro.line < next_line) {
                next_line = event->macro.line;
                next_kind = 2u;
                next_index = i;
            }
        }
        if (next_kind == 0u)
            break;
        cursor_line = next_line;
        if (next_kind == 1u) {
            if (!nc_pp_import_file_effects(project, project->dependencies[next_index].to_file_id,
                                           env, depth + 1u))
                return 0;
        } else {
            if (!nc_pp_apply_event(env, &project->events[next_index], project->diagnostic))
                return 0;
        }
    }
    return 1;
}

static int nc_pp_copy_piece(char *output, uint32_t capacity, uint32_t *cursor, const char *text,
                            uint32_t length) {
    uint32_t i;
    if (*cursor > capacity || length > capacity - *cursor)
        return 0;
    for (i = 0u; i < length; i++)
        output[(*cursor)++] = text[i];
    return 1;
}

static int nc_pp_parse_macro_definition(const char *source, uint32_t *p, uint32_t line_end,
                                        uint32_t file_id, uint32_t line, uint32_t column,
                                        NcMacro *macro, NovaCompilerDiagnostic *diagnostic) {
    uint32_t name_start;
    uint32_t name_len;
    uint32_t replacement_start;
    uint32_t replacement_end;
    uint32_t i;
    macro->name[0] = '\0';
    macro->replacement[0] = '\0';
    macro->parameter_count = 0u;
    macro->kind = NOVAC_MACRO_OBJECT;
    macro->file_id = file_id;
    macro->line = line;
    macro->column = column;
    *p = nc_pp_trim_left(source, *p, line_end);
    name_start = *p;
    if (name_start >= line_end || !nc_is_alpha(source[name_start])) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                      "#define requires an identifier name");
        return 0;
    }
    (*p)++;
    while (*p < line_end && (nc_is_alpha(source[*p]) || nc_is_digit(source[*p])))
        (*p)++;
    name_len = *p - name_start;
    if (name_len >= NOVA_COMPILER_DEBUG_NAME) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_CAPACITY, line, column,
                      "macro name exceeds fixed capacity");
        return 0;
    }
    nc_copy_text(macro->name, NOVA_COMPILER_DEBUG_NAME, source + name_start, name_len);
    if (*p < line_end && source[*p] == '(') {
        macro->kind = NOVAC_MACRO_FUNCTION;
        (*p)++;
        *p = nc_pp_trim_left(source, *p, line_end);
        if (*p < line_end && source[*p] == ')') {
            (*p)++;
        } else {
            for (;;) {
                char parameter[NOVA_COMPILER_DEBUG_NAME];
                if (macro->parameter_count >= NOVAC_PP_MAX_MACRO_PARAMS) {
                    if (diagnostic != NULL)
                        diagnostic->file_id = file_id;
                    (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_CAPACITY, line, column,
                                  "function-like macro parameter capacity exceeded");
                    return 0;
                }
                if (!nc_pp_read_identifier(source, p, line_end, parameter,
                                           (uint32_t)sizeof(parameter))) {
                    if (diagnostic != NULL)
                        diagnostic->file_id = file_id;
                    (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                                  "expected macro parameter identifier");
                    return 0;
                }
                for (i = 0u; i < macro->parameter_count; i++) {
                    if (nc_streq(macro->parameters[i], parameter)) {
                        if (diagnostic != NULL)
                            diagnostic->file_id = file_id;
                        (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                                      "duplicate macro parameter name");
                        return 0;
                    }
                }
                nc_copy_text(macro->parameters[macro->parameter_count], NOVA_COMPILER_DEBUG_NAME,
                             parameter, (uint32_t)nc_strlen(parameter));
                macro->parameter_count++;
                *p = nc_pp_trim_left(source, *p, line_end);
                if (*p < line_end && source[*p] == ')') {
                    (*p)++;
                    break;
                }
                if (*p >= line_end || source[*p] != ',') {
                    if (diagnostic != NULL)
                        diagnostic->file_id = file_id;
                    (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                                  "expected ',' or ')' in macro parameter list");
                    return 0;
                }
                (*p)++;
                *p = nc_pp_trim_left(source, *p, line_end);
            }
        }
    }
    *p = nc_pp_trim_left(source, *p, line_end);
    replacement_start = *p;
    replacement_end = nc_pp_trim_right(source, replacement_start, line_end);
    if (replacement_end - replacement_start >= NOVA_COMPILER_MACRO_REPLACEMENT) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_CAPACITY, line, column,
                      "macro replacement exceeds fixed capacity");
        return 0;
    }
    {
        int quote = 0;
        for (i = replacement_start; i < replacement_end; i++) {
            char c = source[i];
            if (quote != 0) {
                if (c == '\\' && i + 1u < replacement_end) {
                    i++;
                    continue;
                }
                if (c == quote)
                    quote = 0;
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c == '#') {
                if (diagnostic != NULL)
                    diagnostic->file_id = file_id;
                (void)nc_diag(diagnostic, NOVAC_ERR_UNSUPPORTED, line, column,
                              "macro stringification/token-pasting operators are deferred");
                return 0;
            }
        }
    }
    nc_copy_text(macro->replacement, NOVA_COMPILER_MACRO_REPLACEMENT, source + replacement_start,
                 replacement_end - replacement_start);
    return 1;
}

static int nc_pp_parse_undef_name(const char *source, uint32_t *p, uint32_t line_end, char *name,
                                  uint32_t capacity, uint32_t file_id, uint32_t line,
                                  uint32_t column, NovaCompilerDiagnostic *diagnostic) {
    *p = nc_pp_trim_left(source, *p, line_end);
    if (!nc_pp_read_identifier(source, p, line_end, name, capacity)) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                      "#undef requires an identifier name");
        return 0;
    }
    *p = nc_pp_trim_left(source, *p, line_end);
    if (*p != line_end) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                      "tokens after #undef are not supported");
        return 0;
    }
    return 1;
}

static int nc_pp_parse_include_target(const NcPpProject *project, uint32_t file_id,
                                      const char *source, uint32_t *p, uint32_t line_end,
                                      uint32_t line, uint32_t column, uint32_t *target_out) {
    uint32_t path_start;
    uint32_t path_len;
    char include_path[NOVA_COMPILER_TRANSLATION_UNIT_NAME];
    char resolved[NOVA_COMPILER_TRANSLATION_UNIT_NAME];
    int target;
    *p = nc_pp_trim_left(source, *p, line_end);
    if (*p < line_end && source[*p] == '<') {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(
            project->diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
            "system/angle-bracket includes are not supported; use workspace-local quoted headers");
        return 0;
    }
    if (*p >= line_end || source[*p] != '"') {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                      "#include requires a literal quoted project header path");
        return 0;
    }
    (*p)++;
    path_start = *p;
    while (*p < line_end && source[*p] != '"')
        (*p)++;
    if (*p >= line_end) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                      "unterminated quoted include path");
        return 0;
    }
    path_len = *p - path_start;
    if (path_len == 0u || path_len >= NOVA_COMPILER_TRANSLATION_UNIT_NAME) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_INCLUDE_NOT_FOUND, line, column,
                      "include path is empty or exceeds fixed capacity");
        return 0;
    }
    nc_copy_text(include_path, NOVA_COMPILER_TRANSLATION_UNIT_NAME, source + path_start, path_len);
    (*p)++;
    *p = nc_pp_trim_left(source, *p, line_end);
    if (*p != line_end) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                      "tokens after #include are not supported");
        return 0;
    }
    if (!nc_resolve_include_path(project->units[file_id].name, include_path, resolved,
                                 (uint32_t)sizeof(resolved))) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_INCLUDE_NOT_FOUND, line, column,
                      "include path escapes/violates the fixed project path model");
        return 0;
    }
    target = nc_find_unit_by_normalized_name(project->units, project->unit_count, resolved);
    if (target < 0) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_INCLUDE_NOT_FOUND, line, column,
                      "quoted include does not resolve to a supplied project file");
        return 0;
    }
    if (!nc_is_header_name(project->units[(uint32_t)target].name)) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                      "#include target must be a .h project file");
        return 0;
    }
    *target_out = (uint32_t)target;
    return 1;
}

static int
nc_pp_substitute_macro(const NcMacro *macro,
                       char expanded_args[NOVAC_PP_MAX_MACRO_PARAMS][NOVAC_PP_MAX_ARGUMENT_BYTES],
                       char *output, uint32_t capacity, NovaCompilerDiagnostic *diagnostic) {
    uint32_t p = 0u;
    uint32_t cursor = 0u;
    uint32_t length = (uint32_t)nc_strlen(macro->replacement);
    int quote = 0;
    while (p < length) {
        char c = macro->replacement[p];
        if (quote != 0) {
            if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
                goto capacity_error;
            if (c == '\\' && p + 1u < length) {
                p++;
                c = macro->replacement[p];
                if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
                    goto capacity_error;
            } else if (c == quote)
                quote = 0;
            p++;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
                goto capacity_error;
            p++;
            continue;
        }
        if (nc_is_alpha(c)) {
            uint32_t start = p;
            uint32_t i;
            char identifier[NOVA_COMPILER_DEBUG_NAME];
            p++;
            while (p < length &&
                   (nc_is_alpha(macro->replacement[p]) || nc_is_digit(macro->replacement[p])))
                p++;
            if (p - start >= NOVA_COMPILER_DEBUG_NAME)
                goto capacity_error;
            nc_copy_text(identifier, NOVA_COMPILER_DEBUG_NAME, macro->replacement + start,
                         p - start);
            for (i = 0u; i < macro->parameter_count; i++) {
                if (nc_streq(identifier, macro->parameters[i]))
                    break;
            }
            if (i < macro->parameter_count) {
                uint32_t arg_len = (uint32_t)nc_strlen(expanded_args[i]);
                if (!nc_pp_copy_piece(output, capacity, &cursor, expanded_args[i], arg_len))
                    goto capacity_error;
            } else if (!nc_pp_copy_piece(output, capacity, &cursor, macro->replacement + start,
                                         p - start))
                goto capacity_error;
            continue;
        }
        if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
            goto capacity_error;
        p++;
    }
    if (cursor >= capacity)
        goto capacity_error;
    output[cursor] = '\0';
    return 1;
capacity_error:
    if (diagnostic != NULL)
        diagnostic->file_id = macro->file_id;
    (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_CAPACITY, macro->line, macro->column,
                  "macro substitution exceeds fixed line capacity");
    return 0;
}

static int nc_pp_expand_text_depth(const NcMacroEnv *env, NcPpExpansionScratch *scratch,
                                   const char *input, uint32_t input_len, char *output,
                                   uint32_t capacity, uint32_t depth, uint32_t file_id,
                                   uint32_t line, uint32_t column, int preserve_defined,
                                   NovaCompilerDiagnostic *diagnostic) {
    uint32_t p = 0u;
    uint32_t cursor = 0u;
    int quote = 0;
    int block_comment = 0;
    NcPpExpansionFrame *frame;
    if (depth > NOVAC_PP_MAX_EXPANSION_DEPTH) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_RECURSION, line, column,
                      "macro expansion recursion limit exceeded");
        return 0;
    }
    if (scratch == NULL) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_INTERNAL, line, column,
                      "preprocessor expansion scratch is unavailable");
        return 0;
    }
    frame = &scratch->frames[depth];
    while (p < input_len) {
        char c = input[p];
        char next = p + 1u < input_len ? input[p + 1u] : '\0';
        if (block_comment) {
            if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
                goto capacity_error;
            if (c == '*' && next == '/') {
                p++;
                c = input[p];
                if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
                    goto capacity_error;
                block_comment = 0;
            }
            p++;
            continue;
        }
        if (quote != 0) {
            if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
                goto capacity_error;
            if (c == '\\' && p + 1u < input_len) {
                p++;
                c = input[p];
                if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
                    goto capacity_error;
            } else if (c == quote)
                quote = 0;
            p++;
            continue;
        }
        if (c == '/' && next == '/') {
            if (!nc_pp_copy_piece(output, capacity, &cursor, input + p, input_len - p))
                goto capacity_error;
            p = input_len;
            continue;
        }
        if (c == '/' && next == '*') {
            block_comment = 1;
            if (!nc_pp_copy_piece(output, capacity, &cursor, input + p, 2u))
                goto capacity_error;
            p += 2u;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
                goto capacity_error;
            p++;
            continue;
        }
        if (nc_is_alpha(c)) {
            uint32_t start = p;
            uint32_t q;
            char identifier[NOVA_COMPILER_DEBUG_NAME];
            int macro_index;
            p++;
            while (p < input_len && (nc_is_alpha(input[p]) || nc_is_digit(input[p])))
                p++;
            if (p - start >= NOVA_COMPILER_DEBUG_NAME) {
                if (!nc_pp_copy_piece(output, capacity, &cursor, input + start, p - start))
                    goto capacity_error;
                continue;
            }
            nc_copy_text(identifier, NOVA_COMPILER_DEBUG_NAME, input + start, p - start);
            if (preserve_defined && nc_streq(identifier, "defined")) {
                if (!nc_pp_copy_piece(output, capacity, &cursor, input + start, p - start))
                    goto capacity_error;
                q = p;
                while (q < input_len && (input[q] == ' ' || input[q] == '\t'))
                    q++;
                if (q < input_len && input[q] == '(') {
                    uint32_t r = q + 1u;
                    while (r < input_len && (input[r] == ' ' || input[r] == '\t'))
                        r++;
                    if (r < input_len && nc_is_alpha(input[r])) {
                        r++;
                        while (r < input_len && (nc_is_alpha(input[r]) || nc_is_digit(input[r])))
                            r++;
                        while (r < input_len && (input[r] == ' ' || input[r] == '\t'))
                            r++;
                        if (r < input_len && input[r] == ')')
                            r++;
                        if (!nc_pp_copy_piece(output, capacity, &cursor, input + p, r - p))
                            goto capacity_error;
                        p = r;
                    }
                } else if (q < input_len && nc_is_alpha(input[q])) {
                    uint32_t r = q + 1u;
                    while (r < input_len && (nc_is_alpha(input[r]) || nc_is_digit(input[r])))
                        r++;
                    if (!nc_pp_copy_piece(output, capacity, &cursor, input + p, r - p))
                        goto capacity_error;
                    p = r;
                }
                continue;
            }
            macro_index = nc_pp_env_find(env, identifier);
            if (macro_index < 0) {
                if (!nc_pp_copy_piece(output, capacity, &cursor, input + start, p - start))
                    goto capacity_error;
                continue;
            }
            {
                const NcMacro *macro = &env->macros[(uint32_t)macro_index];
                if (macro->kind == NOVAC_MACRO_OBJECT) {
                    uint32_t replacement_len = (uint32_t)nc_strlen(macro->replacement);
                    if (!nc_pp_expand_text_depth(env, scratch, macro->replacement, replacement_len,
                                                 frame->expanded, (uint32_t)sizeof(frame->expanded),
                                                 depth + 1u, file_id, line, column,
                                                 preserve_defined, diagnostic))
                        return 0;
                    if (!nc_pp_copy_piece(output, capacity, &cursor, frame->expanded,
                                          (uint32_t)nc_strlen(frame->expanded)))
                        goto capacity_error;
                    continue;
                }
                q = p;
                while (q < input_len && (input[q] == ' ' || input[q] == '\t'))
                    q++;
                if (q >= input_len || input[q] != '(') {
                    if (!nc_pp_copy_piece(output, capacity, &cursor, input + start, p - start))
                        goto capacity_error;
                    continue;
                }
                {
                    uint32_t arg_count = 0u;
                    uint32_t r = q + 1u;
                    uint32_t arg_start = r;
                    uint32_t nested = 1u;
                    int arg_quote = 0;
                    uint32_t i;
                    for (i = 0u; i < NOVAC_PP_MAX_MACRO_PARAMS; i++) {
                        frame->raw_args[i][0] = '\0';
                        frame->expanded_args[i][0] = '\0';
                    }
                    while (r < input_len && nested != 0u) {
                        char ac = input[r];
                        if (arg_quote != 0) {
                            if (ac == '\\' && r + 1u < input_len) {
                                r += 2u;
                                continue;
                            }
                            if (ac == arg_quote)
                                arg_quote = 0;
                            r++;
                            continue;
                        }
                        if (ac == '"' || ac == '\'') {
                            arg_quote = ac;
                            r++;
                            continue;
                        }
                        if (ac == '(') {
                            nested++;
                            r++;
                            continue;
                        }
                        if (ac == ')') {
                            nested--;
                            if (nested == 0u) {
                                uint32_t a = nc_pp_trim_left(input, arg_start, r);
                                uint32_t b = nc_pp_trim_right(input, a, r);
                                if (arg_count == 0u && b == a && macro->parameter_count == 0u) {
                                    /* zero-argument invocation */
                                } else {
                                    if (arg_count >= NOVAC_PP_MAX_MACRO_PARAMS ||
                                        b - a >= NOVAC_PP_MAX_ARGUMENT_BYTES)
                                        goto invocation_capacity;
                                    nc_copy_text(frame->raw_args[arg_count],
                                                 NOVAC_PP_MAX_ARGUMENT_BYTES, input + a, b - a);
                                    arg_count++;
                                }
                                r++;
                                break;
                            }
                            r++;
                            continue;
                        }
                        if (ac == ',' && nested == 1u) {
                            uint32_t a = nc_pp_trim_left(input, arg_start, r);
                            uint32_t b = nc_pp_trim_right(input, a, r);
                            if (arg_count >= NOVAC_PP_MAX_MACRO_PARAMS ||
                                b - a >= NOVAC_PP_MAX_ARGUMENT_BYTES)
                                goto invocation_capacity;
                            nc_copy_text(frame->raw_args[arg_count], NOVAC_PP_MAX_ARGUMENT_BYTES,
                                         input + a, b - a);
                            arg_count++;
                            r++;
                            arg_start = r;
                            continue;
                        }
                        r++;
                    }
                    if (nested != 0u) {
                        if (diagnostic != NULL)
                            diagnostic->file_id = file_id;
                        (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                                      "unterminated function-like macro invocation");
                        return 0;
                    }
                    if (arg_count != macro->parameter_count) {
                        if (diagnostic != NULL)
                            diagnostic->file_id = file_id;
                        (void)nc_diag(diagnostic, NOVAC_ERR_ARGUMENT_COUNT, line, column,
                                      "function-like macro argument count mismatch");
                        return 0;
                    }
                    for (i = 0u; i < arg_count; i++) {
                        if (!nc_pp_expand_text_depth(
                                env, scratch, frame->raw_args[i],
                                (uint32_t)nc_strlen(frame->raw_args[i]), frame->expanded_args[i],
                                NOVAC_PP_MAX_ARGUMENT_BYTES, depth + 1u, file_id, line, column,
                                preserve_defined, diagnostic))
                            return 0;
                    }
                    {
                        if (!nc_pp_substitute_macro(macro, frame->expanded_args, frame->substituted,
                                                    (uint32_t)sizeof(frame->substituted),
                                                    diagnostic))
                            return 0;
                        if (!nc_pp_expand_text_depth(
                                env, scratch, frame->substituted,
                                (uint32_t)nc_strlen(frame->substituted), frame->expanded,
                                (uint32_t)sizeof(frame->expanded), depth + 1u, file_id, line,
                                column, preserve_defined, diagnostic))
                            return 0;
                        if (!nc_pp_copy_piece(output, capacity, &cursor, frame->expanded,
                                              (uint32_t)nc_strlen(frame->expanded)))
                            goto capacity_error;
                    }
                    p = r;
                    continue;
                invocation_capacity:
                    if (diagnostic != NULL)
                        diagnostic->file_id = file_id;
                    (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_CAPACITY, line, column,
                                  "macro invocation argument exceeds fixed capacity");
                    return 0;
                }
            }
        }
        if (!nc_pp_copy_piece(output, capacity, &cursor, &c, 1u))
            goto capacity_error;
        p++;
    }
    if (cursor >= capacity)
        goto capacity_error;
    output[cursor] = '\0';
    return 1;
capacity_error:
    if (diagnostic != NULL)
        diagnostic->file_id = file_id;
    (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_CAPACITY, line, column,
                  "macro-expanded line exceeds fixed capacity");
    return 0;
}

static void nc_pp_expr_skip(NcPpExpr *expr) {
    while (expr->pos < expr->length &&
           (expr->text[expr->pos] == ' ' || expr->text[expr->pos] == '\t' ||
            expr->text[expr->pos] == '\r'))
        expr->pos++;
}

static int nc_pp_expr_match(NcPpExpr *expr, const char *token) {
    uint32_t save = expr->pos;
    uint32_t i = 0u;
    nc_pp_expr_skip(expr);
    while (token[i] != '\0') {
        if (expr->pos + i >= expr->length || expr->text[expr->pos + i] != token[i]) {
            expr->pos = save;
            return 0;
        }
        i++;
    }
    expr->pos += i;
    return 1;
}

static uint32_t nc_pp_expr_or(NcPpExpr *expr, const NcMacroEnv *env, int evaluate);

static uint32_t nc_pp_expr_primary(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t value = 0u;
    nc_pp_expr_skip(expr);
    if (expr->pos >= expr->length) {
        expr->status =
            nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, expr->line,
                    expr->base_column + expr->pos, "expected preprocessing constant expression");
        return 0u;
    }
    if (expr->text[expr->pos] == '(') {
        expr->pos++;
        value = nc_pp_expr_or(expr, env, evaluate);
        nc_pp_expr_skip(expr);
        if (expr->pos >= expr->length || expr->text[expr->pos] != ')') {
            expr->status =
                nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, expr->line,
                        expr->base_column + expr->pos, "expected ')' in preprocessing condition");
            return 0u;
        }
        expr->pos++;
        return value;
    }
    if (expr->text[expr->pos] == '\'') {
        uint32_t p = ++expr->pos;
        char c;
        if (p >= expr->length)
            goto bad_char;
        c = expr->text[p++];
        if (c == '\\') {
            if (p >= expr->length)
                goto bad_char;
            value = nc_escape_value(expr->text[p++]);
        } else
            value = (uint32_t)(uint8_t)c;
        if (p >= expr->length || expr->text[p] != '\'')
            goto bad_char;
        expr->pos = p + 1u;
        return evaluate ? value : 0u;
    bad_char:
        expr->status = nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, expr->line,
                               expr->base_column + expr->pos,
                               "invalid character literal in preprocessing condition");
        return 0u;
    }
    if (nc_is_digit(expr->text[expr->pos])) {
        uint64_t accum = 0u;
        uint32_t base = 10u;
        if (expr->text[expr->pos] == '0' && expr->pos + 1u < expr->length &&
            (expr->text[expr->pos + 1u] == 'x' || expr->text[expr->pos + 1u] == 'X')) {
            base = 16u;
            expr->pos += 2u;
        }
        while (expr->pos < expr->length) {
            char c = expr->text[expr->pos];
            uint32_t digit;
            if (c >= '0' && c <= '9')
                digit = (uint32_t)(c - '0');
            else if (base == 16u && c >= 'a' && c <= 'f')
                digit = 10u + (uint32_t)(c - 'a');
            else if (base == 16u && c >= 'A' && c <= 'F')
                digit = 10u + (uint32_t)(c - 'A');
            else
                break;
            if (digit >= base)
                break;
            accum = accum * base + digit;
            expr->pos++;
        }
        return evaluate ? (uint32_t)accum : 0u;
    }
    if (nc_is_alpha(expr->text[expr->pos])) {
        uint32_t start = expr->pos;
        char name[NOVA_COMPILER_DEBUG_NAME];
        expr->pos++;
        while (expr->pos < expr->length &&
               (nc_is_alpha(expr->text[expr->pos]) || nc_is_digit(expr->text[expr->pos])))
            expr->pos++;
        if (expr->pos - start >= NOVA_COMPILER_DEBUG_NAME)
            return 0u;
        nc_copy_text(name, NOVA_COMPILER_DEBUG_NAME, expr->text + start, expr->pos - start);
        if (nc_streq(name, "defined")) {
            char macro_name[NOVA_COMPILER_DEBUG_NAME];
            nc_pp_expr_skip(expr);
            if (expr->pos < expr->length && expr->text[expr->pos] == '(') {
                expr->pos++;
                nc_pp_expr_skip(expr);
                if (!nc_pp_read_identifier(expr->text, &expr->pos, expr->length, macro_name,
                                           (uint32_t)sizeof(macro_name))) {
                    expr->status = nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION,
                                           expr->line, expr->base_column + expr->pos,
                                           "defined() requires a macro identifier");
                    return 0u;
                }
                nc_pp_expr_skip(expr);
                if (expr->pos >= expr->length || expr->text[expr->pos] != ')') {
                    expr->status =
                        nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, expr->line,
                                expr->base_column + expr->pos, "defined() requires closing ')'");
                    return 0u;
                }
                expr->pos++;
            } else {
                if (!nc_pp_read_identifier(expr->text, &expr->pos, expr->length, macro_name,
                                           (uint32_t)sizeof(macro_name))) {
                    expr->status = nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION,
                                           expr->line, expr->base_column + expr->pos,
                                           "defined requires a macro identifier");
                    return 0u;
                }
            }
            return evaluate && nc_pp_env_find(env, macro_name) >= 0 ? 1u : 0u;
        }
        /* Undefined identifiers in #if are the integer constant 0. */
        return 0u;
    }
    expr->status =
        nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, expr->line,
                expr->base_column + expr->pos, "unsupported token in preprocessing condition");
    return 0u;
}

static uint32_t nc_pp_expr_unary(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    nc_pp_expr_skip(expr);
    if (nc_pp_expr_match(expr, "!"))
        return evaluate ? (nc_pp_expr_unary(expr, env, evaluate) == 0u ? 1u : 0u)
                        : nc_pp_expr_unary(expr, env, 0);
    if (nc_pp_expr_match(expr, "~"))
        return evaluate ? ~nc_pp_expr_unary(expr, env, evaluate) : nc_pp_expr_unary(expr, env, 0);
    if (nc_pp_expr_match(expr, "+"))
        return nc_pp_expr_unary(expr, env, evaluate);
    if (nc_pp_expr_match(expr, "-"))
        return evaluate ? (uint32_t)(0u - nc_pp_expr_unary(expr, env, evaluate))
                        : nc_pp_expr_unary(expr, env, 0);
    return nc_pp_expr_primary(expr, env, evaluate);
}

static uint32_t nc_pp_expr_mul(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_unary(expr, env, evaluate);
    for (;;) {
        if (nc_pp_expr_match(expr, "*"))
            left = evaluate ? left * nc_pp_expr_unary(expr, env, evaluate)
                            : nc_pp_expr_unary(expr, env, 0);
        else if (nc_pp_expr_match(expr, "/")) {
            uint32_t right = nc_pp_expr_unary(expr, env, evaluate);
            if (evaluate && right == 0u) {
                expr->status = nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, expr->line,
                                       expr->base_column + expr->pos,
                                       "division by zero in preprocessing condition");
                return 0u;
            }
            if (evaluate)
                left /= right;
        } else if (nc_pp_expr_match(expr, "%")) {
            uint32_t right = nc_pp_expr_unary(expr, env, evaluate);
            if (evaluate && right == 0u) {
                expr->status = nc_diag(expr->diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, expr->line,
                                       expr->base_column + expr->pos,
                                       "modulo by zero in preprocessing condition");
                return 0u;
            }
            if (evaluate)
                left %= right;
        } else
            break;
    }
    return left;
}
static uint32_t nc_pp_expr_add(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_mul(expr, env, evaluate);
    for (;;) {
        if (nc_pp_expr_match(expr, "+")) {
            uint32_t right = nc_pp_expr_mul(expr, env, evaluate);
            if (evaluate)
                left += right;
        } else if (nc_pp_expr_match(expr, "-")) {
            uint32_t right = nc_pp_expr_mul(expr, env, evaluate);
            if (evaluate)
                left -= right;
        } else
            break;
    }
    return left;
}
static uint32_t nc_pp_expr_shift(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_add(expr, env, evaluate);
    for (;;) {
        if (nc_pp_expr_match(expr, "<<")) {
            uint32_t right = nc_pp_expr_add(expr, env, evaluate);
            if (evaluate)
                left <<= (right & 31u);
        } else if (nc_pp_expr_match(expr, ">>")) {
            uint32_t right = nc_pp_expr_add(expr, env, evaluate);
            if (evaluate)
                left >>= (right & 31u);
        } else
            break;
    }
    return left;
}
static uint32_t nc_pp_expr_rel(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_shift(expr, env, evaluate);
    for (;;) {
        if (nc_pp_expr_match(expr, "<=")) {
            uint32_t r = nc_pp_expr_shift(expr, env, evaluate);
            if (evaluate)
                left = (int32_t)left <= (int32_t)r ? 1u : 0u;
        } else if (nc_pp_expr_match(expr, ">=")) {
            uint32_t r = nc_pp_expr_shift(expr, env, evaluate);
            if (evaluate)
                left = (int32_t)left >= (int32_t)r ? 1u : 0u;
        } else if (nc_pp_expr_match(expr, "<")) {
            uint32_t r = nc_pp_expr_shift(expr, env, evaluate);
            if (evaluate)
                left = (int32_t)left < (int32_t)r ? 1u : 0u;
        } else if (nc_pp_expr_match(expr, ">")) {
            uint32_t r = nc_pp_expr_shift(expr, env, evaluate);
            if (evaluate)
                left = (int32_t)left > (int32_t)r ? 1u : 0u;
        } else
            break;
    }
    return left;
}
static uint32_t nc_pp_expr_eq(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_rel(expr, env, evaluate);
    for (;;) {
        if (nc_pp_expr_match(expr, "==")) {
            uint32_t r = nc_pp_expr_rel(expr, env, evaluate);
            if (evaluate)
                left = left == r ? 1u : 0u;
        } else if (nc_pp_expr_match(expr, "!=")) {
            uint32_t r = nc_pp_expr_rel(expr, env, evaluate);
            if (evaluate)
                left = left != r ? 1u : 0u;
        } else
            break;
    }
    return left;
}
static uint32_t nc_pp_expr_bitand(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_eq(expr, env, evaluate);
    for (;;) {
        uint32_t save = expr->pos;
        if (!nc_pp_expr_match(expr, "&"))
            break;
        if (expr->pos < expr->length && expr->text[expr->pos] == '&') {
            expr->pos = save;
            break;
        }
        {
            uint32_t r = nc_pp_expr_eq(expr, env, evaluate);
            if (evaluate)
                left &= r;
        }
    }
    return left;
}
static uint32_t nc_pp_expr_xor(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_bitand(expr, env, evaluate);
    while (nc_pp_expr_match(expr, "^")) {
        uint32_t r = nc_pp_expr_bitand(expr, env, evaluate);
        if (evaluate)
            left ^= r;
    }
    return left;
}
static uint32_t nc_pp_expr_bitor(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_xor(expr, env, evaluate);
    for (;;) {
        uint32_t save = expr->pos;
        if (!nc_pp_expr_match(expr, "|"))
            break;
        if (expr->pos < expr->length && expr->text[expr->pos] == '|') {
            expr->pos = save;
            break;
        }
        {
            uint32_t r = nc_pp_expr_xor(expr, env, evaluate);
            if (evaluate)
                left |= r;
        }
    }
    return left;
}
static uint32_t nc_pp_expr_and(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_bitor(expr, env, evaluate);
    while (nc_pp_expr_match(expr, "&&")) {
        int right_eval = evaluate && left != 0u;
        uint32_t r = nc_pp_expr_bitor(expr, env, right_eval);
        if (evaluate)
            left = (left != 0u && r != 0u) ? 1u : 0u;
    }
    return left;
}
static uint32_t nc_pp_expr_or(NcPpExpr *expr, const NcMacroEnv *env, int evaluate) {
    uint32_t left = nc_pp_expr_and(expr, env, evaluate);
    while (nc_pp_expr_match(expr, "||")) {
        int right_eval = evaluate && left == 0u;
        uint32_t r = nc_pp_expr_and(expr, env, right_eval);
        if (evaluate)
            left = (left != 0u || r != 0u) ? 1u : 0u;
    }
    return left;
}

static int nc_pp_eval_condition(const NcMacroEnv *env, NcPpExpansionScratch *expansion_scratch,
                                char *expanded, const char *source, uint32_t start, uint32_t end,
                                uint32_t file_id, uint32_t line, uint32_t column,
                                NovaCompilerDiagnostic *diagnostic, uint32_t *value_out) {
    NcPpExpr expr;
    uint32_t trimmed_end = nc_pp_trim_right(source, start, end);
    start = nc_pp_trim_left(source, start, trimmed_end);
    if (trimmed_end - start >= NOVAC_PP_MAX_LINE_BYTES) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_MACRO_CAPACITY, line, column,
                      "#if condition exceeds fixed capacity");
        return 0;
    }
    if (!nc_pp_expand_text_depth(env, expansion_scratch, source + start, trimmed_end - start,
                                 expanded, NOVAC_PP_MAX_LINE_BYTES, 0u, file_id, line, column, 1,
                                 diagnostic))
        return 0;
    expr.text = expanded;
    expr.length = (uint32_t)nc_strlen(expanded);
    expr.pos = 0u;
    expr.file_id = file_id;
    expr.line = line;
    expr.base_column = column;
    expr.diagnostic = diagnostic;
    expr.status = NOVAC_OK;
    *value_out = nc_pp_expr_or(&expr, env, 1);
    nc_pp_expr_skip(&expr);
    if (expr.status != NOVAC_OK)
        return 0;
    if (expr.pos != expr.length) {
        if (diagnostic != NULL)
            diagnostic->file_id = file_id;
        (void)nc_diag(diagnostic, NOVAC_ERR_CONSTANT_EXPRESSION, line, column + expr.pos,
                      "trailing tokens in preprocessing condition");
        return 0;
    }
    return 1;
}

static int nc_pp_cond_active(const NcPpConditional *stack, uint32_t depth) {
    return depth == 0u ? 1 : stack[depth - 1u].active != 0u;
}

static int nc_pp_scan_file(NcPpProject *project, uint32_t file_id);

static int nc_pp_handle_conditional(NcMacroEnv *env, NcPpConditional *stack,
                                    NcPpExpansionScratch *expansion_scratch, char *line_buffer,
                                    uint32_t *depth, const char *directive, const char *source,
                                    uint32_t *p, uint32_t line_end, uint32_t file_id, uint32_t line,
                                    uint32_t column, NovaCompilerDiagnostic *diagnostic) {
    if (nc_streq(directive, "if") || nc_streq(directive, "ifdef") ||
        nc_streq(directive, "ifndef")) {
        uint32_t parent_active = *depth == 0u ? 1u : stack[*depth - 1u].active;
        uint32_t cond = 0u;
        if (*depth >= NOVAC_PP_MAX_CONDITIONAL_DEPTH) {
            if (diagnostic != NULL)
                diagnostic->file_id = file_id;
            (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                          "conditional preprocessing depth exceeded");
            return 0;
        }
        if (nc_streq(directive, "if")) {
            if (parent_active != 0u &&
                !nc_pp_eval_condition(env, expansion_scratch, line_buffer, source, *p, line_end,
                                      file_id, line, column, diagnostic, &cond))
                return 0;
        } else {
            char name[NOVA_COMPILER_DEBUG_NAME];
            *p = nc_pp_trim_left(source, *p, line_end);
            if (!nc_pp_read_identifier(source, p, line_end, name, (uint32_t)sizeof(name))) {
                if (diagnostic != NULL)
                    diagnostic->file_id = file_id;
                (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                              "#ifdef/#ifndef requires a macro identifier");
                return 0;
            }
            *p = nc_pp_trim_left(source, *p, line_end);
            if (*p != line_end) {
                if (diagnostic != NULL)
                    diagnostic->file_id = file_id;
                (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                              "tokens after #ifdef/#ifndef are not supported");
                return 0;
            }
            cond = nc_pp_env_find(env, name) >= 0 ? 1u : 0u;
            if (nc_streq(directive, "ifndef"))
                cond = cond == 0u ? 1u : 0u;
        }
        stack[*depth].parent_active = parent_active;
        stack[*depth].active = parent_active != 0u && cond != 0u ? 1u : 0u;
        stack[*depth].branch_taken = stack[*depth].active;
        stack[*depth].saw_else = 0u;
        (*depth)++;
        return 1;
    }
    if (nc_streq(directive, "else")) {
        NcPpConditional *current;
        *p = nc_pp_trim_left(source, *p, line_end);
        if (*p != line_end || *depth == 0u) {
            if (diagnostic != NULL)
                diagnostic->file_id = file_id;
            (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                          "invalid #else directive");
            return 0;
        }
        current = &stack[*depth - 1u];
        if (current->saw_else != 0u) {
            if (diagnostic != NULL)
                diagnostic->file_id = file_id;
            (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                          "duplicate #else in conditional group");
            return 0;
        }
        current->saw_else = 1u;
        current->active = current->parent_active != 0u && current->branch_taken == 0u ? 1u : 0u;
        current->branch_taken = 1u;
        return 1;
    }
    if (nc_streq(directive, "endif")) {
        *p = nc_pp_trim_left(source, *p, line_end);
        if (*p != line_end || *depth == 0u) {
            if (diagnostic != NULL)
                diagnostic->file_id = file_id;
            (void)nc_diag(diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                          "invalid #endif directive");
            return 0;
        }
        (*depth)--;
        return 1;
    }
    return 0;
}

static int nc_pp_scan_file(NcPpProject *project, uint32_t file_id) {
    const char *source;
    uint32_t offset = 0u;
    uint32_t line = 1u;
    NcMacroEnv *env;
    NcPpConditional *conditional;
    char *line_buffer;
    uint32_t conditional_depth = 0u;
    int in_block_comment = 0;
    if (file_id >= project->unit_count)
        return 1;
    if (project->scan_scratch == NULL || project->expansion_scratch == NULL) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_INTERNAL, 0u, 0u,
                      "preprocessor scan scratch is unavailable");
        return 0;
    }
    env = &project->scan_scratch->env[file_id];
    conditional = project->scan_scratch->conditional[file_id];
    line_buffer = project->scan_scratch->line_buffer;
    if (project->scan_state[file_id] == 2u)
        return 1;
    if (project->scan_state[file_id] == 1u) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_INCLUDE_CYCLE, 0u, 0u,
                      "include cycle detected; recursive guarded include graphs remain deferred");
        return 0;
    }
    project->scan_state[file_id] = 1u;
    project->active_units[file_id] = 1u;
    env->count = 0u;
    source = project->units[file_id].source;
    while (source[offset] != '\0') {
        uint32_t line_start = offset;
        uint32_t line_end = offset;
        uint32_t p;
        while (source[line_end] != '\0' && source[line_end] != '\n')
            line_end++;
        p = nc_pp_trim_left(source, line_start, line_end);
        if (!in_block_comment && p < line_end && source[p] == '#') {
            uint32_t column = p - line_start + 1u;
            char directive[16];
            p++;
            p = nc_pp_trim_left(source, p, line_end);
            if (!nc_pp_read_identifier(source, &p, line_end, directive,
                                       (uint32_t)sizeof(directive))) {
                if (project->diagnostic != NULL)
                    project->diagnostic->file_id = file_id;
                (void)nc_diag(project->diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                              "expected preprocessing directive name");
                return 0;
            }
            if (nc_streq(directive, "if") || nc_streq(directive, "ifdef") ||
                nc_streq(directive, "ifndef") || nc_streq(directive, "else") ||
                nc_streq(directive, "endif")) {
                if (!nc_pp_handle_conditional(env, conditional, project->expansion_scratch,
                                              line_buffer, &conditional_depth, directive, source,
                                              &p, line_end, file_id, line, column,
                                              project->diagnostic))
                    return 0;
            } else if (nc_pp_cond_active(conditional, conditional_depth)) {
                if (nc_streq(directive, "include")) {
                    uint32_t target;
                    if (!nc_pp_parse_include_target(project, file_id, source, &p, line_end, line,
                                                    column, &target))
                        return 0;
                    if (project->dependency_count >= NOVA_COMPILER_MAX_INCLUDE_DEPENDENCIES) {
                        if (project->diagnostic != NULL)
                            project->diagnostic->file_id = file_id;
                        (void)nc_diag(project->diagnostic, NOVAC_ERR_INCLUDE_CAPACITY, line, column,
                                      "include dependency capacity exceeded");
                        return 0;
                    }
                    if (!nc_pp_scan_file(project, target))
                        return 0;
                    project->dependencies[project->dependency_count].from_file_id = file_id;
                    project->dependencies[project->dependency_count].to_file_id = target;
                    project->dependencies[project->dependency_count].line = line;
                    project->dependencies[project->dependency_count].column = column;
                    project->dependency_count++;
                    if (!nc_pp_import_file_effects(project, target, env, 0u))
                        return 0;
                } else if (nc_streq(directive, "define")) {
                    NcMacro macro;
                    if (!nc_pp_parse_macro_definition(source, &p, line_end, file_id, line, column,
                                                      &macro, project->diagnostic))
                        return 0;
                    if (!nc_pp_env_define(env, &macro, project->diagnostic))
                        return 0;
                    if (!nc_pp_record_event(project, &macro, 0u))
                        return 0;
                } else if (nc_streq(directive, "undef")) {
                    NcMacro macro;
                    macro.kind = NOVAC_MACRO_OBJECT;
                    macro.parameter_count = 0u;
                    macro.replacement[0] = '\0';
                    macro.file_id = file_id;
                    macro.line = line;
                    macro.column = column;
                    if (!nc_pp_parse_undef_name(source, &p, line_end, macro.name,
                                                (uint32_t)sizeof(macro.name), file_id, line, column,
                                                project->diagnostic))
                        return 0;
                    nc_pp_env_undef(env, macro.name);
                    if (!nc_pp_record_event(project, &macro, 1u))
                        return 0;
                } else {
                    if (project->diagnostic != NULL)
                        project->diagnostic->file_id = file_id;
                    (void)nc_diag(project->diagnostic, NOVAC_ERR_PREPROCESSOR, line, column,
                                  "unsupported preprocessing directive; supported: "
                                  "include/define/undef/if/ifdef/ifndef/else/endif");
                    return 0;
                }
            }
        }
        nc_update_block_comment_state(source, line_start, line_end, &in_block_comment);
        offset = source[line_end] == '\n' ? line_end + 1u : line_end;
        line++;
    }
    if (conditional_depth != 0u) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_PREPROCESSOR, line, 1u,
                      "unterminated preprocessing conditional group");
        return 0;
    }
    project->scan_state[file_id] = 2u;
    return 1;
}

static int32_t nc_pp_collect_project(const NovaTranslationUnit *units, uint32_t unit_count,
                                     NcPpProject *project, NovaCompilerDiagnostic *diagnostic) {
    uint32_t i;
    project->units = units;
    project->unit_count = unit_count;
    project->dependency_count = 0u;
    project->event_count = 0u;
    project->diagnostic = diagnostic;
    for (i = 0u; i < NOVA_COMPILER_MAX_TRANSLATION_UNITS; i++) {
        project->scan_state[i] = 0u;
        project->active_units[i] = 0u;
    }
    for (i = 0u; i < unit_count; i++) {
        if (!nc_is_source_name(units[i].name))
            continue;
        if (!nc_pp_scan_file(project, i))
            return diagnostic != NULL ? diagnostic->status : NOVAC_ERR_PREPROCESSOR;
    }
    return NOVAC_OK;
}

static int nc_pp_make_source(const NcPpProject *project, uint32_t file_id, char *output,
                             uint32_t capacity) {
    const char *source = project->units[file_id].source;
    uint32_t offset = 0u;
    uint32_t line = 1u;
    uint32_t cursor = 0u;
    NcPpFileScratch *file_scratch = project->make_scratch;
    NcMacroEnv *env;
    NcPpConditional *conditional;
    char *line_buffer;
    uint32_t conditional_depth = 0u;
    int in_block_comment = 0;
    if (file_scratch == NULL || project->expansion_scratch == NULL) {
        if (project->diagnostic != NULL)
            project->diagnostic->file_id = file_id;
        (void)nc_diag(project->diagnostic, NOVAC_ERR_INTERNAL, 0u, 0u,
                      "preprocessor source scratch is unavailable");
        return 0;
    }
    env = &file_scratch->env;
    conditional = file_scratch->conditional;
    line_buffer = file_scratch->line_buffer;
    env->count = 0u;
    while (source[offset] != '\0') {
        uint32_t line_start = offset;
        uint32_t line_end = offset;
        uint32_t p;
        int directive_line = 0;
        while (source[line_end] != '\0' && source[line_end] != '\n')
            line_end++;
        p = nc_pp_trim_left(source, line_start, line_end);
        if (!in_block_comment && p < line_end && source[p] == '#') {
            uint32_t column = p - line_start + 1u;
            char directive[16];
            directive_line = 1;
            p++;
            p = nc_pp_trim_left(source, p, line_end);
            if (!nc_pp_read_identifier(source, &p, line_end, directive,
                                       (uint32_t)sizeof(directive)))
                return 0;
            if (nc_streq(directive, "if") || nc_streq(directive, "ifdef") ||
                nc_streq(directive, "ifndef") || nc_streq(directive, "else") ||
                nc_streq(directive, "endif")) {
                if (!nc_pp_handle_conditional(env, conditional, project->expansion_scratch,
                                              line_buffer, &conditional_depth, directive, source,
                                              &p, line_end, file_id, line, column,
                                              project->diagnostic))
                    return 0;
            } else if (nc_pp_cond_active(conditional, conditional_depth)) {
                if (nc_streq(directive, "include")) {
                    uint32_t target;
                    if (!nc_pp_parse_include_target(project, file_id, source, &p, line_end, line,
                                                    column, &target))
                        return 0;
                    if (!nc_pp_import_file_effects(project, target, env, 0u))
                        return 0;
                } else if (nc_streq(directive, "define")) {
                    NcMacro macro;
                    if (!nc_pp_parse_macro_definition(source, &p, line_end, file_id, line, column,
                                                      &macro, project->diagnostic))
                        return 0;
                    if (!nc_pp_env_define(env, &macro, project->diagnostic))
                        return 0;
                } else if (nc_streq(directive, "undef")) {
                    char name[NOVA_COMPILER_DEBUG_NAME];
                    if (!nc_pp_parse_undef_name(source, &p, line_end, name, (uint32_t)sizeof(name),
                                                file_id, line, column, project->diagnostic))
                        return 0;
                    nc_pp_env_undef(env, name);
                } else
                    return 0;
            }
        }
        if (!directive_line && nc_pp_cond_active(conditional, conditional_depth)) {
            if (line_end - line_start >= NOVAC_PP_MAX_LINE_BYTES) {
                if (project->diagnostic != NULL)
                    project->diagnostic->file_id = file_id;
                (void)nc_diag(project->diagnostic, NOVAC_ERR_MACRO_CAPACITY, line, 1u,
                              "source line exceeds preprocessing fixed capacity");
                return 0;
            }
            if (!nc_pp_expand_text_depth(env, project->expansion_scratch, source + line_start,
                                         line_end - line_start, line_buffer,
                                         NOVAC_PP_MAX_LINE_BYTES, 0u, file_id, line, 1u, 0,
                                         project->diagnostic))
                return 0;
            if (!nc_pp_copy_piece(output, capacity, &cursor, line_buffer,
                                  (uint32_t)nc_strlen(line_buffer)))
                goto output_capacity;
        }
        if (source[line_end] == '\n') {
            char nl = '\n';
            if (!nc_pp_copy_piece(output, capacity, &cursor, &nl, 1u))
                goto output_capacity;
        }
        nc_update_block_comment_state(source, line_start, line_end, &in_block_comment);
        offset = source[line_end] == '\n' ? line_end + 1u : line_end;
        line++;
    }
    if (conditional_depth != 0u)
        return 0;
    if (cursor >= capacity)
        goto output_capacity;
    output[cursor] = '\0';
    return 1;
output_capacity:
    if (project->diagnostic != NULL)
        project->diagnostic->file_id = file_id;
    (void)nc_diag(project->diagnostic, NOVAC_ERR_SOURCE_CAPACITY, line, 1u,
                  "preprocessed translation unit exceeds fixed source capacity");
    return 0;
}

static int nc_publish_macros(const NcPpProject *project, NovaMacroDefinition *output,
                             uint32_t capacity, uint32_t *count_out,
                             NovaCompilerDiagnostic *diagnostic) {
    uint32_t i;
    uint32_t count = 0u;
    for (i = 0u; i < project->event_count; i++)
        if (project->events[i].is_undef == 0u)
            count++;
    if (count_out != NULL)
        *count_out = count;
    if (output == NULL)
        return 1;
    if (capacity < count) {
        return nc_diag(diagnostic, NOVAC_ERR_DEBUG_CAPACITY, 0u, 0u,
                       "macro metadata buffer is too small") == NOVAC_OK;
    }
    count = 0u;
    for (i = 0u; i < project->event_count; i++) {
        const NcMacroEvent *event = &project->events[i];
        NovaMacroDefinition *out;
        if (event->is_undef != 0u)
            continue;
        out = &output[count++];
        nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, event->macro.name,
                     (uint32_t)nc_strlen(event->macro.name));
        nc_copy_text(out->replacement, NOVA_COMPILER_MACRO_REPLACEMENT, event->macro.replacement,
                     (uint32_t)nc_strlen(event->macro.replacement));
        out->kind = event->macro.kind;
        out->parameter_count = event->macro.parameter_count;
        out->file_id = event->macro.file_id;
        out->line = event->macro.line;
        out->column = event->macro.column;
        out->flags = nc_is_header_name(project->units[event->macro.file_id].name)
                         ? NOVAC_MACRO_FLAG_HEADER
                         : NOVAC_MACRO_FLAG_NONE;
    }
    return 1;
}

static int nc_parse_preprocessed_unit_with_dependencies(NcParser *parser,
                                                        const NcPpProject *project,
                                                        uint32_t file_id, uint8_t *parsed_units,
                                                        char *scratch, uint32_t scratch_capacity) {
    uint32_t i;
    if (file_id >= project->unit_count || project->active_units[file_id] == 0u)
        return 1;
    if (parsed_units[file_id] != 0u)
        return 1;
    for (i = 0u; i < project->dependency_count; i++) {
        if (project->dependencies[i].from_file_id != file_id)
            continue;
        if (!nc_parse_preprocessed_unit_with_dependencies(parser, project,
                                                          project->dependencies[i].to_file_id,
                                                          parsed_units, scratch, scratch_capacity))
            return 0;
    }
    if (!nc_pp_make_source(project, file_id, scratch, scratch_capacity))
        return 0;
    if (!nc_parse_translation_unit(parser, scratch, file_id,
                                   nc_is_header_name(project->units[file_id].name) ? 1u : 0u))
        return 0;
    parsed_units[file_id] = 1u;
    return 1;
}

/* ------------------------------ public API ------------------------------- */
static void nc_parser_init(NcParser *parser, NovaCompilerDiagnostic *diagnostic) {
    parser->node_count = 0u;
    parser->statement_count = 0u;
    parser->expression_count = 0u;
    parser->struct_count = 0u;
    parser->function_count = 0u;
    parser->global_count = 0u;
    parser->constant_count = 0u;
    parser->typedef_count = 0u;
    parser->static_local_count = 0u;
    parser->string_count = 0u;
    parser->unit_count = 0u;
    parser->block_depth = 0u;
    parser->loop_depth = 0u;
    parser->switch_depth = 0u;
    parser->declaration_epoch = 0u;
    parser->current_function_index = NOVAC_INVALID_NODE;
    parser->scope_serial = 0u;
    {
        uint32_t si;
        for (si = 0u; si < NOVAC_MAX_SCOPE_DEPTH; si++)
            parser->scope_stack[si] = 0u;
    }
    parser->current_file_id = 0u;
    parser->current_is_header = 0u;
    parser->diagnostic = diagnostic;
    parser->status = NOVAC_OK;
    parser->current = nc_make_token(NC_TOK_EOF, "", 0u, 1u, 1u, 0u);
    parser->previous = parser->current;
}
static void nc_compute_include_visibility(NcParser *parser, uint32_t unit_count,
                                          const NovaIncludeDependency *dependencies,
                                          uint32_t dependency_count) {
    uint32_t i;
    uint32_t pass;
    parser->unit_count = unit_count;
    for (i = 0u; i < NOVA_COMPILER_MAX_TRANSLATION_UNITS; i++)
        parser->include_visibility[i] = i < unit_count ? nc_file_bit(i) : 0u;
    for (pass = 0u; pass < unit_count; pass++) {
        int changed = 0;
        for (i = 0u; i < dependency_count; i++) {
            uint32_t from = dependencies[i].from_file_id;
            uint32_t to = dependencies[i].to_file_id;
            uint32_t merged =
                parser->include_visibility[from] | parser->include_visibility[to] | nc_file_bit(to);
            if (merged != parser->include_visibility[from]) {
                parser->include_visibility[from] = merged;
                changed = 1;
            }
        }
        if (!changed)
            break;
    }
}
static int32_t nc_validate_translation_units(const NovaTranslationUnit *units, uint32_t unit_count,
                                             NovaCompilerDiagnostic *diagnostic) {
    uint32_t i;
    uint32_t j;
    size_t total_source_bytes = 0u;
    if (units == NULL || unit_count == 0u) {
        return nc_diag(diagnostic, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "project requires at least one translation unit");
    }

    if (unit_count > NOVA_COMPILER_MAX_TRANSLATION_UNITS) {
        return nc_diag(diagnostic, NOVAC_ERR_TRANSLATION_UNIT_CAPACITY, 0u, 0u,
                       "translation-unit capacity exceeded");
    }
    for (i = 0u; i < unit_count; i++) {
        size_t source_bytes;
        size_t name_bytes;
        if (diagnostic != NULL)
            diagnostic->file_id = i;
        if (units[i].name == NULL || units[i].source == NULL) {
            return nc_diag(diagnostic, NOVAC_ERR_ARGUMENT, 0u, 0u,
                           "translation unit requires name and source");
        }
        name_bytes = nc_strlen(units[i].name);
        if (name_bytes == 0u || name_bytes >= NOVA_COMPILER_TRANSLATION_UNIT_NAME) {
            return nc_diag(diagnostic, NOVAC_ERR_ARGUMENT, 0u, 0u,
                           "translation-unit name is empty or too long");
        }
        if (!nc_is_source_name(units[i].name) && !nc_is_header_name(units[i].name)) {
            return nc_diag(diagnostic, NOVAC_ERR_ARGUMENT, 0u, 0u,
                           "project files must use .c or .h extension");
        }
        source_bytes = nc_strlen(units[i].source);
        if (source_bytes > NOVA_COMPILER_MAX_SOURCE_BYTES) {
            return nc_diag(diagnostic, NOVAC_ERR_SOURCE_CAPACITY, 0u, 0u,
                           "translation-unit source exceeds per-file capacity");
        }
        if (total_source_bytes > (size_t)NOVA_COMPILER_MAX_PROJECT_SOURCE_BYTES - source_bytes) {
            return nc_diag(diagnostic, NOVAC_ERR_SOURCE_CAPACITY, 0u, 0u,
                           "project source exceeds fixed total capacity");
        }
        total_source_bytes += source_bytes;
        for (j = 0u; j < i; j++) {
            char left[NOVA_COMPILER_TRANSLATION_UNIT_NAME];
            char right[NOVA_COMPILER_TRANSLATION_UNIT_NAME];
            if (!nc_normalize_path(units[i].name, left, (uint32_t)sizeof(left)) ||
                !nc_normalize_path(units[j].name, right, (uint32_t)sizeof(right))) {
                return nc_diag(diagnostic, NOVAC_ERR_ARGUMENT, 0u, 0u,
                               "translation-unit path cannot be normalized");
            }
            if (nc_streq(left, right)) {
                return nc_diag(diagnostic, NOVAC_ERR_DUPLICATE_TRANSLATION_UNIT, 0u, 0u,
                               "duplicate normalized project-file name");
            }
        }
    }
    return NOVAC_OK;
}
static int nc_publish_debug_strings(const NcParser *parser, NovaDebugString *output,
                                    uint32_t capacity, uint32_t *count_out,
                                    NovaCompilerDiagnostic *diagnostic) {
    uint32_t i;
    if (count_out != NULL)
        *count_out = parser->string_count;
    if (output == NULL)
        return 1;
    if (capacity < parser->string_count) {
        return nc_diag(diagnostic, NOVAC_ERR_DEBUG_CAPACITY, 0u, 0u,
                       "debug string buffer is too small") == NOVAC_OK;
    }
    for (i = 0u; i < parser->string_count; i++) {
        const NcStringLiteral *literal = &parser->strings[i];
        NovaDebugString *out = &output[i];
        uint32_t j = 0u;
        while (j + 1u < NOVA_COMPILER_DEBUG_NAME && j < literal->length &&
               literal->bytes[j] != 0u) {
            uint8_t c = literal->bytes[j];
            out->preview[j] = (char)(c >= 32u && c <= 126u ? c : '.');

            j++;
        }
        out->preview[j] = '\0';
        out->address = literal->address;
        out->byte_size = literal->length;
        out->file_id = literal->file_id;
        out->line = literal->line;
        out->column = literal->column;
    }
    return 1;
}
static uint32_t nc_declaration_struct_index(NcTypeSpec type) {
    if (type.kind == NC_TYPE_STRUCT || type.kind == NC_TYPE_UNION ||
        type.kind == NC_TYPE_STRUCT_PTR || type.kind == NC_TYPE_UNION_PTR)
        return type.struct_index;
    if (type.kind == NC_TYPE_ARRAY &&
        (type.element_kind == NC_TYPE_STRUCT || type.element_kind == NC_TYPE_UNION))
        return type.struct_index;
    return NOVAC_INVALID_NODE;
}
static int nc_publish_declarations(const NcParser *parser, NovaDeclarationMetadata *output,
                                   uint32_t capacity, uint32_t *count_out,
                                   NovaCompilerDiagnostic *diagnostic) {
    uint32_t total;
    uint32_t cursor = 0u;
    uint32_t i;
    if (parser == NULL)
        return 0;
    total = parser->function_count + parser->global_count + parser->typedef_count +
            parser->static_local_count;
    if (count_out != NULL)
        *count_out = total;
    if (output == NULL)
        return 1;
    if (capacity < total || total > NOVA_COMPILER_MAX_DECLARATIONS) {
        return nc_diag(diagnostic, NOVAC_ERR_DEBUG_CAPACITY, 0u, 0u,
                       "declaration metadata buffer is too small") == NOVAC_OK;
    }
    for (i = 0u; i < parser->function_count; i++) {
        const NcFunction *function = &parser->functions[i];
        NovaDeclarationMetadata *out = &output[cursor++];
        nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, function->name,
                     (uint32_t)nc_strlen(function->name));
        out->kind = NOVAC_DECL_FUNCTION;
        out->linkage = function->linkage == NC_LINKAGE_INTERNAL ? NOVAC_LINKAGE_INTERNAL
                                                                : NOVAC_LINKAGE_EXTERNAL;
        out->qualifiers = function->return_type.qualifiers;
        out->pointee_qualifiers = function->return_type.element_qualifiers;
        out->type = nc_debug_type(function->return_type);
        out->struct_index = nc_declaration_struct_index(function->return_type);
        out->target_index = i;
        out->file_id = function->file_id;
        out->line = function->line;
        out->column = function->column;
        out->flags = function->is_defined != 0u ? NOVAC_DECL_FLAG_DEFINITION : NOVAC_DECL_FLAG_NONE;
    }
    for (i = 0u; i < parser->global_count; i++) {
        const NcGlobal *global = &parser->globals[i];
        NovaDeclarationMetadata *out = &output[cursor++];
        nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, global->name,
                     (uint32_t)nc_strlen(global->name));
        out->kind = NOVAC_DECL_GLOBAL;
        out->linkage = global->linkage == NC_LINKAGE_INTERNAL ? NOVAC_LINKAGE_INTERNAL
                                                              : NOVAC_LINKAGE_EXTERNAL;
        out->qualifiers = global->type.qualifiers;
        out->pointee_qualifiers = global->type.element_qualifiers;
        out->type = nc_debug_type(global->type);
        out->struct_index = nc_declaration_struct_index(global->type);
        out->target_index = i;
        out->file_id = global->file_id;
        out->line = global->line;
        out->column = global->column;
        out->flags = global->is_defined != 0u ? NOVAC_DECL_FLAG_DEFINITION : NOVAC_DECL_FLAG_NONE;
        if (global->has_tentative_definition != 0u && global->has_strong_definition == 0u)
            out->flags |= NOVAC_DECL_FLAG_TENTATIVE;
    }
    for (i = 0u; i < parser->typedef_count; i++) {
        const NcTypedef *alias = &parser->typedefs[i];
        NovaDeclarationMetadata *out = &output[cursor++];
        nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, alias->name,
                     (uint32_t)nc_strlen(alias->name));
        out->kind = NOVAC_DECL_TYPEDEF;
        out->linkage = NOVAC_LINKAGE_NONE;
        out->qualifiers = alias->type.qualifiers;
        out->pointee_qualifiers = alias->type.element_qualifiers;
        out->type = nc_debug_type(alias->type);
        out->struct_index = nc_declaration_struct_index(alias->type);
        out->target_index = NOVAC_INVALID_NODE;
        out->file_id = alias->declaration_file_id;
        out->line = alias->line;
        out->column = alias->column;
        out->flags =
            alias->is_block_scope != 0u ? NOVAC_DECL_FLAG_BLOCK_SCOPE : NOVAC_DECL_FLAG_NONE;
    }
    for (i = 0u; i < parser->static_local_count; i++) {
        const NcStaticLocal *local = &parser->static_locals[i];
        NovaDeclarationMetadata *out = &output[cursor++];
        nc_copy_text(out->name, NOVA_COMPILER_DEBUG_NAME, local->name,
                     (uint32_t)nc_strlen(local->name));
        out->kind = NOVAC_DECL_STATIC_LOCAL;
        out->linkage = NOVAC_LINKAGE_NONE;
        out->qualifiers = local->type.qualifiers;
        out->pointee_qualifiers = local->type.element_qualifiers;
        out->type = nc_debug_type(local->type);
        out->struct_index = nc_declaration_struct_index(local->type);
        out->target_index = i;
        out->file_id = local->file_id;
        out->line = local->line;
        out->column = local->column;
        out->flags = NOVAC_DECL_FLAG_DEFINITION | NOVAC_DECL_FLAG_BLOCK_SCOPE |
                     NOVAC_DECL_FLAG_STATIC_STORAGE;
    }
    return 1;
}
static int32_t nc_compile_project_debug_ex5_workspace(
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
    uint32_t macro_capacity, uint32_t *macro_count_out, NcCompileWorkspace *workspace) {
    NcParser *parser = &workspace->parser;
    NcCodegen *cg = &workspace->codegen;
    NcPpProject *pp = &workspace->preprocessor;
    char *preprocessed_source = workspace->preprocessed_source;
    uint32_t i;
    int main_index;
    int32_t unit_status;
    nc_diag_clear(diagnostic_out);
    if (output == NULL || output_capacity == 0u || result_out == NULL) {
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u, "invalid compiler arguments");
    }
    if ((source_map == NULL && source_map_capacity != 0u) ||
        (symbols == NULL && symbol_capacity != 0u) ||
        (functions == NULL && function_capacity != 0u) ||
        (globals == NULL && global_capacity != 0u) ||
        (references == NULL && reference_capacity != 0u) ||
        (includes == NULL && include_capacity != 0u) ||
        (debug_strings == NULL && debug_string_capacity != 0u) ||
        (declarations == NULL && declaration_capacity != 0u) ||
        (macros == NULL && macro_capacity != 0u)) {

        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "debug/analysis buffer pointer/capacity mismatch");
    }
    if (source_map != NULL && source_map_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "source-map count output is required");
    if (symbols != NULL && symbol_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "symbol count output is required");
    if (functions != NULL && function_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "function count output is required");
    if (globals != NULL && global_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "global count output is required");
    if (references != NULL && reference_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "reference count output is required");
    if (includes != NULL && include_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "include-dependency count output is required");
    if (debug_strings != NULL && debug_string_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "debug-string count output is required");
    if (declarations != NULL && declaration_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "declaration count output is required");
    if (macros != NULL && macro_count_out == NULL)
        return nc_diag(diagnostic_out, NOVAC_ERR_ARGUMENT, 0u, 0u,
                       "macro count output is required");
    result_out->bytecode_size = 0u;
    result_out->ast_node_count = 0u;
    result_out->local_count = 0u;
    result_out->statement_count = 0u;
    result_out->expression_count = 0u;
    if (source_map_count_out != NULL)
        *source_map_count_out = 0u;
    if (symbol_count_out != NULL)
        *symbol_count_out = 0u;
    if (function_count_out != NULL)
        *function_count_out = 0u;
    if (global_count_out != NULL)
        *global_count_out = 0u;
    if (reference_count_out != NULL)
        *reference_count_out = 0u;
    if (include_count_out != NULL)
        *include_count_out = 0u;
    if (debug_string_count_out != NULL)
        *debug_string_count_out = 0u;
    if (declaration_count_out != NULL)
        *declaration_count_out = 0u;
    if (macro_count_out != NULL)
        *macro_count_out = 0u;
    pp->expansion_scratch = &workspace->pp_expansion;
    pp->scan_scratch = &workspace->pp_scan;
    pp->make_scratch = &workspace->pp_make;
    unit_status = nc_validate_translation_units(units, unit_count, diagnostic_out);
    if (unit_status != NOVAC_OK)
        return unit_status;
    unit_status = nc_pp_collect_project(units, unit_count, pp, diagnostic_out);
    if (unit_status != NOVAC_OK)
        return unit_status;
    if (includes != NULL && include_capacity < pp->dependency_count)
        return nc_diag(diagnostic_out, NOVAC_ERR_INCLUDE_CAPACITY, 0u, 0u,
                       "include dependency output buffer is too small");
    if (includes != NULL) {
        for (i = 0u; i < pp->dependency_count; i++)
            includes[i] = pp->dependencies[i];
    }
    if (include_count_out != NULL)
        *include_count_out = pp->dependency_count;
    if (!nc_publish_macros(pp, macros, macro_capacity, macro_count_out, diagnostic_out))
        return diagnostic_out != NULL ? diagnostic_out->status : NOVAC_ERR_DEBUG_CAPACITY;
    nc_parser_init(parser, diagnostic_out);
    nc_compute_include_visibility(parser, unit_count, pp->dependencies, pp->dependency_count);
    {
        uint8_t parsed_units[NOVA_COMPILER_MAX_TRANSLATION_UNITS];
        for (i = 0u; i < NOVA_COMPILER_MAX_TRANSLATION_UNITS; i++)
            parsed_units[i] = 0u;
        for (i = 0u; i < unit_count; i++) {
            if (pp->active_units[i] == 0u || !nc_is_source_name(units[i].name))
                continue;
            if (!nc_parse_preprocessed_unit_with_dependencies(parser, pp, i, parsed_units,
                                                              preprocessed_source,
                                                              NOVA_COMPILER_MAX_SOURCE_BYTES + 1u))
                return parser->status != NOVAC_OK
                           ? parser->status
                           : (diagnostic_out != NULL ? diagnostic_out->status
                                                     : NOVAC_ERR_PREPROCESSOR);
        }
    }
    if (!nc_validate_project_program(parser))
        return parser->status;
    if (!nc_publish_debug_strings(parser, debug_strings, debug_string_capacity,
                                  debug_string_count_out, diagnostic_out))
        return diagnostic_out != NULL ? diagnostic_out->status : NOVAC_ERR_DEBUG_CAPACITY;
    if (!nc_publish_declarations(parser, declarations, declaration_capacity, declaration_count_out,
                                 diagnostic_out))
        return diagnostic_out != NULL ? diagnostic_out->status : NOVAC_ERR_DEBUG_CAPACITY;
    main_index = nc_parser_function_index_external(parser, "main");
    if (main_index < 0) {
        if (diagnostic_out != NULL)
            diagnostic_out->file_id = 0u;
        return nc_diag(diagnostic_out, NOVAC_ERR_PARSE, 0u, 0u,
                       "project must define int main(void)");
    }
    if (parser->functions[main_index].parameter_count != 0u) {
        if (diagnostic_out != NULL)
            diagnostic_out->file_id = parser->functions[main_index].file_id;
        return nc_diag(

            diagnostic_out, NOVAC_ERR_ARGUMENT_COUNT, parser->functions[main_index].line,
            parser->functions[main_index].column, "main must not have parameters");
    }
    cg->nodes = parser->nodes;
    cg->structs = parser->structs;
    cg->struct_count = parser->struct_count;
    cg->functions = parser->functions;
    cg->function_count = parser->function_count;
    cg->globals = parser->globals;
    cg->global_count = parser->global_count;
    cg->static_locals = parser->static_locals;
    cg->static_local_count = parser->static_local_count;
    cg->strings = parser->strings;
    cg->string_count = parser->string_count;
    cg->constants = parser->constants;
    cg->constant_count = parser->constant_count;
    cg->output = output;
    cg->capacity = output_capacity;
    cg->size = 0u;
    cg->symbol_count = 0u;
    cg->total_symbol_count = 0u;
    cg->current_function_index = 0u;
    cg->call_patch_count = 0u;
    cg->source_map = source_map;
    cg->source_map_capacity = source_map_capacity;
    cg->source_map_count = 0u;
    cg->debug_symbols = symbols;
    cg->debug_symbol_capacity = symbol_capacity;
    cg->debug_symbol_count = 0u;
    cg->debug_functions = functions;
    cg->debug_function_capacity = function_capacity;
    cg->debug_globals = globals;
    cg->debug_global_capacity = global_capacity;
    cg->debug_global_count = 0u;
    cg->references = references;
    cg->reference_capacity = reference_capacity;
    cg->reference_count = 0u;
    cg->diagnostic = diagnostic_out;
    cg->status = NOVAC_OK;
    for (i = 0u; i < 16u; i++)
        cg->temp_used[i] = 0u;
    for (i = 0u; i < NOVA_COMPILER_MAX_DEBUG_FUNCTIONS; i++) {
        cg->function_starts[i] = 0u;
        cg->function_ends[i] = 0u;
        cg->function_local_counts[i] = 0u;
    }
    /* Address 0 is the stable VM entry trampoline. Vol.10 emits deterministic
    static-data initializers before main; NovaVM still sees ordinary memory. */
    if (!nc_publish_global_metadata(cg))
        return cg->status;
    nc_emit_global_initializers(cg);
    nc_emit_static_local_initializers(cg);
    nc_emit_string_initializers(cg);
    nc_emit_call_placeholder(cg, (uint32_t)main_index);
    nc_emit_u8(cg, NOVA_OP_HALT);
    for (i = 0u; i < parser->function_count && cg->status == NOVAC_OK; i++) {
        if (!nc_emit_function(cg, i))
            break;
    }
    if (cg->status != NOVAC_OK)
        return cg->status;

    for (i = 0u; i < cg->call_patch_count; i++) {
        uint32_t target_index = cg->call_patches[i].function_index;
        if (target_index >= parser->function_count)
            return nc_diag(diagnostic_out, NOVAC_ERR_INTERNAL, 0u, 0u,
                           "invalid call patch function index");
        nc_patch_u32(cg, cg->call_patches[i].target_offset, cg->function_starts[target_index]);
    }
    if (cg->status != NOVAC_OK)
        return cg->status;
    result_out->bytecode_size = cg->size;
    result_out->ast_node_count = parser->node_count;
    result_out->local_count = cg->total_symbol_count;
    result_out->statement_count = parser->statement_count;
    result_out->expression_count = parser->expression_count;
    if (source_map_count_out != NULL)
        *source_map_count_out = cg->source_map_count;
    if (symbol_count_out != NULL)
        *symbol_count_out = cg->debug_symbol_count;
    if (function_count_out != NULL)
        *function_count_out = parser->function_count;
    if (global_count_out != NULL)
        *global_count_out = cg->debug_global_count;
    if (reference_count_out != NULL)
        *reference_count_out = cg->reference_count;
    if (diagnostic_out != NULL)
        diagnostic_out->file_id = 0u;
    return NOVAC_OK;
}
int32_t nova_compile_project_debug_ex5(
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
    uint32_t macro_capacity, uint32_t *macro_count_out) {
    NcCompileWorkspace *workspace;
    int32_t status;
    nc_diag_clear(diagnostic_out);
    workspace = (NcCompileWorkspace *)nc_platform_allocate(sizeof(NcCompileWorkspace));
    if (workspace == NULL) {
        return nc_diag(diagnostic_out, NOVAC_ERR_INTERNAL, 0u, 0u,
                       "compiler workspace allocation failed");
    }
    status = nc_compile_project_debug_ex5_workspace(
        units, unit_count, output, output_capacity, result_out, diagnostic_out, source_map,
        source_map_capacity, source_map_count_out, symbols, symbol_capacity, symbol_count_out,
        functions, function_capacity, function_count_out, globals, global_capacity,
        global_count_out, references, reference_capacity, reference_count_out, includes,
        include_capacity, include_count_out, debug_strings, debug_string_capacity,
        debug_string_count_out, declarations, declaration_capacity, declaration_count_out, macros,
        macro_capacity, macro_count_out, workspace);
    nc_platform_release(workspace, sizeof(NcCompileWorkspace));
    return status;
}

int32_t nova_compile_project_debug_ex4(
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
    uint32_t declaration_capacity, uint32_t *declaration_count_out) {
    return nova_compile_project_debug_ex5(
        units, unit_count, output, output_capacity, result_out, diagnostic_out, source_map,
        source_map_capacity, source_map_count_out, symbols, symbol_capacity, symbol_count_out,
        functions, function_capacity, function_count_out, globals, global_capacity,
        global_count_out, references, reference_capacity, reference_count_out, includes,
        include_capacity, include_count_out, debug_strings, debug_string_capacity,
        debug_string_count_out, declarations, declaration_capacity, declaration_count_out, NULL, 0u,
        NULL);
}
int32_t nova_compile_project_debug_ex3(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out,
    NovaDebugGlobal *globals, uint32_t global_capacity, uint32_t *global_count_out,
    NovaSymbolReference *references, uint32_t reference_capacity, uint32_t *reference_count_out,
    NovaIncludeDependency *includes, uint32_t include_capacity, uint32_t *include_count_out,
    NovaDebugString *debug_strings, uint32_t debug_string_capacity,
    uint32_t *debug_string_count_out) {
    return nova_compile_project_debug_ex4(
        units, unit_count, output, output_capacity, result_out, diagnostic_out, source_map,
        source_map_capacity, source_map_count_out, symbols, symbol_capacity, symbol_count_out,
        functions, function_capacity, function_count_out, globals, global_capacity,
        global_count_out, references, reference_capacity, reference_count_out, includes,
        include_capacity, include_count_out, debug_strings, debug_string_capacity,
        debug_string_count_out, NULL, 0u, NULL);
}
int32_t nova_compile_project_debug_ex2(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out,
    NovaDebugGlobal *globals, uint32_t global_capacity, uint32_t *global_count_out,
    NovaSymbolReference *references, uint32_t reference_capacity, uint32_t *reference_count_out,
    NovaIncludeDependency *includes, uint32_t include_capacity, uint32_t *include_count_out) {
    return nova_compile_project_debug_ex3(
        units, unit_count, output, output_capacity, result_out, diagnostic_out, source_map,
        source_map_capacity, source_map_count_out, symbols, symbol_capacity, symbol_count_out,
        functions, function_capacity, function_count_out, globals, global_capacity,
        global_count_out, references, reference_capacity, reference_count_out, includes,
        include_capacity, include_count_out, NULL, 0u, NULL);
}
int32_t nova_compile_project_debug_ex(

    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out,
    NovaDebugGlobal *globals, uint32_t global_capacity, uint32_t *global_count_out,
    NovaSymbolReference *references, uint32_t reference_capacity, uint32_t *reference_count_out) {
    return nova_compile_project_debug_ex2(
        units, unit_count, output, output_capacity, result_out, diagnostic_out, source_map,
        source_map_capacity, source_map_count_out, symbols, symbol_capacity, symbol_count_out,
        functions, function_capacity, function_count_out, globals, global_capacity,
        global_count_out, references, reference_capacity, reference_count_out, NULL, 0u, NULL);
}
int32_t nova_compile_project_debug(
    const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,
    uint32_t output_capacity, NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
    NovaSourceMapEntry *source_map, uint32_t source_map_capacity, uint32_t *source_map_count_out,
    NovaDebugSymbol *symbols, uint32_t symbol_capacity, uint32_t *symbol_count_out,
    NovaDebugFunction *functions, uint32_t function_capacity, uint32_t *function_count_out) {
    return nova_compile_project_debug_ex(
        units, unit_count, output, output_capacity, result_out, diagnostic_out, source_map,
        source_map_capacity, source_map_count_out, symbols, symbol_capacity, symbol_count_out,
        functions, function_capacity, function_count_out, NULL, 0u, NULL, NULL, 0u, NULL);
}
int32_t nova_compile_project(const NovaTranslationUnit *units, uint32_t unit_count, uint8_t *output,

                             uint32_t output_capacity, NovaCompileResult *result_out,
                             NovaCompilerDiagnostic *diagnostic_out) {
    return nova_compile_project_debug(units, unit_count, output, output_capacity, result_out,
                                      diagnostic_out, NULL, 0u, NULL, NULL, 0u, NULL, NULL, 0u,
                                      NULL);
}
int32_t nova_compile_c_debug(const char *source, uint8_t *output, uint32_t output_capacity,
                             NovaCompileResult *result_out, NovaCompilerDiagnostic *diagnostic_out,
                             NovaSourceMapEntry *source_map, uint32_t source_map_capacity,
                             uint32_t *source_map_count_out, NovaDebugSymbol *symbols,
                             uint32_t symbol_capacity, uint32_t *symbol_count_out,
                             NovaDebugFunction *functions, uint32_t function_capacity,
                             uint32_t *function_count_out) {
    NovaTranslationUnit unit;
    unit.name = "main.c";
    unit.source = source;
    return nova_compile_project_debug(
        &unit, 1u, output, output_capacity, result_out, diagnostic_out, source_map,
        source_map_capacity, source_map_count_out, symbols, symbol_capacity, symbol_count_out,
        functions, function_capacity, function_count_out);
}
int32_t nova_compile_c(const char *source, uint8_t *output,

                       uint32_t output_capacity, NovaCompileResult *result_out,
                       NovaCompilerDiagnostic *diagnostic_out) {
    return nova_compile_c_debug(source, output, output_capacity, result_out, diagnostic_out, NULL,
                                0u, NULL, NULL, 0u, NULL, NULL, 0u, NULL);
}
const char *nova_compiler_version(void) {
    return "NovaC 0.21.0";
}
