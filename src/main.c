#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define SV_IMPLEMENTATION
#include "sv.h"

typedef struct Expr Expr;
typedef size_t Expr_Index;

typedef enum {
    EXPR_KIND_NUMBER = 0,
    EXPR_KIND_CELL,
    EXPR_KIND_PLUS,
} Expr_Kind;

typedef struct {
    Expr_Index lhs;
    Expr_Index rhs;
} Expr_Plus;

typedef struct {
    size_t row;
    size_t col;
} Cell_Index;

typedef union {
    double number;
    Cell_Index cell;
    Expr_Plus plus;
} Expr_As;

struct Expr {
    Expr_Kind kind;
    Expr_As as;
};

typedef struct {
    size_t count;
    size_t capacity;
    Expr *items;
} Expr_Buffer;

Expr_Index expr_buffer_alloc(Expr_Buffer *eb) {
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

Expr *expr_buffer_at(Expr_Buffer *eb, Expr_Index index) {
    assert(index < eb->count);
    return &eb->items[index];
}

void expr_buffer_dump(FILE *stream, const Expr_Buffer *eb, Expr_Index root) {
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

typedef enum {
    CELL_KIND_TEXT = 0,
    CELL_KIND_NUMBER,
    CELL_KIND_EXPR,
    CELL_KIND_CLONE,
} Cell_Kind;

const char *cell_kind_as_cstr(Cell_Kind kind) {
    switch (kind)
    {
        case CELL_KIND_TEXT: 
            return "TEXT";
        case CELL_KIND_NUMBER: 
            return "NUMBER";
        case CELL_KIND_EXPR: 
            return "EXPR";
    default:
        assert(0 && "unreacheble");
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

typedef struct {
    Cell_Kind kind;
    Cell_As as;
    Eval_Status status;
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

    fprintf(stderr, "ERROR: unknown token starts with `%c`\n", *source->data);
    exit(1);
}

typedef struct {
    size_t capacity;
    char *cstr;
} Tmp_Cstr;

char *tmp_cstr_fill(Tmp_Cstr *tc, const char *data, size_t data_size) {
    if(data_size + 1 >= tc->capacity) {
        tc->capacity = data_size + 1;
        tc->cstr = realloc(tc->cstr, tc->capacity);
    }

    memcpy(tc->cstr, data, data_size);
    tc->cstr[data_size] = '\0';
    return tc->cstr;
}

bool sv_strtod(String_View sv, Tmp_Cstr *tc, double *out)
{
    char *ptr = tmp_cstr_fill(tc, sv.data, sv.count);
    char *endptr = NULL;
    double result = strtod(ptr, &endptr);
    if (out) *out = result;
    return endptr != ptr && *endptr == '\0';
}

bool sv_strtol(String_View sv, Tmp_Cstr *tc, long int *out)
{
    char *ptr = tmp_cstr_fill(tc, sv.data, sv.count);
    char *endptr = NULL;
    long int result = strtol(ptr, &endptr, 10);
    if (out) *out = result;
    return endptr != ptr && *endptr == '\0';
}

Expr_Index parse_primary_expr(String_View *source, Tmp_Cstr *tc, Expr_Buffer *eb) {
    String_View token = next_token(source);

    if (token.count == 0) {
        fprintf(stderr, "ERROR: expected primary expression token, but got end of input\n");
        exit(1);
    }

    Expr_Index expr_index = expr_buffer_alloc(eb);
    Expr *expr = expr_buffer_at(eb, expr_index);

    if (sv_strtod(token, tc, &expr->as.number)) {
        expr->kind = EXPR_KIND_NUMBER;
    } else {
        expr->kind = EXPR_KIND_CELL;

        if (!isupper(*token.data)) {
            fprintf(stderr, "ERROR: cell reference must start with capital letter\n");
            exit(1);
        }

        expr->as.cell.col = *token.data - 'A';

        sv_chop_left(&token, 1);

        long int row = 0;
        if (!sv_strtol(token, tc, &row)) {
            fprintf(stderr, "ERROR: cell reference must have an integer as the row number\n");
            exit(1);
        }

        expr->as.cell.row = (size_t) row;
    }

    return expr_index;
}

Expr_Index parse_plus_expr(String_View *source, Tmp_Cstr *tc, Expr_Buffer *eb) {
    Expr_Index lhs_index = parse_primary_expr(source, tc, eb);

    String_View token = next_token(source);
    if(token.data != NULL && sv_eq(token, SV("+"))) {
        Expr_Index rhs_index = parse_plus_expr(source, tc, eb);

        Expr_Index expr_index = expr_buffer_alloc(eb);
        Expr *expr = expr_buffer_at(eb, expr_index);


        expr->kind = EXPR_KIND_PLUS;
        expr->as.plus.lhs = lhs_index;
        expr->as.plus.rhs = rhs_index;

        return expr_index;
    }

    return lhs_index;
}

void dump_expr(FILE *stream, Expr_Buffer *eb, Expr_Index expr_index, int level) {
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

Expr_Index parse_expr(String_View *source, Tmp_Cstr *tc, Expr_Buffer *eb) {
    return parse_plus_expr(source, tc, eb);
}

Cell *table_cell_at(Table *table, Cell_Index index) {
    assert(index.row < table->rows);
    assert(index.col < table->cols);

    return &table->cells[index.row * table->cols + index.col];
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

void parse_table_from_content(Table *table, Expr_Buffer *eb, Tmp_Cstr *tc, String_View content)
{
    for (size_t row = 0; content.count > 0; ++row) {
        String_View line = sv_chop_by_delim(&content, '\n');
        for (size_t col = 0; line.count > 0; ++col) {
            String_View cell_value = sv_trim(sv_chop_by_delim(&line, '|'));
            Cell_Index cell_index = {
                .col = col,
                .row = row,
            };

            Cell *cell = table_cell_at(table, cell_index);

            if (sv_starts_with(cell_value, SV("="))) {
                sv_chop_left(&cell_value, 1);
                cell->kind = CELL_KIND_EXPR;
                cell->as.expr.index = parse_expr(&cell_value, tc, eb);
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
                    fprintf(stderr, "ERROR: "SV_Fmt" is not a correct direction to clone a cell from \n", SV_Arg(cell_value));
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

void table_eval_cell(Table *table, Expr_Buffer *eb, Cell_Index cell_index);

double table_eval_expr(Table *table, Expr_Buffer *eb, Expr_Index expr_index) {

    Expr *expr = expr_buffer_at(eb, expr_index);

    switch(expr->kind) {
        case EXPR_KIND_NUMBER:
            return expr->as.number;
        case EXPR_KIND_CELL: {
            table_eval_cell(table, eb, expr->as.cell);

            Cell *cell = table_cell_at(table, expr->as.cell);
            switch(cell->kind) {
                case CELL_KIND_NUMBER: 
                    return cell->as.number;
                case CELL_KIND_TEXT: {
                    fprintf(stderr, "ERROR: CELL(%zu, %zu): text cells may not participate in math expressions\n", expr->as.cell.row, expr->as.cell.col);
                    exit(1);
                } break;
                case CELL_KIND_EXPR: {
                    return cell->as.expr.value;
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

Dir opposite_dir(Dir dir) {
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

// TODO: check neighbor bounds
Cell_Index nbor_in_dir(Cell_Index index, Dir dir) {
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

Expr_Index move_expr_in_dir(Expr_Buffer *eb, Expr_Index root, Dir dir) {
    switch (expr_buffer_at(eb, root)->kind) {
        case EXPR_KIND_NUMBER:
            return root;
        case EXPR_KIND_CELL: {
            Expr_Index new_index = expr_buffer_alloc(eb);

            expr_buffer_at(eb, new_index)->kind = EXPR_KIND_CELL;
            expr_buffer_at(eb, new_index)->as.cell = nbor_in_dir(expr_buffer_at(eb, root)->as.cell, dir);

            return new_index;
        } break;
        case EXPR_KIND_PLUS: {
            Expr_Index new_index = expr_buffer_alloc(eb);

            expr_buffer_at(eb, new_index)->kind = EXPR_KIND_PLUS;

            Expr_Index tmp = move_expr_in_dir(eb, expr_buffer_at(eb, root)->as.plus.lhs, dir);
            expr_buffer_at(eb, new_index)->as.plus.lhs = tmp;

            tmp = move_expr_in_dir(eb, expr_buffer_at(eb, root)->as.plus.rhs, dir);
            expr_buffer_at(eb, new_index)->as.plus.rhs = tmp;

            return new_index;
        } break;
        default: {
            assert(0 && "unreachable: your memory is probably corrupted somewhere");
            exit(1);
        }
    }
}

void table_eval_cell(Table *table, Expr_Buffer *eb, Cell_Index cell_index) {
    Cell *cell = table_cell_at(table, cell_index);

    switch(cell->kind) {
        case CELL_KIND_TEXT:
        case CELL_KIND_NUMBER:
            cell->status = EVALUATED;
            break;
        case CELL_KIND_EXPR: { 
            if(cell->status == INPROGRESS) {
                fprintf(stderr, "ERROR: circular dependency is detected!\n");
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
                fprintf(stderr, "ERROR: circular dependency is detected!\n");
                exit(1);
            }

            if(cell->status == UNEVALUATED) {
                cell->status = INPROGRESS;
                
                Dir dir = cell->as.clone;
                Cell_Index nbor_index = nbor_in_dir(cell_index, dir);
                table_eval_cell(table, eb, nbor_index);

                
                Cell *nbor = table_cell_at(table, nbor_index);
                cell->kind = nbor->kind;
                cell->as = nbor->as;

                if(cell->kind == CELL_KIND_EXPR) {
                    cell->as.expr.index = move_expr_in_dir(eb, cell->as.expr.index, opposite_dir(dir));
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

    Expr_Buffer eb = {0};
    Table table = {0};
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
            Cell *cell = table_cell_at(&table, cell_index);

            switch(cell->kind) {
                case CELL_KIND_TEXT:
                    printf(SV_Fmt, SV_Arg(cell->as.text));
                    break;
                case CELL_KIND_NUMBER:
                    printf("%lf", cell->as.number);
                    break;
                case CELL_KIND_EXPR:
                    printf("%lf", cell->as.expr.value);
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
