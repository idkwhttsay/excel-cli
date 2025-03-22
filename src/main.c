/*
 * Simple Spreadsheet Expression Parser
 *
 * This program implements a basic expression parsing system for a spreadsheet-like application.
 * It supports operations such as numerical expressions, cell references, and addition operations.
 * The program reads a CSV file containing expressions and parses them into an expression tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define SV_IMPLEMENTATION
#include "sv.h"

// An "unreachable" macro for reporting unreachable locations of code
#define UNREACHABLE(message)                         \
    do {                                             \
        fprintf(stderr, "%s:%d: UNREACHABLE: %s\n",  \
                __FILE__, __LINE__, message);        \
        exit(1);                                     \
    } while(0)

// Forward declaration of the Expr structure
typedef struct Expr Expr;
typedef size_t Expr_Index;

#define PRINT_OFFSET -20

// Enum representing different kinds of expressions
typedef enum {
    EXPR_KIND_NUMBER = 0, // Numeric constant
    EXPR_KIND_CELL,       // Cell reference
    EXPR_KIND_BOP,        // Binary operation
    EXPR_KIND_UOP,        // Unary operation
} Expr_Kind;

typedef enum {
    BOP_KIND_PLUS = 0,    // Plus BOP
    BOP_KIND_MINUS,       // Minus BOP
    BOP_KIND_MULT,        // Multiplication BOP
    BOP_KIND_DIV,         // Division BOP
    BOP_KIND_POW,         // Power BOP
    COUNT_BOP_KINDS,      // Number of binary operations
} Bop_Kind;

// Binary operation definitions
typedef struct {
    Bop_Kind kind;
    String_View token;
    size_t precedence;
} Bop_Def;

// Precedence of binary operations
typedef enum {
    BOP_PRECEDENCE0 = 0,
    BOP_PRECEDENCE1,
    COUNT_BOP_PRECEDENCE,
} Bop_Precedence;

// Table of binary operation definitions
static_assert(COUNT_BOP_KINDS == 5, 
    "The amount of binary operators has changed. Please adjust the definition table accrodingly.\n");
static const Bop_Def bop_defs[COUNT_BOP_KINDS] = 
{
    [BOP_KIND_PLUS] = {
        .kind = BOP_KIND_PLUS,
        .token = SV_STATIC("+"),
        .precedence = BOP_PRECEDENCE0,
    }, 
    [BOP_KIND_MINUS] = {
        .kind = BOP_KIND_MINUS,
        .token = SV_STATIC("-"),
        .precedence = BOP_PRECEDENCE0,
    },
    [BOP_KIND_MULT] = {
        .kind = BOP_KIND_MULT,
        .token = SV_STATIC("*"),
        .precedence = BOP_PRECEDENCE1,
    },
    [BOP_KIND_DIV] = {
        .kind = BOP_KIND_DIV,
        .token = SV_STATIC("/"),
        .precedence = BOP_PRECEDENCE1,
    },
    [BOP_KIND_POW] = {
        .kind = BOP_KIND_POW,
        .token = SV_STATIC("^"),
        .precedence = BOP_PRECEDENCE1,
    },
};

// Determine binary operation's definition by token
const Bop_Def *bop_def_by_token(String_View token)
{
    for(Bop_Kind kind = 0; kind < COUNT_BOP_KINDS; ++kind) {
        if(sv_eq(bop_defs[kind].token, token)) {
            return &bop_defs[kind];
        }
    }

    return NULL;
}

// Binary operation getter
Bop_Def get_bop_def(Bop_Kind kind) 
{
    assert(kind >= 0);
    assert(kind < COUNT_BOP_KINDS);
    return bop_defs[kind];
}

// Structure for representing a binary expression
typedef struct {
    Bop_Kind kind;  // Kind of binary expression
    Expr_Index lhs; // Left-hand side expression
    Expr_Index rhs; // Right-hand side expression
} Expr_Bop;

// Unary operations' kinds
typedef enum {
    UOP_KIND_MINUS,
} Uop_Kind;

// Unary expression structure
typedef struct {
    Uop_Kind kind;
    Expr_Index param;
} Expr_Uop;

// Structure representing a cell index in the spreadsheet
typedef struct {
    size_t row;
    size_t col;
} Cell_Index;

// Union storing different types of expressions
typedef union {
    double number;
    Cell_Index cell;
    Expr_Bop bop;
    Expr_Uop uop;
} Expr_As;

// Structure representing an expression
struct Expr {
    Expr_Kind kind;  // Type of the expression
    Expr_As as;      // Expression data
    const char *file_path; // File path for error tracking
    size_t file_row; // Row in the source file
    size_t file_col; // Column in the source file
};

// Buffer to store and manage expressions dynamically
typedef struct {
    size_t count;    // Number of expressions stored
    size_t capacity; // Total capacity of the buffer
    Expr *items;     // Array of expressions
} Expr_Buffer;

/**
 * Allocates a new expression in the buffer.
 * Dynamically resizes the buffer if necessary.
 * 
 * @param eb Pointer to the expression buffer.
 * @return The index of the newly allocated expression.
 */
Expr_Index expr_buffer_alloc(Expr_Buffer *eb) 
{
    if(eb->count >= eb->capacity) {
        if(eb->capacity == 0) {
            assert(eb->items == NULL);
            eb->capacity = 128;
        } else {
            eb->capacity *= 2;
        }

        eb->items = realloc(eb->items, sizeof(Expr) * eb->capacity);
    }   

    memset(&eb->items[eb->count], 0, sizeof(Expr));
    return eb->count++;
}

/**
 * Retrieves an expression from the buffer at a given index.
 *
 * @param eb Pointer to the expression buffer.
 * @param index Index of the expression to retrieve.
 * @return Pointer to the requested expression.
 */
Expr *expr_buffer_at(Expr_Buffer *eb, Expr_Index index) 
{
    assert(index < eb->count);
    return &eb->items[index];
}

/**
 * Dumps the expression buffer to a file.
 *
 * @param stream Output file stream.
 * @param eb Pointer to the expression buffer.
 * @param root Root index of the expression tree.
 */
void expr_buffer_dump(FILE *stream, const Expr_Buffer *eb, Expr_Index root) 
{
    fwrite(&root, sizeof(root), 1, stream);
    fwrite(&eb->count, sizeof(eb->count), 1, stream); 
    fwrite(eb->items, sizeof(Expr), eb->count, stream);
}

// Enums defining spreadsheet cell types and evaluation status
typedef enum {
    DIR_LEFT = 0, // Direction left
    DIR_RIGHT,    // Direction right
    DIR_UP,       // Direction up
    DIR_DOWN,     // Direction down
} Dir;

typedef enum {
    CELL_KIND_TEXT = 0,
    CELL_KIND_NUMBER,
    CELL_KIND_EXPR,
    CELL_KIND_CLONE,
} Cell_Kind;

/**
 * Prints the kind of a cell as a string.
 *
 * @param kind The cell kind enum.
 * @return A string representation of the cell kind.
 */
const char *cell_kind_as_cstr(Cell_Kind kind) 
{
    switch (kind)
    {
        case CELL_KIND_TEXT: 
            return "TEXT";
        case CELL_KIND_NUMBER: 
            return "NUMBER";
        case CELL_KIND_EXPR: 
            return "EXPR";
        case CELL_KIND_CLONE:
            return "CLONE";
        default:
            UNREACHABLE("Unknown cell kind");
    }
}

typedef enum {
    UNEVALUATED = 0,
    INPROGRESS,
    EVALUATED,
} Eval_Status;

typedef struct {
    Expr_Index index;
    double value;
} Cell_Expr;

typedef union {
    String_View text;
    double number;
    Cell_Expr expr;
    Dir clone;
} Cell_As;

// Structure representing a spreadsheet cell
typedef struct {
    Cell_Kind kind;
    Cell_As as;
    Eval_Status status;

    size_t file_row;
    size_t file_col;
} Cell;

// Table structure representing the spreadsheet
typedef struct {
    Cell *cells;
    size_t rows;
    size_t cols;
    const char *file_path;
} Table;

/**
 * Checks if a character is valid for a name.
 * Valid characters are alphanumeric or underscore.
 * 
 * @param c Character to check.
 * @return true if the character is valid for a name, false otherwise.
 */
bool is_name(char c) 
{
    return isalnum(c) || c == '_';
}

/**
 * Calcualtes the number in the power of n
 * 
 * @param n the power
 * @param num number which will be exponetiated 
 * @return number in the power of n
 */
double bin_pow(double num, int n) 
{
    if(n == 0) return 1.0;

    if(n % 2 == 0) {
        double tmp = bin_pow(num, n / 2);
        return tmp*tmp;
    } else {
        return num * bin_pow(num, n - 1);
    }
}

typedef struct {
    String_View text;
    const char *file_path;
    size_t file_row;
    size_t file_col;
} Token;

typedef struct {
    String_View source;
    const char* file_path;
    size_t file_row;
    const char *line_start;
} Lexer;

/**
 * Gets the current column position within the file for the lexer.
 * Calculated as the offset from the start of the current line.
 * 
 * @param lexer Pointer to the lexer structure.
 * @return The current column position (1-based indexing).
 */
size_t lexer_file_col(const Lexer *lexer) {
    return lexer->source.data - lexer->line_start + 1;
}

/**
 * Prints the current location of the lexer in the format "file:row:col: ".
 * Used for error reporting.
 * 
 * @param lexer Pointer to the lexer structure.
 * @param stream Output file stream.
 */
void lexer_print_loc(const Lexer *lexer, FILE *stream) {
    fprintf(stream, "%s:%zu:%zu: ", lexer->file_path, lexer->file_row, lexer_file_col(lexer));
}

/**
 * Copies the next token from the lexer's source.
 * Handles operators, identifiers, and reports errors for unknown tokens.
 * 
 * @param lexer Pointer to the lexer structure.
 * @return The copied token.
 */
Token lexer_peek_token(Lexer *lexer)
{
    lexer->source = sv_trim(lexer->source);

    Token token;
    memset(&token, 0, sizeof(token));
    token.file_path = lexer->file_path;
    token.file_row = lexer->file_row;
    token.file_col = lexer_file_col(lexer);

    if (lexer->source.count == 0) {
        return token;
    }

    if (*lexer->source.data == '+' || 
        *lexer->source.data == '-' || 
        *lexer->source.data == '*' || 
        *lexer->source.data == '/' ||
        *lexer->source.data == '(' ||
        *lexer->source.data == ')' ||
        *lexer->source.data == '^'
    ) {
        token.text = (String_View) {
            .count = 1,
            .data = lexer->source.data
        };
        return token;
    }

    if (is_name(*lexer->source.data)) {
        token.text = sv_take_left_while(lexer->source, is_name);
        return token;
    }

    lexer_print_loc(lexer, stderr);
    fprintf(stderr, "ERROR: unknown token starts with `%c`\n", *lexer->source.data);
    exit(1);
}

/**
 * Extracts the next token from the lexer's source.
 * Handles operators, identifiers, and reports errors for unknown tokens.
 * 
 * @param lexer Pointer to the lexer structure.
 * @return The extracted token.
 */
Token lexer_next_token(Lexer *lexer)
{
    Token token = lexer_peek_token(lexer);
    sv_chop_left(&lexer->source, token.text.count);
    return token;
}

void lexer_expect_no_tokens(Lexer* lexer)
{
    Token token = lexer_next_token(lexer);
    if(token.text.data != NULL) {
        fprintf(stderr, "%s:%zu:%zu: ERROR: unexpected token `"SV_Fmt"`\n", 
            token.file_path, 
            token.file_row, 
            token.file_col, 
            SV_Arg(token.text));
        exit(1);
    }
}
typedef struct {
    size_t capacity;
    char *cstr;
} Tmp_Cstr;

/**
 * Fills a temporary C-string with the provided data.
 * Ensures the buffer is large enough and null-terminates the string.
 * 
 * @param tc Pointer to the temporary C-string structure.
 * @param data Pointer to the data to copy.
 * @param data_size Size of the data to copy.
 * @return Pointer to the filled C-string.
 */
char *tmp_cstr_fill(Tmp_Cstr *tc, const char *data, size_t data_size) 
{
    if(data_size + 1 >= tc->capacity) {
        tc->capacity = data_size + 1;
        tc->cstr = realloc(tc->cstr, tc->capacity);
    }

    memcpy(tc->cstr, data, data_size);
    tc->cstr[data_size] = '\0';
    return tc->cstr;
}

/**
 * Converts a String_View to a double.
 * Creates a temporary null-terminated string and uses strtod for conversion.
 * 
 * @param sv The String_View to convert.
 * @param tc Pointer to a temporary C-string structure.
 * @param out Pointer to store the resulting double value.
 * @return true if conversion succeeded, false otherwise.
 */
bool sv_strtod(String_View sv, Tmp_Cstr *tc, double *out)
{
    char *ptr = tmp_cstr_fill(tc, sv.data, sv.count);
    char *endptr = NULL;
    double result = strtod(ptr, &endptr);
    if (out) *out = result;
    return endptr != ptr && *endptr == '\0';
}

/**
 * Converts a String_View to a long integer.
 * Creates a temporary null-terminated string and uses strtol for conversion.
 * 
 * @param sv The String_View to convert.
 * @param tc Pointer to a temporary C-string structure.
 * @param out Pointer to store the resulting long integer value.
 * @return true if conversion succeeded, false otherwise.
 */
bool sv_strtol(String_View sv, Tmp_Cstr *tc, long int *out)
{
    char *ptr = tmp_cstr_fill(tc, sv.data, sv.count);
    char *endptr = NULL;
    long int result = strtol(ptr, &endptr, 10);
    if (out) *out = result;
    return endptr != ptr && *endptr == '\0';
}

Expr_Index parse_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb);

/**
 * Parses a primary expression (number or cell reference).
 * Handles the most basic elements of expressions.
 * 
 * @param lexer Pointer to the lexer structure.
 * @param tc Pointer to a temporary C-string structure.
 * @param eb Pointer to the expression buffer.
 * @return Index of the parsed primary expression.
 */
Expr_Index parse_primary_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb) 
{
    Token token = lexer_next_token(lexer);

    if (token.text.count == 0) {
        lexer_print_loc(lexer, stderr);
        fprintf(stderr, "ERROR: expected primary expression token, but got end of input\n");
        exit(1);
    }

    double number = 0.0;

    if (sv_strtod(token.text, tc, &number)) {
        Expr_Index expr_index = expr_buffer_alloc(eb);
        Expr *expr = expr_buffer_at(eb, expr_index);
        expr->kind = EXPR_KIND_NUMBER;
        expr->as.number = number;
        expr->file_path = token.file_path;
        expr->file_row = token.file_row;
        expr->file_col = token.file_col;
        return expr_index;
    } else if(sv_eq(token.text, SV("("))) {
        Expr_Index expr_index = parse_expr(lexer, tc, eb);
        token = lexer_next_token(lexer);
        if(!sv_eq(token.text, SV(")"))) {
            fprintf(stderr, "%s:%zu:%zu: ERROR: expected token ')' but got '"SV_Fmt"'\n", 
                token.file_path, token.file_row, token.file_col, SV_Arg(token.text));
            exit(1);
        }

        return expr_index;
    } else if (sv_eq(token.text, SV("-"))){
        Expr_Index param_index = parse_expr(lexer, tc, eb);
        Expr_Index expr_index = expr_buffer_alloc(eb);
        {
            Expr *expr = expr_buffer_at(eb, expr_index);
            expr->kind = EXPR_KIND_UOP;
            expr->as.uop.kind = UOP_KIND_MINUS;
            expr->as.uop.param = param_index;
            expr->file_path = token.file_path;
            expr->file_row = token.file_row;
            expr->file_col = token.file_col;
        }
        return expr_index;
    } else {
        Expr_Index expr_index = expr_buffer_alloc(eb);
        Expr *expr = expr_buffer_at(eb, expr_index);
        expr->file_path = token.file_path;
        expr->file_row = token.file_row;
        expr->file_col = token.file_col;
        expr->kind = EXPR_KIND_CELL;

        if (!isupper(*token.text.data)) {
            lexer_print_loc(lexer, stderr);
            fprintf(stderr, "ERROR: cell reference must start with capital letter\n");
            exit(1);
        }

        expr->as.cell.col = *token.text.data - 'A';

        sv_chop_left(&token.text, 1);

        long int row = 0;
        if (!sv_strtol(token.text, tc, &row)) {
            lexer_print_loc(lexer, stderr);
            fprintf(stderr, "ERROR: cell reference must have an integer as the row number\n" );
            exit(1);
        }

        expr->as.cell.row = (size_t) row;
        return expr_index;
    }
}


/**
 * Parses an binary operation expression.
 * Handles expressions with addition operators, recursively parsing the right-hand side.
 * 
 * @param lexer Pointer to the lexer structure.
 * @param tc Pointer to a temporary C-string structure.
 * @param eb Pointer to the expression buffer.
 * @param precedence Current precedence of the expression
 * @return Index of the parsed addition expression or primary expression.
 */
Expr_Index parse_bop_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb, size_t precedence)
{
    if (precedence >= COUNT_BOP_PRECEDENCE) {
        return parse_primary_expr(lexer, tc, eb);
    }

    Expr_Index lhs_index = parse_bop_expr(lexer, tc, eb, precedence + 1);

    Token token = lexer_peek_token(lexer);
    const Bop_Def *def = bop_def_by_token(token.text);

    if (def != NULL && def->precedence == precedence) {
        token = lexer_next_token(lexer);
        Expr_Index rhs_index = parse_bop_expr(lexer, tc, eb, precedence);

        Expr_Index expr_index = expr_buffer_alloc(eb);
        {
            Expr *expr = expr_buffer_at(eb, expr_index);
            expr->kind = EXPR_KIND_BOP;
            expr->as.bop.kind = def->kind;
            expr->as.bop.lhs = lhs_index;
            expr->as.bop.rhs = rhs_index;
            expr->file_path = token.file_path;
            expr->file_row = token.file_row;
            expr->file_col = token.file_col;
        }

        return expr_index;
    }

    return lhs_index;
}

/**
 * Retrieves a cell from the table at a given index.
 * Verifies the index is within bounds before returning.
 *
 * @param table Pointer to the table structure.
 * @param index Index of the cell to retrieve.
 * @return Pointer to the requested cell.
 */
Cell *table_cell_at(Table *table, Cell_Index index) 
{
    assert(index.row < table->rows);
    assert(index.col < table->cols);

    return &table->cells[index.row * table->cols + index.col];
}

/**
 * Dumps the table contents to an output stream.
 * Prints each cell's file location and kind.
 *
 * @param stream Output file stream.
 * @param table Pointer to the table structure.
 */
void dump_table(FILE *stream, Table *table) 
{
    for(size_t row = 0; row < table->rows; ++row) {
        for(size_t col = 0; col < table->cols; ++col) {
            Cell_Index cell_index = {
                .col = col,
                .row = row,
            };

            Cell *cell = table_cell_at(table, cell_index);
            fprintf(stream, "%s:%zu:%zu: %s\n", table->file_path, cell->file_row, cell->file_col, cell_kind_as_cstr(cell->kind));
        }
    }
}

/**
 * Recursively prints an expression tree.
 * Output is indented based on the tree depth to show structure.
 *
 * @param stream Output file stream.
 * @param eb Pointer to the expression buffer.
 * @param expr_index Index of the expression to dump.
 * @param level Current nesting level (for indentation).
 */
void dump_expr(FILE *stream, Expr_Buffer *eb, Expr_Index expr_index, int level) 
{
    fprintf(stream, "%*s", level * 2, "");

    Expr *expr = expr_buffer_at(eb, expr_index);
    
    switch(expr->kind) {
        case EXPR_KIND_NUMBER:
            fprintf(stream, "NUMBER: %lf\n", expr->as.number);
            break;
        case EXPR_KIND_CELL:
            fprintf(stream, "CELL(%zu, %zu)\n", expr->as.cell.row, expr->as.cell.col);
            break;
        case EXPR_KIND_UOP:
            switch(expr->as.uop.kind) {
                case UOP_KIND_MINUS:
                    fprintf(stream, "UOP(MINUS): \n");
                    break;
                default:
                    UNREACHABLE("Unknown unary operator kind");
            }

            dump_expr(stream, eb, expr->as.uop.param, level + 1);
            break;
        case EXPR_KIND_BOP:
            switch(expr->as.bop.kind) {
                case BOP_KIND_PLUS:
                    fprintf(stream, "BOP(PLUS): \n");
                    break;
                case BOP_KIND_MINUS:
                    fprintf(stream, "BOP(MINUS): \n");
                    break;
                case BOP_KIND_MULT:
                    fprintf(stream, "BOP(MULT): \n");
                    break;
                case BOP_KIND_DIV:
                    fprintf(stream, "BOP(DIV): \n");
                    break;
                case BOP_KIND_POW:
                    fprintf(stream, "BOP(POW): \n");
                    break;
                case COUNT_BOP_KINDS:
                default: {
                    UNREACHABLE("Unknown binary operator kind");
                }
            }

            dump_expr(stream, eb, expr->as.bop.lhs, level + 1);
            dump_expr(stream, eb, expr->as.bop.rhs, level + 1);
            break;
        default: {
            UNREACHABLE("Unknown expression kind");
        }
    }
}

/**
 * Parses an expression from the given lexer.
 * This is the entry point for expression parsing.
 *
 * @param lexer Pointer to the lexer structure.
 * @param tc Pointer to a temporary C-string structure.
 * @param eb Pointer to the expression buffer.
 * @return Index of the parsed expression.
 */
Expr_Index parse_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb) 
{
    return parse_bop_expr(lexer, tc, eb, BOP_PRECEDENCE0);
}

/**
 * Prints usage information for the program.
 * Displays the correct command-line syntax.
 *
 * @param stream Output file stream.
 */
void print_usage(FILE *stream) 
{
    fprintf(stream, "Usage: ./excel-cli <input.csv> <output.csv>\n");
}

/**
 * Reads the contents of a CSV file into memory.
 * Allocates memory for the file contents and reads the entire file.
 *
 * @param file_path Path to the CSV file.
 * @param size Pointer to store the size of the file.
 * @return Pointer to the allocated buffer containing the file contents, or NULL on error.
 */
char *read_csv(const char *file_path, size_t *size) 
{

    char *buffer = NULL;
    FILE *f = fopen(file_path, "rb");
    if(f == NULL) {
        goto error;
    }

    if(fseek(f, 0, SEEK_END) < 0) goto error;

    long m = ftell(f); // size of file
    if(m < 0) goto error;
    
    buffer = malloc(sizeof(char) * m);
    if(buffer == NULL) goto error;

    if(fseek(f, 0, SEEK_SET) < 0) goto error;
    
    size_t n = fread(buffer, 1, m, f);
    assert(n == (size_t) m);

    if(ferror(f)) goto error;

    if(size) {
        *size = n;
    }

    fclose(f);
    return buffer;

error:
    if(f) fclose(f);
    if(buffer) free(buffer);

    return NULL;
}

/**
 * Parses table content from a String_View into the table structure.
 * Processes each cell in the table and sets up appropriate expressions and references.
 *
 * @param table Pointer to the table structure.
 * @param eb Pointer to the expression buffer.
 * @param tc Pointer to a temporary C-string structure.
 * @param content String_View containing the CSV content.
 */
void parse_table_from_content(Table *table, Expr_Buffer *eb, Tmp_Cstr *tc, String_View content)
{
    for (size_t row = 0; row < table->rows; ++row) {
        String_View line = sv_chop_by_delim(&content, '\n');
        const char *const line_start = line.data;
        for (size_t col = 0; col < table->cols; ++col) {
            String_View cell_value = sv_trim(sv_chop_by_delim(&line, '|'));
            Cell_Index cell_index = {
                .col = col,
                .row = row,
            };

            Cell *cell = table_cell_at(table, cell_index);
            cell->file_row = row + 1;
            cell->file_col = cell_value.data - line_start + 1;


            if (sv_starts_with(cell_value, SV("="))) {
                sv_chop_left(&cell_value, 1);
                cell->kind = CELL_KIND_EXPR;
                Lexer lexer = {
                    .file_path = table->file_path,
                    .file_row = cell->file_row,
                    .line_start = line_start,
                    .source = cell_value,
                };
                cell->as.expr.index = parse_expr(&lexer, tc, eb);
                lexer_expect_no_tokens(&lexer);
            } else if(sv_starts_with(cell_value, SV(":"))) {
                sv_chop_left(&cell_value, 1);
                cell->kind = CELL_KIND_CLONE;
                if(sv_eq(cell_value, SV("<"))) {
                    cell->as.clone = DIR_LEFT;
                } else if(sv_eq(cell_value, SV(">"))) {
                    cell->as.clone = DIR_RIGHT;
                } else if(sv_eq(cell_value, SV("^"))) {
                    cell->as.clone = DIR_UP;
                } else if(sv_eq(cell_value, SV("v"))) {
                    cell->as.clone = DIR_DOWN;
                } else {
                    fprintf(stderr, "%s:%zu:%zu: ERROR: "SV_Fmt" is not a correct direction to clone a cell from \n", table->file_path, cell->file_row, cell->file_col, SV_Arg(cell_value));
                    exit(1);
                }
            } else {
                if (sv_strtod(cell_value,tc, &cell->as.number)) {
                    cell->kind = CELL_KIND_NUMBER;
                } else {
                    cell->kind = CELL_KIND_TEXT;
                    cell->as.text = cell_value;
                }
            }
        }
    }
}

/**
 * Estimates the size of a table from its content.
 * Counts the number of rows and maximum number of columns.
 *
 * @param content String_View containing the CSV content.
 * @param out_rows Pointer to store the number of rows.
 * @param out_cols Pointer to store the number of columns.
 */
void estimate_table_size(String_View content, size_t *out_rows, size_t *out_cols) 
{
    size_t rows = 0;
    size_t cols = 0;
    for(; content.count > 0; ++rows) {
        String_View line = sv_chop_by_delim(&content, '\n');

        size_t col = 0;
        for(; line.count > 0; ++col) {
            sv_trim(sv_chop_by_delim(&line, '|'));
        }

        if(cols < col) {
            cols = col;
        }
    }

    if(out_rows) *out_rows = rows;
    if(out_cols) *out_cols = cols;
}

void table_eval_cell(Table *table, Expr_Buffer *eb, Cell_Index cell_index);

/**
 * Evaluates an expression in the context of a table.
 * Handles numeric values, cell references, and addition operations.
 *
 * @param table Pointer to the table structure.
 * @param eb Pointer to the expression buffer.
 * @param expr_index Index of the expression to evaluate.
 * @return The numeric result of evaluating the expression.
 */
double table_eval_expr(Table *table, Expr_Buffer *eb, Expr_Index expr_index) 
{
    Expr *expr = expr_buffer_at(eb, expr_index);

    switch(expr->kind) {
        case EXPR_KIND_NUMBER:
            return expr->as.number;
        case EXPR_KIND_CELL: {
            table_eval_cell(table, eb, expr->as.cell);

            Cell *target_cell = table_cell_at(table, expr->as.cell);
            switch(target_cell->kind) {
                case CELL_KIND_NUMBER: 
                    return target_cell->as.number;
                case CELL_KIND_TEXT: {
                    fprintf(stderr, "%s:%zu:%zu ERROR: text cells may not participate in math expressions\n", 
                        expr->file_path, expr->file_row, expr->file_col);
                    fprintf(stderr, "%s:%zu:%zu: NOTE: the text cell is located here\n", 
                        table->file_path, target_cell->file_row, target_cell->file_col);
                    exit(1);
                } break;
                case CELL_KIND_EXPR: {
                    return target_cell->as.expr.value;
                } break;
                case CELL_KIND_CLONE: {
                    UNREACHABLE("Clone cell should be evaluated to the expression cell at this point");
                } break; 
            }
        } break;
        case EXPR_KIND_BOP: {
            double lhs = table_eval_expr(table, eb, expr->as.bop.lhs);
            double rhs = table_eval_expr(table, eb, expr->as.bop.rhs);
            
            switch (expr->as.bop.kind) {
                case BOP_KIND_PLUS: return lhs + rhs;
                case BOP_KIND_MINUS: return lhs - rhs;
                case BOP_KIND_MULT: return lhs * rhs;
                case BOP_KIND_DIV: return lhs / rhs;
                case BOP_KIND_POW: return bin_pow(lhs, (int) rhs);
                case COUNT_BOP_KINDS:
                default: {
                    UNREACHABLE("Unknown binary operator kind");
                }
            }
        } break;
        case EXPR_KIND_UOP: {
            double param = table_eval_expr(table, eb, expr->as.uop.param);
            switch(expr->as.uop.kind) {
                case UOP_KIND_MINUS:
                    return -param;
                default:
                UNREACHABLE("Unknown unary operator kind");
            }
        }
    }

    return 0;
}

/**
 * Returns the opposite direction.
 * LEFT <-> RIGHT, UP <-> DOWN
 *
 * @param dir The direction to reverse.
 * @return The opposite direction.
 */
Dir opposite_dir(Dir dir) 
{
    switch(dir) {
        case DIR_LEFT: return DIR_RIGHT;
        case DIR_RIGHT: return DIR_LEFT;
        case DIR_UP: return DIR_DOWN;
        case DIR_DOWN: return DIR_UP;
        default: {
            UNREACHABLE("Unknown direction");
        }
    }
}

/**
 * Returns the cell index of a neighboring cell in the specified direction.
 *
 * @param index The starting cell index.
 * @param dir The direction to move.
 * @return The new cell index.
 */
Cell_Index nbor_in_dir(Cell_Index index, Dir dir) 
{
    switch(dir) {
        case DIR_LEFT:
            index.col --;
            break;
        case DIR_RIGHT:
            index.col ++;
            break;
        case DIR_UP:
            index.row --;
            break;
        case DIR_DOWN:
            index.row ++;
            break;
        default: {
            UNREACHABLE("Unknown direction");
        }
    }

    return index;
}

/**
 * Moves an expression in a specified direction.
 * Creates a new expression with adjusted cell references.
 *
 * @param table Pointer to the table structure.
 * @param cell_index Index of the current cell.
 * @param eb Pointer to the expression buffer.
 * @param root Index of the root expression to move.
 * @param dir Direction to move the expression.
 * @return Index of the new, moved expression.
 */
Expr_Index move_expr_in_dir(Table *table, Cell_Index cell_index, Expr_Buffer *eb, Expr_Index root, Dir dir) 
{
    Cell *cell = table_cell_at(table, cell_index);

    switch (expr_buffer_at(eb, root)->kind) {
        case EXPR_KIND_NUMBER:
            return root;
        case EXPR_KIND_CELL: {

            Expr_Index new_index = expr_buffer_alloc(eb); 
            {
                Expr *new_expr = expr_buffer_at(eb, new_index);
                new_expr->kind = EXPR_KIND_CELL;
                new_expr->as.cell = nbor_in_dir(expr_buffer_at(eb, root)->as.cell, dir);

                new_expr->file_path = table->file_path;
                new_expr->file_row = cell->file_row;
                new_expr->file_col = cell->file_col;
            }
            return new_index;
        } break;
        case EXPR_KIND_BOP: {
            Expr_Bop bop = {0};

            {
                Expr *root_expr = expr_buffer_at(eb, root);
                bop = root_expr->as.bop;
            }


            bop.lhs = move_expr_in_dir(table, cell_index, eb, bop.lhs, dir);
            bop.rhs = move_expr_in_dir(table, cell_index, eb, bop.rhs, dir);

            Expr_Index new_index = expr_buffer_alloc(eb);
            
            {
                Expr *new_expr = expr_buffer_at(eb, new_index);
                new_expr->kind = EXPR_KIND_BOP;
                new_expr->as.bop = bop;
                new_expr->file_path = table->file_path;
                new_expr->file_col = cell->file_col;
                new_expr->file_row = cell->file_row;
            }
        
            return new_index;
        } break;
        case EXPR_KIND_UOP: {
            Expr_Uop uop = {0};
            {
                Expr *root_expr = expr_buffer_at(eb, root);
                uop = root_expr->as.uop;
            }

            uop.param = move_expr_in_dir(table, cell_index, eb, uop.param, dir);
            Expr_Index new_index = expr_buffer_alloc(eb);
            {
                Expr *new_expr = expr_buffer_at(eb, new_index);
                new_expr->kind = EXPR_KIND_UOP;
                new_expr->as.uop = uop;
                new_expr->file_path = table->file_path;
                new_expr->file_col = cell->file_col;
                new_expr->file_row = cell->file_row;
            }

            return new_index;
        } break;
        default: {
            UNREACHABLE("Unknown expression kind");
        }
    }
}

/**
 * Evaluates a cell in the table.
 * Handles different cell types and their evaluation rules.
 * Detects circular dependencies.
 *
 * @param table Pointer to the table structure.
 * @param eb Pointer to the expression buffer.
 * @param cell_index Index of the cell to evaluate.
 */
void table_eval_cell(Table *table, Expr_Buffer *eb, Cell_Index cell_index) 
{
    Cell *cell = table_cell_at(table, cell_index);

    switch(cell->kind) {
        case CELL_KIND_TEXT:
        case CELL_KIND_NUMBER:
            cell->status = EVALUATED;
            break;
        case CELL_KIND_EXPR: { 
            if(cell->status == INPROGRESS) {
                fprintf(stderr, "%s:%zu:%zu: ERROR: circular dependency is detected!\n", table->file_path, cell->file_row, cell->file_col);
                exit(1);
            }

            if(cell->status == UNEVALUATED) {
                cell->status = INPROGRESS;
                cell->as.expr.value = table_eval_expr(table, eb, cell->as.expr.index);
                cell->status = EVALUATED;
            }
        } break;

        case CELL_KIND_CLONE: {
            if(cell->status == INPROGRESS) {
                fprintf(stderr, "%s:%zu:%zu: ERROR: circular dependency is detected!\n", table->file_path, cell->file_row, cell->file_col);
                exit(1);
            }

            if(cell->status == UNEVALUATED) {
                cell->status = INPROGRESS;
                
                Dir dir = cell->as.clone;
                Cell_Index nbor_index = nbor_in_dir(cell_index, dir);
                
                if(nbor_index.row >= table->rows || nbor_index.col >= table->cols) {
                    fprintf(stderr, "%s:%zu:%zu: ERROR: trying to clone a cell outside of the table\n", table->file_path, cell->file_row, cell->file_col);
                    exit(1);
                }
                
                table_eval_cell(table, eb, nbor_index);
                
                Cell *nbor = table_cell_at(table, nbor_index);
                cell->kind = nbor->kind;
                cell->as = nbor->as;

                if(cell->kind == CELL_KIND_EXPR) {
                    cell->as.expr.index = move_expr_in_dir(table, cell_index, eb, cell->as.expr.index, opposite_dir(dir));
                    cell->as.expr.value = table_eval_expr(table, eb, cell->as.expr.index);
                }

                cell->status = EVALUATED;
            } else {
               UNREACHABLE("Evaluated cloens are an absurd. When a clone cell is evaluated it becomes its neighbor kind");
                exit(1);
            }
        } break;
    }
}

/**
 * Takes the first n characters from a string and returns them as a new null-terminated string.
 * Helper function for displaying text values.
 *
 * @param input The input string.
 * @param count Number of characters to take.
 * @return A newly allocated string containing the first n characters of input, or NULL on error.
 */
const char* sv_take_first_n(const char* input, size_t count) {
    if (input == NULL) {
        return NULL;
    }
    
    size_t input_length = strlen(input);
    
    if (count > input_length) {
        count = input_length;
    }
    
    char* result = (char*)malloc((count + 1) * sizeof(char));
    if (result == NULL) {
        return NULL; // Memory allocation failed
    }
    
    for (size_t i = 0; i < count; i++) {
        result[i] = input[i];
    }
    
    result[count] = '\0';
    
    return result;
}

/**
 * Main function for the spreadsheet program.
 * Parses command-line arguments, reads the input file, processes the spreadsheet,
 * evaluates all cells, and outputs the results.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return 0 on success, non-zero on error.
 */
int main(int argc, char **argv) 
{
    if(argc < 3) {
        print_usage(stderr);
        fprintf(stderr, "ERROR: input or output files are not provided\n");
        exit(1);
    }

    const char *input_file_path = argv[1];
    const char *output_file_path = argv[2];

    size_t content_size = 0;
    char *content = read_csv(input_file_path, &content_size);

    if(content == NULL) {
        fprintf(stderr, "ERROR: could not read file %s: %s\n", input_file_path, strerror(errno));
        exit(1);
    }

    FILE *out_file = fopen(output_file_path, "w");
    if (out_file == NULL) {
        fprintf(stderr, "ERROR: could not write to file %s: %s\n", output_file_path, strerror(errno));
        exit(1);
    }

    String_View input = {
        .count = content_size,
        .data = content,
    };

    Expr_Buffer eb = {0};
    Table table = {
        .file_path = input_file_path,
    };
    Tmp_Cstr tc = {0};

    estimate_table_size(input, &table.rows, &table.cols);
    table.cells = malloc(sizeof(*table.cells) * table.rows * table.cols);
    memset(table.cells, 0, sizeof(*table.cells) * table.rows * table.cols);
    parse_table_from_content(&table, &eb, &tc, input);

    // Evaluate each cell
    for(size_t row = 0; row < table.rows; ++row) {
        for(size_t col = 0; col < table.cols; ++col) {
            Cell_Index cell_index = {
                .col = col,
                .row = row,
            };

            table_eval_cell(&table, &eb, cell_index);
        }
    }

    // Estimate column widths
    size_t *col_widths = malloc(sizeof(size_t) * table.cols);
    {
        for (size_t col = 0; col < table.cols; ++col) {
            col_widths[col] = 0;
            for (size_t row = 0; row < table.rows; ++row) {
                Cell_Index cell_index = {
                    .row = row,
                    .col = col,
                };

                Cell *cell = table_cell_at(&table, cell_index);
                size_t width = 0;
                switch (cell->kind) {
                case CELL_KIND_TEXT:
                    width = cell->as.text.count;
                    break;
                case CELL_KIND_NUMBER: {
                    int n = snprintf(NULL, 0, "%lf", cell->as.number);
                    assert(n >= 0);
                    width = (size_t) n;
                } break;
                case CELL_KIND_EXPR: {
                    int n = snprintf(NULL, 0, "%lf", cell->as.expr.value);
                    assert(n >= 0);
                    width = (size_t) n;
                } break;
                case CELL_KIND_CLONE:
                    UNREACHABLE("Cell should never be a clone after the evaluation");
                    break;
                default:
                    UNREACHABLE("");
                    break;
                }

                if (col_widths[col] < width) {
                    col_widths[col] = width;
                }
            }
        }
    }

    // Render the table
    for(size_t row = 0; row < table.rows; ++row) {
        for(size_t col = 0; col < table.cols; ++col) {
            Cell_Index cell_index = {
                .col = col,
                .row = row,
            };

            Cell *cell = table_cell_at(&table, cell_index);
            int printn = 0;

            switch(cell->kind) {
                case CELL_KIND_TEXT: 
                    printn = fprintf(out_file, SV_Fmt, SV_Arg(cell->as.text));
                    break;
                case CELL_KIND_NUMBER:
                    printn = fprintf(out_file, "%lf", cell->as.number);
                    break;
                case CELL_KIND_EXPR:
                    printn = fprintf(out_file, "%lf", cell->as.expr.value);
                    break;
                case CELL_KIND_CLONE:
                    UNREACHABLE("Cell should never be a clone after evalution");
                    break;
                default:
                    UNREACHABLE("Unknown cell kind");
                    break;
            }

            assert(0 <= printn);
            assert((size_t) printn <= col_widths[col]);
            fprintf(out_file, "%*s", (int) (col_widths[col] - printn), "");

            if(col < table.cols - 1) fprintf(out_file, " | ");
        }

        fprintf(out_file, "\n");
    }

    free(col_widths);
    free(content);
    free(table.cells);
    free(eb.items);
    free(tc.cstr);

    return 0;
}
