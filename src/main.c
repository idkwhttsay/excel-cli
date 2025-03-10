#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define SV_IMPLEMENTATION
#include "../sv.h"

typedef struct Expr Expr;

typedef enum {
    EXPR_KIND_NUMBER = 0,
    EXPR_KIND_CELL, 
    EXPR_KIND_PLUS,
} Expr_Kind;

typedef struct {
    Expr *lhs;
    Expr *rhs;
} Expr_Plus;

typedef struct {
    size_t row;
    size_t col;
} Expr_Cell;
typedef union {
    double number;
    Expr_Cell cell;
    Expr_Plus plus; 
} Expr_As;

struct Expr {
    Expr_Kind kind;
    Expr_As as;
};
typedef enum {
    CELL_KIND_TEXT = 0,
    CELL_KIND_NUMBER,
    CELL_KIND_EXPR,
} Cell_Kind;

const char *cell_kind_as_cstr(Cell_Kind kind) {
    switch (kind)
    {
    case CELL_KIND_TEXT: return "TEXT";
    case CELL_KIND_NUMBER: return "NUMBER";
    case CELL_KIND_EXPR: return "EXPR";
    default:
        assert(0 && "unreacheble");
        exit(1);
    }
}

typedef union {
    String_View text;
    double number;
    Expr *expr;
} Cell_as;

typedef struct {
    Cell_Kind kind;
    Cell_as as;
} Cell;

typedef struct {
    Cell *cells;
    size_t rows;
    size_t cols;
} Table;

bool is_name(char c) {
    return isalnum(c) || c == '_';
}

String_View next_token(String_View *source) {
    *source = sv_trim(*source);

    if (source->count == 0) {
        return SV_NULL;
    }

    if (*source->data == '+') {
        return sv_chop_left(source, 1);
    }

    if (is_name(*source->data)) {
        return sv_chop_left_while(source, is_name);
    }

    fprintf(stderr, "ERROR: unknown token starts with `%c`", *source->data);
    exit(1);
}

bool sv_strtod(String_View sv, double *out) {
    static char tmp_buffer[1024 * 4];
    assert(sv.count < sizeof(tmp_buffer));
    snprintf(tmp_buffer, sizeof(tmp_buffer), SV_Fmt, SV_Arg(sv));

    char *endptr = NULL;
    double result = strtod(tmp_buffer, &endptr);
    if(out) *out = result;

    return endptr != tmp_buffer && *endptr == '\0';
}

bool sv_strtol(String_View sv, long int *out) {
    static char tmp_buffer[1024 * 4];
    assert(sv.count < sizeof(tmp_buffer));
    snprintf(tmp_buffer, sizeof(tmp_buffer), SV_Fmt, SV_Arg(sv));

    char *endptr = NULL;
    long int result = strtol(tmp_buffer, &endptr, 10);
    if(out) *out = result;

    return endptr != tmp_buffer && *endptr == '\0';
}

Expr *parse_primary_expr(String_View *source) {
    String_View token = next_token(source);

    if (token.count == 0) {
        fprintf(stderr, "ERROR: expected primary expression token, but got end of input\n");
        exit(1);
    }

    Expr *expr = malloc(sizeof(Expr));
    memset(expr, 0, sizeof(Expr));

    if (sv_strtod(token, &expr->as.number)) {
        expr->kind = EXPR_KIND_NUMBER;
    } else {
        expr->kind = EXPR_KIND_CELL;

        if (!isupper(*token.data)) {
            fprintf(stderr, "ERROR: cell reference must start with capital letter");
            exit(1);
        }

        expr->as.cell.col = *token.data - 'A';

        sv_chop_left(&token, 1);

        long int row = 0;
        if (!sv_strtol(token, &row)) {
            fprintf(stderr, "ERROR: cell reference must have an integer as the row number\n");
            exit(1);
        }

        expr->as.cell.row = (size_t) row;
    }

    return expr;
}

Expr *parse_plus_expr(String_View *source) {
    Expr *lhs = parse_primary_expr(source);

    String_View token = next_token(source);
    if(token.data != NULL && sv_eq(token, SV("+"))) {
        Expr *rhs = parse_plus_expr(source);

        Expr *expr = malloc(sizeof(Expr));
        expr->kind = EXPR_KIND_PLUS;
        expr->as.plus.lhs = lhs;
        expr->as.plus.rhs = rhs;

        return expr;
    }

    return lhs;
}

void dump_expr(FILE *stream, Expr *expr, int level) {
    fprintf(stream, "%*s", level * 2, "");
    
    switch(expr->kind) {
        case EXPR_KIND_NUMBER:
            fprintf(stream, "NUMBER: %lf\n", expr->as.number);
            break;
        case EXPR_KIND_CELL:
            fprintf(stream, "CELL(%zu, %zu)\n", expr->as.cell.row, expr->as.cell.col);
            break;
        case EXPR_KIND_PLUS:
            fprintf(stream, "PLUS: \n");
            dump_expr(stream, expr->as.plus.lhs, level + 1);
            dump_expr(stream, expr->as.plus.rhs, level + 1);
            break;
    }
}

Expr *parse_expr(String_View *source) {
    return parse_plus_expr(source);
}

Table table_alloc(size_t rows, size_t cols) {
    Table table = {0};
    table.cols = cols;
    table.rows = rows;
    table.cells = malloc(sizeof(char) * rows * cols);

    if(table.cells == NULL) {
        fprintf(stderr, "ERROR: could not allocate memory for the table\n");
        exit(1);
    }

    memset(table.cells, 0, sizeof(Cell) * rows * cols);
    return table;
}

Cell *table_cell_at(Table *table, size_t row, size_t col) {
    assert(row < table->rows);
    assert(col < table->cols);
    assert(row >= 0);
    assert(col >= 0);

    return &table->cells[row * table->cols + col];
}

void print_usage(FILE *stream) {
    fprintf(stream, "Usage: ./excel-cli <input.csv>\n");
}

char *read_csv(const char *file_path, size_t *size) {

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

void parse_table_from_content(Table* table, String_View content) {
    for(size_t row = 0; content.count > 0; ++row) {
        String_View line = sv_chop_by_delim(&content, '\n');
        for(size_t col = 0; line.count > 0; ++col) {
            String_View raw_cell = sv_trim(sv_chop_by_delim(&line, '|'));
            Cell *cell = table_cell_at(table, row, col);

            if(sv_starts_with(raw_cell, SV("="))) {
                sv_chop_left(&raw_cell, 1);
                cell->kind = CELL_KIND_EXPR;
                cell->as.expr = parse_expr(&raw_cell);
            } else {
                if(sv_strtod(raw_cell, &cell->as.number)) {
                    cell->kind = CELL_KIND_NUMBER;
                } else {
                    cell->kind = CELL_KIND_TEXT;
                    cell->as.text = raw_cell;
                }
            }
        }
    }
}

void estimate_table_size(String_View content, size_t *out_rows, size_t *out_cols) {
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

int main(int argc, char **argv) {
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

    size_t rows, cols;
    estimate_table_size(input, &rows, &cols);
    Table table = table_alloc(rows, cols);
    parse_table_from_content(&table, input);

    for(size_t row = 0; row < table.rows; ++row) {
        for(size_t col = 0; col < table.cols; ++col) {
            Cell *cell = table_cell_at(&table, row, col);
            printf("CELL(%zu, %zu): ", row, col);

            switch (cell->kind) {
                case CELL_KIND_TEXT: 
                    printf("TEXT(\""SV_Fmt"\")\n", SV_Arg(cell->as.text)); 
                    break;
                case CELL_KIND_NUMBER: 
                    printf("TEXT(%lf)\n", cell->as.number); 
                    break;
                case CELL_KIND_EXPR: 
                    printf("EXPR\n");
                    dump_expr(stdout, cell->as.expr, 1); 
                    break;
            }
        }
    }

    return 0;
}
