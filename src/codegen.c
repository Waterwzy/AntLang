/*
 * codegen.c - translate an AntLang program into standalone C99, and
 * optionally drive a host C compiler to produce a native executable.
 *
 * The generated program contains its own tiny black-cell hash set, so it
 * has exactly the same semantics as the interpreter, including the
 * infinite white plane used when OUT = false.
 */
#include "antc.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* generated C source                                                  */
/* ------------------------------------------------------------------ */

static void emit_set_runtime(FILE *out)
{
    fputs(
        "typedef struct {\n"
        "    int64_t x, y;\n"
        "    unsigned char state; /* 0 empty, 1 occupied, 2 tombstone */\n"
        "} Slot;\n"
        "\n"
        "typedef struct {\n"
        "    Slot *slots;\n"
        "    size_t cap, count, tombs;\n"
        "} Set;\n"
        "\n"
        "static uint64_t mix64(uint64_t z)\n"
        "{\n"
        "    z += 0x9E3779B97F4A7C15ULL;\n"
        "    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;\n"
        "    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;\n"
        "    return z ^ (z >> 31);\n"
        "}\n"
        "\n"
        "static size_t hash_cell(int64_t x, int64_t y)\n"
        "{\n"
        "    uint64_t h = mix64((uint64_t)x + 0x9E3779B97F4A7C15ULL);\n"
        "    h ^= mix64((uint64_t)y + 0xD1B54A32D192ED03ULL);\n"
        "    return (size_t)mix64(h);\n"
        "}\n"
        "\n"
        "static void *rt_calloc(size_t n, size_t size)\n"
        "{\n"
        "    void *p;\n"
        "    if (n != 0 && size > (size_t)-1 / n) {\n"
        "        fputs(\"antc runtime: out of memory\\n\", stderr);\n"
        "        exit(1);\n"
        "    }\n"
        "    p = calloc(n ? n : 1, size ? size : 1);\n"
        "    if (p == NULL) {\n"
        "        fputs(\"antc runtime: out of memory\\n\", stderr);\n"
        "        exit(1);\n"
        "    }\n"
        "    return p;\n"
        "}\n"
        "\n"
        "static void set_insert_raw(Slot *slots, size_t cap, int64_t x,\n"
        "                           int64_t y)\n"
        "{\n"
        "    size_t mask = cap - 1;\n"
        "    size_t i = hash_cell(x, y) & mask;\n"
        "    while (slots[i].state == 1) {\n"
        "        i = (i + 1) & mask;\n"
        "    }\n"
        "    slots[i].x = x;\n"
        "    slots[i].y = y;\n"
        "    slots[i].state = 1;\n"
        "}\n"
        "\n"
        "static void set_rehash(Set *s, size_t newcap)\n"
        "{\n"
        "    Slot *old = s->slots;\n"
        "    size_t oldcap = s->cap;\n"
        "    Slot *fresh = rt_calloc(newcap, sizeof(*fresh));\n"
        "    size_t i;\n"
        "    s->slots = fresh;\n"
        "    s->cap = newcap;\n"
        "    s->count = 0;\n"
        "    s->tombs = 0;\n"
        "    for (i = 0; i < oldcap; i++) {\n"
        "        if (old[i].state == 1) {\n"
        "            set_insert_raw(fresh, newcap, old[i].x, old[i].y);\n"
        "            s->count++;\n"
        "        }\n"
        "    }\n"
        "    free(old);\n"
        "}\n"
        "\n"
        "static void set_init(Set *s)\n"
        "{\n"
        "    s->cap = 1024;\n"
        "    s->slots = rt_calloc(s->cap, sizeof(*s->slots));\n"
        "    s->count = 0;\n"
        "    s->tombs = 0;\n"
        "}\n"
        "\n"
        "static void set_free(Set *s)\n"
        "{\n"
        "    free(s->slots);\n"
        "    s->slots = NULL;\n"
        "}\n"
        "\n"
        "static void set_add(Set *s, int64_t x, int64_t y)\n"
        "{\n"
        "    size_t mask;\n"
        "    size_t i;\n"
        "    size_t first_tomb = (size_t)-1;\n"
        "\n"
        "    if ((s->count + s->tombs + 1) * 4 >= s->cap * 3) {\n"
        "        set_rehash(s, s->cap * 2);\n"
        "    }\n"
        "    mask = s->cap - 1;\n"
        "    i = hash_cell(x, y) & mask;\n"
        "    for (;;) {\n"
        "        if (s->slots[i].state == 0) {\n"
        "            size_t at = first_tomb != (size_t)-1 ? first_tomb : i;\n"
        "            s->slots[at].x = x;\n"
        "            s->slots[at].y = y;\n"
        "            s->slots[at].state = 1;\n"
        "            s->count++;\n"
        "            if (first_tomb != (size_t)-1) {\n"
        "                s->tombs--;\n"
        "            }\n"
        "            return;\n"
        "        }\n"
        "        if (s->slots[i].state == 2) {\n"
        "            if (first_tomb == (size_t)-1) {\n"
        "                first_tomb = i;\n"
        "            }\n"
        "        } else if (s->slots[i].x == x && s->slots[i].y == y) {\n"
        "            return;\n"
        "        }\n"
        "        i = (i + 1) & mask;\n"
        "    }\n"
        "}\n"
        "\n"
        "static void set_remove(Set *s, int64_t x, int64_t y)\n"
        "{\n"
        "    size_t mask = s->cap - 1;\n"
        "    size_t i = hash_cell(x, y) & mask;\n"
        "\n"
        "    for (;;) {\n"
        "        if (s->slots[i].state == 0) {\n"
        "            return;\n"
        "        }\n"
        "        if (s->slots[i].state == 1 && s->slots[i].x == x &&\n"
        "            s->slots[i].y == y) {\n"
        "            s->slots[i].state = 2;\n"
        "            s->count--;\n"
        "            s->tombs++;\n"
        "            if (s->count == 0) {\n"
        "                memset(s->slots, 0, s->cap * sizeof(*s->slots));\n"
        "                s->tombs = 0;\n"
        "            }\n"
        "            return;\n"
        "        }\n"
        "        i = (i + 1) & mask;\n"
        "    }\n"
        "}\n"
        "\n"
        "static int set_has(const Set *s, int64_t x, int64_t y)\n"
        "{\n"
        "    size_t mask = s->cap - 1;\n"
        "    size_t i = hash_cell(x, y) & mask;\n"
        "\n"
        "    for (;;) {\n"
        "        if (s->slots[i].state == 0) {\n"
        "            return 0;\n"
        "        }\n"
        "        if (s->slots[i].state == 1 && s->slots[i].x == x &&\n"
        "            s->slots[i].y == y) {\n"
        "            return 1;\n"
        "        }\n"
        "        i = (i + 1) & mask;\n"
        "    }\n"
        "}\n"
        "\n"
        "static int set_bbox(const Set *s, int64_t *minx, int64_t *miny,\n"
        "                    int64_t *maxx, int64_t *maxy)\n"
        "{\n"
        "    size_t i;\n"
        "    int found = 0;\n"
        "    for (i = 0; i < s->cap; i++) {\n"
        "        if (s->slots[i].state != 1) {\n"
        "            continue;\n"
        "        }\n"
        "        if (!found) {\n"
        "            *minx = *maxx = s->slots[i].x;\n"
        "            *miny = *maxy = s->slots[i].y;\n"
        "            found = 1;\n"
        "        } else {\n"
        "            if (s->slots[i].x < *minx) *minx = s->slots[i].x;\n"
        "            if (s->slots[i].x > *maxx) *maxx = s->slots[i].x;\n"
        "            if (s->slots[i].y < *miny) *miny = s->slots[i].y;\n"
        "            if (s->slots[i].y > *maxy) *maxy = s->slots[i].y;\n"
        "        }\n"
        "    }\n"
        "    return found;\n"
        "}\n"
        "\n",
        out);
}

static void emit_main_runtime(FILE *out)
{
    fputs(
        "static void usage(const char *argv0)\n"
        "{\n"
        "    printf(\"Usage: %s [--trace] [--view board|bbox] "
        "[--no-board]\\n\", argv0);\n"
        "}\n"
        "\n"
        "int main(int argc, char **argv)\n"
        "{\n"
        "    int trace = 0;\n"
        "    int show_board = 1;\n"
        "    int view_bbox = 0;\n"
        "    int i;\n"
        "    int x, y;\n"
        "    int dir = START_DIR;\n"
        "    int64_t ax = START_X;\n"
        "    int64_t ay = START_Y;\n"
        "    unsigned long long steps = 0;\n"
        "    int halt = 1; /* 1 = LOOP, 0 = OUT */\n"
        "    Set black;\n"
        "\n"
        "    for (i = 1; i < argc; i++) {\n"
        "        if (strcmp(argv[i], \"--trace\") == 0) {\n"
        "            trace = 1;\n"
        "        } else if (strcmp(argv[i], \"--no-board\") == 0) {\n"
        "            show_board = 0;\n"
        "        } else if (strcmp(argv[i], \"--view\") == 0) {\n"
        "            if (i + 1 >= argc) {\n"
        "                fputs(\"error: --view needs board or bbox\\n\", "
        "stderr);\n"
        "                return 2;\n"
        "            }\n"
        "            i++;\n"
        "            if (strcmp(argv[i], \"board\") == 0) {\n"
        "                view_bbox = 0;\n"
        "            } else if (strcmp(argv[i], \"bbox\") == 0) {\n"
        "                view_bbox = 1;\n"
        "            } else {\n"
        "                fprintf(stderr, \"error: unknown view '%s'\\n\",\n"
        "                        argv[i]);\n"
        "                return 2;\n"
        "            }\n"
        "        } else if (strncmp(argv[i], \"--view=\", 7) == 0) {\n"
        "            if (strcmp(argv[i] + 7, \"board\") == 0) {\n"
        "                view_bbox = 0;\n"
        "            } else if (strcmp(argv[i] + 7, \"bbox\") == 0) {\n"
        "                view_bbox = 1;\n"
        "            } else {\n"
        "                fprintf(stderr, \"error: unknown view '%s'\\n\",\n"
        "                        argv[i] + 7);\n"
        "                return 2;\n"
        "            }\n"
        "        } else if (strcmp(argv[i], \"--help\") == 0 ||\n"
        "                   strcmp(argv[i], \"-h\") == 0) {\n"
        "            usage(argv[0]);\n"
        "            return 0;\n"
        "        } else {\n"
        "            fprintf(stderr, \"error: unknown option '%s'\\n\",\n"
        "                    argv[i]);\n"
        "            return 2;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    set_init(&black);\n"
        "    for (y = 0; y < BOARD_H; y++) {\n"
        "        for (x = 0; x < BOARD_W; x++) {\n"
        "            if (INIT_ROWS[y][x] == 'B') {\n"
        "                set_add(&black, (int64_t)x + 1, (int64_t)y + 1);\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "\n"
        "    for (;;) {\n"
        "        int is_black;\n"
        "        int nd;\n"
        "        int64_t nx, ny;\n"
        "\n"
        "        if (HAS_LOOP && steps >= LOOP_LIMIT) {\n"
        "            halt = 1;\n"
        "            break;\n"
        "        }\n"
        "        is_black = set_has(&black, ax, ay);\n"
        "        nd = is_black ? (dir + 3) & 3 : (dir + 1) & 3;\n"
        "        if (is_black) {\n"
        "            set_remove(&black, ax, ay);\n"
        "        } else {\n"
        "            set_add(&black, ax, ay);\n"
        "        }\n"
        "        nx = ax + (nd == 0 ? 1 : nd == 2 ? -1 : 0);\n"
        "        ny = ay + (nd == 1 ? 1 : nd == 3 ? -1 : 0);\n"
        "        steps++;\n"
        "        if (trace) {\n"
        "            printf(\"STEP %llu: (%lld,%lld) %c, %c -> %c, %c, \"\n"
        "                   \"(%lld,%lld,%c)\\n\",\n"
        "                   steps, (long long)ax, (long long)ay,\n"
        "                   is_black ? 'B' : 'W', \"ESWN\"[dir], "
        "\"ESWN\"[nd],\n"
        "                   is_black ? 'W' : 'B', (long long)nx,\n"
        "                   (long long)ny, \"ESWN\"[nd]);\n"
        "        }\n"
        "        ax = nx;\n"
        "        ay = ny;\n"
        "        dir = nd;\n"
        "        if (OUT_FLAG &&\n"
        "            (ax < 1 || ax > BOARD_W || ay < 1 || ay > BOARD_H)) {\n"
        "            halt = 0;\n"
        "            break;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    if (show_board) {\n"
        "        int64_t x0 = 1, y0 = 1, x1 = BOARD_W, y1 = BOARD_H;\n"
        "        uint64_t vw, vh;\n"
        "        int64_t xx, yy;\n"
        "\n"
        "        if (view_bbox) {\n"
        "            int64_t bx0, by0, bx1, by1;\n"
        "            if (set_bbox(&black, &bx0, &by0, &bx1, &by1)) {\n"
        "                if (bx0 < x0) x0 = bx0;\n"
        "                if (by0 < y0) y0 = by0;\n"
        "                if (bx1 > x1) x1 = bx1;\n"
        "                if (by1 > y1) y1 = by1;\n"
        "            }\n"
        "            if (ax < x0) x0 = ax;\n"
        "            if (ay < y0) y0 = ay;\n"
        "            if (ax > x1) x1 = ax;\n"
        "            if (ay > y1) y1 = ay;\n"
        "        }\n"
        "        vw = (uint64_t)(x1 - x0 + 1);\n"
        "        vh = (uint64_t)(y1 - y0 + 1);\n"
        "        if (vw > MAX_VIEW_CELLS || vh > MAX_VIEW_CELLS ||\n"
        "            vw * vh > MAX_VIEW_CELLS) {\n"
        "            fprintf(stderr,\n"
        "                    \"error: requested view is %llu x %llu cells, \"\n"
        "                    \"which exceeds the %llu cell output limit\\n\",\n"
        "                    (unsigned long long)vw,\n"
        "                    (unsigned long long)vh,\n"
        "                    (unsigned long long)MAX_VIEW_CELLS);\n"
        "            set_free(&black);\n"
        "            return 1;\n"
        "        }\n"
        "        printf(\"BOXES = (%d, %d)\\n\", BOARD_W, BOARD_H);\n"
        "        if (view_bbox) {\n"
        "            printf(\"VIEW = (%lld, %lld, %llu, %llu)\\n\",\n"
        "                   (long long)x0, (long long)y0,\n"
        "                   (unsigned long long)vw,\n"
        "                   (unsigned long long)vh);\n"
        "        }\n"
        "        for (yy = y0; yy <= y1; yy++) {\n"
        "            for (xx = x0; xx <= x1; xx++) {\n"
        "                putchar(set_has(&black, xx, yy) ? 'B' : 'W');\n"
        "                if (xx < x1) {\n"
        "                    putchar(' ');\n"
        "                }\n"
        "            }\n"
        "            putchar('\\n');\n"
        "        }\n"
        "    }\n"
        "\n"
        "    printf(\"ANT = (%lld, %lld, %c)\\n\", (long long)ax,\n"
        "           (long long)ay, \"ESWN\"[dir]);\n"
        "    printf(\"STEPS = %llu\\n\", steps);\n"
        "    printf(\"HALT = %s\\n\", halt == 0 ? \"OUT\" : \"LOOP\");\n"
        "    set_free(&black);\n"
        "    return 0;\n"
        "}\n",
        out);
}

int generate_c(const Program *p, FILE *out, Diag *diag)
{
    int x;
    int y;

    fputs(
        "/*\n"
        " * Generated by antc " ANTC_VERSION ".  Do not edit.\n"
        " *\n"
        " * This is a standalone C99 program.  Compile it directly, e.g.\n"
        " *   cc -std=c99 -O2 thisfile.c -o program\n"
        " */\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdint.h>\n"
        "\n",
        out);
    fprintf(out, "#define BOARD_W %d\n", p->width);
    fprintf(out, "#define BOARD_H %d\n", p->height);
    fprintf(out, "#define START_X %lldLL\n", p->ax);
    fprintf(out, "#define START_Y %lldLL\n", p->ay);
    fprintf(out, "#define START_DIR %d\n", (int)p->dir);
    fprintf(out, "#define OUT_FLAG %d\n", p->out ? 1 : 0);
    fprintf(out, "#define HAS_LOOP %d\n", p->has_loop ? 1 : 0);
    fprintf(out, "#define LOOP_LIMIT %lluULL\n", p->loop_limit);
    fprintf(out, "#define MAX_VIEW_CELLS %lluULL\n",
            (unsigned long long)ANTC_MAX_VIEW_CELLS);
    fputs("\nstatic const char *const INIT_ROWS[BOARD_H] = {\n", out);
    for (y = 0; y < p->height; y++) {
        fputs("    \"", out);
        for (x = 0; x < p->width; x++) {
            fputc(p->cells[(size_t)y * (size_t)p->width + (size_t)x] ==
                          COLOR_B
                      ? 'B'
                      : 'W',
                  out);
        }
        fputs("\",\n", out);
    }
    fputs("};\n\n", out);

    emit_set_runtime(out);
    emit_main_runtime(out);

    if (ferror(out)) {
        diag_error(diag, NULL, 0, 0, "failed to write the generated C source");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* native build support                                                */
/* ------------------------------------------------------------------ */

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

static int str_ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static const char *base_name(const char *path)
{
    const char *base = path;
    const char *p;
    for (p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

static char *try_path(const char *dir, const char *name)
{
#ifdef _WIN32
    const char sep = '\\';
    const char *ext = ".exe";
#else
    const char sep = '/';
    const char *ext = "";
#endif
    size_t need;
    char *path;
    int has_ext = strchr(name, '.') != NULL;
    int has_dir = strchr(name, '/') != NULL || strchr(name, '\\') != NULL;

    if (has_dir) {
        if (file_exists(name)) {
            return xstrdup(name);
        }
#ifdef _WIN32
        if (!has_ext) {
            need = strlen(name) + strlen(ext) + 1;
            path = xmalloc(need);
            strcpy(path, name);
            strcat(path, ext);
            if (file_exists(path)) {
                return path;
            }
            free(path);
        }
#endif
        return NULL;
    }

    need = strlen(dir) + 1 + strlen(name) + strlen(ext) + 1;
    path = xmalloc(need);
    if (dir[0] == '\0') {
        snprintf(path, need, "%s", name);
    } else {
        snprintf(path, need, "%s%c%s", dir, sep, name);
    }
    if (file_exists(path)) {
        return path;
    }
    if (!has_ext) {
        strcat(path, ext);
        if (file_exists(path)) {
            return path;
        }
    }
    free(path);
    return NULL;
}

static char *find_on_path(const char *name)
{
    const char *path_env;
    char *copy;
    char *p;
#ifdef _WIN32
    const char *sep = ";";
#else
    const char *sep = ":";
#endif
    char *found = NULL;

    if (name == NULL || *name == '\0') {
        return NULL;
    }
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return try_path("", name);
    }

    path_env = getenv("PATH");
    if (path_env == NULL) {
        return NULL;
    }
    copy = xstrdup(path_env);
    p = copy;
    for (;;) {
        char *end = strstr(p, sep);
        if (end != NULL) {
            *end = '\0';
        }
        found = try_path(p, name);
        if (found != NULL) {
            break;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    free(copy);
    return found;
}

static char *find_cc(void)
{
    static const char *candidates[] = {
#ifdef _WIN32
        "cl", "gcc", "clang", "tcc", "cc"
#else
        "cc", "gcc", "clang", "tcc"
#endif
    };
    const char *env = getenv("CC");
    size_t i;
    char *found;

    if (env != NULL && *env != '\0') {
        found = find_on_path(env);
        return found != NULL ? found : xstrdup(env);
    }
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        found = find_on_path(candidates[i]);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

#ifdef _MSC_VER
#define ANTC_POPEN _popen
#define ANTC_PCLOSE _pclose
#else
#define ANTC_POPEN popen
#define ANTC_PCLOSE pclose
#endif

static int is_tcc(const char *cc)
{
    return str_ieq(base_name(cc), "tcc") || str_ieq(base_name(cc), "tcc.exe");
}

#ifdef _WIN32
static int is_msvc(const char *cc)
{
    const char *base = base_name(cc);
    return str_ieq(base, "cl") || str_ieq(base, "cl.exe") ||
           str_ieq(base, "clang-cl") || str_ieq(base, "clang-cl.exe");
}
#endif

static void remove_quiet(const char *path)
{
    if (path != NULL) {
        remove(path);
    }
}

#ifdef _WIN32
/*
 * Locate the Visual Studio developer environment without requiring the
 * caller to open a Developer Command Prompt.  Returns the path to
 * vcvars64.bat when vswhere can find a C++ toolset.
 */
static int find_vcvars(char *out, size_t out_size)
{
    const char *pf86 = getenv("ProgramFiles(x86)");
    const char *pf = getenv("ProgramFiles");
    char vswhere[1024];
    char cmd[2048];
    char line[1024];
    FILE *pipe;
    int found = 0;

    if (pf86 != NULL && *pf86 != '\0') {
        snprintf(vswhere, sizeof(vswhere),
                 "%s\\Microsoft Visual Studio\\Installer\\vswhere.exe",
                 pf86);
    } else if (pf != NULL && *pf != '\0') {
        snprintf(vswhere, sizeof(vswhere),
                 "%s\\Microsoft Visual Studio\\Installer\\vswhere.exe", pf);
    } else {
        return 0;
    }
    if (!file_exists(vswhere)) {
        return 0;
    }
    snprintf(cmd, sizeof(cmd),
             "call \"%s\" -latest -products * -requires "
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
             "-property installationPath",
             vswhere);
    pipe = ANTC_POPEN(cmd, "r");
    if (pipe == NULL) {
        return 0;
    }
    if (fgets(line, sizeof(line), pipe) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && isspace((unsigned char)line[n - 1])) {
            line[--n] = '\0';
        }
        if (n > 0) {
            snprintf(out, out_size, "%s\\VC\\Auxiliary\\Build\\vcvars64.bat",
                     line);
            found = file_exists(out);
        }
    }
    ANTC_PCLOSE(pipe);
    return found;
}
static int build_with_vcvars(const char *ant_path, const char *c_path,
                             const char *out_path, const char *cc_path,
                             const char *vcvars, int keep_c, Diag *diag)
{
    size_t obj_len = strlen(c_path) + 5;
    char *obj_path = xmalloc(obj_len);
    char *batch_path;
    char cmd[8192];
    FILE *bf;
    int rc;

    snprintf(obj_path, obj_len, "%s.obj", c_path);
    batch_path = xmalloc(strlen(out_path) + 12);
    snprintf(batch_path, strlen(out_path) + 12, "%s.build.bat", out_path);

    bf = fopen(batch_path, "wb");
    if (bf == NULL) {
        diag_error(diag, NULL, 0, 0, "cannot create '%s': %s", batch_path,
                   strerror(errno));
        free(obj_path);
        free(batch_path);
        return 0;
    }
    fprintf(bf, "@echo off\r\n");
    fprintf(bf, "call \"%s\" >nul || exit /b 1\r\n", vcvars);
    fprintf(bf, "\"%s\" /nologo /O2 /TC \"%s\" /Fe:\"%s\" /Fo:\"%s\"\r\n",
            cc_path != NULL ? cc_path : "cl", c_path, out_path, obj_path);
    if (fclose(bf) != 0) {
        diag_error(diag, NULL, 0, 0, "cannot finish writing '%s': %s",
                   batch_path, strerror(errno));
        remove_quiet(batch_path);
        free(obj_path);
        free(batch_path);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "call \"%s\"", batch_path);
    rc = system(cmd);
    if (rc != 0) {
        diag_error(diag, NULL, 0, 0,
                   "C compiler failed (exit code %d); C source kept at '%s'",
                   rc, c_path);
        fprintf(stderr, "antc: batch file kept at '%s'\n", batch_path);
        remove_quiet(obj_path);
        free(obj_path);
        free(batch_path);
        return 0;
    }

    remove_quiet(batch_path);
    remove_quiet(obj_path);
    if (keep_c) {
        fprintf(stderr, "antc: kept C source at '%s'\n", c_path);
    } else {
        remove_quiet(c_path);
    }
    free(obj_path);
    free(batch_path);
    (void)ant_path;
    return 1;
}
#endif

int build_native(const char *ant_path, const Program *p, const char *out_path,
                 const char *cc, int keep_c, Diag *diag)
{
    size_t c_len = strlen(out_path) + 7;
    char *c_path = xmalloc(c_len);
    char *obj_path = NULL;
    char *cc_path = NULL;
    FILE *f;
    char cmd[8192];
    int rc;
    int ok = 1;

    snprintf(c_path, c_len, "%s.ant.c", out_path);
    f = fopen(c_path, "wb");
    if (f == NULL) {
        diag_error(diag, NULL, 0, 0, "cannot create '%s': %s", c_path,
                   strerror(errno));
        free(c_path);
        return 0;
    }
    if (!generate_c(p, f, diag)) {
        fclose(f);
        remove_quiet(c_path);
        free(c_path);
        return 0;
    }
    if (fclose(f) != 0) {
        diag_error(diag, NULL, 0, 0, "cannot finish writing '%s': %s",
                   c_path, strerror(errno));
        remove_quiet(c_path);
        free(c_path);
        return 0;
    }

    cc_path = cc != NULL && *cc != '\0' ? find_on_path(cc) : find_cc();
#ifdef _WIN32
    if (cc_path == NULL || is_msvc(cc_path)) {
        char vcvars[1024];
        if (find_vcvars(vcvars, sizeof(vcvars))) {
            int result = build_with_vcvars(ant_path, c_path, out_path,
                                           cc_path, vcvars, keep_c, diag);
            free(cc_path);
            free(c_path);
            return result;
        }
    }
#endif
    if (cc_path == NULL) {
        diag_error(diag, NULL, 0, 0,
                   "no C compiler found; use --cc <compiler> or set CC "
                   "(while building '%s')",
                   ant_path);
        fprintf(stderr, "antc: C source kept at '%s'\n", c_path);
        free(c_path);
        return 0;
    }

#ifdef _WIN32
    if (is_msvc(cc_path)) {
        size_t obj_len = strlen(c_path) + 5;
        obj_path = xmalloc(obj_len);
        snprintf(obj_path, obj_len, "%s.obj", c_path);
        /* system() runs cmd /c; the extra outer quotes stop cmd from
         * dropping the quotes around a compiler path containing spaces. */
        rc = snprintf(cmd, sizeof(cmd),
                      "\"\"%s\" /nologo /O2 /TC \"%s\" /Fe:\"%s\" "
                      "/Fo:\"%s\"\"",
                      cc_path, c_path, out_path, obj_path);
    } else
#endif
    if (is_tcc(cc_path)) {
#ifdef _WIN32
        rc = snprintf(cmd, sizeof(cmd), "\"\"%s\" -o \"%s\" \"%s\"\"",
                      cc_path, out_path, c_path);
#else
        rc = snprintf(cmd, sizeof(cmd), "\"%s\" -o \"%s\" \"%s\"", cc_path,
                      out_path, c_path);
#endif
    } else {
#ifdef _WIN32
        rc = snprintf(cmd, sizeof(cmd),
                      "\"\"%s\" -std=c99 -O2 -o \"%s\" \"%s\"\"", cc_path,
                      out_path, c_path);
#else
        rc = snprintf(cmd, sizeof(cmd),
                      "\"%s\" -std=c99 -O2 -o \"%s\" \"%s\"", cc_path,
                      out_path, c_path);
#endif
    }
    if (rc < 0 || (size_t)rc >= sizeof(cmd)) {
        diag_error(diag, NULL, 0, 0, "compiler command line is too long");
        free(cc_path);
        free(c_path);
        free(obj_path);
        return 0;
    }

    rc = system(cmd);
    if (rc != 0) {
        diag_error(diag, NULL, 0, 0,
                   "C compiler failed (exit code %d); C source kept at '%s'",
                   rc, c_path);
        fprintf(stderr, "antc: command was: %s\n", cmd);
        ok = 0;
    }

    if (ok) {
        if (keep_c) {
            fprintf(stderr, "antc: kept C source at '%s'\n", c_path);
        } else {
            remove_quiet(c_path);
        }
    }
    remove_quiet(obj_path);
    free(cc_path);
    free(c_path);
    free(obj_path);
    return ok;
}
