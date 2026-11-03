/*
 * antc.h - shared declarations for the AntLang compiler.
 *
 * AntLang is a small esoteric language based on Langton's ant.
 * A program is a rectangular board plus the ant's initial state and
 * optional halting rules.  antc parses such a program, can simulate it,
 * and can translate it into a standalone C99 program.
 */
#ifndef ANTC_H
#define ANTC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ANTC_VERSION "1.0.0"

/* Hard limits.  They keep generated C sources and terminal output sane. */
#define ANTC_MAX_DIM 4096
#define ANTC_MAX_CELLS 1000000u
#define ANTC_MAX_VIEW_CELLS 4000000ULL
#define ANTC_MAX_LOOP 1000000000000000000ULL /* 10^18 */
#define ANTC_MAX_COORD 1000000000LL
#define ANTC_MAX_SOURCE (64u * 1024u * 1024u)

/* Directions are numbered clockwise on screen: E, S, W, N.
 * Turning right is therefore (d + 1) & 3, turning left is (d + 3) & 3. */
typedef enum {
    DIR_E = 0,
    DIR_S = 1,
    DIR_W = 2,
    DIR_N = 3
} Direction;

typedef enum {
    COLOR_W = 0,
    COLOR_B = 1
} Color;

typedef enum {
    VIEW_BOARD = 0, /* the declared BOXES rectangle (default) */
    VIEW_BBOX = 1   /* initial board + every black cell + final ant */
} ViewMode;

typedef enum {
    HALT_OUT = 0,
    HALT_LOOP = 1
} HaltReason;

typedef struct {
    int width;                 /* columns, x = 1..width */
    int height;                /* rows,    y = 1..height */
    unsigned char *cells;      /* row-major, top row first, 0 = W, 1 = B */
    long long ax;              /* 1-based, x grows to the right */
    long long ay;              /* 1-based, y grows downwards */
    Direction dir;
    int has_out;
    int out;                   /* effective value; 0 when OUT is omitted */
    int has_loop;
    unsigned long long loop_limit;
} Program;

typedef struct {
    long long ax;
    long long ay;
    Direction dir;
    unsigned long long steps;
    HaltReason halt;
} RunResult;

typedef struct CellSet CellSet;

typedef struct {
    int errors;
    int warnings;
} Diag;

typedef struct {
    const char *path; /* borrowed */
    char *text;       /* owned, NUL terminated */
    size_t len;
} Source;

/* util.c */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
void fatal_oom(void);
int read_source(const char *path, Source *src, Diag *diag);
void source_free(Source *src);
void diag_error(Diag *d, const Source *src, int line, int col,
                const char *fmt, ...);
void diag_warning(Diag *d, const Source *src, int line, int col,
                  const char *fmt, ...);
const char *dir_name(Direction d);
int dir_from_char(int c, Direction *out);

/* parse.c */
int parse_program(const Source *src, Program *p, Diag *diag);
void program_free(Program *p);

/* sim.c */
CellSet *cellset_new(void);
void cellset_free(CellSet *s);
void cellset_add(CellSet *s, long long x, long long y);
void cellset_remove(CellSet *s, long long x, long long y);
int cellset_has(const CellSet *s, long long x, long long y);
size_t cellset_count(const CellSet *s);
int cellset_bbox(const CellSet *s, long long *minx, long long *miny,
                 long long *maxx, long long *maxy);

int simulate(const Program *p, int trace, FILE *out,
             CellSet **final_black, RunResult *result);
int print_result(const Program *p, const CellSet *black, RunResult result,
                 ViewMode view, int show_board, FILE *out);

/* codegen.c */
int generate_c(const Program *p, FILE *out, Diag *diag);
int build_native(const char *ant_path, const Program *p, const char *out_path,
                 const char *cc, int keep_c, Diag *diag);

#endif /* ANTC_H */
