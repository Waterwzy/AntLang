/*
 * sim.c - the AntLang execution engine and the final-board renderer.
 *
 * The board is finite only when it is displayed.  Internally we store the
 * set of black cells, so OUT=false can treat the plane outside BOXES as an
 * endless field of white cells without allocating it.
 */
#include "antc.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    long long x;
    long long y;
    unsigned char state; /* 0 = empty, 1 = occupied, 2 = tombstone */
} Slot;

struct CellSet {
    Slot *slots;
    size_t cap;   /* always a power of two */
    size_t count; /* occupied slots */
    size_t tombs; /* tombstone slots */
};

#define INITIAL_CAP 1024u

static uint64_t mix64(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static size_t hash_cell(long long x, long long y)
{
    uint64_t h = mix64((uint64_t)x + 0x9E3779B97F4A7C15ULL);
    h ^= mix64((uint64_t)y + 0xD1B54A32D192ED03ULL);
    return (size_t)mix64(h);
}

static void set_insert_raw(Slot *slots, size_t cap, long long x, long long y)
{
    size_t mask = cap - 1;
    size_t i = hash_cell(x, y) & mask;
    while (slots[i].state == 1) {
        i = (i + 1) & mask;
    }
    slots[i].x = x;
    slots[i].y = y;
    slots[i].state = 1;
}

static void cellset_rehash(CellSet *s, size_t newcap)
{
    Slot *old = s->slots;
    size_t oldcap = s->cap;
    Slot *fresh;
    size_t i;

    fresh = xcalloc(newcap, sizeof(*fresh));
    s->slots = fresh;
    s->cap = newcap;
    s->count = 0;
    s->tombs = 0;
    for (i = 0; i < oldcap; i++) {
        if (old[i].state == 1) {
            set_insert_raw(fresh, newcap, old[i].x, old[i].y);
            s->count++;
        }
    }
    free(old);
}

CellSet *cellset_new(void)
{
    CellSet *s = xmalloc(sizeof(*s));
    s->slots = xcalloc(INITIAL_CAP, sizeof(*s->slots));
    s->cap = INITIAL_CAP;
    s->count = 0;
    s->tombs = 0;
    return s;
}

void cellset_free(CellSet *s)
{
    if (s != NULL) {
        free(s->slots);
        free(s);
    }
}

void cellset_add(CellSet *s, long long x, long long y)
{
    size_t mask;
    size_t i;
    size_t first_tomb = (size_t)-1;

    if ((s->count + s->tombs + 1) * 4 >= s->cap * 3) {
        cellset_rehash(s, s->cap * 2);
    }
    mask = s->cap - 1;
    i = hash_cell(x, y) & mask;
    for (;;) {
        if (s->slots[i].state == 0) {
            size_t at = first_tomb != (size_t)-1 ? first_tomb : i;
            s->slots[at].x = x;
            s->slots[at].y = y;
            s->slots[at].state = 1;
            s->count++;
            if (first_tomb != (size_t)-1) {
                s->tombs--;
            }
            return;
        }
        if (s->slots[i].state == 2) {
            if (first_tomb == (size_t)-1) {
                first_tomb = i;
            }
        } else if (s->slots[i].x == x && s->slots[i].y == y) {
            return; /* already black */
        }
        i = (i + 1) & mask;
    }
}

void cellset_remove(CellSet *s, long long x, long long y)
{
    size_t mask = s->cap - 1;
    size_t i = hash_cell(x, y) & mask;

    for (;;) {
        if (s->slots[i].state == 0) {
            return;
        }
        if (s->slots[i].state == 1 &&
            s->slots[i].x == x && s->slots[i].y == y) {
            s->slots[i].state = 2;
            s->count--;
            s->tombs++;
            if (s->count == 0) {
                /* All useful entries are gone; drop the tombstones too. */
                memset(s->slots, 0, s->cap * sizeof(*s->slots));
                s->tombs = 0;
            }
            return;
        }
        i = (i + 1) & mask;
    }
}

int cellset_has(const CellSet *s, long long x, long long y)
{
    size_t mask = s->cap - 1;
    size_t i = hash_cell(x, y) & mask;

    for (;;) {
        if (s->slots[i].state == 0) {
            return 0;
        }
        if (s->slots[i].state == 1 &&
            s->slots[i].x == x && s->slots[i].y == y) {
            return 1;
        }
        i = (i + 1) & mask;
    }
}

size_t cellset_count(const CellSet *s)
{
    return s->count;
}

int cellset_bbox(const CellSet *s, long long *minx, long long *miny,
                 long long *maxx, long long *maxy)
{
    size_t i;
    int found = 0;

    for (i = 0; i < s->cap; i++) {
        if (s->slots[i].state != 1) {
            continue;
        }
        if (!found) {
            *minx = *maxx = s->slots[i].x;
            *miny = *maxy = s->slots[i].y;
            found = 1;
        } else {
            if (s->slots[i].x < *minx) {
                *minx = s->slots[i].x;
            }
            if (s->slots[i].x > *maxx) {
                *maxx = s->slots[i].x;
            }
            if (s->slots[i].y < *miny) {
                *miny = s->slots[i].y;
            }
            if (s->slots[i].y > *maxy) {
                *maxy = s->slots[i].y;
            }
        }
    }
    return found;
}

static const int DX[4] = {1, 0, -1, 0};
static const int DY[4] = {0, 1, 0, -1};

int simulate(const Program *p, int trace, FILE *out,
             CellSet **final_black, RunResult *result)
{
    CellSet *black = cellset_new();
    long long ax = p->ax;
    long long ay = p->ay;
    Direction dir = p->dir;
    unsigned long long steps = 0;
    HaltReason halt = HALT_LOOP;
    int y;

    for (y = 0; y < p->height; y++) {
        int x;
        for (x = 0; x < p->width; x++) {
            if (p->cells[(size_t)y * (size_t)p->width + (size_t)x] ==
                COLOR_B) {
                cellset_add(black, x + 1, y + 1);
            }
        }
    }

    for (;;) {
        int is_black;
        Direction nd;
        long long nx;
        long long ny;

        if (p->has_loop && steps >= p->loop_limit) {
            halt = HALT_LOOP;
            break;
        }

        is_black = cellset_has(black, ax, ay);
        nd = is_black ? (Direction)((dir + 3) & 3)
                      : (Direction)((dir + 1) & 3);
        if (is_black) {
            cellset_remove(black, ax, ay);
        } else {
            cellset_add(black, ax, ay);
        }

        nx = ax + DX[nd];
        ny = ay + DY[nd];
        steps++;

        if (trace) {
            fprintf(out,
                    "STEP %llu: (%lld,%lld) %c, %s -> %s, %c, "
                    "(%lld,%lld,%s)\n",
                    steps, ax, ay, is_black ? 'B' : 'W', dir_name(dir),
                    dir_name(nd), is_black ? 'W' : 'B', nx, ny,
                    dir_name(nd));
        }

        ax = nx;
        ay = ny;
        dir = nd;

        if (p->out && (ax < 1 || ax > p->width ||
                       ay < 1 || ay > p->height)) {
            halt = HALT_OUT;
            break;
        }
    }

    result->ax = ax;
    result->ay = ay;
    result->dir = dir;
    result->steps = steps;
    result->halt = halt;
    *final_black = black;
    return 0;
}

int print_result(const Program *p, const CellSet *black, RunResult result,
                 ViewMode view, int show_board, FILE *out)
{
    long long x0 = 1;
    long long y0 = 1;
    long long x1 = p->width;
    long long y1 = p->height;
    unsigned long long vw;
    unsigned long long vh;
    long long x;
    long long y;

    if (show_board && view == VIEW_BBOX) {
        long long bx0, by0, bx1, by1;
        if (cellset_bbox(black, &bx0, &by0, &bx1, &by1)) {
            if (bx0 < x0) {
                x0 = bx0;
            }
            if (by0 < y0) {
                y0 = by0;
            }
            if (bx1 > x1) {
                x1 = bx1;
            }
            if (by1 > y1) {
                y1 = by1;
            }
        }
        if (result.ax < x0) {
            x0 = result.ax;
        }
        if (result.ay < y0) {
            y0 = result.ay;
        }
        if (result.ax > x1) {
            x1 = result.ax;
        }
        if (result.ay > y1) {
            y1 = result.ay;
        }
    }

    vw = (unsigned long long)(x1 - x0 + 1);
    vh = (unsigned long long)(y1 - y0 + 1);
    if (show_board &&
        (vw > ANTC_MAX_VIEW_CELLS || vh > ANTC_MAX_VIEW_CELLS ||
         vw * vh > ANTC_MAX_VIEW_CELLS)) {
        fprintf(stderr,
                "antc: requested view is %llu x %llu cells, which exceeds "
                "the %llu cell output limit; use --view board\n",
                vw, vh, (unsigned long long)ANTC_MAX_VIEW_CELLS);
        return -1;
    }

    if (show_board) {
        fprintf(out, "BOXES = (%d, %d)\n", p->width, p->height);
        if (view == VIEW_BBOX) {
            fprintf(out, "VIEW = (%lld, %lld, %llu, %llu)\n", x0, y0, vw,
                    vh);
        }
        for (y = y0; y <= y1; y++) {
            for (x = x0; x <= x1; x++) {
                fputc(cellset_has(black, x, y) ? 'B' : 'W', out);
                if (x < x1) {
                    fputc(' ', out);
                }
            }
            fputc('\n', out);
        }
    }

    fprintf(out, "ANT = (%lld, %lld, %s)\n", result.ax, result.ay,
            dir_name(result.dir));
    fprintf(out, "STEPS = %llu\n", result.steps);
    fprintf(out, "HALT = %s\n", result.halt == HALT_OUT ? "OUT" : "LOOP");
    if (ferror(out)) {
        return -1;
    }
    return 0;
}
