#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 8192
#define MAX_TOKEN 2048
#define MAX_EXPR 16384
#define MAX_VARIABLES 512
#define MAX_NAME 128
#define MAX_BLOCKS 256
#define MAX_DATA_ITEMS 2048
#define MAX_DATA_LABELS 512
#define MAX_STATEMENT_ARGUMENTS 20

typedef enum TokenKind {
    TOKEN_END,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_IDENTIFIER,
    TOKEN_OPERATOR,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COMMA
} TokenKind;

typedef struct Token {
    TokenKind kind;
    char text[MAX_TOKEN];
} Token;

typedef struct Lexer {
    const char *source;
    size_t position;
    Token current;
} Lexer;

typedef struct Expression {
    char code[MAX_EXPR];
    bool is_string;
    bool valid;
} Expression;

typedef struct Variable {
    char basic_name[MAX_NAME];
    char c_name[MAX_NAME];
    bool is_string;
    bool is_array;
} Variable;

typedef enum BlockKind {
    BLOCK_IF,
    BLOCK_DO,
    BLOCK_FOR
} BlockKind;

typedef struct Block {
    BlockKind kind;
    char variable[MAX_NAME];
    size_t identifier;
} Block;

typedef struct DataItem {
    char text[MAX_TOKEN];
} DataItem;

typedef struct DataLabel {
    char name[MAX_NAME];
    size_t position;
} DataLabel;

typedef struct Compiler {
    FILE *output;
    const char *input_name;
    int line_number;
    int indentation;
    int errors;
    Variable variables[MAX_VARIABLES];
    size_t variable_count;
    Block blocks[MAX_BLOCKS];
    size_t block_count;
    size_t total_for_loops;
    size_t next_for_loop;
    size_t total_gosubs;
    size_t next_gosub;
    bool uses_gosub_stack;
    DataItem data_items[MAX_DATA_ITEMS];
    size_t data_count;
    DataLabel data_labels[MAX_DATA_LABELS];
    size_t data_label_count;
    bool uses_data;
    bool uses_windows_ui;
    char window_name[256];
    char icon_path[MAX_TOKEN];
    bool has_window_size;
    double window_size[4];
} Compiler;

static Expression statement_arguments[MAX_STATEMENT_ARGUMENTS];

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U) {
        return;
    }
    (void)snprintf(destination, capacity, "%s", source != NULL ? source : "");
}

static int compare_case_insensitive(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        int a = tolower((unsigned char)*left);
        int b = tolower((unsigned char)*right);
        if (a != b) {
            return a - b;
        }
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

static bool starts_word(const char *text, const char *word)
{
    size_t length = strlen(word);
    if (strlen(text) < length) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        if (tolower((unsigned char)text[index]) !=
            tolower((unsigned char)word[index])) {
            return false;
        }
    }
    return text[length] == '\0' || isspace((unsigned char)text[length]);
}

static bool is_windows_ui_statement(const char *statement)
{
    static const char *const words[] = {
        "screen", "position", "line", "circle", "paint", "pset",
        "preset", "control", "dcontrol", "dbutton", "cbutton",
        "setctext", "setfocus", "radioon", "radiooff", "checkon",
        "checkoff", "list", "createfont", "selectfont", "mainmenu",
        "addsubmenu", "menuitemon", "menuitemgray", "messagebox",
        "dialog", "openfileread", "openfilesave", "palette",
        "createbitmap", "selectbitmap", "selectdisplay", "selectprint",
        "loadbitmap", "storebitmap", "copybits", "stretchbits",
        "printcontrol", "scrollarea", "on paint gosub"
    };
    for (size_t index = 0U; index < sizeof(words) / sizeof(words[0]); ++index) {
        if (starts_word(statement, words[index])) return true;
    }
    if (starts_word(statement, "get") || starts_word(statement, "put")) {
        const char *arguments = statement + 3;
        while (isspace((unsigned char)*arguments)) ++arguments;
        return *arguments != '#';
    }
    return false;
}

static char *trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return text;
    }
    end = text + strlen(text) - 1U;
    while (end >= text && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return text;
}

static void compiler_error(Compiler *compiler, const char *format, ...)
{
    va_list arguments;
    fprintf(stderr, "%s:%d: error: ", compiler->input_name,
            compiler->line_number);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    ++compiler->errors;
}

static void emit(Compiler *compiler, const char *format, ...)
{
    va_list arguments;
    for (int index = 0; index < compiler->indentation; ++index) {
        fputs("    ", compiler->output);
    }
    va_start(arguments, format);
    vfprintf(compiler->output, format, arguments);
    va_end(arguments);
    fputc('\n', compiler->output);
}

static void emit_c_string(FILE *output, const char *value)
{
    fputc('"', output);
    for (; *value != '\0'; ++value) {
        unsigned char ch = (unsigned char)*value;
        if (ch == '\\' || ch == '"') {
            fputc('\\', output);
            fputc(ch, output);
        } else if (ch == '\n') {
            fputs("\\n", output);
        } else if (ch == '\r') {
            fputs("\\r", output);
        } else if (ch == '\t') {
            fputs("\\t", output);
        } else if (isprint(ch)) {
            fputc(ch, output);
        } else {
            fprintf(output, "\\%03o", ch);
        }
    }
    fputc('"', output);
}

static void sanitize_identifier(const char *basic_name, char *c_name,
                                size_t capacity)
{
    size_t output = 0;
    const char *prefix = "bbv_";
    while (*prefix != '\0' && output + 1U < capacity) {
        c_name[output++] = *prefix++;
    }
    for (size_t index = 0; basic_name[index] != '\0' && output + 8U < capacity;
         ++index) {
        unsigned char ch = (unsigned char)basic_name[index];
        if (isalnum(ch) || ch == '_') {
            c_name[output++] = (char)tolower(ch);
        } else if (ch == '$') {
            memcpy(c_name + output, "_string", 7U);
            output += 7U;
        } else if (ch == '%') {
            memcpy(c_name + output, "_integer", 8U);
            output += 8U;
        } else if (ch == '&') {
            memcpy(c_name + output, "_long", 5U);
            output += 5U;
        }
    }
    c_name[output] = '\0';
}

static Variable *find_variable(Compiler *compiler, const char *name)
{
    for (size_t index = 0; index < compiler->variable_count; ++index) {
        if (compare_case_insensitive(compiler->variables[index].basic_name,
                                     name) == 0) {
            return &compiler->variables[index];
        }
    }
    return NULL;
}

static Variable *add_variable(Compiler *compiler, const char *name)
{
    Variable *existing = find_variable(compiler, name);
    Variable *variable;
    size_t length;
    if (existing != NULL) {
        return existing;
    }
    if (compiler->variable_count >= MAX_VARIABLES) {
        compiler_error(compiler, "too many variables");
        return NULL;
    }
    variable = &compiler->variables[compiler->variable_count++];
    copy_text(variable->basic_name, sizeof(variable->basic_name), name);
    sanitize_identifier(name, variable->c_name, sizeof(variable->c_name));
    length = strlen(name);
    variable->is_string = length > 0U && name[length - 1U] == '$';
    variable->is_array = false;
    return variable;
}

static void lexer_next(Lexer *lexer)
{
    const char *source = lexer->source;
    size_t position = lexer->position;
    size_t output = 0;
    while (isspace((unsigned char)source[position])) {
        ++position;
    }
    lexer->current.text[0] = '\0';
    if (source[position] == '\0' || source[position] == ';') {
        lexer->current.kind = TOKEN_END;
    } else if (source[position] == '&' &&
               (source[position + 1U] == 'h' || source[position + 1U] == 'H')) {
        unsigned long value;
        char *end_pointer;
        value = strtoul(source + position + 2U, &end_pointer, 16);
        lexer->current.kind = TOKEN_NUMBER;
        (void)snprintf(lexer->current.text, sizeof(lexer->current.text), "%lu",
                       value);
        position = (size_t)(end_pointer - source);
    } else if (isdigit((unsigned char)source[position]) ||
               (source[position] == '.' &&
                isdigit((unsigned char)source[position + 1U]))) {
        lexer->current.kind = TOKEN_NUMBER;
        while ((isalnum((unsigned char)source[position]) ||
                source[position] == '.' || source[position] == '+' ||
                source[position] == '-') && output + 1U < MAX_TOKEN) {
            char ch = source[position];
            if ((ch == '+' || ch == '-') && output > 0U &&
                lexer->current.text[output - 1U] != 'e' &&
                lexer->current.text[output - 1U] != 'E') {
                break;
            }
            lexer->current.text[output++] = ch;
            ++position;
        }
        lexer->current.text[output] = '\0';
    } else if (source[position] == '"') {
        lexer->current.kind = TOKEN_STRING;
        ++position;
        while (source[position] != '\0' && source[position] != '"' &&
               output + 2U < MAX_TOKEN) {
            char ch = source[position++];
            if (ch == '\\' || ch == '"') {
                lexer->current.text[output++] = '\\';
            }
            lexer->current.text[output++] = ch;
        }
        if (source[position] == '"') {
            ++position;
        }
        lexer->current.text[output] = '\0';
    } else if (isalpha((unsigned char)source[position]) ||
               source[position] == '_') {
        lexer->current.kind = TOKEN_IDENTIFIER;
        while ((isalnum((unsigned char)source[position]) ||
                source[position] == '_' || source[position] == '$' ||
                source[position] == '%' || source[position] == '&') &&
               output + 1U < MAX_TOKEN) {
            lexer->current.text[output++] = source[position++];
        }
        lexer->current.text[output] = '\0';
    } else if (source[position] == '(') {
        lexer->current.kind = TOKEN_LEFT_PAREN;
        lexer->current.text[0] = source[position++];
        lexer->current.text[1] = '\0';
    } else if (source[position] == ')') {
        lexer->current.kind = TOKEN_RIGHT_PAREN;
        lexer->current.text[0] = source[position++];
        lexer->current.text[1] = '\0';
    } else if (source[position] == ',') {
        lexer->current.kind = TOKEN_COMMA;
        lexer->current.text[0] = source[position++];
        lexer->current.text[1] = '\0';
    } else {
        lexer->current.kind = TOKEN_OPERATOR;
        lexer->current.text[output++] = source[position++];
        if ((lexer->current.text[0] == '<' || lexer->current.text[0] == '>') &&
            (source[position] == '=' || source[position] == '>')) {
            lexer->current.text[output++] = source[position++];
        }
        lexer->current.text[output] = '\0';
    }
    lexer->position = position;
}

static void expression_format(Expression *expression, bool is_string,
                              const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(expression->code, sizeof(expression->code), format,
                    arguments);
    va_end(arguments);
    expression->is_string = is_string;
    expression->valid = true;
}

static int operator_precedence(const Token *token)
{
    if (token->kind == TOKEN_IDENTIFIER) {
        if (compare_case_insensitive(token->text, "or") == 0) return 1;
        if (compare_case_insensitive(token->text, "and") == 0) return 2;
        if (compare_case_insensitive(token->text, "mod") == 0) return 5;
        return 0;
    }
    if (token->kind != TOKEN_OPERATOR) return 0;
    if (strcmp(token->text, "=") == 0 || strcmp(token->text, "<>") == 0 ||
        strcmp(token->text, "<") == 0 || strcmp(token->text, ">") == 0 ||
        strcmp(token->text, "<=") == 0 || strcmp(token->text, ">=") == 0)
        return 3;
    if (strcmp(token->text, "+") == 0 || strcmp(token->text, "-") == 0)
        return 4;
    if (strcmp(token->text, "*") == 0 || strcmp(token->text, "/") == 0 ||
        strcmp(token->text, "\\") == 0)
        return 5;
    if (strcmp(token->text, "^") == 0) return 6;
    return 0;
}

static Expression parse_expression_precedence(Compiler *compiler, Lexer *lexer,
                                              int minimum_precedence);

static bool extract_numeric_array_name(const Expression *expression,
                                       char *name, size_t capacity)
{
    const char *prefix = "bb_num_array_get(&";
    const char *start;
    const char *end;
    size_t length;
    if (expression == NULL || strncmp(expression->code, prefix, strlen(prefix)) != 0)
        return false;
    start = expression->code + strlen(prefix);
    end = strchr(start, ',');
    if (end == NULL) return false;
    length = (size_t)(end - start);
    if (length >= capacity) length = capacity - 1U;
    memcpy(name, start, length);
    name[length] = '\0';
    return true;
}

static bool extract_string_array_name(const Expression *expression,
                                      char *name, size_t capacity)
{
    const char *prefix = "bb_string_array_get(&";
    const char *start;
    const char *end;
    size_t length;
    if (expression == NULL || strncmp(expression->code, prefix, strlen(prefix)) != 0)
        return false;
    start = expression->code + strlen(prefix);
    end = strchr(start, ',');
    if (end == NULL) return false;
    length = (size_t)(end - start);
    if (length >= capacity) length = capacity - 1U;
    memcpy(name, start, length);
    name[length] = '\0';
    return true;
}

static Expression parse_primary(Compiler *compiler, Lexer *lexer)
{
    Expression result = {{0}, false, false};
    Token token = lexer->current;
    if (token.kind == TOKEN_NUMBER) {
        expression_format(&result, false, "(%s)", token.text);
        lexer_next(lexer);
        return result;
    }
    if (token.kind == TOKEN_STRING) {
        expression_format(&result, true, "\"%s\"", token.text);
        lexer_next(lexer);
        return result;
    }
    if (token.kind == TOKEN_OPERATOR &&
        (strcmp(token.text, "+") == 0 || strcmp(token.text, "-") == 0)) {
        lexer_next(lexer);
        result = parse_primary(compiler, lexer);
        if (result.valid && !result.is_string) {
            char inner[MAX_EXPR];
            copy_text(inner, sizeof(inner), result.code);
            expression_format(&result, false, "(%s%s)", token.text, inner);
        }
        return result;
    }
    if (token.kind == TOKEN_LEFT_PAREN) {
        lexer_next(lexer);
        result = parse_expression_precedence(compiler, lexer, 1);
        if (lexer->current.kind != TOKEN_RIGHT_PAREN) {
            compiler_error(compiler, "expected ')' in expression");
            result.valid = false;
        } else {
            lexer_next(lexer);
        }
        return result;
    }
    if (token.kind == TOKEN_IDENTIFIER) {
        char name[MAX_TOKEN];
        Expression arguments[4];
        int argument_count = 0;
        copy_text(name, sizeof(name), token.text);
        lexer_next(lexer);
        if (lexer->current.kind == TOKEN_LEFT_PAREN) {
            lexer_next(lexer);
            while (lexer->current.kind != TOKEN_RIGHT_PAREN &&
                   lexer->current.kind != TOKEN_END && argument_count < 4) {
                arguments[argument_count++] =
                    parse_expression_precedence(compiler, lexer, 1);
                if (lexer->current.kind == TOKEN_COMMA) {
                    lexer_next(lexer);
                } else {
                    break;
                }
            }
            if (lexer->current.kind != TOKEN_RIGHT_PAREN) {
                compiler_error(compiler, "expected ')' after function arguments");
                return result;
            }
            lexer_next(lexer);
        }

        if (compare_case_insensitive(name, "ostype") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_ostype()");
        else if (compare_case_insensitive(name, "system") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_system(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "inkey$") == 0 && argument_count == 0)
            expression_format(&result, true, "bb_inkey()");
        else if (compare_case_insensitive(name, "len") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_len(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "asc") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_asc(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "chr$") == 0 && argument_count == 1)
            expression_format(&result, true, "bb_chr(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "left$") == 0 && argument_count == 2)
            expression_format(&result, true, "bb_left(%s, %s)",
                              arguments[0].code, arguments[1].code);
        else if (compare_case_insensitive(name, "right$") == 0 && argument_count == 2)
            expression_format(&result, true, "bb_right(%s, %s)",
                              arguments[0].code, arguments[1].code);
        else if (compare_case_insensitive(name, "mid$") == 0 && argument_count == 2)
            expression_format(&result, true, "bb_mid(%s, %s, 2048.0)",
                              arguments[0].code, arguments[1].code);
        else if (compare_case_insensitive(name, "mid$") == 0 && argument_count == 3)
            expression_format(&result, true, "bb_mid(%s, %s, %s)",
                              arguments[0].code, arguments[1].code,
                              arguments[2].code);
        else if (compare_case_insensitive(name, "str$") == 0 && argument_count == 1)
            expression_format(&result, true, "bb_str(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "space$") == 0 && argument_count == 1)
            expression_format(&result, true, "bb_space(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "ucase$") == 0 && argument_count == 1)
            expression_format(&result, true, "bb_ucase(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "instr") == 0 && argument_count == 2)
            expression_format(&result, false, "bb_instr(1.0, %s, %s)",
                              arguments[0].code, arguments[1].code);
        else if (compare_case_insensitive(name, "instr") == 0 && argument_count == 3)
            expression_format(&result, false, "bb_instr(%s, %s, %s)",
                              arguments[0].code, arguments[1].code,
                              arguments[2].code);
        else if (compare_case_insensitive(name, "eof") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_file_eof(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "loc") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_file_loc(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "input$") == 0 && argument_count == 2)
            expression_format(&result, true, "bb_file_input_string(%s, %s)",
                              arguments[0].code, arguments[1].code);
        else if (compare_case_insensitive(name, "dialog$") == 0 && argument_count == 1)
            expression_format(&result, true, "bb_dialog_value(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "dir$") == 0 && argument_count == 0)
            expression_format(&result, true, "bb_directory(NULL, 0.0)");
        else if (compare_case_insensitive(name, "dir$") == 0 && argument_count == 1)
            expression_format(&result, true, "bb_directory(%s, 0.0)",
                              arguments[0].code);
        else if (compare_case_insensitive(name, "dir$") == 0 && argument_count == 2)
            expression_format(&result, true, "bb_directory(%s, %s)",
                              arguments[0].code, arguments[1].code);
        else if (compare_case_insensitive(name, "getctext") == 0 && argument_count == 1)
            expression_format(&result, true, "bb_get_control_text(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "setfocus") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_set_focus(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "getfocus") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_get_focus()");
        else if (compare_case_insensitive(name, "list") == 0 && argument_count == 4)
            expression_format(&result, false, "bb_list(%s, %s, %s, %s)",
                              arguments[0].code, arguments[1].code,
                              arguments[2].code, arguments[3].code);
        else if (compare_case_insensitive(name, "dlen") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_text_length(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "font") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_font_info(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "device") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_device_info(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "snddev") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_sound_device(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "mouseon") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_mouse_on()");
        else if (compare_case_insensitive(name, "mousex") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_mouse_x()");
        else if (compare_case_insensitive(name, "mousey") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_mouse_y()");
        else if (compare_case_insensitive(name, "mouseb") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_mouse_button()");
        else if (compare_case_insensitive(name, "display") == 0 && argument_count == 0)
            expression_format(&result, false, "0.0");
        else if (compare_case_insensitive(name, "print") == 0 && argument_count == 0)
            expression_format(&result, false, "1.0");
        else if (compare_case_insensitive(name, "xor") == 0 && argument_count == 0)
            expression_format(&result, false, "1.0");
        else if ((compare_case_insensitive(name, "bitmaph") == 0 ||
                  compare_case_insensitive(name, "bitmapc") == 0) &&
                 argument_count == 2) {
            char array_name[MAX_NAME];
            if (!extract_numeric_array_name(&arguments[1], array_name,
                                            sizeof(array_name))) {
                compiler_error(compiler, "%s requires a numeric array element",
                               name);
                return result;
            }
            expression_format(&result, false, "%s(%s, &%s)",
                              compare_case_insensitive(name, "bitmaph") == 0
                                  ? "bb_bitmap_header" : "bb_bitmap_colors",
                              arguments[0].code, array_name);
        }
        else if (compare_case_insensitive(name, "int") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_int(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "abs") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_abs(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "val") == 0 && argument_count == 1)
            expression_format(&result, false, "bb_val(%s)", arguments[0].code);
        else if (compare_case_insensitive(name, "freemem") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_freemem()");
        else if (compare_case_insensitive(name, "rnd") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_rnd()");
        else if (compare_case_insensitive(name, "timer") == 0 && argument_count == 0)
            expression_format(&result, false, "bb_timer()");
        else if (compare_case_insensitive(name, "time$") == 0 && argument_count == 0)
            expression_format(&result, true, "bb_time_string()");
        else if (compare_case_insensitive(name, "date$") == 0 && argument_count == 0)
            expression_format(&result, true, "bb_date_string()");
        else {
            Variable *variable = add_variable(compiler, name);
            if (variable != NULL && variable->is_array && argument_count > 0) {
                const char *function = variable->is_string
                    ? "bb_string_array_get" : "bb_num_array_get";
                if (argument_count == 1)
                    expression_format(&result, variable->is_string,
                                      "%s(&%s, 1U, (double)(%s))", function,
                                      variable->c_name, arguments[0].code);
                else if (argument_count == 2)
                    expression_format(&result, variable->is_string,
                                      "%s(&%s, 2U, (double)(%s), (double)(%s))", function,
                                      variable->c_name, arguments[0].code,
                                      arguments[1].code);
                else if (argument_count == 3)
                    expression_format(&result, variable->is_string,
                                      "%s(&%s, 3U, (double)(%s), (double)(%s), (double)(%s))", function,
                                      variable->c_name, arguments[0].code,
                                      arguments[1].code, arguments[2].code);
                else if (argument_count == 4)
                    expression_format(&result, variable->is_string,
                                      "%s(&%s, 4U, (double)(%s), (double)(%s), (double)(%s), (double)(%s))", function,
                                      variable->c_name, arguments[0].code,
                                      arguments[1].code, arguments[2].code,
                                      arguments[3].code);
            } else if (argument_count != 0) {
                compiler_error(compiler, "unsupported function or array '%s'", name);
                return result;
            }
            if (variable != NULL && argument_count == 0)
                expression_format(&result, variable->is_string, "%s",
                                  variable->c_name);
        }
        return result;
    }
    compiler_error(compiler, "expected expression near '%s'", token.text);
    return result;
}

static Expression combine_binary(Compiler *compiler, Expression left,
                                 const char *operation, Expression right)
{
    Expression result = {{0}, false, false};
    const char *c_operation = operation;
    bool comparison = false;
    if (!left.valid || !right.valid) return result;
    if (strcmp(operation, "=") == 0) { c_operation = "=="; comparison = true; }
    else if (strcmp(operation, "<>") == 0) { c_operation = "!="; comparison = true; }
    else if (strcmp(operation, "<") == 0 || strcmp(operation, ">") == 0 ||
             strcmp(operation, "<=") == 0 || strcmp(operation, ">=") == 0)
        comparison = true;

    if (comparison && (left.is_string || right.is_string)) {
        if (!left.is_string || !right.is_string) {
            compiler_error(compiler, "cannot compare a string with a number");
            return result;
        }
        expression_format(&result, false, "(strcmp(%s, %s) %s 0)",
                          left.code, right.code, c_operation);
    } else if (strcmp(operation, "+") == 0 && left.is_string && right.is_string) {
        expression_format(&result, true, "bb_concat(%s, %s)", left.code,
                          right.code);
    } else if (left.is_string || right.is_string) {
        compiler_error(compiler, "operator '%s' is invalid for strings", operation);
    } else if (compare_case_insensitive(operation, "and") == 0) {
        expression_format(&result, false, "((double)((long)(%s) & (long)(%s)))",
                          left.code, right.code);
    } else if (compare_case_insensitive(operation, "or") == 0) {
        expression_format(&result, false, "((double)((long)(%s) | (long)(%s)))",
                          left.code, right.code);
    } else if (compare_case_insensitive(operation, "mod") == 0) {
        expression_format(&result, false, "fmod(%s, %s)", left.code, right.code);
    } else if (strcmp(operation, "\\") == 0) {
        expression_format(&result, false, "((double)((long)(%s) / (long)(%s)))",
                          left.code, right.code);
    } else if (strcmp(operation, "^") == 0) {
        expression_format(&result, false, "pow(%s, %s)", left.code, right.code);
    } else {
        expression_format(&result, false, "(%s %s %s)", left.code, c_operation,
                          right.code);
    }
    return result;
}

static Expression parse_expression_precedence(Compiler *compiler, Lexer *lexer,
                                              int minimum_precedence)
{
    Expression left = parse_primary(compiler, lexer);
    int precedence;
    while ((precedence = operator_precedence(&lexer->current)) >=
           minimum_precedence) {
        char operation[MAX_TOKEN];
        Expression right;
        copy_text(operation, sizeof(operation), lexer->current.text);
        lexer_next(lexer);
        right = parse_expression_precedence(compiler, lexer, precedence + 1);
        left = combine_binary(compiler, left, operation, right);
    }
    return left;
}

static Expression parse_expression(Compiler *compiler, const char *source)
{
    Lexer lexer = {source, 0U, {TOKEN_END, {0}}};
    Expression result;
    lexer_next(&lexer);
    result = parse_expression_precedence(compiler, &lexer, 1);
    if (result.valid && lexer.current.kind != TOKEN_END &&
        lexer.current.kind != TOKEN_COMMA) {
        compiler_error(compiler, "unexpected token '%s'", lexer.current.text);
        result.valid = false;
    }
    return result;
}

static int parse_statement_arguments(Compiler *compiler, const char *source)
{
    Lexer lexer = {source, 0U, {TOKEN_END, {0}}};
    int count = 0;
    lexer_next(&lexer);
    while (lexer.current.kind != TOKEN_END &&
           count < MAX_STATEMENT_ARGUMENTS) {
        statement_arguments[count] =
            parse_expression_precedence(compiler, &lexer, 1);
        if (!statement_arguments[count].valid) return -1;
        ++count;
        if (lexer.current.kind == TOKEN_COMMA)
            lexer_next(&lexer);
        else if (lexer.current.kind != TOKEN_END) {
            compiler_error(compiler, "expected comma near '%s'",
                           lexer.current.text);
            return -1;
        }
    }
    if (lexer.current.kind != TOKEN_END) {
        compiler_error(compiler, "too many statement arguments");
        return -1;
    }
    return count;
}

static const char *find_word_case_insensitive(const char *text, const char *word)
{
    size_t length = strlen(word);
    bool quoted = false;
    int depth = 0;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        if (text[index] == '"') quoted = !quoted;
        if (quoted) continue;
        if (text[index] == '(') ++depth;
        else if (text[index] == ')' && depth > 0) --depth;
        if (depth == 0 && (index == 0U || isspace((unsigned char)text[index - 1U]))) {
            size_t matched = 0;
            while (matched < length && text[index + matched] != '\0' &&
                   tolower((unsigned char)text[index + matched]) ==
                       tolower((unsigned char)word[matched])) {
                ++matched;
            }
            if (matched == length &&
                (text[index + length] == '\0' ||
                 isspace((unsigned char)text[index + length]))) {
                return text + index;
            }
        }
    }
    return NULL;
}

static void push_block(Compiler *compiler, BlockKind kind)
{
    if (compiler->block_count >= MAX_BLOCKS) {
        compiler_error(compiler, "too many nested blocks");
        return;
    }
    compiler->blocks[compiler->block_count].kind = kind;
    compiler->blocks[compiler->block_count].variable[0] = '\0';
    compiler->blocks[compiler->block_count].identifier = 0U;
    ++compiler->block_count;
}

static bool pop_block(Compiler *compiler, BlockKind expected)
{
    if (compiler->block_count == 0U ||
        compiler->blocks[compiler->block_count - 1U].kind != expected) {
        compiler_error(compiler, "mismatched block terminator");
        return false;
    }
    --compiler->block_count;
    return true;
}

static void compile_statement(Compiler *compiler, char *statement);
static bool valid_variable_name(const char *name);
static char *find_assignment(char *statement);
static bool parse_reference(Compiler *compiler, const char *source,
                            Variable **variable, Expression arguments[4],
                            int *argument_count);
static void emit_array_operation(Compiler *compiler, const char *function,
                                 const char *array_name, const char *value,
                                 Expression arguments[4], int argument_count);
static bool split_two_arguments(Compiler *compiler, char *arguments,
                                Expression *first, Expression *second);
static void for_each_statement(char *line,
                               void (*callback)(Compiler *, char *),
                               Compiler *compiler);

static bool parse_if_parts(Compiler *compiler, char *statement,
                           Expression *condition, char *tail,
                           size_t tail_capacity)
{
    const char *then_word = find_word_case_insensitive(statement, "then");
    char condition_text[MAX_LINE];
    size_t condition_length;
    if (then_word == NULL) {
        copy_text(condition_text, sizeof(condition_text), statement);
        tail[0] = '\0';
        *condition = parse_expression(compiler, trim(condition_text));
        if (!condition->valid || condition->is_string) {
            if (condition->is_string)
                compiler_error(compiler, "IF condition must be numeric");
            return false;
        }
        return true;
    }
    condition_length = (size_t)(then_word - statement);
    if (condition_length >= sizeof(condition_text))
        condition_length = sizeof(condition_text) - 1U;
    memcpy(condition_text, statement, condition_length);
    condition_text[condition_length] = '\0';
    copy_text(tail, tail_capacity, then_word + 4);
    *condition = parse_expression(compiler, trim(condition_text));
    if (!condition->valid || condition->is_string) {
        if (condition->is_string)
            compiler_error(compiler, "IF condition must be numeric");
        return false;
    }
    return true;
}

static void compile_print(Compiler *compiler, char *arguments)
{
    char *cursor = trim(arguments);
    bool newline = true;
    if (*cursor == '\0') {
        emit(compiler, "bb_print_newline();");
        return;
    }
    while (*cursor != '\0') {
        char *start = cursor;
        char separator = '\0';
        bool quoted = false;
        int depth = 0;
        while (*cursor != '\0') {
            if (*cursor == '"') quoted = !quoted;
            if (!quoted) {
                if (*cursor == '(') ++depth;
                else if (*cursor == ')' && depth > 0) --depth;
                else if (depth == 0 && (*cursor == ';' || *cursor == ',')) {
                    separator = *cursor;
                    break;
                }
            }
            ++cursor;
        }
        if (*cursor != '\0') *cursor++ = '\0';
        start = trim(start);
        if (*start != '\0') {
            Expression expression = parse_expression(compiler, start);
            if (expression.valid) {
                if (expression.is_string)
                    emit(compiler, "bb_print_string(%s);", expression.code);
                else
                    emit(compiler, "bb_print_number(%s);", expression.code);
            }
        }
        newline = separator != ';';
    }
    if (newline) emit(compiler, "bb_print_newline();");
}

static void compile_print_using(Compiler *compiler, char *arguments)
{
    char *separator = arguments;
    bool quoted = false;
    Expression format;
    Expression value;
    bool newline = true;
    while (*separator != '\0') {
        if (*separator == '"') quoted = !quoted;
        if (!quoted && *separator == ';') break;
        ++separator;
    }
    if (*separator != ';') {
        compiler_error(compiler, "PRINT USING requires a value");
        return;
    }
    *separator = '\0';
    format = parse_expression(compiler, trim(arguments));
    arguments = trim(separator + 1);
    {
        size_t length = strlen(arguments);
        if (length > 0U && arguments[length - 1U] == ';') {
            arguments[length - 1U] = '\0';
            newline = false;
        }
    }
    value = parse_expression(compiler, trim(arguments));
    if (!format.valid || !format.is_string || !value.valid || value.is_string)
        return;
    emit(compiler, "bb_print_string(bb_format_using(%s, %s));", format.code,
         value.code);
    if (newline) emit(compiler, "bb_print_newline();");
}

static Expression parse_file_number(Compiler *compiler, char *text)
{
    text = trim(text);
    if (*text == '#') ++text;
    return parse_expression(compiler, trim(text));
}

static void compile_file_print(Compiler *compiler, char *arguments)
{
    char *comma = strchr(arguments, ',');
    Expression file_number;
    char *cursor;
    bool newline = true;
    if (comma == NULL) {
        compiler_error(compiler, "file PRINT requires a comma after file number");
        return;
    }
    *comma = '\0';
    file_number = parse_file_number(compiler, arguments);
    if (!file_number.valid || file_number.is_string) return;
    cursor = trim(comma + 1);
    while (*cursor != '\0') {
        char *start = cursor;
        char separator = '\0';
        bool quoted = false;
        int depth = 0;
        while (*cursor != '\0') {
            if (*cursor == '"') quoted = !quoted;
            if (!quoted) {
                if (*cursor == '(') ++depth;
                else if (*cursor == ')' && depth > 0) --depth;
                else if (depth == 0 && (*cursor == ';' || *cursor == ',')) {
                    separator = *cursor;
                    break;
                }
            }
            ++cursor;
        }
        if (*cursor != '\0') *cursor++ = '\0';
        start = trim(start);
        if (*start != '\0') {
            Expression expression = parse_expression(compiler, start);
            if (expression.valid) {
                if (expression.is_string)
                    emit(compiler, "bb_file_print_string(%s, %s);",
                         file_number.code, expression.code);
                else
                    emit(compiler, "bb_file_print_number(%s, %s);",
                         file_number.code, expression.code);
            }
        }
        if (separator == ',')
            emit(compiler, "bb_file_print_separator(%s, 1.0);",
                 file_number.code);
        newline = separator != ';';
    }
    if (newline)
        emit(compiler, "bb_file_print_newline(%s);", file_number.code);
}

static void compile_open(Compiler *compiler, char *arguments)
{
    const char *for_word = find_word_case_insensitive(arguments, "for");
    char path_text[MAX_LINE];
    char remainder[MAX_LINE];
    const char *as_word;
    char mode_text[MAX_LINE];
    char file_text[MAX_LINE];
    char length_text[MAX_LINE] = "128";
    Expression path;
    Expression file_number;
    Expression record_length;
    int mode;
    if (for_word == NULL) {
        compiler_error(compiler, "OPEN without FOR");
        return;
    }
    {
        size_t length = (size_t)(for_word - arguments);
        if (length >= sizeof(path_text)) length = sizeof(path_text) - 1U;
        memcpy(path_text, arguments, length);
        path_text[length] = '\0';
    }
    copy_text(remainder, sizeof(remainder), for_word + 3);
    as_word = find_word_case_insensitive(remainder, "as");
    if (as_word == NULL) {
        compiler_error(compiler, "OPEN without AS");
        return;
    }
    {
        size_t length = (size_t)(as_word - remainder);
        if (length >= sizeof(mode_text)) length = sizeof(mode_text) - 1U;
        memcpy(mode_text, remainder, length);
        mode_text[length] = '\0';
    }
    copy_text(file_text, sizeof(file_text), as_word + 2);
    {
        char *cursor = trim(file_text);
        char *end;
        if (*cursor == '#') ++cursor;
        end = cursor;
        while (*end != '\0' && !isspace((unsigned char)*end)) ++end;
        if (*end != '\0') {
            char *options = trim(end + 1);
            *end = '\0';
            if (tolower((unsigned char)options[0]) == 'l' &&
                tolower((unsigned char)options[1]) == 'e' &&
                tolower((unsigned char)options[2]) == 'n') {
                char *equals = strchr(options, '=');
                if (equals != NULL)
                    copy_text(length_text, sizeof(length_text), equals + 1);
            }
        }
        memmove(file_text, cursor, strlen(cursor) + 1U);
    }
    if (starts_word(trim(mode_text), "input")) mode = 1;
    else if (starts_word(trim(mode_text), "output")) mode = 2;
    else if (starts_word(trim(mode_text), "append")) mode = 3;
    else if (starts_word(trim(mode_text), "random")) mode = 4;
    else {
        compiler_error(compiler, "unsupported OPEN mode '%s'", mode_text);
        return;
    }
    path = parse_expression(compiler, trim(path_text));
    file_number = parse_expression(compiler, trim(file_text));
    record_length = parse_expression(compiler, trim(length_text));
    if (!path.valid || !path.is_string || !file_number.valid ||
        file_number.is_string || !record_length.valid || record_length.is_string)
        return;
    emit(compiler,
         "if (bb_file_open(%s, %s, %.1f, %s) != 0) return bb_runtime_error(\"cannot open file\");",
         path.code, file_number.code, (double)mode, record_length.code);
}

static void compile_close(Compiler *compiler, char *arguments)
{
    Expression file_number = parse_file_number(compiler, arguments);
    if (file_number.valid && !file_number.is_string)
        emit(compiler, "bb_file_close(%s);", file_number.code);
}

static void compile_get_put(Compiler *compiler, char *arguments, bool put)
{
    char *comma = strchr(arguments, ',');
    Expression file_number;
    Expression record_number;
    if (comma == NULL) {
        compiler_error(compiler, "%s requires file and record number",
                       put ? "PUT" : "GET");
        return;
    }
    *comma = '\0';
    file_number = parse_file_number(compiler, arguments);
    record_number = parse_expression(compiler, trim(comma + 1));
    if (file_number.valid && !file_number.is_string && record_number.valid &&
        !record_number.is_string)
        emit(compiler, "%s(%s, %s);", put ? "bb_file_put" : "bb_file_get",
             file_number.code, record_number.code);
}

static void compile_field(Compiler *compiler, char *arguments)
{
    char *comma = strchr(arguments, ',');
    Expression file_number;
    char *cursor;
    if (comma == NULL) {
        compiler_error(compiler, "FIELD requires bindings");
        return;
    }
    *comma = '\0';
    file_number = parse_file_number(compiler, arguments);
    if (!file_number.valid || file_number.is_string) return;
    emit(compiler, "bb_file_field_clear(%s);", file_number.code);
    cursor = comma + 1;
    while (*trim(cursor) != '\0') {
        char *next = cursor;
        const char *as_word;
        char binding[MAX_LINE];
        char width_text[MAX_LINE];
        char variable_text[MAX_NAME];
        size_t length;
        Expression width;
        Variable *variable;
        while (*next != '\0' && *next != ',') ++next;
        length = (size_t)(next - cursor);
        if (length >= sizeof(binding)) length = sizeof(binding) - 1U;
        memcpy(binding, cursor, length);
        binding[length] = '\0';
        as_word = find_word_case_insensitive(binding, "as");
        if (as_word == NULL) {
            compiler_error(compiler, "FIELD binding without AS");
            return;
        }
        length = (size_t)(as_word - binding);
        if (length >= sizeof(width_text)) length = sizeof(width_text) - 1U;
        memcpy(width_text, binding, length);
        width_text[length] = '\0';
        copy_text(variable_text, sizeof(variable_text), as_word + 2);
        width = parse_expression(compiler, trim(width_text));
        variable = find_variable(compiler, trim(variable_text));
        if (!width.valid || width.is_string || variable == NULL ||
            !variable->is_string || variable->is_array) {
            compiler_error(compiler, "invalid FIELD binding '%s'", binding);
            return;
        }
        emit(compiler, "bb_file_field_bind(%s, %s, %s, sizeof %s);",
             file_number.code, width.code, variable->c_name, variable->c_name);
        if (*next == '\0') break;
        cursor = next + 1;
    }
}

static void compile_file_input(Compiler *compiler, char *arguments)
{
    char *comma = strchr(arguments, ',');
    Expression file_number;
    char *cursor;
    if (comma == NULL) {
        compiler_error(compiler, "file INPUT requires targets");
        return;
    }
    *comma = '\0';
    file_number = parse_file_number(compiler, arguments);
    if (!file_number.valid || file_number.is_string) return;
    cursor = comma + 1;
    while (*trim(cursor) != '\0') {
        char *separator = cursor;
        char target[MAX_LINE];
        int depth = 0;
        size_t length;
        Variable *variable = NULL;
        Expression subscripts[4];
        int subscript_count = 0;
        while (*separator != '\0') {
            if (*separator == '(') ++depth;
            else if (*separator == ')' && depth > 0) --depth;
            else if (*separator == ',' && depth == 0) break;
            ++separator;
        }
        length = (size_t)(separator - cursor);
        if (length >= sizeof(target)) length = sizeof(target) - 1U;
        memcpy(target, cursor, length);
        target[length] = '\0';
        if (!parse_reference(compiler, trim(target), &variable, subscripts,
                             &subscript_count)) {
            compiler_error(compiler, "invalid file INPUT target '%s'", target);
            return;
        }
        if (subscript_count > 0 && variable->is_array) {
            char provider[MAX_EXPR];
            (void)snprintf(provider, sizeof(provider),
                           variable->is_string
                               ? "bb_file_input_next(%s)"
                               : "bb_val(bb_file_input_next(%s))",
                           file_number.code);
            emit_array_operation(compiler,
                                 variable->is_string ? "bb_string_array_set"
                                                     : "bb_num_array_set",
                                 variable->c_name, provider, subscripts,
                                 subscript_count);
        } else if (subscript_count > 0) {
            compiler_error(compiler, "subscripted scalar in file INPUT");
            return;
        } else if (variable->is_string) {
            emit(compiler,
                 "bb_set_string(%s, sizeof %s, bb_file_input_next(%s));",
                 variable->c_name, variable->c_name, file_number.code);
        } else {
            emit(compiler, "%s = bb_val(bb_file_input_next(%s));",
                 variable->c_name, file_number.code);
        }
        if (*separator == '\0') break;
        cursor = separator + 1;
    }
}

static void compile_lset(Compiler *compiler, char *statement)
{
    char *equals = find_assignment(statement);
    Variable *variable;
    Expression value;
    if (equals == NULL) {
        compiler_error(compiler, "LSET without '='");
        return;
    }
    *equals = '\0';
    variable = find_variable(compiler, trim(statement));
    value = parse_expression(compiler, trim(equals + 1));
    if (variable == NULL || !variable->is_string || variable->is_array ||
        !value.valid || !value.is_string) {
        compiler_error(compiler, "invalid LSET assignment");
        return;
    }
    emit(compiler, "bb_lset(%s, sizeof %s, %s);", variable->c_name,
         variable->c_name, value.code);
}

static void emit_runtime_call(Compiler *compiler, const char *function,
                              int argument_count)
{
    static char code[131072];
    size_t used = (size_t)snprintf(code, sizeof(code), "%s(", function);
    for (int index = 0; index < argument_count && used < sizeof(code); ++index) {
        used += (size_t)snprintf(code + used, sizeof(code) - used, "%s%s",
                                index == 0 ? "" : ", ",
                                statement_arguments[index].code);
    }
    (void)snprintf(code + (used < sizeof(code) ? used : sizeof(code) - 1U),
                   used < sizeof(code) ? sizeof(code) - used : 1U, ");");
    emit(compiler, "%s", code);
}

static char *matching_parenthesis(char *open)
{
    int depth = 0;
    bool quoted = false;
    for (char *cursor = open; *cursor != '\0'; ++cursor) {
        if (*cursor == '"') quoted = !quoted;
        if (quoted) continue;
        if (*cursor == '(') ++depth;
        else if (*cursor == ')' && --depth == 0) return cursor;
    }
    return NULL;
}

static bool parse_point(Compiler *compiler, char *source, Expression *x,
                        Expression *y, char **remainder)
{
    char *close;
    char pair[MAX_LINE];
    size_t length;
    source = trim(source);
    if (*source != '(') {
        compiler_error(compiler, "expected coordinate pair");
        return false;
    }
    close = matching_parenthesis(source);
    if (close == NULL) {
        compiler_error(compiler, "unterminated coordinate pair");
        return false;
    }
    length = (size_t)(close - (source + 1));
    if (length >= sizeof(pair)) length = sizeof(pair) - 1U;
    memcpy(pair, source + 1, length);
    pair[length] = '\0';
    if (!split_two_arguments(compiler, pair, x, y)) return false;
    *remainder = trim(close + 1);
    return true;
}

static void compile_line_command(Compiler *compiler, char *arguments)
{
    Expression x1, y1, x2, y2, color, style;
    char *remainder;
    char *second;
    char *close;
    char pair[MAX_LINE];
    char tail[MAX_LINE];
    size_t length;
    if (!parse_point(compiler, arguments, &x1, &y1, &remainder)) return;
    if (*remainder == '-') ++remainder;
    remainder = trim(remainder);
    if (*remainder != '(') {
        compiler_error(compiler, "LINE requires a second coordinate pair");
        return;
    }
    second = remainder + 1;
    close = matching_parenthesis(remainder);
    if (close != NULL) {
        length = (size_t)(close - second);
        if (length >= sizeof(pair)) length = sizeof(pair) - 1U;
        memcpy(pair, second, length);
        pair[length] = '\0';
        if (!split_two_arguments(compiler, pair, &x2, &y2)) return;
        copy_text(tail, sizeof(tail), trim(close + 1));
        remainder = trim(tail);
        if (*remainder == ',') ++remainder;
    } else {
        /* The original samples include one tolerated missing ')' line. */
        copy_text(tail, sizeof(tail), second);
        if (parse_statement_arguments(compiler, tail) < 3) {
            compiler_error(compiler, "invalid LINE arguments");
            return;
        }
        x2 = statement_arguments[0];
        y2 = statement_arguments[1];
        color = statement_arguments[2];
        expression_format(&style, false,
                          strstr(tail, "bf") != NULL || strstr(tail, "BF") != NULL
                              ? "2.0" : "0.0");
        emit(compiler, "bb_graphics_line(%s, %s, %s, %s, %s, %s);",
             x1.code, y1.code, x2.code, y2.code, color.code, style.code);
        return;
    }
    {
        char *separator = strrchr(remainder, ',');
        char style_text[MAX_NAME] = "";
        if (separator != NULL) {
            copy_text(style_text, sizeof(style_text), trim(separator + 1));
            *separator = '\0';
        }
        color = parse_expression(compiler, trim(remainder));
        if (!color.valid || color.is_string) return;
        if (compare_case_insensitive(style_text, "bf") == 0)
            expression_format(&style, false, "2.0");
        else if (compare_case_insensitive(style_text, "b") == 0)
            expression_format(&style, false, "1.0");
        else
            expression_format(&style, false, "0.0");
    }
    emit(compiler, "bb_graphics_line(%s, %s, %s, %s, %s, %s);",
         x1.code, y1.code, x2.code, y2.code, color.code, style.code);
}

static void compile_circle_command(Compiler *compiler, char *arguments)
{
    Expression x, y;
    char *remainder;
    int count;
    if (!parse_point(compiler, arguments, &x, &y, &remainder)) return;
    if (*remainder == ',') ++remainder;
    count = parse_statement_arguments(compiler, remainder);
    if (count < 2 || count > 4) {
        compiler_error(compiler, "CIRCLE expects radius, color, and optional angles");
        return;
    }
    emit(compiler, "bb_graphics_circle(%s, %s, %s, %s, %s, %s);",
         x.code, y.code, statement_arguments[0].code,
         statement_arguments[1].code,
         count >= 3 ? statement_arguments[2].code : "0.0",
         count >= 4 ? statement_arguments[3].code : "0.0");
}

static void compile_paint_command(Compiler *compiler, char *arguments)
{
    Expression x, y;
    char *remainder;
    int count;
    if (!parse_point(compiler, arguments, &x, &y, &remainder)) return;
    if (*remainder == ',') ++remainder;
    count = parse_statement_arguments(compiler, remainder);
    if (count < 1 || count > 2) {
        compiler_error(compiler, "PAINT expects color and optional border");
        return;
    }
    emit(compiler, "bb_graphics_paint(%s, %s, %s, %s);", x.code, y.code,
         statement_arguments[0].code,
         count == 2 ? statement_arguments[1].code : statement_arguments[0].code);
}

static void compile_pset_command(Compiler *compiler, char *arguments,
                                 bool preset)
{
    Expression x, y;
    char *remainder;
    Expression color;
    if (!parse_point(compiler, arguments, &x, &y, &remainder)) return;
    if (*remainder == ',') ++remainder;
    if (*trim(remainder) == '\0')
        expression_format(&color, false, preset ? "0.0" : "15.0");
    else
        color = parse_expression(compiler, trim(remainder));
    if (color.valid && !color.is_string)
        emit(compiler, "bb_graphics_pset(%s, %s, %s);", x.code, y.code,
             color.code);
}

static bool numeric_array_from_text(Compiler *compiler, char *text,
                                    char *array_name, size_t capacity)
{
    char copy[MAX_LINE];
    char *clean;
    Variable *variable;
    Expression expression;
    copy_text(copy, sizeof(copy), text);
    clean = trim(copy);
    variable = find_variable(compiler, clean);
    if (variable != NULL && variable->is_array && !variable->is_string) {
        copy_text(array_name, capacity, variable->c_name);
        return true;
    }
    expression = parse_expression(compiler, clean);
    return expression.valid &&
           extract_numeric_array_name(&expression, array_name, capacity);
}

static void compile_graphics_get(Compiler *compiler, char *arguments)
{
    Expression x1, y1, x2, y2;
    char *remainder;
    char *second;
    char *close;
    char pair[MAX_LINE];
    char array_name[MAX_NAME];
    size_t length;
    if (!parse_point(compiler, arguments, &x1, &y1, &remainder)) return;
    if (*remainder == '-') ++remainder;
    remainder = trim(remainder);
    if (*remainder != '(') {
        compiler_error(compiler, "graphics GET requires second point");
        return;
    }
    second = remainder + 1;
    close = matching_parenthesis(remainder);
    if (close == NULL) {
        compiler_error(compiler, "graphics GET has invalid second point");
        return;
    }
    length = (size_t)(close - second);
    if (length >= sizeof(pair)) length = sizeof(pair) - 1U;
    memcpy(pair, second, length);
    pair[length] = '\0';
    if (!split_two_arguments(compiler, pair, &x2, &y2)) return;
    remainder = trim(close + 1);
    if (*remainder == ',') ++remainder;
    if (!numeric_array_from_text(compiler, remainder, array_name,
                                 sizeof(array_name))) {
        compiler_error(compiler, "graphics GET requires numeric array storage");
        return;
    }
    emit(compiler, "bb_graphics_get(%s, %s, %s, %s, &%s);", x1.code,
         y1.code, x2.code, y2.code, array_name);
}

static void compile_graphics_put(Compiler *compiler, char *arguments)
{
    Expression x, y;
    char *remainder;
    char *separator;
    char array_name[MAX_NAME];
    Expression operation;
    if (!parse_point(compiler, arguments, &x, &y, &remainder)) return;
    if (*remainder == ',') ++remainder;
    remainder = trim(remainder);
    separator = strrchr(remainder, ',');
    if (separator != NULL) {
        *separator = '\0';
        operation = parse_expression(compiler, trim(separator + 1));
    } else {
        expression_format(&operation, false, "0.0");
    }
    if (!numeric_array_from_text(compiler, remainder, array_name,
                                 sizeof(array_name))) {
        compiler_error(compiler, "graphics PUT requires numeric array storage");
        return;
    }
    if (operation.valid && !operation.is_string)
        emit(compiler, "bb_graphics_put(%s, %s, &%s, %s);", x.code, y.code,
             array_name, operation.code);
}

static bool compile_simple_call(Compiler *compiler, char *arguments,
                                const char *function, int expected)
{
    int count = parse_statement_arguments(compiler, trim(arguments));
    if (count != expected) {
        compiler_error(compiler, "%s expects %d argument(s)", function,
                       expected);
        return false;
    }
    emit_runtime_call(compiler, function, count);
    return true;
}

static bool split_two_arguments(Compiler *compiler, char *arguments,
                                Expression *first, Expression *second)
{
    bool quoted = false;
    int depth = 0;
    char *separator = NULL;
    for (char *cursor = arguments; *cursor != '\0'; ++cursor) {
        if (*cursor == '"') quoted = !quoted;
        if (quoted) continue;
        if (*cursor == '(') ++depth;
        else if (*cursor == ')' && depth > 0) --depth;
        else if (*cursor == ',' && depth == 0) {
            separator = cursor;
            break;
        }
    }
    if (separator == NULL) {
        compiler_error(compiler, "expected two comma-separated arguments");
        return false;
    }
    *separator = '\0';
    *first = parse_expression(compiler, trim(arguments));
    *second = parse_expression(compiler, trim(separator + 1));
    return first->valid && second->valid && !first->is_string &&
           !second->is_string;
}

static bool is_decimal_label(const char *text)
{
    if (*text == '\0') return false;
    while (*text != '\0') {
        if (!isdigit((unsigned char)*text)) return false;
        ++text;
    }
    return true;
}

static void emit_goto(Compiler *compiler, const char *target)
{
    char c_name[MAX_NAME];
    char copy[MAX_NAME];
    copy_text(copy, sizeof(copy), target);
    target = trim(copy);
    if (is_decimal_label(target)) {
        emit(compiler, "goto bb_line_%s;", target);
    } else if (valid_variable_name(target)) {
        sanitize_identifier(target, c_name, sizeof(c_name));
        emit(compiler, "goto bb_label_%s;", c_name + 4);
    } else {
        compiler_error(compiler, "invalid GOTO target '%s'", target);
    }
}

static char *find_assignment(char *statement)
{
    bool quoted = false;
    int depth = 0;
    for (char *cursor = statement; *cursor != '\0'; ++cursor) {
        if (*cursor == '"') quoted = !quoted;
        if (quoted) continue;
        if (*cursor == '(') ++depth;
        else if (*cursor == ')' && depth > 0) --depth;
        else if (depth == 0 && *cursor == '=' && cursor > statement &&
                 cursor[-1] != '<' && cursor[-1] != '>' && cursor[1] != '=')
            return cursor;
    }
    return NULL;
}

static bool valid_variable_name(const char *name)
{
    if (!isalpha((unsigned char)*name) && *name != '_') return false;
    ++name;
    while (*name != '\0') {
        if (!isalnum((unsigned char)*name) && *name != '_' && *name != '$' &&
            *name != '%' && *name != '&') return false;
        ++name;
    }
    return true;
}

static bool parse_reference(Compiler *compiler, const char *source,
                            Variable **variable, Expression arguments[4],
                            int *argument_count)
{
    Lexer lexer = {source, 0U, {TOKEN_END, {0}}};
    char name[MAX_NAME];
    *argument_count = 0;
    lexer_next(&lexer);
    if (lexer.current.kind != TOKEN_IDENTIFIER) return false;
    copy_text(name, sizeof(name), lexer.current.text);
    *variable = add_variable(compiler, name);
    lexer_next(&lexer);
    if (lexer.current.kind == TOKEN_LEFT_PAREN) {
        lexer_next(&lexer);
        while (lexer.current.kind != TOKEN_RIGHT_PAREN &&
               lexer.current.kind != TOKEN_END && *argument_count < 4) {
            arguments[*argument_count] =
                parse_expression_precedence(compiler, &lexer, 1);
            ++*argument_count;
            if (lexer.current.kind == TOKEN_COMMA)
                lexer_next(&lexer);
            else
                break;
        }
        if (lexer.current.kind != TOKEN_RIGHT_PAREN) return false;
        lexer_next(&lexer);
    }
    return lexer.current.kind == TOKEN_END && *variable != NULL;
}

static void emit_array_operation(Compiler *compiler, const char *function,
                                 const char *array_name, const char *value,
                                 Expression arguments[4], int argument_count)
{
    const char *prefix = value != NULL ? value : "";
    const char *separator = value != NULL ? ", " : "";
    if (argument_count == 1)
        emit(compiler, "%s(&%s, %s%s1U, (double)(%s));", function, array_name,
             prefix, separator, arguments[0].code);
    else if (argument_count == 2)
        emit(compiler, "%s(&%s, %s%s2U, (double)(%s), (double)(%s));", function, array_name,
             prefix, separator, arguments[0].code, arguments[1].code);
    else if (argument_count == 3)
        emit(compiler, "%s(&%s, %s%s3U, (double)(%s), (double)(%s), (double)(%s));", function, array_name,
             prefix, separator, arguments[0].code, arguments[1].code,
             arguments[2].code);
    else if (argument_count == 4)
        emit(compiler, "%s(&%s, %s%s4U, (double)(%s), (double)(%s), (double)(%s), (double)(%s));", function,
             array_name, prefix, separator, arguments[0].code,
             arguments[1].code, arguments[2].code, arguments[3].code);
}

static void compile_dim(Compiler *compiler, char *declaration)
{
    Variable *variable = NULL;
    Expression bounds[4];
    int bound_count = 0;
    if (!parse_reference(compiler, trim(declaration), &variable, bounds,
                         &bound_count) || bound_count == 0) {
        compiler_error(compiler, "invalid DIM declaration '%s'", declaration);
        return;
    }
    variable->is_array = true;
    emit_array_operation(compiler,
                         variable->is_string ? "bb_string_array_dim"
                                             : "bb_num_array_dim",
                         variable->c_name, NULL, bounds, bound_count);
}

static void compile_assignment(Compiler *compiler, char *left, char *right)
{
    Variable *variable = NULL;
    Expression arguments[4];
    int argument_count = 0;
    Expression expression;
    left = trim(left);
    right = trim(right);
    if (starts_word(left, "let")) left = trim(left + 3);
    if (!parse_reference(compiler, left, &variable, arguments,
                         &argument_count)) {
        compiler_error(compiler, "unsupported assignment target '%s'", left);
        return;
    }
    expression = parse_expression(compiler, right);
    if (variable == NULL || !expression.valid) return;
    if (variable->is_string != expression.is_string) {
        compiler_error(compiler, "type mismatch assigning '%s'", left);
    } else if (argument_count > 0 && variable->is_array) {
        emit_array_operation(compiler,
                             variable->is_string ? "bb_string_array_set"
                                                 : "bb_num_array_set",
                             variable->c_name, expression.code, arguments,
                             argument_count);
    } else if (argument_count > 0) {
        compiler_error(compiler, "subscripted scalar '%s'", left);
    } else if (variable->is_array) {
        emit(compiler, "%s(&%s, %s);",
             variable->is_string ? "bb_string_array_fill" : "bb_num_array_fill",
             variable->c_name, expression.code);
    } else if (variable->is_string) {
        emit(compiler, "bb_set_string(%s, sizeof %s, %s);", variable->c_name,
             variable->c_name, expression.code);
    } else {
        emit(compiler, "%s = %s;", variable->c_name, expression.code);
    }
}

static void compile_inline_if_statement(Compiler *compiler, char *statement)
{
    statement = trim(statement);
    if (is_decimal_label(statement))
        emit_goto(compiler, statement);
    else
        compile_statement(compiler, statement);
}

static void compile_if(Compiler *compiler, char *statement)
{
    char tail[MAX_LINE];
    Expression expression;
    if (!parse_if_parts(compiler, statement, &expression, tail, sizeof(tail)))
        return;
    if (*trim(tail) == '\0') {
        emit(compiler, "if ((%s) != 0.0) {", expression.code);
        ++compiler->indentation;
        push_block(compiler, BLOCK_IF);
    } else {
        const char *else_word = find_word_case_insensitive(tail, "else");
        emit(compiler, "if ((%s) != 0.0) {", expression.code);
        ++compiler->indentation;
        if (else_word != NULL) {
            char true_statement[MAX_LINE];
            size_t length = (size_t)(else_word - tail);
            if (length >= sizeof(true_statement))
                length = sizeof(true_statement) - 1U;
            memcpy(true_statement, tail, length);
            true_statement[length] = '\0';
            for_each_statement(true_statement, compile_inline_if_statement,
                               compiler);
            --compiler->indentation;
            emit(compiler, "} else {");
            ++compiler->indentation;
            for_each_statement(trim((char *)else_word + 4),
                               compile_inline_if_statement, compiler);
        } else {
            for_each_statement(tail, compile_inline_if_statement, compiler);
        }
        --compiler->indentation;
        emit(compiler, "}");
    }
}

static void compile_for(Compiler *compiler, char *statement)
{
    char *equals = find_assignment(statement);
    const char *to_word;
    const char *step_word;
    char variable_name[MAX_NAME];
    char start_text[MAX_LINE];
    char end_text[MAX_LINE];
    char step_text[MAX_LINE];
    Variable *variable;
    Expression start;
    Expression end;
    Expression step;
    size_t identifier;
    if (equals == NULL) {
        compiler_error(compiler, "FOR without '='");
        return;
    }
    *equals = '\0';
    copy_text(variable_name, sizeof(variable_name), trim(statement));
    if (!valid_variable_name(variable_name)) {
        compiler_error(compiler, "invalid FOR variable '%s'", variable_name);
        return;
    }
    to_word = find_word_case_insensitive(equals + 1, "to");
    if (to_word == NULL) {
        compiler_error(compiler, "FOR without TO");
        return;
    }
    {
        size_t length = (size_t)(to_word - (equals + 1));
        if (length >= sizeof(start_text)) length = sizeof(start_text) - 1U;
        memcpy(start_text, equals + 1, length);
        start_text[length] = '\0';
    }
    copy_text(end_text, sizeof(end_text), to_word + 2);
    step_word = find_word_case_insensitive(end_text, "step");
    copy_text(step_text, sizeof(step_text), "1");
    if (step_word != NULL) {
        size_t length = (size_t)(step_word - end_text);
        copy_text(step_text, sizeof(step_text), step_word + 4);
        end_text[length] = '\0';
    }
    variable = add_variable(compiler, variable_name);
    start = parse_expression(compiler, trim(start_text));
    end = parse_expression(compiler, trim(end_text));
    step = parse_expression(compiler, trim(step_text));
    if (variable == NULL || variable->is_string || variable->is_array ||
        !start.valid || start.is_string || !end.valid || end.is_string ||
        !step.valid || step.is_string)
        return;
    identifier = compiler->next_for_loop++;
    emit(compiler, "%s = %s;", variable->c_name, start.code);
    emit(compiler, "bb_for_end[%zu] = %s;", identifier, end.code);
    emit(compiler, "bb_for_step[%zu] = %s;", identifier, step.code);
    emit(compiler,
         "while ((bb_for_step[%zu] >= 0.0) ? (%s <= bb_for_end[%zu]) : "
         "(%s >= bb_for_end[%zu])) {",
         identifier, variable->c_name, identifier, variable->c_name,
         identifier);
    ++compiler->indentation;
    push_block(compiler, BLOCK_FOR);
    if (compiler->block_count > 0U) {
        Block *block = &compiler->blocks[compiler->block_count - 1U];
        copy_text(block->variable, sizeof(block->variable), variable->c_name);
        block->identifier = identifier;
    }
}

static void compile_next(Compiler *compiler, char *variable_name)
{
    Block *block;
    variable_name = trim(variable_name);
    if (compiler->block_count == 0U ||
        compiler->blocks[compiler->block_count - 1U].kind != BLOCK_FOR) {
        compiler_error(compiler, "NEXT outside FOR block");
        return;
    }
    block = &compiler->blocks[compiler->block_count - 1U];
    if (*variable_name != '\0') {
        Variable *variable = find_variable(compiler, variable_name);
        if (variable == NULL || strcmp(variable->c_name, block->variable) != 0) {
            compiler_error(compiler, "NEXT variable '%s' does not match FOR",
                           variable_name);
            return;
        }
    }
    emit(compiler, "%s += bb_for_step[%zu];", block->variable,
         block->identifier);
    --compiler->block_count;
    --compiler->indentation;
    emit(compiler, "}");
}

static void compile_gosub(Compiler *compiler, char *target)
{
    size_t identifier = compiler->next_gosub++;
    emit(compiler,
         "if (bb_gosub_sp >= 256U) return bb_runtime_error(\"GOSUB stack overflow\");");
    emit(compiler, "bb_gosub_stack[bb_gosub_sp++] = %zuU;", identifier);
    emit_goto(compiler, target);
    emit(compiler, "bb_gosub_return_%zu: ;", identifier);
}

static void compile_return(Compiler *compiler)
{
    emit(compiler,
         "if (bb_gosub_sp == 0U) return bb_runtime_error(\"RETURN without GOSUB\");");
    emit(compiler, "switch (bb_gosub_stack[--bb_gosub_sp]) {");
    ++compiler->indentation;
    for (size_t index = 0U; index < compiler->total_gosubs; ++index)
        emit(compiler, "case %zuU: goto bb_gosub_return_%zu;", index, index);
    emit(compiler, "default: return bb_runtime_error(\"invalid GOSUB return address\");");
    --compiler->indentation;
    emit(compiler, "}");
}

static void compile_read(Compiler *compiler, char *targets)
{
    char *cursor = targets;
    while (*trim(cursor) != '\0') {
        char target[MAX_LINE];
        char *separator = cursor;
        bool quoted = false;
        int depth = 0;
        size_t length;
        Variable *variable = NULL;
        Expression arguments[4];
        int argument_count = 0;
        while (*separator != '\0') {
            if (*separator == '"') quoted = !quoted;
            if (!quoted) {
                if (*separator == '(') ++depth;
                else if (*separator == ')' && depth > 0) --depth;
                else if (*separator == ',' && depth == 0) break;
            }
            ++separator;
        }
        length = (size_t)(separator - cursor);
        if (length >= sizeof(target)) length = sizeof(target) - 1U;
        memcpy(target, cursor, length);
        target[length] = '\0';
        if (!parse_reference(compiler, trim(target), &variable, arguments,
                             &argument_count)) {
            compiler_error(compiler, "invalid READ target '%s'", target);
            return;
        }
        emit(compiler,
             "if (bb_data_cursor >= bb_data_count) return bb_runtime_error(\"READ past end of DATA\");");
        if (argument_count > 0 && variable->is_array) {
            emit_array_operation(compiler,
                                 variable->is_string ? "bb_string_array_set"
                                                     : "bb_num_array_set",
                                 variable->c_name,
                                 variable->is_string
                                     ? "bb_data[bb_data_cursor++]"
                                     : "bb_val(bb_data[bb_data_cursor++])",
                                 arguments, argument_count);
        } else if (argument_count > 0) {
            compiler_error(compiler, "subscripted scalar in READ '%s'", target);
            return;
        } else if (variable->is_string) {
            emit(compiler, "bb_set_string(%s, sizeof %s, bb_data[bb_data_cursor++]);",
                 variable->c_name, variable->c_name);
        } else {
            emit(compiler, "%s = bb_val(bb_data[bb_data_cursor++]);",
                 variable->c_name);
        }
        if (*separator == '\0') break;
        cursor = separator + 1;
    }
}

static void compile_restore(Compiler *compiler, char *target)
{
    target = trim(target);
    if (*target == '\0') {
        emit(compiler, "bb_data_cursor = 0U;");
        return;
    }
    for (size_t index = 0U; index < compiler->data_label_count; ++index) {
        if (compare_case_insensitive(compiler->data_labels[index].name,
                                     target) == 0) {
            emit(compiler, "bb_data_cursor = %zuU;",
                 compiler->data_labels[index].position);
            return;
        }
    }
    compiler_error(compiler, "unknown RESTORE label '%s'", target);
}

static void compile_input(Compiler *compiler, char *arguments)
{
    char prompt[MAX_EXPR] = "\"\"";
    char *targets = trim(arguments);
    char *cursor;
    if (*targets == '#') {
        compile_file_input(compiler, targets);
        return;
    }
    if (*targets == '"') {
        Lexer lexer = {targets, 0U, {TOKEN_END, {0}}};
        lexer_next(&lexer);
        if (lexer.current.kind != TOKEN_STRING) {
            compiler_error(compiler, "invalid INPUT prompt");
            return;
        }
        (void)snprintf(prompt, sizeof(prompt), "\"%s\"", lexer.current.text);
        cursor = targets + lexer.position;
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (*cursor == ';' || *cursor == ',') ++cursor;
        targets = trim(cursor);
    }
    emit(compiler, "bb_input_begin(%s);", prompt);
    cursor = targets;
    while (*trim(cursor) != '\0') {
        char target[MAX_LINE];
        char *separator = cursor;
        int depth = 0;
        size_t length;
        Variable *variable = NULL;
        Expression subscripts[4];
        int subscript_count = 0;
        while (*separator != '\0') {
            if (*separator == '(') ++depth;
            else if (*separator == ')' && depth > 0) --depth;
            else if (*separator == ',' && depth == 0) break;
            ++separator;
        }
        length = (size_t)(separator - cursor);
        if (length >= sizeof(target)) length = sizeof(target) - 1U;
        memcpy(target, cursor, length);
        target[length] = '\0';
        if (!parse_reference(compiler, trim(target), &variable, subscripts,
                             &subscript_count)) {
            compiler_error(compiler, "invalid INPUT target '%s'", target);
            return;
        }
        if (subscript_count > 0 && variable->is_array) {
            emit_array_operation(compiler,
                                 variable->is_string ? "bb_string_array_set"
                                                     : "bb_num_array_set",
                                 variable->c_name,
                                 variable->is_string ? "bb_input_next()"
                                                     : "bb_val(bb_input_next())",
                                 subscripts, subscript_count);
        } else if (subscript_count > 0) {
            compiler_error(compiler, "subscripted scalar in INPUT '%s'", target);
            return;
        } else if (variable->is_string) {
            emit(compiler, "bb_set_string(%s, sizeof %s, bb_input_next());",
                 variable->c_name, variable->c_name);
        } else {
            emit(compiler, "%s = bb_val(bb_input_next());", variable->c_name);
        }
        if (*separator == '\0') break;
        cursor = separator + 1;
    }
}

static void compile_statement(Compiler *compiler, char *statement)
{
    char *equals;
    size_t statement_length;
    statement = trim(statement);
    if (*statement == '\0' || starts_word(statement, "rem") || *statement == '\'')
        return;
    {
        const char *inline_rem = find_word_case_insensitive(statement, "rem");
        if (inline_rem != NULL && inline_rem != statement) {
            *((char *)inline_rem) = '\0';
            statement = trim(statement);
        }
    }

    if (isdigit((unsigned char)*statement)) {
        char *cursor = statement;
        while (isdigit((unsigned char)*cursor)) ++cursor;
        if (isspace((unsigned char)*cursor)) {
            char saved = *cursor;
            *cursor = '\0';
            emit(compiler, "bb_line_%s: ;", statement);
            *cursor = saved;
            compile_statement(compiler, trim(cursor));
            return;
        }
    }

    if (is_windows_ui_statement(statement)) compiler->uses_windows_ui = true;

    statement_length = strlen(statement);
    if (is_decimal_label(statement)) {
        emit(compiler, "bb_line_%s: ;", statement);
    } else if (statement_length > 1U && statement[statement_length - 1U] == ':') {
        char c_name[MAX_NAME];
        statement[statement_length - 1U] = '\0';
        if (!valid_variable_name(trim(statement))) {
            compiler_error(compiler, "invalid label '%s'", statement);
            return;
        }
        sanitize_identifier(trim(statement), c_name, sizeof(c_name));
        emit(compiler, "bb_label_%s: ;", c_name + 4);
    } else if (starts_word(statement, "screen")) {
        int count = parse_statement_arguments(compiler, trim(statement + 6));
        if (count == 1) {
            expression_format(&statement_arguments[1], false, "0.0");
            emit_runtime_call(compiler, "bb_screen", 2);
        } else if (count == 2) {
            emit_runtime_call(compiler, "bb_screen", 2);
        } else {
            compiler_error(compiler, "SCREEN expects one or two arguments");
        }
    } else if (starts_word(statement, "position")) {
        int count = parse_statement_arguments(compiler, trim(statement + 8));
        if (count == 4) {
            expression_format(&statement_arguments[4], false, "0.0");
            emit_runtime_call(compiler, "bb_position", 5);
        } else if (count == 5) {
            emit_runtime_call(compiler, "bb_position", 5);
        } else {
            compiler_error(compiler, "POSITION expects four or five arguments");
        }
    } else if (starts_word(statement, "line")) {
        compile_line_command(compiler, trim(statement + 4));
    } else if (starts_word(statement, "circle")) {
        compile_circle_command(compiler, trim(statement + 6));
    } else if (starts_word(statement, "paint")) {
        compile_paint_command(compiler, trim(statement + 5));
    } else if (starts_word(statement, "pset")) {
        compile_pset_command(compiler, trim(statement + 4), false);
    } else if (starts_word(statement, "preset")) {
        compile_pset_command(compiler, trim(statement + 6), true);
    } else if (starts_word(statement, "control")) {
        (void)compile_simple_call(compiler, statement + 7, "bb_control", 11);
    } else if (starts_word(statement, "dcontrol")) {
        (void)compile_simple_call(compiler, statement + 8,
                                  "bb_delete_control", 1);
    } else if (starts_word(statement, "setctext")) {
        (void)compile_simple_call(compiler, statement + 8,
                                  "bb_set_control_text", 2);
    } else if (starts_word(statement, "radioon")) {
        int count = parse_statement_arguments(compiler, trim(statement + 7));
        if (count == 1) {
            expression_format(&statement_arguments[1], false, "1.0");
            emit_runtime_call(compiler, "bb_radio", 2);
        }
    } else if (starts_word(statement, "radiooff")) {
        int count = parse_statement_arguments(compiler, trim(statement + 8));
        if (count == 1) {
            expression_format(&statement_arguments[1], false, "0.0");
            emit_runtime_call(compiler, "bb_radio", 2);
        }
    } else if (starts_word(statement, "createfont")) {
        int count = parse_statement_arguments(compiler, trim(statement + 10));
        if (count == 14) {
            statement_arguments[14] = statement_arguments[13];
            expression_format(&statement_arguments[13], false, "0.0");
            emit_runtime_call(compiler, "bb_create_font", 15);
        } else if (count == 15) {
            emit_runtime_call(compiler, "bb_create_font", 15);
        } else {
            compiler_error(compiler, "CREATEFONT expects 14 or 15 arguments");
        }
    } else if (starts_word(statement, "selectfont")) {
        (void)compile_simple_call(compiler, statement + 10,
                                  "bb_select_font", 1);
    } else if (starts_word(statement, "mainmenu")) {
        (void)compile_simple_call(compiler, statement + 8, "bb_main_menu", 6);
    } else if (starts_word(statement, "addsubmenu")) {
        (void)compile_simple_call(compiler, statement + 10,
                                  "bb_add_submenu", 3);
    } else if (starts_word(statement, "menuitemon")) {
        int count = parse_statement_arguments(compiler, trim(statement + 10));
        if (count == 1) {
            expression_format(&statement_arguments[1], false, "1.0");
            emit_runtime_call(compiler, "bb_menu_item_state", 2);
        }
    } else if (starts_word(statement, "menuitemgray")) {
        int count = parse_statement_arguments(compiler, trim(statement + 12));
        if (count == 1) {
            expression_format(&statement_arguments[1], false, "0.0");
            emit_runtime_call(compiler, "bb_menu_item_state", 2);
        }
    } else if (starts_word(statement, "messagebox")) {
        (void)compile_simple_call(compiler, statement + 10,
                                  "bb_message_box", 3);
    } else if (starts_word(statement, "dialog")) {
        int count = parse_statement_arguments(compiler, trim(statement + 6));
        char array_name[MAX_NAME];
        if (count == 7 &&
            extract_string_array_name(&statement_arguments[0], array_name,
                                      sizeof(array_name))) {
            emit(compiler, "bb_custom_dialog(&%s, %s, %s, %s, %s, %s, %s);",
                 array_name, statement_arguments[1].code,
                 statement_arguments[2].code, statement_arguments[3].code,
                 statement_arguments[4].code, statement_arguments[5].code,
                 statement_arguments[6].code);
        } else {
            compiler_error(compiler, "DIALOG expects a string array and six arguments");
        }
    } else if (starts_word(statement, "openfileread")) {
        int count = parse_statement_arguments(compiler, trim(statement + 12));
        if (count == 4) {
            expression_format(&statement_arguments[4], false, "0.0");
            emit_runtime_call(compiler, "bb_open_file_dialog", 5);
        }
    } else if (starts_word(statement, "openfilesave")) {
        int count = parse_statement_arguments(compiler, trim(statement + 12));
        if (count == 4) {
            expression_format(&statement_arguments[4], false, "1.0");
            emit_runtime_call(compiler, "bb_open_file_dialog", 5);
        }
    } else if (starts_word(statement, "playsound")) {
        (void)compile_simple_call(compiler, statement + 9, "bb_play_sound", 2);
    } else if (starts_word(statement, "sleep")) {
        (void)compile_simple_call(compiler, statement + 5, "bb_sleep", 1);
    } else if (starts_word(statement, "palette")) {
        (void)compile_simple_call(compiler, statement + 7, "bb_palette", 2);
    } else if (starts_word(statement, "createbitmap")) {
        (void)compile_simple_call(compiler, statement + 12,
                                  "bb_create_bitmap", 4);
    } else if (starts_word(statement, "selectbitmap")) {
        (void)compile_simple_call(compiler, statement + 12,
                                  "bb_select_bitmap", 1);
    } else if (compare_case_insensitive(statement, "selectdisplay") == 0) {
        emit(compiler, "bb_select_display();");
    } else if (compare_case_insensitive(statement, "selectprint") == 0) {
        emit(compiler, "bb_select_print();");
    } else if (starts_word(statement, "loadbitmap")) {
        (void)compile_simple_call(compiler, statement + 10,
                                  "bb_load_bitmap", 11);
    } else if (starts_word(statement, "storebitmap")) {
        (void)compile_simple_call(compiler, statement + 11,
                                  "bb_store_bitmap", 8);
    } else if (starts_word(statement, "copybits")) {
        (void)compile_simple_call(compiler, statement + 8, "bb_copy_bits", 9);
    } else if (starts_word(statement, "stretchbits")) {
        (void)compile_simple_call(compiler, statement + 11,
                                  "bb_stretch_bits", 11);
    } else if (starts_word(statement, "printcontrol")) {
        int count = parse_statement_arguments(compiler, trim(statement + 12));
        if (count == 1) {
            emit(compiler,
                 "bb_print_control(%s, NULL, 0.0, 0.0, 0.0, 0.0, 0.0);",
                 statement_arguments[0].code);
        } else if (count == 7) {
            emit(compiler,
                 "bb_print_control(%s, &(%s), %s, %s, %s, %s, %s);",
                 statement_arguments[0].code, statement_arguments[1].code,
                 statement_arguments[2].code, statement_arguments[3].code,
                 statement_arguments[4].code, statement_arguments[5].code,
                 statement_arguments[6].code);
        } else {
            compiler_error(compiler, "PRINTCONTROL expects one or seven arguments");
        }
    } else if (starts_word(statement, "seterrlevel")) {
        (void)compile_simple_call(compiler, statement + 11,
                                  "bb_set_error_level", 1);
    } else if (starts_word(statement, "scrollarea")) {
        (void)compile_simple_call(compiler, statement + 10,
                                  "bb_scroll_area", 4);
    } else if (starts_word(statement, "setcom")) {
        (void)compile_simple_call(compiler, statement + 6, "bb_set_com", 2);
    } else if (starts_word(statement, "on paint gosub")) {
        emit(compiler, "bb_on_paint();");
    } else if (starts_word(statement, "print")) {
        char *arguments = trim(statement + 5);
        if (*arguments == '#')
            compile_file_print(compiler, arguments);
        else if (starts_word(arguments, "using"))
            compile_print_using(compiler, trim(arguments + 5));
        else
            compile_print(compiler, arguments);
    } else if (compare_case_insensitive(statement, "cls") == 0) {
        emit(compiler, "bb_cls();");
    } else if (starts_word(statement, "color")) {
        Expression foreground;
        Expression background;
        if (split_two_arguments(compiler, trim(statement + 5), &foreground,
                                &background))
            emit(compiler, "bb_color(%s, %s);", foreground.code,
                 background.code);
    } else if (starts_word(statement, "locate")) {
        Expression row;
        Expression column;
        if (split_two_arguments(compiler, trim(statement + 6), &row, &column))
            emit(compiler, "bb_locate(%s, %s);", row.code, column.code);
    } else if (starts_word(statement, "goto")) {
        emit_goto(compiler, statement + 4);
    } else if (starts_word(statement, "gosub")) {
        compile_gosub(compiler, trim(statement + 5));
    } else if (compare_case_insensitive(statement, "return") == 0) {
        compile_return(compiler);
    } else if (starts_word(statement, "dim")) {
        compile_dim(compiler, trim(statement + 3));
    } else if (starts_word(statement, "open")) {
        compile_open(compiler, trim(statement + 4));
    } else if (starts_word(statement, "close")) {
        compile_close(compiler, trim(statement + 5));
    } else if (starts_word(statement, "field")) {
        compile_field(compiler, trim(statement + 5));
    } else if (starts_word(statement, "get")) {
        char *arguments = trim(statement + 3);
        if (*arguments == '#')
            compile_get_put(compiler, arguments, false);
        else
            compile_graphics_get(compiler, arguments);
    } else if (starts_word(statement, "put")) {
        char *arguments = trim(statement + 3);
        if (*arguments == '#')
            compile_get_put(compiler, arguments, true);
        else
            compile_graphics_put(compiler, arguments);
    } else if (starts_word(statement, "lset")) {
        compile_lset(compiler, trim(statement + 4));
    } else if (starts_word(statement, "read")) {
        compile_read(compiler, trim(statement + 4));
    } else if (starts_word(statement, "input")) {
        char *arguments = trim(statement + 5);
        if (*arguments == '#')
            compile_file_input(compiler, arguments);
        else
            compile_input(compiler, arguments);
    } else if (starts_word(statement, "restore")) {
        compile_restore(compiler, trim(statement + 7));
    } else if (starts_word(statement, "data")) {
        /* DATA is emitted as a table before the executable statements. */
    } else if (starts_word(statement, "for")) {
        compile_for(compiler, trim(statement + 3));
    } else if (starts_word(statement, "next")) {
        compile_next(compiler, trim(statement + 4));
    } else if (compare_case_insensitive(statement, "beep") == 0) {
        emit(compiler, "bb_beep();");
    } else if (starts_word(statement, "randomize")) {
        char *seed_text = trim(statement + 9);
        if (*seed_text == '\0') {
            emit(compiler, "bb_randomize(0.0);");
        } else {
            Expression seed = parse_expression(compiler, seed_text);
            if (seed.valid && !seed.is_string)
                emit(compiler, "bb_randomize(%s);", seed.code);
        }
    } else if (starts_word(statement, "if")) {
        compile_if(compiler, trim(statement + 2));
    } else if (starts_word(statement, "elseif") ||
               starts_word(statement, "else if")) {
        char tail[MAX_LINE];
        Expression expression;
        if (compiler->block_count == 0U ||
            compiler->blocks[compiler->block_count - 1U].kind != BLOCK_IF) {
            compiler_error(compiler, "ELSEIF outside IF block");
            return;
        }
        if (!parse_if_parts(compiler,
                            trim(statement + (starts_word(statement, "else if") ? 7 : 6)),
                            &expression, tail,
                            sizeof(tail)))
            return;
        --compiler->indentation;
        emit(compiler, "} else if ((%s) != 0.0) {", expression.code);
        ++compiler->indentation;
        if (*trim(tail) != '\0') compile_statement(compiler, trim(tail));
    } else if (compare_case_insensitive(statement, "else") == 0) {
        if (compiler->block_count == 0U ||
            compiler->blocks[compiler->block_count - 1U].kind != BLOCK_IF) {
            compiler_error(compiler, "ELSE outside IF block");
            return;
        }
        --compiler->indentation;
        emit(compiler, "} else {");
        ++compiler->indentation;
    } else if (compare_case_insensitive(statement, "end if") == 0 ||
               compare_case_insensitive(statement, "endif") == 0) {
        if (pop_block(compiler, BLOCK_IF)) {
            --compiler->indentation;
            emit(compiler, "}");
        }
    } else if (starts_word(statement, "do while")) {
        Expression expression = parse_expression(compiler, trim(statement + 8));
        if (expression.valid && !expression.is_string) {
            emit(compiler, "while ((%s) != 0.0) {", expression.code);
            ++compiler->indentation;
            push_block(compiler, BLOCK_DO);
        }
    } else if (compare_case_insensitive(statement, "do") == 0) {
        emit(compiler, "do {");
        ++compiler->indentation;
        push_block(compiler, BLOCK_DO);
    } else if (starts_word(statement, "loop while")) {
        Expression expression = parse_expression(compiler, trim(statement + 10));
        if (pop_block(compiler, BLOCK_DO)) {
            --compiler->indentation;
            if (expression.valid && !expression.is_string)
                emit(compiler, "} while ((%s) != 0.0);", expression.code);
        }
    } else if (compare_case_insensitive(statement, "loop") == 0) {
        if (pop_block(compiler, BLOCK_DO)) {
            --compiler->indentation;
            emit(compiler, "}");
        }
    } else if (compare_case_insensitive(statement, "stop") == 0 ||
               compare_case_insensitive(statement, "end") == 0) {
        emit(compiler, "return 0;");
    } else if ((equals = find_assignment(statement)) != NULL) {
        *equals = '\0';
        compile_assignment(compiler, statement, equals + 1);
    } else {
        compiler_error(compiler, "unsupported statement '%s'", statement);
    }
}

static bool is_reserved_word(const char *name)
{
    static const char *const words[] = {
        "rem", "windows", "print", "if", "then", "elseif", "else",
        "end", "endif", "do", "while", "loop", "stop", "let", "and",
        "or", "mod", "ostype", "system", "inkey$", "len", "asc", "chr$", "int",
        "abs", "val", "freemem", "rnd", "timer", "time$", "date$",
        "left$", "right$", "mid$", "str$", "space$", "ucase$", "instr",
        "eof", "loc", "input$",
        "dialog$", "dir$", "getctext", "setfocus", "getfocus", "list", "dlen",
        "font", "device", "snddev", "mouseon", "mousex", "mousey", "mouseb",
        "bitmaph", "bitmapc",
        "cls", "color", "locate", "goto", "gosub", "return", "dim",
        "for", "to", "step", "next", "data", "read", "restore", "input", "beep",
        "open", "close", "field", "get", "put", "lset", "as", "len",
        "random", "output", "append", "randomize"
    };
    for (size_t index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        if (compare_case_insensitive(name, words[index]) == 0) return true;
    }
    return false;
}

static void add_data_label(Compiler *compiler, const char *name)
{
    if (compiler->data_label_count >= MAX_DATA_LABELS) {
        compiler_error(compiler, "too many DATA labels");
        return;
    }
    copy_text(compiler->data_labels[compiler->data_label_count].name,
              sizeof(compiler->data_labels[compiler->data_label_count].name),
              name);
    compiler->data_labels[compiler->data_label_count].position =
        compiler->data_count;
    ++compiler->data_label_count;
}

static void collect_data(Compiler *compiler, char *items)
{
    char *cursor = items;
    while (*trim(cursor) != '\0') {
        char value[MAX_TOKEN];
        char *start = trim(cursor);
        char *end;
        size_t length;
        if (*start == '"') {
            ++start;
            end = start;
            while (*end != '\0' && *end != '"') ++end;
            length = (size_t)(end - start);
            if (*end == '"') ++end;
            while (isspace((unsigned char)*end)) ++end;
        } else {
            end = start;
            while (*end != '\0' && *end != ',') ++end;
            length = (size_t)(end - start);
            while (length > 0U &&
                   isspace((unsigned char)start[length - 1U]))
                --length;
        }
        if (length >= sizeof(value)) length = sizeof(value) - 1U;
        memcpy(value, start, length);
        value[length] = '\0';
        if (compiler->data_count >= MAX_DATA_ITEMS) {
            compiler_error(compiler, "too many DATA items");
            return;
        }
        copy_text(compiler->data_items[compiler->data_count].text,
                  sizeof(compiler->data_items[compiler->data_count].text),
                  value);
        ++compiler->data_count;
        while (*end != '\0' && *end != ',') ++end;
        if (*end != ',') break;
        cursor = end + 1;
    }
}

static void collect_variables(Compiler *compiler, char *statement)
{
    char *clean = trim(statement);
    char *cursor = clean;
    size_t clean_length = strlen(clean);
    bool paint_handler = starts_word(clean, "on paint gosub");
    if (starts_word(clean, "rem")) {
        char *metadata = trim(clean + 3);
        if (starts_word(metadata, "windows name")) {
            char *quote = strchr(metadata, '"');
            if (quote != NULL) {
                char *end = strchr(quote + 1, '"');
                if (end != NULL) *end = '\0';
                copy_text(compiler->window_name, sizeof(compiler->window_name),
                          quote + 1);
            }
        } else if (starts_word(metadata, "windows size")) {
            char *values = trim(metadata + strlen("windows size"));
            if (sscanf(values, "%lf,%lf,%lf,%lf",
                       &compiler->window_size[0], &compiler->window_size[1],
                       &compiler->window_size[2], &compiler->window_size[3]) == 4)
                compiler->has_window_size = true;
        } else if (strlen(metadata) >= 6U && metadata[0] == '$' &&
                   tolower((unsigned char)metadata[1]) == 'i' &&
                   tolower((unsigned char)metadata[2]) == 'c' &&
                   tolower((unsigned char)metadata[3]) == 'o' &&
                   tolower((unsigned char)metadata[4]) == 'n' &&
                   metadata[5] == ':') {
            char *value = trim(metadata + 6);
            char quote = *value;
            char *end;
            if (quote == '\'' || quote == '"') ++value;
            end = value + strlen(value);
            if (quote == '\'' || quote == '"') {
                char *closing = strchr(value, quote);
                if (closing != NULL) end = closing;
            }
            while (end > value && isspace((unsigned char)end[-1])) --end;
            *end = '\0';
            copy_text(compiler->icon_path, sizeof(compiler->icon_path), value);
        }
        return;
    }
    if (clean_length > 1U && clean[clean_length - 1U] == ':') {
        clean[clean_length - 1U] = '\0';
        if (valid_variable_name(clean) || is_decimal_label(clean)) {
            add_data_label(compiler, clean);
            return;
        }
        clean[clean_length - 1U] = ':';
    }
    if (isdigit((unsigned char)*clean)) {
        char *after_label = clean;
        while (isdigit((unsigned char)*after_label)) ++after_label;
        if (isspace((unsigned char)*after_label)) {
            char saved = *after_label;
            *after_label = '\0';
            add_data_label(compiler, clean);
            *after_label = saved;
            clean = trim(after_label);
            cursor = clean;
            clean_length = strlen(clean);
        }
    }
    if (starts_word(clean, "data")) {
        compiler->uses_data = true;
        collect_data(compiler, trim(clean + 4));
        return;
    }
    if (starts_word(clean, "read") || starts_word(clean, "restore"))
        compiler->uses_data = true;
    if (starts_word(clean, "for")) ++compiler->total_for_loops;
    if (starts_word(clean, "dim")) {
        char *name_start = trim(clean + 3);
        char name[MAX_NAME];
        size_t length = 0U;
        while ((isalnum((unsigned char)name_start[length]) ||
                name_start[length] == '_' || name_start[length] == '$' ||
                name_start[length] == '%' || name_start[length] == '&') &&
               length + 1U < sizeof(name)) {
            name[length] = name_start[length];
            ++length;
        }
        name[length] = '\0';
        if (length > 0U) {
            Variable *variable = add_variable(compiler, name);
            if (variable != NULL) variable->is_array = true;
        }
    }
    while (*cursor != '\0') {
        char name[MAX_NAME];
        size_t length = 0;
        if (*cursor == '\'') break;
        if (*cursor == '"') {
            ++cursor;
            while (*cursor != '\0' && *cursor != '"') ++cursor;
            if (*cursor == '"') ++cursor;
            continue;
        }
        if (!isalpha((unsigned char)*cursor) && *cursor != '_') {
            ++cursor;
            continue;
        }
        while ((isalnum((unsigned char)*cursor) || *cursor == '_' ||
                *cursor == '$' || *cursor == '%' || *cursor == '&') &&
               length + 1U < sizeof(name)) {
            name[length++] = *cursor++;
        }
        name[length] = '\0';
        if (compare_case_insensitive(name, "rem") == 0) break;
        if (compare_case_insensitive(name, "gosub") == 0 && !paint_handler) {
            ++compiler->total_gosubs;
            compiler->uses_gosub_stack = true;
        }
        if (compare_case_insensitive(name, "return") == 0)
            compiler->uses_gosub_stack = true;
        if (!is_reserved_word(name)) (void)add_variable(compiler, name);
    }
}

static void for_each_statement(char *line,
                               void (*callback)(Compiler *, char *),
                               Compiler *compiler)
{
    char *clean = trim(line);
    char *cursor = line;
    char *start = line;
    bool quoted = false;
    /* REM metadata can contain colons and single-quoted values. Preserve the
       complete comment so directives such as $ICON are available to the
       metadata collector instead of being split as BASIC statements. */
    if (starts_word(clean, "rem")) {
        callback(compiler, clean);
        return;
    }
    while (true) {
        if (*cursor == '"') quoted = !quoted;
        if (!quoted && *cursor == '\'') {
            *cursor = '\0';
            callback(compiler, start);
            break;
        }
        if (!quoted && *cursor == ':') {
            char prefix[MAX_LINE];
            size_t prefix_length = (size_t)(cursor - start);
            char *candidate;
            char *after_colon = trim(cursor + 1);
            if (starts_word(after_colon, "rem") || *after_colon == '\'') {
                *cursor = '\0';
                callback(compiler, start);
                break;
            }
            if (prefix_length >= sizeof(prefix))
                prefix_length = sizeof(prefix) - 1U;
            memcpy(prefix, start, prefix_length);
            prefix[prefix_length] = '\0';
            candidate = trim(prefix);
            if (isdigit((unsigned char)*candidate)) {
                while (isdigit((unsigned char)*candidate)) ++candidate;
                candidate = trim(candidate);
            }
            /* Colons after THEN belong to the same single-line IF.  They
               are split later while the conditional block is open. */
            if (starts_word(candidate, "if") &&
                find_word_case_insensitive(candidate, "then") != NULL) {
                ++cursor;
                continue;
            }
        }
        if ((!quoted && *cursor == ':') || *cursor == '\0') {
            char saved = *cursor;
            char segment[MAX_LINE];
            size_t length = (size_t)(cursor - start);
            char *candidate;
            if (length >= sizeof(segment) - 2U) length = sizeof(segment) - 2U;
            memcpy(segment, start, length);
            segment[length] = '\0';
            candidate = trim(segment);
            if (saved == ':' && valid_variable_name(candidate)) {
                segment[length++] = ':';
                segment[length] = '\0';
            }
            callback(compiler, segment);
            if (saved == '\0') break;
            start = cursor + 1;
        }
        ++cursor;
    }
}

static int translate_file(const char *input_path, const char *output_path)
{
    FILE *input = fopen(input_path, "r");
    FILE *output;
    char line[MAX_LINE];
    static Compiler compiler;
    memset(&compiler, 0, sizeof(compiler));
    if (input == NULL) {
        fprintf(stderr, "bbasicc: cannot open '%s': %s\n", input_path,
                strerror(errno));
        return 1;
    }
    compiler.input_name = input_path;
    {
        const char *base = input_path;
        const char *slash = strrchr(input_path, '/');
        const char *backslash = strrchr(input_path, '\\');
        char *extension;
        if (slash != NULL && slash + 1 > base) base = slash + 1;
        if (backslash != NULL && backslash + 1 > base) base = backslash + 1;
        copy_text(compiler.window_name, sizeof(compiler.window_name), base);
        extension = strrchr(compiler.window_name, '.');
        if (extension != NULL) *extension = '\0';
    }

    while (fgets(line, sizeof(line), input) != NULL) {
        ++compiler.line_number;
        for_each_statement(line, collect_variables, &compiler);
    }
    rewind(input);
    compiler.line_number = 0;

    output = fopen(output_path, "w");
    if (output == NULL) {
        fprintf(stderr, "bbasicc: cannot create '%s': %s\n", output_path,
                strerror(errno));
        fclose(input);
        return 1;
    }
    compiler.output = output;
    fputs("/* Generated by modern BasicBasic. */\n", output);
    fputs("#include \"bbasic_runtime.h\"\n", output);
    fputs("#include <math.h>\n#include <string.h>\n\n", output);
    fputs("int main(void)\n{\n", output);
    compiler.indentation = 1;
    if (compiler.uses_data && compiler.data_count == 0U) {
        emit(&compiler, "const char *bb_data[1] = {\"\"};");
    } else if (compiler.uses_data) {
        emit(&compiler, "const char *bb_data[%zu] = {", compiler.data_count);
        ++compiler.indentation;
        for (size_t index = 0U; index < compiler.data_count; ++index) {
            for (int indent = 0; indent < compiler.indentation; ++indent)
                fputs("    ", output);
            emit_c_string(output, compiler.data_items[index].text);
            fputs(index + 1U < compiler.data_count ? ",\n" : "\n", output);
        }
        --compiler.indentation;
        emit(&compiler, "};");
    }
    if (compiler.uses_data) {
        emit(&compiler, "const size_t bb_data_count = %zuU;", compiler.data_count);
        emit(&compiler, "size_t bb_data_cursor = 0U;");
    }
    for (size_t index = 0; index < compiler.variable_count; ++index) {
        Variable *variable = &compiler.variables[index];
        if (variable->is_array && variable->is_string)
            emit(&compiler, "BbStringArray %s = {0};", variable->c_name);
        else if (variable->is_array)
            emit(&compiler, "BbNumArray %s = {0};", variable->c_name);
        else if (variable->is_string)
            emit(&compiler, "char %s[2049] = \"\";", variable->c_name);
        else
            emit(&compiler, "double %s = 0.0;", variable->c_name);
    }
    if (compiler.total_for_loops > 0U) {
        emit(&compiler, "double bb_for_end[%zu] = {0};", compiler.total_for_loops);
        emit(&compiler, "double bb_for_step[%zu] = {0};", compiler.total_for_loops);
    }
    if (compiler.uses_gosub_stack) {
        emit(&compiler, "size_t bb_gosub_stack[256] = {0};");
        emit(&compiler, "size_t bb_gosub_sp = 0U;");
    }
    if (compiler.window_name[0] != '\0') {
        for (int indent = 0; indent < compiler.indentation; ++indent)
            fputs("    ", output);
        fputs("bb_window_name(", output);
        emit_c_string(output, compiler.window_name);
        fputs(");\n", output);
    }
    if (compiler.has_window_size)
        emit(&compiler, "bb_window_size_hint(%g, %g, %g, %g);",
             compiler.window_size[0], compiler.window_size[1],
             compiler.window_size[2], compiler.window_size[3]);
    if (compiler.variable_count > 0U) fputc('\n', output);

    while (fgets(line, sizeof(line), input) != NULL) {
        ++compiler.line_number;
        for_each_statement(line, compile_statement, &compiler);
    }
    while (compiler.block_count > 0U) {
        compiler_error(&compiler, "unclosed block at end of file");
        --compiler.block_count;
        if (compiler.indentation > 1) --compiler.indentation;
        emit(&compiler, "}");
    }
    emit(&compiler, "return 0;");
    fputs("}\n", output);
    if (compiler.uses_windows_ui)
        fputs("\n/* BBASIC_SUBSYSTEM: WINDOWS */\n", output);
    else
        fputs("\n/* BBASIC_SUBSYSTEM: CONSOLE */\n", output);
    if (compiler.icon_path[0] != '\0')
        fprintf(output, "/* BBASIC_ICON: %s */\n", compiler.icon_path);
    fclose(output);
    fclose(input);
    if (compiler.errors != 0) {
        (void)remove(output_path);
        fprintf(stderr, "bbasicc: translation failed with %d error(s)\n",
                compiler.errors);
        return 1;
    }
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s INPUT.BAS [-o OUTPUT.c]\n", program);
}

int main(int argc, char **argv)
{
    const char *input_path;
    const char *output_path = "out.c";
    if (argc != 2 && argc != 4) {
        usage(argv[0]);
        return 2;
    }
    input_path = argv[1];
    if (argc == 4) {
        if (strcmp(argv[2], "-o") != 0) {
            usage(argv[0]);
            return 2;
        }
        output_path = argv[3];
    }
    return translate_file(input_path, output_path);
}
