/*
 * parse.c - AntLang source parser and validator.
 *
 * The grammar is deliberately line oriented:
 *
 *   BOXES = (width, height);
 *   ANT   = (x, y, E|N|W|S);
 *   OUT   = true|false;
 *   LOOP  = non-negative-integer;
 *   BOARD =
 *   B W B ...
 *   ...
 *
 * Directives may appear in any order, but the BOARD rows must follow the
 * BOARD directive until the next directive.  Semicolons are optional,
 * comments start with '#' or '//', and keywords are case-insensitive.
 */
#include "antc.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t len;
    int line;
} BoardRow;

typedef struct {
    const char *p;
    const char *end;
    const char *line_start;
} Cursor;

typedef struct {
    BoardRow *rows;
    size_t count;
    size_t cap;
} RowList;

static void skip_ws(Cursor *c)
{
    while (c->p < c->end && isspace((unsigned char)*c->p)) {
        c->p++;
    }
}

static int at_end(const Cursor *c)
{
    return c->p >= c->end;
}

static int col_of(const Cursor *c)
{
    return (int)(c->p - c->line_start) + 1;
}

static int expect_char(Cursor *c, char ch, Diag *diag, const Source *src,
                       int line, const char *what)
{
    skip_ws(c);
    if (at_end(c) || *c->p != ch) {
        diag_error(diag, src, line, col_of(c), "expected '%c' %s", ch, what);
        return 0;
    }
    c->p++;
    return 1;
}

/* Parse a decimal signed 64-bit integer.  No spaces are allowed between
 * the sign and the digits. */
static int parse_ll(Cursor *c, long long *out)
{
    int neg = 0;
    unsigned long long v = 0;

    skip_ws(c);
    if (!at_end(c) && (*c->p == '-' || *c->p == '+')) {
        neg = (*c->p == '-');
        c->p++;
    }
    if (at_end(c) || !isdigit((unsigned char)*c->p)) {
        return 0;
    }
    while (!at_end(c) && isdigit((unsigned char)*c->p)) {
        unsigned int d = (unsigned int)(*c->p - '0');
        if (v > (ULLONG_MAX - d) / 10ULL) {
            return 0;
        }
        v = v * 10ULL + d;
        c->p++;
    }
    if (neg) {
        if (v > (unsigned long long)LLONG_MAX + 1ULL) {
            return 0;
        }
        if (v == (unsigned long long)LLONG_MAX + 1ULL) {
            *out = LLONG_MIN;
        } else {
            *out = -(long long)v;
        }
    } else {
        if (v > (unsigned long long)LLONG_MAX) {
            return 0;
        }
        *out = (long long)v;
    }
    return 1;
}

static int parse_ull(Cursor *c, unsigned long long *out)
{
    unsigned long long v = 0;

    skip_ws(c);
    if (at_end(c) || !isdigit((unsigned char)*c->p)) {
        return 0;
    }
    while (!at_end(c) && isdigit((unsigned char)*c->p)) {
        unsigned int d = (unsigned int)(*c->p - '0');
        if (v > (ULLONG_MAX - d) / 10ULL) {
            return 0;
        }
        v = v * 10ULL + d;
        c->p++;
    }
    *out = v;
    return 1;
}

static int parse_word(Cursor *c, char *buf, size_t bufsz)
{
    size_t n = 0;

    skip_ws(c);
    if (at_end(c) || !isalpha((unsigned char)*c->p)) {
        return 0;
    }
    while (!at_end(c) && isalpha((unsigned char)*c->p)) {
        if (n + 1 < bufsz) {
            buf[n] = (char)toupper((unsigned char)*c->p);
        }
        n++;
        c->p++;
    }
    if (n >= bufsz) {
        n = bufsz - 1;
    }
    buf[n] = '\0';
    return 1;
}

static int finish_value(Cursor *c, Diag *diag, const Source *src, int line,
                        const char *what)
{
    skip_ws(c);
    if (!at_end(c) && *c->p == ';') {
        c->p++;
        skip_ws(c);
    }
    if (!at_end(c)) {
        diag_error(diag, src, line, col_of(c),
                   "unexpected text after %s value", what);
        return 0;
    }
    return 1;
}

/* Parse an identifier at the cursor.  Returns 1 on success, 0 if there is
 * no identifier, -1 if it is too long for the buffer. */
static int parse_ident(Cursor *c, char *buf, size_t bufsz, size_t *len_out)
{
    size_t n = 0;

    skip_ws(c);
    if (at_end(c) ||
        !(isalpha((unsigned char)*c->p) || *c->p == '_')) {
        return 0;
    }
    while (!at_end(c) &&
           (isalnum((unsigned char)*c->p) || *c->p == '_')) {
        if (n + 1 < bufsz) {
            buf[n] = (char)toupper((unsigned char)*c->p);
        }
        n++;
        c->p++;
    }
    if (n >= bufsz) {
        return -1;
    }
    buf[n] = '\0';
    if (len_out != NULL) {
        *len_out = n;
    }
    return 1;
}

static int row_add(RowList *list, char *data, size_t len, int line)
{
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        list->rows = xrealloc(list->rows, cap * sizeof(*list->rows));
        list->cap = cap;
    }
    list->rows[list->count].data = data;
    list->rows[list->count].len = len;
    list->rows[list->count].line = line;
    list->count++;
    return 1;
}

static void rows_free(RowList *list)
{
    size_t i;
    for (i = 0; i < list->count; i++) {
        free(list->rows[i].data);
    }
    free(list->rows);
    list->rows = NULL;
    list->count = 0;
    list->cap = 0;
}

/* A BOARD row is a whitespace-separated sequence of B/W cells.  Compact
 * rows such as "BWBWW" are accepted too, and every cell may optionally be
 * followed by whitespace.  A single trailing semicolon is allowed. */
static int parse_row(const Source *src, int line, const char *line_start,
                     const char *p, const char *end, RowList *list,
                     Diag *diag)
{
    char *data;
    size_t len = 0;
    const char *q;
    const char *row_start = p;

    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    while (end > p && isspace((unsigned char)end[-1])) {
        end--;
    }
    if (end > p && end[-1] == ';') {
        end--;
        while (end > p && isspace((unsigned char)end[-1])) {
            end--;
        }
    }
    if (p == end) {
        diag_error(diag, src, line, (int)(p - line_start) + 1,
                   "empty BOARD row");
        return 0;
    }

    data = xmalloc((size_t)(end - p) + 1);
    q = p;
    while (q < end) {
        while (q < end && isspace((unsigned char)*q)) {
            q++;
        }
        if (q == end) {
            break;
        }
        if (*q == ';') {
            diag_error(diag, src, line, (int)(q - line_start) + 1,
                       "';' is only allowed at the end of a BOARD row");
            free(data);
            return 0;
        }
        while (q < end && !isspace((unsigned char)*q)) {
            int ch = toupper((unsigned char)*q);
            if (ch != 'B' && ch != 'W') {
                diag_error(diag, src, line, (int)(q - line_start) + 1,
                           "invalid cell '%c'; expected B or W",
                           (char)*q);
                free(data);
                return 0;
            }
            if (len >= ANTC_MAX_DIM) {
                diag_error(diag, src, line, (int)(q - line_start) + 1,
                           "BOARD row is longer than %d cells",
                           ANTC_MAX_DIM);
                free(data);
                return 0;
            }
            data[len++] = (char)ch;
            q++;
        }
    }
    if (len == 0) {
        diag_error(diag, src, line, (int)(row_start - line_start) + 1,
                   "empty BOARD row");
        free(data);
        return 0;
    }
    data[len] = '\0';
    row_add(list, data, len, line);
    return 1;
}

static int parse_boxes(Cursor *c, Program *p, int *seen, Diag *diag,
                       const Source *src, int line)
{
    long long w, h;

    if (*seen) {
        diag_error(diag, src, line, col_of(c),
                   "duplicate BOXES directive");
        return 0;
    }
    if (!expect_char(c, '(', diag, src, line, "after BOXES =")) {
        return 0;
    }
    if (!parse_ll(c, &w)) {
        diag_error(diag, src, line, col_of(c),
                   "expected an integer width in BOXES");
        return 0;
    }
    if (!expect_char(c, ',', diag, src, line, "between BOXES width and height")) {
        return 0;
    }
    if (!parse_ll(c, &h)) {
        diag_error(diag, src, line, col_of(c),
                   "expected an integer height in BOXES");
        return 0;
    }
    if (!expect_char(c, ')', diag, src, line, "after BOXES height")) {
        return 0;
    }
    if (!finish_value(c, diag, src, line, "BOXES")) {
        return 0;
    }
    if (w < 1 || w > ANTC_MAX_DIM) {
        diag_error(diag, src, line, 1,
                   "BOXES width must be between 1 and %d (got %lld)",
                   ANTC_MAX_DIM, w);
        return 0;
    }
    if (h < 1 || h > ANTC_MAX_DIM) {
        diag_error(diag, src, line, 1,
                   "BOXES height must be between 1 and %d (got %lld)",
                   ANTC_MAX_DIM, h);
        return 0;
    }
    if ((unsigned long long)w * (unsigned long long)h > ANTC_MAX_CELLS) {
        diag_error(diag, src, line, 1,
                   "BOXES has %lld cells; the limit is %u",
                   w * h, (unsigned)ANTC_MAX_CELLS);
        return 0;
    }
    p->width = (int)w;
    p->height = (int)h;
    *seen = 1;
    return 1;
}

static int parse_ant(Cursor *c, Program *p, int *seen, Diag *diag,
                     const Source *src, int line)
{
    long long x, y;
    char word[16];
    Direction d;

    if (*seen) {
        diag_error(diag, src, line, col_of(c), "duplicate ANT directive");
        return 0;
    }
    if (!expect_char(c, '(', diag, src, line, "after ANT =")) {
        return 0;
    }
    if (!parse_ll(c, &x)) {
        diag_error(diag, src, line, col_of(c), "expected an integer x in ANT");
        return 0;
    }
    if (!expect_char(c, ',', diag, src, line, "between ANT x and y")) {
        return 0;
    }
    if (!parse_ll(c, &y)) {
        diag_error(diag, src, line, col_of(c), "expected an integer y in ANT");
        return 0;
    }
    if (!expect_char(c, ',', diag, src, line, "after ANT y")) {
        return 0;
    }
    if (!parse_word(c, word, sizeof(word))) {
        diag_error(diag, src, line, col_of(c),
                   "expected a direction (E, N, W or S) in ANT");
        return 0;
    }
    if (word[0] == '\0' || word[1] != '\0' ||
        !dir_from_char((unsigned char)word[0], &d)) {
        diag_error(diag, src, line, col_of(c),
                   "invalid direction '%s'; expected E, N, W or S", word);
        return 0;
    }
    if (!expect_char(c, ')', diag, src, line, "after ANT direction")) {
        return 0;
    }
    if (!finish_value(c, diag, src, line, "ANT")) {
        return 0;
    }
    if (x < -ANTC_MAX_COORD || x > ANTC_MAX_COORD) {
        diag_error(diag, src, line, 1,
                   "ANT x is outside the supported range [-%lld, %lld]",
                   ANTC_MAX_COORD, ANTC_MAX_COORD);
        return 0;
    }
    if (y < -ANTC_MAX_COORD || y > ANTC_MAX_COORD) {
        diag_error(diag, src, line, 1,
                   "ANT y is outside the supported range [-%lld, %lld]",
                   ANTC_MAX_COORD, ANTC_MAX_COORD);
        return 0;
    }
    p->ax = x;
    p->ay = y;
    p->dir = d;
    *seen = 1;
    return 1;
}

static int parse_out(Cursor *c, Program *p, int *seen, Diag *diag,
                     const Source *src, int line)
{
    char word[16];
    int value;

    if (*seen) {
        diag_error(diag, src, line, col_of(c), "duplicate OUT directive");
        return 0;
    }
    if (!parse_word(c, word, sizeof(word))) {
        diag_error(diag, src, line, col_of(c),
                   "expected true or false after OUT =");
        return 0;
    }
    if (strcmp(word, "TRUE") == 0) {
        value = 1;
    } else if (strcmp(word, "FALSE") == 0) {
        value = 0;
    } else {
        diag_error(diag, src, line, col_of(c),
                   "invalid OUT value '%s'; expected true or false", word);
        return 0;
    }
    if (!finish_value(c, diag, src, line, "OUT")) {
        return 0;
    }
    p->out = value;
    *seen = 1;
    return 1;
}

static int parse_loop(Cursor *c, Program *p, int *seen, Diag *diag,
                      const Source *src, int line)
{
    unsigned long long value;

    if (*seen) {
        diag_error(diag, src, line, col_of(c), "duplicate LOOP directive");
        return 0;
    }
    if (!parse_ull(c, &value)) {
        diag_error(diag, src, line, col_of(c),
                   "expected a non-negative integer after LOOP =");
        return 0;
    }
    if (!finish_value(c, diag, src, line, "LOOP")) {
        return 0;
    }
    if (value > ANTC_MAX_LOOP) {
        diag_error(diag, src, line, 1,
                   "LOOP is too large (limit is %llu)",
                   (unsigned long long)ANTC_MAX_LOOP);
        return 0;
    }
    p->loop_limit = value;
    *seen = 1;
    return 1;
}

static void strip_comment(const char **pp, const char **endp)
{
    const char *p = *pp;
    const char *end = *endp;
    const char *q;
    for (q = p; q < end; q++) {
        if (*q == '#' ||
            (*q == '/' && q + 1 < end && q[1] == '/')) {
            *endp = q;
            return;
        }
    }
}

int parse_program(const Source *src, Program *p, Diag *diag)
{
    size_t pos = 0;
    int line = 1;
    int seen_boxes = 0;
    int seen_ant = 0;
    int seen_out = 0;
    int seen_loop = 0;
    int seen_board = 0;
    int present_boxes = 0;
    int present_ant = 0;
    int present_out = 0;
    int present_loop = 0;
    int present_board = 0;
    int board_active = 0;
    int board_line = 0;
    RowList rows;
    int ok = 1;
    size_t i;

    memset(p, 0, sizeof(*p));
    memset(&rows, 0, sizeof(rows));

    while (pos < src->len) {
        const char *line_start = src->text + pos;
        size_t begin = pos;
        size_t end;
        const char *pstart;
        const char *pend;
        Cursor c;
        char name[64];
        size_t name_len = 0;
        int ident;
        const char *look;

        while (pos < src->len && src->text[pos] != '\n') {
            pos++;
        }
        end = pos;
        if (pos < src->len) {
            pos++; /* consume '\n' */
        }
        if (end > begin && src->text[end - 1] == '\r') {
            end--;
        }

        pstart = src->text + begin;
        pend = src->text + end;
        strip_comment(&pstart, &pend);
        while (pstart < pend && isspace((unsigned char)*pstart)) {
            pstart++;
        }
        while (pend > pstart && isspace((unsigned char)pend[-1])) {
            pend--;
        }
        if (pstart == pend) {
            line++;
            continue;
        }

        c.p = pstart;
        c.end = pend;
        c.line_start = line_start;
        ident = parse_ident(&c, name, sizeof(name), &name_len);
        look = c.p;
        while (look < pend && isspace((unsigned char)*look)) {
            look++;
        }
        if (ident == 1 && look < pend && *look == '=') {
            /* A directive line. */
            c.p = look + 1;
            board_active = 0;

            if (strcmp(name, "BOXES") == 0) {
                present_boxes = 1;
                if (!parse_boxes(&c, p, &seen_boxes, diag, src, line)) {
                    ok = 0;
                }
            } else if (strcmp(name, "ANT") == 0) {
                present_ant = 1;
                if (!parse_ant(&c, p, &seen_ant, diag, src, line)) {
                    ok = 0;
                }
            } else if (strcmp(name, "OUT") == 0) {
                present_out = 1;
                if (!parse_out(&c, p, &seen_out, diag, src, line)) {
                    ok = 0;
                }
            } else if (strcmp(name, "LOOP") == 0) {
                present_loop = 1;
                if (!parse_loop(&c, p, &seen_loop, diag, src, line)) {
                    ok = 0;
                }
            } else if (strcmp(name, "BOARD") == 0) {
                present_board = 1;
                if (seen_board) {
                    diag_error(diag, src, line, 1,
                               "duplicate BOARD directive");
                    ok = 0;
                } else {
                    Cursor rest;
                    seen_board = 1;
                    board_line = line;
                    board_active = 1;
                    rest.p = c.p;
                    rest.end = pend;
                    rest.line_start = line_start;
                    while (rest.p < rest.end &&
                           isspace((unsigned char)*rest.p)) {
                        rest.p++;
                    }
                    if (rest.p < rest.end &&
                        !parse_row(src, line, line_start, rest.p, rest.end,
                                   &rows, diag)) {
                        ok = 0;
                    }
                }
            } else {
                if (strcmp(name, "BOX") == 0) {
                    present_boxes = 1;
                    diag_error(diag, src, line, 1,
                               "the directive is spelled BOXES, not BOX");
                } else {
                    diag_error(diag, src, line, 1,
                               "unknown directive '%s'", name);
                }
                ok = 0;
            }
        } else if (ident == -1 && look < pend && *look == '=') {
            diag_error(diag, src, line, 1,
                       "directive name is too long");
            ok = 0;
        } else if (board_active) {
            if (!parse_row(src, line, line_start, pstart, pend, &rows,
                           diag)) {
                ok = 0;
            }
        } else if (ident == -1) {
            diag_error(diag, src, line, 1,
                       "expected a directive (NAME = value)");
            ok = 0;
        } else if (seen_board) {
            diag_error(diag, src, line, 1,
                       "unexpected text; BOARD rows must follow the BOARD "
                       "directive directly");
            ok = 0;
        } else {
            diag_error(diag, src, line, 1,
                       "expected a directive (NAME = value)");
            ok = 0;
        }
        line++;
    }

    if (src->len == 0) {
        diag_error(diag, src, 1, 1, "source file is empty");
        ok = 0;
    }
    if (!present_boxes) {
        diag_error(diag, src, 1, 1, "missing BOXES directive");
        ok = 0;
    }
    if (!present_ant) {
        diag_error(diag, src, 1, 1, "missing ANT directive");
        ok = 0;
    }
    if (!present_board) {
        diag_error(diag, src, 1, 1, "missing BOARD directive");
        ok = 0;
    }
    if (!present_out && !present_loop) {
        diag_error(diag, src, 1, 1,
                   "program needs at least one of OUT or LOOP");
        ok = 0;
    }

    p->has_out = seen_out;
    p->has_loop = seen_loop;
    if (!seen_out) {
        p->out = 0;
    }
    if (seen_out && !p->out && !present_loop) {
        diag_error(diag, src, 1, 1,
                   "OUT is false and LOOP is missing; the program would "
                   "never halt");
        ok = 0;
    }

    if (seen_boxes && seen_board && ok) {
        if (rows.count != (size_t)p->height) {
            diag_error(diag, src,
                       rows.count > 0 ? rows.rows[rows.count - 1].line
                                      : board_line,
                       1,
                       "BOARD has %zu row%s but BOXES height is %d",
                       rows.count, rows.count == 1 ? "" : "s", p->height);
            ok = 0;
        }
        for (i = 0; i < rows.count && ok; i++) {
            if (rows.rows[i].len != (size_t)p->width) {
                diag_error(diag, src, rows.rows[i].line, 1,
                           "BOARD row %zu has %zu cells but BOXES width is %d",
                           i + 1, rows.rows[i].len, p->width);
                ok = 0;
            }
        }
    }

    if (ok && seen_boxes && seen_ant && seen_out && p->out) {
        if (p->ax < 1 || p->ax > p->width ||
            p->ay < 1 || p->ay > p->height) {
            diag_error(diag, src, 1, 1,
                       "with OUT = true the initial ant (%lld, %lld) must be "
                       "inside the %dx%d board",
                       p->ax, p->ay, p->width, p->height);
            ok = 0;
        }
    }

    if (ok && seen_boxes && seen_board) {
        size_t cells = (size_t)p->width * (size_t)p->height;
        size_t y;
        p->cells = xcalloc(cells, 1);
        for (y = 0; y < (size_t)p->height; y++) {
            size_t x;
            for (x = 0; x < (size_t)p->width; x++) {
                p->cells[y * (size_t)p->width + x] =
                    rows.rows[y].data[x] == 'B' ? COLOR_B : COLOR_W;
            }
        }
    }

    rows_free(&rows);
    if (!ok || diag->errors > 0) {
        program_free(p);
        return 0;
    }
    return 1;
}

void program_free(Program *p)
{
    if (p != NULL) {
        free(p->cells);
        p->cells = NULL;
    }
}
