#define _POSIX_C_SOURCE 200809L

#include "lint.h"
#include "lex.h"
#include "keywords.h"

#include <ctype.h>
#include <strings.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- diagnostic list management ---------- */

static int diag_reserve(bpp_diag_list_t *list, size_t need) {
    if (list->n + need <= list->cap) return 0;
    size_t nc = list->cap ? list->cap : 8;
    while (nc < list->n + need) nc *= 2;
    bpp_diagnostic_t *p = realloc(list->items, nc * sizeof(*p));
    if (!p) return -1;
    memset(p + list->cap, 0, (nc - list->cap) * sizeof(*p));
    list->items = p;
    list->cap   = nc;
    return 0;
}

/* Note: format-string args are forwarded via vsnprintf; this function is
 * variadic but format-checking is omitted because gcc cannot follow the
 * indirection through bpp_strdup. */
static char *xasprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return NULL; }
    char *buf = malloc((size_t) n + 1);
    if (!buf) { va_end(ap2); return NULL; }
    vsnprintf(buf, (size_t) n + 1, fmt, ap2);
    va_end(ap2);
    return buf;
}

static void emit(bpp_diag_list_t *list, bpp_severity_t sev, int lineno, int col,
                 const char *code, char *msg, char *suggestion,
                 char *replacement_line, int replacement_lineno) {
    if (diag_reserve(list, 1) != 0) {
        free(msg); free(suggestion); free(replacement_line);
        return;
    }
    bpp_diagnostic_t *d = &list->items[list->n++];
    d->severity            = sev;
    d->lineno              = lineno;
    d->column              = col;
    d->code                = bpp_strdup(code);
    d->message             = msg;
    d->suggestion          = suggestion;
    d->replacement_line    = replacement_line;
    d->replacement_lineno  = replacement_lineno;
}

void bpp_diag_list_free(bpp_diag_list_t *list) {
    if (!list) return;
    for (size_t i = 0; i < list->n; i++) {
        free(list->items[i].code);
        free(list->items[i].message);
        free(list->items[i].suggestion);
        free(list->items[i].replacement_line);
    }
    free(list->items);
    list->items = NULL;
    list->n = list->cap = 0;
}

/* ---------- fix construction helpers ---------- */

/* Build a replacement raw line that substitutes the keyword name (preserving
 * indentation, padding before '=', and the rest of the value). Optionally
 * supply a new value string; if NULL the original value text is preserved. */
static char *build_renamed_line(const bpp_line_t *line, const char *new_key,
                                const char *new_value) {
    if (!line->raw || !line->key_orig) return NULL;
    /* Determine slices:
     *   [0,            key_col-1)         leading whitespace
     *   [key_col-1,    key_col-1+|orig|)  old key
     *   [end-of-key,   eq_col)            spaces + '='
     *   value section
     */
    const char *raw    = line->raw;
    size_t      key_off = (size_t)(line->key_col - 1);
    size_t      orig_len = strlen(line->key_orig);
    size_t      after_key = key_off + orig_len;

    size_t total_cap = strlen(raw) + strlen(new_key) + (new_value ? strlen(new_value) : 0) + 8;
    char *out = malloc(total_cap);
    if (!out) return NULL;
    size_t w = 0;

    memcpy(out + w, raw, key_off); w += key_off;
    memcpy(out + w, new_key, strlen(new_key)); w += strlen(new_key);

    if (new_value) {
        /* Replace the entire value too; use " = <new_value>" form. */
        memcpy(out + w, " = ", 3); w += 3;
        memcpy(out + w, new_value, strlen(new_value)); w += strlen(new_value);
    } else {
        /* Preserve everything from end-of-old-key onward. */
        memcpy(out + w, raw + after_key, strlen(raw) - after_key);
        w += strlen(raw) - after_key;
    }
    out[w] = '\0';
    return out;
}

/* Derive a jobname from an old outfile value: strip any trailing extension. */
static char *derive_jobname(const char *outfile_value) {
    if (!outfile_value || !*outfile_value) return bpp_strdup("out");
    /* trim leading whitespace */
    while (*outfile_value && isspace((unsigned char) *outfile_value)) outfile_value++;
    const char *end = outfile_value + strlen(outfile_value);
    while (end > outfile_value && isspace((unsigned char) end[-1])) end--;
    /* find last '.' after last '/' */
    const char *slash = NULL;
    for (const char *p = outfile_value; p < end; p++) {
        if (*p == '/' || *p == '\\') slash = p;
    }
    const char *start_of_base = slash ? slash + 1 : outfile_value;
    const char *dot = NULL;
    for (const char *p = start_of_base; p < end; p++) {
        if (*p == '.') dot = p;
    }
    const char *cut = dot ? dot : end;
    size_t len = (size_t)(cut - outfile_value);
    char *s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, outfile_value, len);
    s[len] = '\0';
    return s;
}

/* ---------- value validators ---------- */

/* Count tokens in a value string. */
static int count_tokens(const char *value) {
    if (!value) return 0;
    int n = 0;
    const char *p = value;
    while (*p) {
        while (*p && isspace((unsigned char) *p)) p++;
        if (!*p || *p == '*' || *p == '#') break;
        n++;
        while (*p && !isspace((unsigned char) *p) && *p != '*' && *p != '#') p++;
    }
    return n;
}

/* Parse a double from a token, returning 1 on success. */
static int parse_double(const char *tok, double *out) {
    if (!tok || !*tok) return 0;
    char *end = NULL;
    errno = 0;
    double v = strtod(tok, &end);
    if (errno != 0) return 0;
    if (end == tok) return 0;
    while (*end && isspace((unsigned char) *end)) end++;
    if (*end != '\0') return 0;
    *out = v;
    return 1;
}

/* Test if value's first token starts with one of the strings in `prefixes`.
 * Returns the matching prefix or NULL. */
static const char *value_starts_with_word(const char *value, const char *const *words) {
    if (!value) return NULL;
    while (*value && isspace((unsigned char) *value)) value++;
    for (int i = 0; words[i]; i++) {
        size_t L = strlen(words[i]);
        if (strncasecmp(value, words[i], L) == 0 &&
            (value[L] == '\0' || isspace((unsigned char) value[L]))) {
            return words[i];
        }
    }
    return NULL;
}

/* ---------- per-keyword soft-warning checks ---------- */

static void check_print(bpp_diag_list_t *out, const bpp_line_t *line) {
    int n = count_tokens(line->value);
    char tok[64] = {0};
    if (line->value) sscanf(line->value, "%63s", tok);
    /* The 4.x parser requires 4-5 bits, OR a single token "-1". */
    if (n == 1 && strcmp(tok, "-1") != 0) {
        emit(out, SEV_ERROR, line->lineno, line->val_col, "BPP010",
             xasprintf("'print' has a single value in this file; BPP 4.x requires 4-5 boolean bits (or -1)."),
             xasprintf("Replace with e.g. 'print = 1 0 0 0 0' (samples only) or 'print = 1 0 0 0' (samples; no per-locus q matrix)."),
             NULL, 0);
    }
}

static void check_thetaprior(bpp_diag_list_t *out, const bpp_line_t *line) {
    if (!line->value) return;
    static const char *dist_words[] = { "invgamma", "gamma", "beta", NULL };
    const char *dist = value_starts_with_word(line->value, dist_words);
    int nt = count_tokens(line->value);

    /* Bare-numeric form -> treated as invgamma. Check alpha > 2 since v4.8.2. */
    if (!dist && nt >= 2) {
        char ta[64] = {0};
        sscanf(line->value, "%63s", ta);
        double alpha;
        if (parse_double(ta, &alpha) && alpha <= 2.0) {
            emit(out, SEV_WARNING, line->lineno, line->val_col, "BPP011",
                 xasprintf("'thetaprior' bare-numeric form is treated as invgamma; BPP v4.8.2+ rejects invgamma when alpha <= 2 (alpha=%g here).", alpha),
                 xasprintf("Either use 'thetaprior = gamma %s ...' or choose alpha > 2 for invgamma.", ta),
                 NULL, 0);
        }
    } else if (dist && bpp_strieq(dist, "invgamma")) {
        /* Explicit invgamma: same alpha > 2 check. */
        char ta[64] = {0}, tb[64] = {0};
        sscanf(line->value, "%*s %63s %63s", ta, tb);
        double alpha;
        if (parse_double(ta, &alpha) && alpha <= 2.0) {
            emit(out, SEV_WARNING, line->lineno, line->val_col, "BPP011",
                 xasprintf("'thetaprior = invgamma %s ...': BPP v4.8.2+ requires alpha > 2.", ta),
                 xasprintf("Use alpha > 2, or switch to a gamma prior."),
                 NULL, 0);
        }
    }
}

static void check_tauprior(bpp_diag_list_t *out, const bpp_line_t *line) {
    if (!line->value) return;
    static const char *dist_words[] = { "invgamma", "gamma", NULL };
    const char *dist = value_starts_with_word(line->value, dist_words);
    int nt = count_tokens(line->value);
    if (!dist && nt == 3) {
        emit(out, SEV_WARNING, line->lineno, line->val_col, "BPP012",
             xasprintf("'tauprior' has three numeric tokens; the third is ignored in BPP 4.x (it was a Dirichlet alpha in some BPP 3.x builds)."),
             xasprintf("Drop the third value and add the distribution name: 'tauprior = invgamma a b'."),
             NULL, 0);
    }
}

static void check_finetune_positional(bpp_diag_list_t *out, const bpp_line_t *line) {
    if (!line->value) return;
    /* New (>=v4.8.1) syntax has the colon embedded inside subsequent tokens
     * (e.g. "Gage:5"). Old positional form has a colon attached to the first
     * token ("1:") OR no colons at all. */
    int has_kv = 0;
    int has_postnum_colon = 0;
    const char *p = line->value;
    int idx = 0;
    while (*p) {
        while (*p && isspace((unsigned char) *p)) p++;
        if (!*p || *p == '*' || *p == '#') break;
        const char *tok = p;
        while (*p && !isspace((unsigned char) *p) && *p != '*' && *p != '#') p++;
        /* did this token contain a colon, not in position 1 of the line? */
        for (const char *q = tok; q < p; q++) {
            if (*q == ':') {
                if (idx == 0) {
                    /* "1:" — old positional indicator */
                    has_postnum_colon = 1;
                } else {
                    has_kv = 1;
                }
                break;
            }
        }
        idx++;
    }
    if (has_postnum_colon && !has_kv) {
        emit(out, SEV_WARNING, line->lineno, line->val_col, "BPP013",
             xasprintf("'finetune' uses the legacy positional format ('1: f f f ...'); since BPP v4.8.1 the values are given as 'key:value' pairs."),
             xasprintf("Rewrite as e.g. 'finetune = 1 Gage:5 Gspr:0.001 tau:0.001 mix:0.3 lrht:0.33'. See the BPP manual for the full key list."),
             NULL, 0);
    }
}

static void check_locusrate_legacy(bpp_diag_list_t *out, const bpp_line_t *line) {
    /* Legacy form: 'locusrate = 1 alpha' (2 tokens). BPP 4.x post-v4.1.4
     * needs >=4 tokens for prior=1, or just '0', '2 <file>', '3 a b'. */
    int n = count_tokens(line->value);
    if (n == 2) {
        /* '1 X' is the suspicious case */
        char first[64] = {0};
        sscanf(line->value, "%63s", first);
        if (strcmp(first, "1") == 0) {
            emit(out, SEV_ERROR, line->lineno, line->val_col, "BPP014",
                 xasprintf("'locusrate = 1 <alpha>' is the pre-v4.1.4 form. BPP 4.1.4+ requires 'locusrate = 1 a_mubar b_mubar a_mui [prior]'."),
                 xasprintf("Replace with the full 4-argument form: 'locusrate = 1 <a_mubar> <b_mubar> <a_mui> [DIR|IID]'."),
                 NULL, 0);
        }
    }
}

/* ---------- multi-line block detection ---------- */

/* Returns the number of follow-up lines a multi-line keyword consumes,
 * counting only non-blank / non-comment lines. */
static int species_tree_followups(const bpp_line_t *line) {
    /* First value token is N (species count). 2 follow-up lines if N==1
     * (counts then... actually if N==1 there is no Newick), 2 follow-ups
     * if N>=2 (counts + Newick). We treat the line itself as containing N
     * names; counts go on the next line; Newick on the line after.
     * For linting purposes we just want to skip lines that look like
     * continuation, so we err on the side of consuming 2. */
    (void) line;
    return 2;
}

static int migration_followups(const bpp_line_t *line) {
    if (!line || !line->value) return 0;
    char tok[64] = {0};
    sscanf(line->value, "%63s", tok);
    int n = atoi(tok);
    return n > 0 ? n : 0;
}

/* ---------- main lint pass ---------- */

int bpp_lint(const bpp_file_t *f, const bpp_lint_opts_t *opts,
             bpp_diag_list_t *out)
{
    kw_mode_t mode = opts->simulate ? MODE_SIM : MODE_INFER;
    int errors = 0;

    /* Track which lines are "consumed" by a multi-line block so we don't
     * mis-diagnose continuation lines as malformed statements. */
    int *skip = NULL;
    if (f->n > 0) {
        skip = calloc(f->n, sizeof(int));
        if (!skip) return -1;
    }

    /* First pass: detect multi-line block headers and mark their follow-ups. */
    for (size_t i = 0; i < f->n; i++) {
        const bpp_line_t *L = &f->lines[i];
        if (!L->key) continue;
        int followups = 0;
        if (bpp_strieq(L->key, "species&tree")) followups = species_tree_followups(L);
        else if (bpp_strieq(L->key, "migration")) followups = migration_followups(L);
        else if (bpp_strieq(L->key, "sequenceerror")) {
            char tok[64] = {0};
            if (L->value) sscanf(L->value, "%63s", tok);
            followups = atoi(tok);
        }
        /* Skip the next `followups` non-blank, non-comment lines. */
        size_t j = i + 1;
        while (followups > 0 && j < f->n) {
            const bpp_line_t *N = &f->lines[j];
            if (!bpp_is_blank(N->raw)) {
                /* Comment-only lines: raw starts with optional ws + '*' or '#' */
                const char *p = N->raw;
                while (*p && isspace((unsigned char) *p)) p++;
                if (*p != '*' && *p != '#') {
                    skip[j] = 1;
                    followups--;
                }
            }
            j++;
        }
    }

    /* Second pass: diagnose. */
    for (size_t i = 0; i < f->n; i++) {
        const bpp_line_t *L = &f->lines[i];
        if (skip[i]) continue;
        if (!L->key) {
            /* line has no '=' — only a problem if it has non-comment content */
            if (!bpp_is_blank(L->raw)) {
                const char *p = L->raw;
                while (*p && isspace((unsigned char) *p)) p++;
                if (*p && *p != '*' && *p != '#') {
                    emit(out, SEV_WARNING, L->lineno, (int)(p - L->raw) + 1, "BPP002",
                         xasprintf("line has no '=' assignment and is not a comment or recognised continuation"),
                         NULL, NULL, 0);
                }
            }
            continue;
        }

        const bpp_keyword_t *k = bpp_keyword_find(L->key);
        if (!k) {
            /* unknown — try fuzzy match */
            const bpp_keyword_t *sugg = bpp_keyword_suggest(L->key, mode, 2);
            if (sugg) {
                emit(out, SEV_ERROR, L->lineno, L->key_col, "BPP001",
                     xasprintf("unknown keyword '%s'", L->key_orig),
                     xasprintf("did you mean '%s'?", sugg->name),
                     NULL, 0);
            } else {
                emit(out, SEV_ERROR, L->lineno, L->key_col, "BPP001",
                     xasprintf("unknown keyword '%s'", L->key_orig),
                     NULL, NULL, 0);
            }
            errors++;
            continue;
        }

        /* Mode mismatch: a sim-only keyword in inference mode, or vice versa. */
        if ((k->mode & mode) == 0) {
            const char *want = (mode == MODE_SIM) ? "--simulate" : "inference";
            emit(out, SEV_WARNING, L->lineno, L->key_col, "BPP003",
                 xasprintf("'%s' is recognised by BPP but only in %s mode; this file is being linted as %s",
                           k->name,
                           (mode == MODE_SIM) ? "inference" : "--simulate",
                           want),
                 NULL, NULL, 0);
        }

        switch (k->status) {
        case KW_VALID:
        case KW_VALID_SIM:
            /* fine — but run value-format soft checks below */
            break;

        case KW_RENAMED: {
            char *new_value = NULL;
            /* Special case: outfile → jobname needs value transformation. */
            if (bpp_strieq(L->key, "outfile") || bpp_strieq(L->key, "mcmcfile")) {
                new_value = derive_jobname(L->value);
            }
            char *fix = build_renamed_line(L, k->replacement, new_value);
            free(new_value);
            char *msg = xasprintf("legacy keyword '%s' was renamed to '%s'%s",
                                  L->key_orig, k->replacement,
                                  k->note ? "" : "");
            char *sugg = k->note ? bpp_strdup(k->note) : NULL;
            emit(out, SEV_ERROR, L->lineno, L->key_col, "BPP020",
                 msg, sugg, fix, L->lineno);
            errors++;
            break;
        }

        case KW_REPARAMETERISED: {
            /* Don't auto-rewrite — the user must review parameter values. */
            char *msg = xasprintf("legacy keyword '%s' became '%s' with a changed parameterisation",
                                  L->key_orig, k->replacement);
            char *sugg = k->note ? bpp_strdup(k->note) : NULL;
            emit(out, SEV_ERROR, L->lineno, L->key_col, "BPP021",
                 msg, sugg, NULL, 0);
            errors++;
            break;
        }

        case KW_REMOVED: {
            char *msg = xasprintf("keyword '%s' is no longer supported", L->key_orig);
            char *sugg = k->note ? bpp_strdup(k->note) : NULL;
            emit(out, SEV_ERROR, L->lineno, L->key_col, "BPP022", msg, sugg, NULL, 0);
            errors++;
            break;
        }

        case KW_UNIMPLEMENTED: {
            char *msg = xasprintf("keyword '%s' is recognised by BPP 4.x but unimplemented (the parser aborts)", L->key_orig);
            char *sugg = k->note ? bpp_strdup(k->note) : NULL;
            emit(out, SEV_ERROR, L->lineno, L->key_col, "BPP023", msg, sugg, NULL, 0);
            errors++;
            break;
        }

        case KW_UNKNOWN:
        default:
            break;
        }

        /* Soft-warning value checks for known-valid keywords. */
        if (k->status == KW_VALID || k->status == KW_VALID_SIM) {
            if (bpp_strieq(L->key, "print"))         check_print(out, L);
            else if (bpp_strieq(L->key, "thetaprior")) check_thetaprior(out, L);
            else if (bpp_strieq(L->key, "tauprior"))   check_tauprior(out, L);
            else if (bpp_strieq(L->key, "finetune"))   check_finetune_positional(out, L);
            else if (bpp_strieq(L->key, "locusrate") && mode == MODE_INFER) check_locusrate_legacy(out, L);

            /* Empty value */
            if (L->value && bpp_is_blank(L->value)
                && !bpp_strieq(L->key, "species&tree")) {
                emit(out, SEV_WARNING, L->lineno, L->eq_col, "BPP004",
                     xasprintf("keyword '%s' has an empty value", L->key_orig),
                     NULL, NULL, 0);
            }
        }
    }

    free(skip);
    return errors;
}

/* ---------- diagnostic printing ---------- */

static const char *sev_label(bpp_severity_t s) {
    switch (s) {
    case SEV_ERROR:   return "error";
    case SEV_WARNING: return "warning";
    case SEV_INFO:    return "info";
    }
    return "?";
}

void bpp_diag_print(const bpp_diag_list_t *diags, const char *path) {
    if (!diags) return;
    for (size_t i = 0; i < diags->n; i++) {
        const bpp_diagnostic_t *d = &diags->items[i];
        fprintf(stderr, "%s:%d:%d: %s [%s]: %s\n",
                path, d->lineno, d->column,
                sev_label(d->severity),
                d->code ? d->code : "",
                d->message ? d->message : "");
        if (d->suggestion) {
            fprintf(stderr, "  note: %s\n", d->suggestion);
        }
        if (d->replacement_line) {
            fprintf(stderr, "  fix:  %s\n", d->replacement_line);
        }
    }
}

/* ---------- autofix application ---------- */

int bpp_apply_fixes(bpp_file_t *f, const bpp_diag_list_t *diags) {
    int applied = 0;
    for (size_t i = 0; i < diags->n; i++) {
        const bpp_diagnostic_t *d = &diags->items[i];
        if (!d->replacement_line) continue;
        if (d->replacement_lineno <= 0) continue;
        /* find the line by lineno (linear; n is small) */
        for (size_t j = 0; j < f->n; j++) {
            if (f->lines[j].lineno == d->replacement_lineno) {
                free(f->lines[j].raw);
                f->lines[j].raw = bpp_strdup(d->replacement_line);
                /* clear cached parse; downstream consumers shouldn't rely on
                 * .key / .value being consistent with .raw after a fix */
                applied++;
                break;
            }
        }
    }
    return applied;
}

int bpp_file_write(const bpp_file_t *f, const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    for (size_t i = 0; i < f->n; i++) {
        if (f->lines[i].raw) {
            fputs(f->lines[i].raw, fp);
        }
        fputc('\n', fp);
    }
    fclose(fp);
    return 0;
}
