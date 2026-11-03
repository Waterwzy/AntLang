/*
 * main.c - command line interface for antc.
 */
#include "antc.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void print_version(void)
{
    printf("antc %s\n", ANTC_VERSION);
}

static void print_usage(FILE *out)
{
    fputs(
        "antc " ANTC_VERSION " - AntLang compiler and interpreter\n"
        "\n"
        "Usage:\n"
        "  antc run   <file.ant> [--trace] [--view board|bbox] "
        "[--no-board]\n"
        "  antc check <file.ant>\n"
        "  antc emit  <file.ant> [-o <file.c>]\n"
        "  antc build <file.ant> [-o <program>] [--cc <compiler>] "
        "[--keep-c]\n"
        "  antc <file.ant>              shorthand for \"antc run\"\n"
        "  antc help | --help\n"
        "  antc version | --version\n"
        "\n"
        "Language:\n"
        "  BOXES = (width, height);\n"
        "  ANT   = (x, y, E|N|W|S);\n"
        "  OUT   = true|false;          optional, default false\n"
        "  LOOP  = max_steps;           optional, non-negative integer\n"
        "  BOARD =\n"
        "  B W B ...\n"
        "\n"
        "  Coordinates are 1-based with (1,1) at the top-left.  E, S, W,\n"
        "  N mean right, down, left, up.  At least one of OUT or LOOP is\n"
        "  required; OUT = false requires LOOP.  A white cell turns the\n"
        "  ant right, a black cell turns it left, the current cell flips,\n"
        "  and then the ant moves one cell forward.\n"
        "\n"
        "Output:\n"
        "  BOXES, the final board, ANT, STEPS and HALT.  --trace prints\n"
        "  every step first.  --view bbox shows the smallest rectangle\n"
        "  containing the declared board, every black cell and the ant.\n"
        "\n"
        "Exit status: 0 success, 1 source/runtime error, 2 usage error.\n",
        out);
}

static int is_help(const char *s)
{
    return strcmp(s, "help") == 0 || strcmp(s, "--help") == 0 ||
           strcmp(s, "-h") == 0;
}

static int is_version(const char *s)
{
    return strcmp(s, "version") == 0 || strcmp(s, "--version") == 0 ||
           strcmp(s, "-V") == 0;
}

static int parse_view(const char *s, ViewMode *view)
{
    if (strcmp(s, "board") == 0) {
        *view = VIEW_BOARD;
        return 1;
    }
    if (strcmp(s, "bbox") == 0) {
        *view = VIEW_BBOX;
        return 1;
    }
    return 0;
}

static int has_suffix_ci(const char *s, const char *suffix)
{
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    size_t i;
    if (m > n) {
        return 0;
    }
    for (i = 0; i < m; i++) {
        if (tolower((unsigned char)s[n - m + i]) !=
            tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static char *default_program_path(const char *input)
{
    size_t len = strlen(input);
#ifdef _WIN32
    const char *suffix = ".exe";
#else
    const char *suffix = "";
#endif
    char *out;

    if (has_suffix_ci(input, ".ant")) {
        len -= 4;
    }
    out = xmalloc(len + strlen(suffix) + 1);
    memcpy(out, input, len);
    strcpy(out + len, suffix);
    return out;
}

#ifdef _WIN32
static char *ensure_exe_suffix(const char *path)
{
    const char *last_sep = strrchr(path, '\\');
    const char *last_slash = strrchr(path, '/');
    const char *base;
    const char *dot;
    size_t len;
    char *out;

    if (last_slash != NULL && (last_sep == NULL || last_slash > last_sep)) {
        last_sep = last_slash;
    }
    base = last_sep != NULL ? last_sep + 1 : path;
    dot = strrchr(base, '.');
    if (dot != NULL && dot != base) {
        return xstrdup(path);
    }
    len = strlen(path);
    out = xmalloc(len + 5);
    memcpy(out, path, len);
    strcpy(out + len, ".exe");
    return out;
}
#endif

static int load_program(const char *path, Source *src, Program *prog,
                        Diag *diag)
{
    if (!read_source(path, src, diag)) {
        return 0;
    }
    if (!parse_program(src, prog, diag)) {
        return 0;
    }
    return 1;
}

static int cmd_run(int argc, char **argv, int first_arg)
{
    const char *file = NULL;
    ViewMode view = VIEW_BOARD;
    int trace = 0;
    int show_board = 1;
    int i;
    Source src;
    Program prog;
    Diag diag;
    CellSet *black = NULL;
    RunResult result;

    for (i = first_arg; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--no-board") == 0) {
            show_board = 0;
        } else if (strcmp(argv[i], "--view") == 0) {
            if (i + 1 >= argc || !parse_view(argv[++i], &view)) {
                fprintf(stderr, "antc: --view needs 'board' or 'bbox'\n");
                return 2;
            }
        } else if (strncmp(argv[i], "--view=", 7) == 0) {
            if (!parse_view(argv[i] + 7, &view)) {
                fprintf(stderr, "antc: unknown view '%s'\n", argv[i] + 7);
                return 2;
            }
        } else if (is_help(argv[i])) {
            print_usage(stdout);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "antc: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (file == NULL) {
            file = argv[i];
        } else {
            fprintf(stderr, "antc: unexpected argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if (file == NULL) {
        fprintf(stderr, "antc: run needs a source file\n");
        return 2;
    }

    memset(&diag, 0, sizeof(diag));
    if (!load_program(file, &src, &prog, &diag)) {
        if (src.text != NULL) {
            source_free(&src);
        }
        return 1;
    }
    if (simulate(&prog, trace, stdout, &black, &result) != 0) {
        program_free(&prog);
        source_free(&src);
        return 1;
    }
    i = print_result(&prog, black, result, view, show_board, stdout);
    cellset_free(black);
    program_free(&prog);
    source_free(&src);
    return i == 0 ? 0 : 1;
}

static int cmd_check(int argc, char **argv)
{
    const char *file = NULL;
    int i;
    Source src;
    Program prog;
    Diag diag;

    for (i = 2; i < argc; i++) {
        if (is_help(argv[i])) {
            print_usage(stdout);
            return 0;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "antc: unknown option '%s'\n", argv[i]);
            return 2;
        }
        if (file != NULL) {
            fprintf(stderr, "antc: unexpected argument '%s'\n", argv[i]);
            return 2;
        }
        file = argv[i];
    }
    if (file == NULL) {
        fprintf(stderr, "antc: check needs a source file\n");
        return 2;
    }

    memset(&diag, 0, sizeof(diag));
    memset(&src, 0, sizeof(src));
    if (!load_program(file, &src, &prog, &diag)) {
        if (src.text != NULL) {
            source_free(&src);
        }
        return 1;
    }
    printf("%s: OK: BOXES=%dx%d, ANT=(%lld,%lld,%s), OUT=%s, LOOP=",
           file, prog.width, prog.height, prog.ax, prog.ay,
           dir_name(prog.dir), prog.out ? "true" : "false");
    if (prog.has_loop) {
        printf("%llu\n", prog.loop_limit);
    } else {
        printf("none\n");
    }
    program_free(&prog);
    source_free(&src);
    return 0;
}

static int cmd_emit(int argc, char **argv)
{
    const char *file = NULL;
    const char *out_path = NULL;
    int i;
    Source src;
    Program prog;
    Diag diag;
    FILE *out;
    int ok;

    for (i = 2; i < argc; i++) {
        if (is_help(argv[i])) {
            print_usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "antc: -o needs a file name\n");
                return 2;
            }
            out_path = argv[++i];
        } else if (strncmp(argv[i], "-o", 2) == 0 && argv[i][2] != '\0') {
            out_path = argv[i] + 2;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "antc: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (file == NULL) {
            file = argv[i];
        } else {
            fprintf(stderr, "antc: unexpected argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if (file == NULL) {
        fprintf(stderr, "antc: emit needs a source file\n");
        return 2;
    }

    memset(&diag, 0, sizeof(diag));
    memset(&src, 0, sizeof(src));
    if (!load_program(file, &src, &prog, &diag)) {
        if (src.text != NULL) {
            source_free(&src);
        }
        return 1;
    }

    if (out_path != NULL) {
        out = fopen(out_path, "wb");
        if (out == NULL) {
            fprintf(stderr, "antc: cannot create '%s': %s\n", out_path,
                    strerror(errno));
            program_free(&prog);
            source_free(&src);
            return 1;
        }
    } else {
        out = stdout;
    }
    ok = generate_c(&prog, out, &diag);
    if (out != stdout && fclose(out) != 0) {
        ok = 0;
        fprintf(stderr, "antc: cannot finish writing '%s': %s\n", out_path,
                strerror(errno));
    }
    program_free(&prog);
    source_free(&src);
    return ok ? 0 : 1;
}

static int cmd_build(int argc, char **argv)
{
    const char *file = NULL;
    const char *out_path = NULL;
    const char *cc = NULL;
    int keep_c = 0;
    int i;
    Source src;
    Program prog;
    Diag diag;
    char *default_out = NULL;
    char *normalized_out = NULL;
    int ok;

    for (i = 2; i < argc; i++) {
        if (is_help(argv[i])) {
            print_usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "antc: -o needs a file name\n");
                return 2;
            }
            out_path = argv[++i];
        } else if (strncmp(argv[i], "-o", 2) == 0 && argv[i][2] != '\0') {
            out_path = argv[i] + 2;
        } else if (strcmp(argv[i], "--cc") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "antc: --cc needs a compiler\n");
                return 2;
            }
            cc = argv[++i];
        } else if (strncmp(argv[i], "--cc=", 5) == 0) {
            cc = argv[i] + 5;
        } else if (strcmp(argv[i], "--keep-c") == 0) {
            keep_c = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "antc: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (file == NULL) {
            file = argv[i];
        } else {
            fprintf(stderr, "antc: unexpected argument '%s'\n", argv[i]);
            return 2;
        }
    }
    if (file == NULL) {
        fprintf(stderr, "antc: build needs a source file\n");
        return 2;
    }

    memset(&diag, 0, sizeof(diag));
    memset(&src, 0, sizeof(src));
    if (!load_program(file, &src, &prog, &diag)) {
        if (src.text != NULL) {
            source_free(&src);
        }
        return 1;
    }
    if (out_path == NULL) {
        default_out = default_program_path(file);
        out_path = default_out;
    }
#ifdef _WIN32
    else {
        normalized_out = ensure_exe_suffix(out_path);
        out_path = normalized_out;
    }
#endif

    ok = build_native(file, &prog, out_path, cc, keep_c, &diag);
    if (ok) {
        printf("built %s\n", out_path);
    }
    free(normalized_out);
    free(default_out);
    program_free(&prog);
    source_free(&src);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *cmd;

    if (argc < 2) {
        print_usage(stdout);
        return 0;
    }
    cmd = argv[1];
    if (is_help(cmd)) {
        print_usage(stdout);
        return 0;
    }
    if (is_version(cmd)) {
        print_version();
        return 0;
    }
    if (strcmp(cmd, "run") == 0) {
        return cmd_run(argc, argv, 2);
    }
    if (strcmp(cmd, "check") == 0) {
        return cmd_check(argc, argv);
    }
    if (strcmp(cmd, "emit") == 0) {
        return cmd_emit(argc, argv);
    }
    if (strcmp(cmd, "build") == 0) {
        return cmd_build(argc, argv);
    }
    if (cmd[0] == '-') {
        fprintf(stderr, "antc: unknown option '%s'\n", cmd);
        fprintf(stderr, "Try 'antc --help'.\n");
        return 2;
    }
    return cmd_run(argc, argv, 1);
}
