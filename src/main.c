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

// Forward declaration of the Expr structure
typedef struct Expr Expr;
typedef size_t Expr_Index;

#define PRINT_OFFSET -20 // Offset for formatted printing

// Enum representing different kinds of expressions
typedef enum {
    EXPR_KIND_NUMBER = 0, // Numeric constant
    EXPR_KIND_CELL,       // Cell reference
    EXPR_KIND_PLUS        // Addition operation
} Expr_Kind;

// Structure for representing an addition expression
typedef struct {
    Expr_Index lhs; // Left-hand side expression
    Expr_Index rhs; // Right-hand side expression
} Expr_Plus;

// Structure representing a cell index in the spreadsheet
typedef struct {
    size_t row;
    size_t col;
} Cell_Index;

// Union storing different types of expressions
typedef union {
    double number;  // Numeric value
    Cell_Index cell; // Cell reference
    Expr_Plus plus;  // Addition operation
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
    if (eb->count >= eb->capacity) {
        if (eb->capacity == 0) {
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

typedef enum {
    DIR_LEFT = 0,
    DIR_RIGHT,
    DIR_UP,
    DIR_DOWN,
} Dir;

// Enums defining spreadsheet cell types and evaluation status
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
        case CELL_KIND_TEXT: return "TEXT";
        case CELL_KIND_NUMBER: return "NUMBER";
        case CELL_KIND_EXPR: return "EXPR";
        case CELL_KIND_CLONE: return "CLONE";
        default:
            assert(0 && "unreachable");
            exit(1);
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

typedef struct {
    String_View text;
    const char *file_path;
    size_t file_row;
    size_t file_col;
} Token;

/**
 * Extracts the next token from the lexer's source.
 * Handles operators, identifiers, and reports errors for unknown tokens.
 * 
 * @param lexer Pointer to the lexer structure.
 * @return The extracted token.
 */
Token lexer_next_token(Lexer *lexer) 
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

    if (*lexer->source.data == '+') {
        token.text = sv_chop_left(&lexer->source, 1);
        return token;
    }

    if (is_name(*lexer->source.data)) {
        token.text = sv_chop_left_while(&lexer->source, is_name);
        return token;
    }

    lexer_print_loc(lexer, stderr);
    fprintf(stderr, "ERROR: unknown token starts with `%c`\n", *lexer->source.data);
    exit(1);
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

    Expr_Index expr_index = expr_buffer_alloc(eb);
    Expr *expr = expr_buffer_at(eb, expr_index);
    expr->file_path = token.file_path;
    expr->file_row = token.file_row;
    expr->file_col = token.file_col;

    if (sv_strtod(token.text, tc, &expr->as.number)) {
        expr->kind = EXPR_KIND_NUMBER;
    } else {
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
    }

    return expr_index;
}

/**
 * Parses an addition expression.
 * Handles expressions with addition operators, recursively parsing the right-hand side.
 * 
 * @param lexer Pointer to the lexer structure.
 * @param tc Pointer to a temporary C-string structure.
 * @param eb Pointer to the expression buffer.
 * @return Index of the parsed addition expression or primary expression.
 */
Expr_Index parse_plus_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb) 
{
    Expr_Index lhs_index = parse_primary_expr(lexer, tc, eb);


    Token token = lexer_next_token(lexer);
    if(token.text.data != NULL && sv_eq(token.text, SV("+"))) {
        Expr_Index rhs_index = parse_plus_expr(lexer, tc, eb);

        Expr_Index expr_index = expr_buffer_alloc(eb);
        Expr *expr = expr_buffer_at(eb, expr_index);


        expr->kind = EXPR_KIND_PLUS;
        expr->as.plus.lhs = lhs_index;
        expr->as.plus.rhs = rhs_index;
        expr->file_path = token.file_path;
        expr->file_row = token.file_row;
        expr->file_col = token.file_col;

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
            Cell_Index cell_index = {.col = col, .row = row};
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
        case EXPR_KIND_PLUS:
            fprintf(stream, "PLUS: \n");
            dump_expr(stream, eb, expr->as.plus.lhs, level + 1);
            dump_expr(stream, eb, expr->as.plus.rhs, level + 1);
            break;
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
    return parse_plus_expr(lexer, tc, eb);
}

/**
 * Prints usage information for the program.
 * Displays the correct command-line syntax.
 *
 * @param stream Output file stream.
 */
void print_usage(FILE *stream) 
{
    fprintf(stream, "Usage: ./excel-cli <input.csv>\n");
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
                    assert(0 && "unreacheable");
                } break; 
            }
        } break;
        case EXPR_KIND_PLUS: {
            double lhs = table_eval_expr(table, eb, expr->as.plus.lhs);
            double rhs = table_eval_expr(table, eb, expr->as.plus.rhs);
            return lhs + rhs;
        } break;
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
            assert(0 && "unreachable: your memory is probably corrupted!\n");
            exit(1);
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
            assert(0 && "unreachable: your memory is probably corrupted!\n");
            exit(1);
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
        case EXPR_KIND_PLUS: {
            Expr_Index lhs, rhs;

            {
                Expr *root_expr = expr_buffer_at(eb, root);
                lhs = root_expr->as.plus.lhs;
                rhs = root_expr->as.plus.rhs;    
            }


            lhs = move_expr_in_dir(table, cell_index, eb, lhs, dir);
            rhs = move_expr_in_dir(table, cell_index, eb, rhs, dir);

            Expr_Index new_index = expr_buffer_alloc(eb);
            
            {
                Expr *new_expr = expr_buffer_at(eb, new_index);
                new_expr->kind = EXPR_KIND_PLUS;
                new_expr->as.plus.lhs = lhs;
                new_expr->as.plus.rhs = rhs;
                new_expr->file_path = table->file_path;
                new_expr->file_col = cell->file_col;
                new_expr->file_row = cell->file_row;
            }
        
            return new_index;
        } break;
        default: {
            assert(0 && "unreachable: your memory is probably corrupted somewhere");
            exit(1);
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
                assert(0 && "unreachable: evaluated cloens are an absurd. When a clone cell is evaluated it becomes its neighbor kind");
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
    if(argc < 2) {
        print_usage(stderr);
        fprintf(stderr, "ERROR: input file is not provided\n");
        exit(1);
    }

    const char *input_file_path = argv[1];

    size_t content_size = 0;
    char *content = read_csv(input_file_path, &content_size);

    if(content == NULL) {
        fprintf(stderr, "ERROR: could not read file %s: %s\n", input_file_path, strerror(errno));
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

    for(size_t row = 0; row < table.rows; ++row) {
        for(size_t col = 0; col < table.cols; ++col) {
            Cell_Index cell_index = {
                .col = col,
                .row = row,
            };

            table_eval_cell(&table, &eb, cell_index);
        }
    }

    for(size_t row = 0; row < table.rows; ++row) {
        for(size_t col = 0; col < table.cols; ++col) {
            Cell_Index cell_index = {
                .col = col,
                .row = row,
            };

            Cell *cell = table_cell_at(&table, cell_index);

            switch(cell->kind) {
                case CELL_KIND_TEXT: {
                    const char *text = sv_take_first_n(cell->as.text.data, cell->as.text.count);
                    printf("%-*s", PRINT_OFFSET, text);
                    break;
                }
                case CELL_KIND_NUMBER:
                    printf("%-*lf", PRINT_OFFSET, cell->as.number);
                    break;
                case CELL_KIND_EXPR:
                    printf("%-*lf", PRINT_OFFSET, cell->as.expr.value);
                    break;
                case CELL_KIND_CLONE:
                    assert(0 && "unreachable: cell should never be a clone after evalution");
                    break;
            }

            if(col < table.cols - 1) printf(" | ");
        }

        printf("\n");
    }

    free(content);
    free(table.cells);
    free(eb.items);
    free(tc.cstr);

    return 0;
}
