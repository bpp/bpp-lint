#include "lex.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_LINE_CAP 64

static char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *bpp_strdup(const char *s) {
    if (!s) return NULL;
    return xstrndup(s, strlen(s));
}

char *bpp_strdup_lower(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (!p) return NULL;
    for (size_t i = 0; i < n; i++) {
        p[i] = (char) tolower((unsigned char) s[i]);
    }
    p[n] = '\0';
    return p;
}

int bpp_strieq(const char *a, const char *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

int bpp_is_blank(const char *s) {
    if (!s) return 1;
    while (*s) {
        if (!isspace((unsigned char) *s)) return 0;
        s++;
    }
    return 1;
}

/* min of three */
static int min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

int bpp_levenshtein(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    if (la == 0) return (int) lb;
    if (lb == 0) return (int) la;

    /* Allocate two rows to keep memory O(min(la,lb)). */
    int *prev = malloc((lb + 1) * sizeof(int));
    int *cur  = malloc((lb + 1) * sizeof(int));
    if (!prev || !cur) { free(prev); free(cur); return -1; }

    for (size_t j = 0; j <= lb; j++) prev[j] = (int) j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = (int) i;
        int ca = tolower((unsigned char) a[i - 1]);
        for (size_t j = 1; j <= lb; j++) {
            int cb = tolower((unsigned char) b[j - 1]);
            int cost = (ca == cb) ? 0 : 1;
            cur[j] = min3(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost);
        }
        int *tmp = prev; prev = cur; cur = tmp;
    }
    int d = prev[lb];
    free(prev); free(cur);
    return d;
}

/* Find a comment marker (* or #) outside the quoted/special context.
 * BPP treats both as line-tail comments. Returns pointer to marker or NULL.
 *
 * Special-case: BPP uses '*' as a comment but also as a delimiter in some
 * value parsers. For purposes of stripping line-trailing comments, we treat
 * any '*' or '#' as a comment start. (BPP itself does the same; see
 * cfile.c `is_emptyline` and `get_token`.)
 */
static const char *find_comment(const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == '*' || *p == '#') return p;
    }
    return NULL;
}

int bpp_tokenise_value(const char *value, char **out_tokens, int max_tokens) {
    if (!value) return 0;
    int n_stored = 0;
    int n_total  = 0;
    const char *p = value;

    while (*p) {
        while (*p && isspace((unsigned char) *p)) p++;
        if (!*p) break;
        if (*p == '*' || *p == '#') break;  /* trailing comment */
        const char *start = p;
        while (*p && !isspace((unsigned char) *p) && *p != '*' && *p != '#') p++;
        size_t len = (size_t)(p - start);
        if (n_stored < max_tokens) {
            char *tok = xstrndup(start, len);
            if (!tok) return -1;
            out_tokens[n_stored++] = tok;
        }
        n_total++;
    }
    /* return total count, but caller only sees n_stored tokens */
    (void) n_total;
    return n_stored;
}

void bpp_tokens_free(char **tokens, int n) {
    for (int i = 0; i < n; i++) free(tokens[i]);
}

/* Strip trailing whitespace in place. */
static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char) s[n - 1])) s[--n] = '\0';
}

/* Skip leading whitespace; return pointer. */
static const char *ltrim(const char *s) {
    while (*s && isspace((unsigned char) *s)) s++;
    return s;
}

/* Parse a single raw line into a bpp_line_t.
 * Mutates the input by populating fields; does not own raw initially, but
 * after this call all fields point into freshly-allocated buffers. */
static int parse_line(bpp_line_t *line, const char *raw, int lineno) {
    line->lineno   = lineno;
    line->key      = NULL;
    line->key_orig = NULL;
    line->value    = NULL;
    line->key_col  = 0;
    line->eq_col   = 0;
    line->val_col  = 0;
    line->raw      = bpp_strdup(raw);
    if (!line->raw) return -1;
    rtrim(line->raw);

    /* Find the '=' before any comment marker. */
    const char *eq = NULL;
    for (const char *p = line->raw; *p; p++) {
        if (*p == '*' || *p == '#') break;
        if (*p == '=') { eq = p; break; }
    }
    if (!eq) {
        /* No assignment; might be a continuation line for species&tree /
         * migration / etc. Leave key/value NULL so callers can decide. */
        return 0;
    }

    /* Extract LHS (keyword). */
    const char *ls = ltrim(line->raw);
    if (ls >= eq) return 0;  /* '=' with no key -> ignore */

    /* Strip trailing whitespace from key. */
    const char *le = eq;
    while (le > ls && isspace((unsigned char) le[-1])) le--;
    size_t klen = (size_t)(le - ls);
    if (klen == 0) return 0;

    line->key_orig = xstrndup(ls, klen);
    if (!line->key_orig) return -1;
    line->key = bpp_strdup_lower(line->key_orig);
    if (!line->key) return -1;
    line->key_col = (int)(ls - line->raw) + 1;
    line->eq_col  = (int)(eq - line->raw) + 1;

    /* Extract value: everything after '=' up to a comment. */
    const char *vs = ltrim(eq + 1);
    if (!*vs || *vs == '*' || *vs == '#') {
        line->value = bpp_strdup("");
        if (!line->value) return -1;
        return 0;
    }
    const char *comm = find_comment(vs);
    const char *ve   = comm ? comm : vs + strlen(vs);
    while (ve > vs && isspace((unsigned char) ve[-1])) ve--;
    line->value   = xstrndup(vs, (size_t)(ve - vs));
    if (!line->value) return -1;
    line->val_col = (int)(vs - line->raw) + 1;
    return 0;
}

int bpp_file_load(bpp_file_t *f, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    f->path  = bpp_strdup(path);
    f->cap   = INITIAL_LINE_CAP;
    f->n     = 0;
    f->lines = calloc(f->cap, sizeof(bpp_line_t));
    if (!f->path || !f->lines) { fclose(fp); return -1; }

    size_t bufcap = 1024;
    char  *buf    = malloc(bufcap);
    if (!buf) { fclose(fp); return -1; }

    int lineno = 0;
    while (1) {
        size_t used = 0;
        int    done = 0;
        while (1) {
            if (used + 1 >= bufcap) {
                bufcap *= 2;
                char *nb = realloc(buf, bufcap);
                if (!nb) { free(buf); fclose(fp); return -1; }
                buf = nb;
            }
            int c = fgetc(fp);
            if (c == EOF) {
                done = 1;
                break;
            }
            if (c == '\n') break;
            buf[used++] = (char) c;
        }
        buf[used] = '\0';
        if (done && used == 0) break;

        lineno++;
        if (f->n >= f->cap) {
            size_t nc = f->cap * 2;
            bpp_line_t *nl = realloc(f->lines, nc * sizeof(bpp_line_t));
            if (!nl) { free(buf); fclose(fp); return -1; }
            memset(nl + f->cap, 0, (nc - f->cap) * sizeof(bpp_line_t));
            f->lines = nl;
            f->cap = nc;
        }
        if (parse_line(&f->lines[f->n], buf, lineno) != 0) {
            free(buf); fclose(fp); return -1;
        }
        f->n++;

        if (done) break;
    }

    free(buf);
    fclose(fp);
    return 0;
}

void bpp_file_free(bpp_file_t *f) {
    if (!f) return;
    for (size_t i = 0; i < f->n; i++) {
        free(f->lines[i].raw);
        free(f->lines[i].key);
        free(f->lines[i].key_orig);
        free(f->lines[i].value);
    }
    free(f->lines);
    free(f->path);
    f->lines = NULL;
    f->path  = NULL;
    f->n = f->cap = 0;
}
