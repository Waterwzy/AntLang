/*
 * util.c - memory, file input and diagnostics.
 */
#include "antc.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void fatal_oom(void)
{
    fputs("antc: out of memory\n", stderr);
    exit(1);
}

void *xmalloc(size_t n)
{
    void *p;
    if (n == 0) {
        n = 1;
    }
    p = malloc(n);
    if (p == NULL) {
        fatal_oom();
    }
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p;
    if (n != 0 && size > (size_t)-1 / n) {
        fatal_oom();
    }
    p = calloc(n ? n : 1, size ? size : 1);
    if (p == NULL) {
        fatal_oom();
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q;
    if (n == 0) {
        n = 1;
    }
    q = realloc(p, n);
    if (q == NULL) {
        fatal_oom();
    }
    return q;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

int read_source(const char *path, Source *src, Diag *diag)
{
    FILE *f;
    long size;
    size_t got;
    char *buf;

    memset(src, 0, sizeof(*src));
    src->path = path;

    f = fopen(path, "rb");
    if (f == NULL) {
        diag_error(diag, NULL, 0, 0, "cannot open '%s': %s", path,
                   strerror(errno));
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        diag_error(diag, NULL, 0, 0, "cannot seek '%s': %s", path,
                   strerror(errno));
        fclose(f);
        return 0;
    }
    size = ftell(f);
    if (size < 0) {
        diag_error(diag, NULL, 0, 0, "cannot determine size of '%s': %s",
                   path, strerror(errno));
        fclose(f);
        return 0;
    }
    if ((unsigned long)size > ANTC_MAX_SOURCE) {
        diag_error(diag, NULL, 0, 0,
                   "'%s' is too large (limit is %u bytes)", path,
                   (unsigned)ANTC_MAX_SOURCE);
        fclose(f);
        return 0;
    }
    rewind(f);
    buf = xmalloc((size_t)size + 1);
    got = fread(buf, 1, (size_t)size, f);
    if (got != (size_t)size) {
        diag_error(diag, NULL, 0, 0, "cannot read '%s': %s", path,
                   ferror(f) ? strerror(errno) : "unexpected end of file");
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    buf[got] = '\0';

    /* Accept a UTF-8 BOM; AntLang source is otherwise plain ASCII. */
    if (got >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, got - 2); /* includes the NUL */
        got -= 3;
    }

    if (memchr(buf, '\0', got) != NULL) {
        diag_error(diag, NULL, 0, 0,
                   "'%s' contains NUL bytes; AntLang sources must be text",
                   path);
        free(buf);
        return 0;
    }

    src->text = buf;
    src->len = got;
    return 1;
}

void source_free(Source *src)
{
    if (src != NULL) {
        free(src->text);
        src->text = NULL;
        src->len = 0;
    }
}

/* Find one 1-based line in the source.  Only used on the error path. */
static int find_line(const Source *src, int line, const char **start,
                     size_t *len)
{
    size_t pos = 0;
    int n = 1;

    if (line < 1) {
        return 0;
    }
    while (pos <= src->len) {
        size_t begin = pos;
        size_t end;
        while (pos < src->len && src->text[pos] != '\n') {
            pos++;
        }
        end = pos;
        if (end > begin && src->text[end - 1] == '\r') {
            end--;
        }
        if (n == line) {
            *start = src->text + begin;
            *len = end - begin;
            return 1;
        }
        if (pos >= src->len) {
            break;
        }
        pos++; /* skip '\n' */
        n++;
    }
    return 0;
}

static void print_diag(const char *kind, Diag *diag, const Source *src,
                       int line, int col, const char *fmt, va_list ap)
{
    if (diag != NULL) {
        if (strcmp(kind, "error") == 0) {
            diag->errors++;
        } else {
            diag->warnings++;
        }
    }

    if (src != NULL && src->path != NULL && line > 0) {
        fprintf(stderr, "%s:%d:%d: %s: ", src->path, line,
                col > 0 ? col : 1, kind);
    } else if (src != NULL && src->path != NULL) {
        fprintf(stderr, "%s: %s: ", src->path, kind);
    } else if (line > 0) {
        fprintf(stderr, "line %d: %s: ", line, kind);
    } else {
        fprintf(stderr, "antc: %s: ", kind);
    }
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);

    if (src != NULL && src->text != NULL && line > 0) {
        const char *start = NULL;
        size_t len = 0;
        if (find_line(src, line, &start, &len)) {
            size_t shown = len;
            size_t skip = 0;
            size_t caret;
            if (col < 1) {
                col = 1;
            }
            /* Keep the caret visible even for very long BOARD lines. */
            if ((size_t)col > 81) {
                skip = (size_t)col - 41;
            }
            if (skip > shown) {
                skip = shown;
            }
            if (shown - skip > 120) {
                shown = skip + 120;
            }
            fputs("  ", stderr);
            if (skip > 0) {
                fputs("...", stderr);
            }
            fwrite(start + skip, 1, shown - skip, stderr);
            fputs("\n  ", stderr);
            if (skip > 0) {
                fputs("   ", stderr);
            }
            caret = (size_t)col - 1;
            if (caret < skip) {
                caret = 0;
            } else {
                caret -= skip;
            }
            while (caret-- > 0) {
                fputc(' ', stderr);
            }
            fputs("^\n", stderr);
        }
    }
}

void diag_error(Diag *d, const Source *src, int line, int col,
                const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_diag("error", d, src, line, col, fmt, ap);
    va_end(ap);
}

void diag_warning(Diag *d, const Source *src, int line, int col,
                  const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_diag("warning", d, src, line, col, fmt, ap);
    va_end(ap);
}

const char *dir_name(Direction d)
{
    switch (d) {
    case DIR_E:
        return "E";
    case DIR_S:
        return "S";
    case DIR_W:
        return "W";
    case DIR_N:
        return "N";
    }
    return "?";
}

int dir_from_char(int c, Direction *out)
{
    switch (toupper((unsigned char)c)) {
    case 'E':
        *out = DIR_E;
        return 1;
    case 'S':
        *out = DIR_S;
        return 1;
    case 'W':
        *out = DIR_W;
        return 1;
    case 'N':
        *out = DIR_N;
        return 1;
    default:
        return 0;
    }
}
