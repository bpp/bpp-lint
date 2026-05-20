#define _POSIX_C_SOURCE 200809L

#include "lint.h"
#include "lex.h"
#include "keywords.h"

#include <ctype.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- color output ---------- */

/* Resolved color state: 0 = no color, 1 = color. Set by bpp_color_set. */
static int color_on = 0;

/* SGR sequences. Kept minimal and standard. */
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_BRED    "\033[1;31m"
#define ANSI_BYELLOW "\033[1;33m"

static const char *c(const char *seq) { return color_on ? seq : ""; }

void bpp_color_set(bpp_color_mode_t mode) {
    switch (mode) {
    case BPP_COLOR_ALWAYS: color_on = 1; return;
    case BPP_COLOR_NEVER:  color_on = 0; return;
    case BPP_COLOR_AUTO:
    default: {
        const char *nc = getenv("NO_COLOR");
        if (nc && *nc) { color_on = 0; return; }
        const char *term = getenv("TERM");
        if (term && strcmp(term, "dumb") == 0) { color_on = 0; return; }
        color_on = isatty(fileno(stderr)) ? 1 : 0;
        return;
    }
    }
}

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
        /* Auto-fix: preserve the user's first bit, pad with zeros to 5 fields.
         * This is the safe interpretation: "user wanted samples printed and
         * nothing else extra". */
        char new_value[32];
        snprintf(new_value, sizeof(new_value), "%s 0 0 0 0", tok);
        char *fix = build_renamed_line(line, "print", new_value);
        emit(out, SEV_ERROR, line->lineno, line->val_col, "BPP010",
             xasprintf("'print' has a single value in this file; BPP 4.x requires 4-5 boolean bits (or -1)."),
             xasprintf("Pad to 5 bits: 'print = %s 0 0 0 0' (samples only). The four extra bits enable per-locus rate, h-scalars, gene trees, and q-matrix output respectively.", tok),
             fix, line->lineno);
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
        /* Auto-fix: keep the first two tokens (alpha, beta), drop the third
         * (which BPP 4.x silently ignores anyway). The user can later add an
         * explicit 'invgamma'/'gamma' prefix if they want non-default
         * distribution; the bare-numeric form is still accepted. */
        char ta[64] = {0}, tb[64] = {0};
        sscanf(line->value, "%63s %63s", ta, tb);
        char new_value[160];
        snprintf(new_value, sizeof(new_value), "%s %s", ta, tb);
        char *fix = build_renamed_line(line, "tauprior", new_value);
        emit(out, SEV_WARNING, line->lineno, line->val_col, "BPP012",
             xasprintf("'tauprior' has three numeric tokens; the third is ignored in BPP 4.x (it was a Dirichlet alpha in some BPP 3.x builds)."),
             xasprintf("Drop the third value. Optionally prefix with 'invgamma' or 'gamma' to make the distribution explicit."),
             fix, line->lineno);
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

/* ---------- completeness check helpers ---------- */

/* Find the first line where `key` is the keyword (case-insensitive).
 * Returns NULL if the keyword is not set anywhere in the file. */
static const bpp_line_t *find_set_line(const bpp_file_t *f, const char *key) {
    for (size_t i = 0; i < f->n; i++) {
        if (f->lines[i].key && bpp_strieq(f->lines[i].key, key)) {
            return &f->lines[i];
        }
    }
    return NULL;
}

/* True if `key` is set, OR if any legacy-renamed predecessor of `key` is set
 * (since --fix would migrate the legacy entry to the modern name). */
static int is_effectively_set(const bpp_file_t *f, const char *key) {
    if (find_set_line(f, key)) return 1;
    for (int i = 0; ; i++) {
        const bpp_keyword_t *k = bpp_keyword_at(i);
        if (!k) break;
        if ((k->status == KW_RENAMED || k->status == KW_REPARAMETERISED) &&
            k->replacement && bpp_strieq(k->replacement, key) &&
            find_set_line(f, k->name)) {
            return 1;
        }
    }
    return 0;
}

/* Parse the first whitespace-separated token of value as an int.
 * Returns 0 if value is NULL or unparseable (callers must handle that
 * sentinel using context). */
static int first_int(const char *value) {
    if (!value) return 0;
    while (*value && isspace((unsigned char) *value)) value++;
    return atoi(value);
}

/* Scan all lines for a substring; case-sensitive. Used to detect the MSC-I
 * marker '&phi=' that BPP requires inside a species&tree Newick. */
static int file_contains(const bpp_file_t *f, const char *needle) {
    for (size_t i = 0; i < f->n; i++) {
        if (f->lines[i].raw && strstr(f->lines[i].raw, needle)) return 1;
    }
    return 0;
}

static void check_completeness(const bpp_file_t *f, const bpp_lint_opts_t *opts,
                               bpp_diag_list_t *out, int *errors)
{
    kw_mode_t mode = opts->simulate ? MODE_SIM : MODE_INFER;

    /* Track keywords already flagged as required-but-missing so we don't
     * also emit a misleading BPP103 "default will be used" for them. */
    const char *flagged[16] = {0};
    int n_flagged = 0;
#define FLAG_MISSING(name) do { \
    if (n_flagged < (int)(sizeof(flagged)/sizeof(flagged[0]))) \
        flagged[n_flagged++] = (name); \
} while (0)

    /* --- Must-set keywords (no useful default; BPP aborts if missing) --- */
    static const char *must_set_infer[] = {
        "seqfile", "nloci", "nsample", "jobname",
        "species&tree", "tauprior", "thetaprior",
        /* 'model' has a JC69 default (bpp.c sets opt_model = BPP_DNA_MODEL_DEFAULT
         * before load_cfile); BPP103 covers its missing-but-defaulted case. */
        NULL
    };
    static const char *must_set_sim[] = {
        "seqfile", "treefile", "species&tree", "loci&length",
        NULL
    };
    const char *const *must_set = (mode == MODE_SIM) ? must_set_sim : must_set_infer;

    for (int i = 0; must_set[i]; i++) {
        if (!is_effectively_set(f, must_set[i])) {
            emit(out, SEV_ERROR, 0, 0, "BPP100",
                 xasprintf("required keyword '%s' is not set", must_set[i]),
                 xasprintf("BPP will refuse to run without '%s' in the control file.", must_set[i]),
                 NULL, 0);
            (*errors)++;
            FLAG_MISSING(must_set[i]);
        }
    }

    /* --- Conditional requirements (inference mode only) --- */
    if (mode == MODE_INFER) {
        const bpp_line_t *st = find_set_line(f, "species&tree");
        int species_count = (st && st->value) ? first_int(st->value) : 0;

        if (species_count > 1 && !is_effectively_set(f, "imapfile")) {
            emit(out, SEV_ERROR, 0, 0, "BPP101",
                 xasprintf("'imapfile' is required when species count > 1 (species&tree declares %d species)", species_count),
                 xasprintf("Provide a path to a tab-separated individual->species map."),
                 NULL, 0);
            (*errors)++;
            FLAG_MISSING("imapfile");
        }

        int sd_on = 0, str_on = 0;
        const bpp_line_t *sd = find_set_line(f, "speciesdelimitation");
        if (sd) sd_on = (first_int(sd->value) == 1);
        const bpp_line_t *str_ = find_set_line(f, "speciestree");
        if (str_) str_on = (first_int(str_->value) == 1);
        if ((sd_on || str_on) && !is_effectively_set(f, "speciesmodelprior")) {
            emit(out, SEV_ERROR, 0, 0, "BPP101",
                 xasprintf("'speciesmodelprior' is required when %s%s%s is enabled",
                           sd_on ? "speciesdelimitation" : "",
                           (sd_on && str_on) ? " or " : "",
                           str_on ? "speciestree" : ""),
                 xasprintf("Use 0 (uniform labeled histories) or 1 (uniform rooted trees, recommended default)."),
                 NULL, 0);
            (*errors)++;
            FLAG_MISSING("speciesmodelprior");
        }

        /* MSC-I introgression: any Newick node has '&phi=' annotation. */
        if (file_contains(f, "&phi=") && !is_effectively_set(f, "phiprior")) {
            emit(out, SEV_ERROR, 0, 0, "BPP101",
                 xasprintf("'phiprior' is required when 'species&tree' contains MSC-I introgression (a Newick node has '&phi=' annotation)"),
                 xasprintf("Beta(a,b) prior on the introgression probability phi, e.g. 'phiprior = 1 1'."),
                 NULL, 0);
            (*errors)++;
            FLAG_MISSING("phiprior");
        }

        /* MSC-M migration: wprior required if migration N > 0. */
        const bpp_line_t *mig = find_set_line(f, "migration");
        if (mig && first_int(mig->value) > 0 && !is_effectively_set(f, "wprior")) {
            emit(out, SEV_ERROR, 0, 0, "BPP101",
                 xasprintf("'wprior' is required when 'migration' specifies one or more connections"),
                 xasprintf("Provide a Gamma(a,b) prior on w (= 4M/theta), e.g. 'wprior = 2 200'."),
                 NULL, 0);
            (*errors)++;
            FLAG_MISSING("wprior");
        }

        /* --- Illegal-in-context warnings --- */
        const bpp_line_t *ph = find_set_line(f, "phase");
        if (species_count == 1 && ph) {
            emit(out, SEV_WARNING, ph->lineno, ph->key_col, "BPP102",
                 xasprintf("'phase' is meaningful only when species count > 1; this file has 1 species"),
                 NULL, NULL, 0);
        }

        const bpp_line_t *mdl = find_set_line(f, "model");
        int is_gtr = 0;
        if (mdl && mdl->value) {
            char mtok[32] = {0};
            sscanf(mdl->value, "%31s", mtok);
            if (bpp_strieq(mtok, "gtr") || strcmp(mtok, "7") == 0) is_gtr = 1;
        }
        if (!is_gtr) {
            const bpp_line_t *qr = find_set_line(f, "qrates");
            if (qr) {
                emit(out, SEV_WARNING, qr->lineno, qr->key_col, "BPP102",
                     xasprintf("'qrates' is only used with model = GTR; the current model is '%s'",
                               mdl && mdl->value ? mdl->value : "(unset)"),
                     NULL, NULL, 0);
            }
            const bpp_line_t *bf = find_set_line(f, "basefreqs");
            if (bf) {
                emit(out, SEV_WARNING, bf->lineno, bf->key_col, "BPP102",
                     xasprintf("'basefreqs' is only used with model = GTR; the current model is '%s'",
                               mdl && mdl->value ? mdl->value : "(unset)"),
                     NULL, NULL, 0);
            }
        }
    }

    /* --- Missing-with-default warnings (suppressed by --no-defaults) --- */
    if (opts->show_defaults) {
        for (int i = 0; ; i++) {
            const bpp_keyword_t *k = bpp_keyword_at(i);
            if (!k) break;
            if (k->status != KW_VALID && k->status != KW_VALID_SIM) continue;
            if ((k->mode & mode) == 0) continue;
            if (!k->default_value) continue;
            if (is_effectively_set(f, k->name)) continue;
            /* Already flagged as required-but-missing? The BPP100/BPP101
             * error supersedes; don't double up with a (now wrong) note that
             * the default will be used. */
            int suppress = 0;
            for (int j = 0; j < n_flagged; j++) {
                if (bpp_strieq(flagged[j], k->name)) { suppress = 1; break; }
            }
            if (suppress) continue;
            emit(out, SEV_WARNING, 0, 0, "BPP103",
                 xasprintf("'%s' not set; using default '%s'", k->name, k->default_value),
                 NULL, NULL, 0);
        }
    }
#undef FLAG_MISSING
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

    /* Completeness pass: flag missing required keywords, illegal-in-context
     * usages, and (optionally) keywords falling back to BPP's default. */
    check_completeness(f, opts, out, &errors);

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

/* Return SGR color for a severity level. */
static const char *sev_color(bpp_severity_t s) {
    switch (s) {
    case SEV_ERROR:   return ANSI_BRED;
    case SEV_WARNING: return ANSI_BYELLOW;
    case SEV_INFO:    return ANSI_BLUE;
    }
    return "";
}

void bpp_diag_print(const bpp_diag_list_t *diags, const char *path) {
    if (!diags) return;
    for (size_t i = 0; i < diags->n; i++) {
        const bpp_diagnostic_t *d = &diags->items[i];

        if (d->lineno > 0) {
            /* path:line:col: severity [CODE]: message */
            fprintf(stderr,
                    "%s%s%s:%s%d%s:%s%d%s: %s%s%s [%s%s%s]: %s\n",
                    c(ANSI_BOLD), path,           c(ANSI_RESET),
                    c(ANSI_BOLD), d->lineno,      c(ANSI_RESET),
                    c(ANSI_BOLD), d->column,      c(ANSI_RESET),
                    c(sev_color(d->severity)), sev_label(d->severity), c(ANSI_RESET),
                    c(ANSI_CYAN), d->code ? d->code : "", c(ANSI_RESET),
                    d->message ? d->message : "");
        } else {
            /* File-level diagnostic (missing required, missing default).
             * Omit the :line:col: that wouldn't refer to anything. */
            fprintf(stderr,
                    "%s%s%s: %s%s%s [%s%s%s]: %s\n",
                    c(ANSI_BOLD), path, c(ANSI_RESET),
                    c(sev_color(d->severity)), sev_label(d->severity), c(ANSI_RESET),
                    c(ANSI_CYAN), d->code ? d->code : "", c(ANSI_RESET),
                    d->message ? d->message : "");
        }
        if (d->suggestion) {
            fprintf(stderr, "  %snote:%s %s%s%s\n",
                    c(ANSI_GREEN), c(ANSI_RESET),
                    c(ANSI_DIM), d->suggestion, c(ANSI_RESET));
        }
        if (d->replacement_line) {
            fprintf(stderr, "  %sfix:%s  %s%s%s\n",
                    c(ANSI_GREEN), c(ANSI_RESET),
                    c(ANSI_DIM), d->replacement_line, c(ANSI_RESET));
        }
    }
}

/* ---------- unified diff emission ---------- */

/* Comparator for sorting (lineno, replacement) pairs by lineno. */
struct diff_entry {
    int   lineno;
    const char *new_line;
};

static int diff_entry_cmp(const void *a, const void *b) {
    int la = ((const struct diff_entry *) a)->lineno;
    int lb = ((const struct diff_entry *) b)->lineno;
    return la - lb;
}

#define DIFF_CONTEXT 3

int bpp_emit_diff(const bpp_file_t *f, const bpp_diag_list_t *diags,
                  const char *display_path, FILE *fp)
{
    /* 1. Gather (lineno, replacement) entries from diagnostics.
     *    We keep only the FIRST replacement seen per lineno (in diag order)
     *    in the rare case multiple diagnostics target the same line. */
    struct diff_entry *entries = malloc(diags->n * sizeof(*entries));
    if (!entries) return 0;
    size_t ne = 0;
    for (size_t i = 0; i < diags->n; i++) {
        const bpp_diagnostic_t *d = &diags->items[i];
        if (!d->replacement_line || d->replacement_lineno <= 0) continue;
        /* dedup by lineno */
        int dup = 0;
        for (size_t k = 0; k < ne; k++) {
            if (entries[k].lineno == d->replacement_lineno) { dup = 1; break; }
        }
        if (dup) continue;
        entries[ne].lineno   = d->replacement_lineno;
        entries[ne].new_line = d->replacement_line;
        ne++;
    }
    if (ne == 0) { free(entries); return 0; }

    qsort(entries, ne, sizeof(*entries), diff_entry_cmp);

    /* 2. Group entries into hunks. Two changes belong in the same hunk if
     *    their context windows would touch or overlap; with C=3 lines of
     *    context, that means lineno_{i+1} - lineno_i <= 2*C + 1 = 7. */
    /* Use plain diff -u headers (no a/ b/ git prefix). Works with both
     * 'patch -p0' from the same directory and a user editor's diff viewer. */
    fprintf(fp, "--- %s\n+++ %s\n", display_path, display_path);

    int total_lines = (int) f->n;
    size_t i = 0;
    int hunks = 0;
    while (i < ne) {
        size_t j = i;
        while (j + 1 < ne && entries[j + 1].lineno - entries[j].lineno <= 2 * DIFF_CONTEXT + 1) {
            j++;
        }
        /* Hunk covers lines [first - C, last + C], clamped to [1, total]. */
        int first = entries[i].lineno;
        int last  = entries[j].lineno;
        int from  = first - DIFF_CONTEXT;
        int to    = last  + DIFF_CONTEXT;
        if (from < 1) from = 1;
        if (to > total_lines) to = total_lines;

        /* Count old/new lines in this hunk (same here: 1-for-1 replacements). */
        int old_count = to - from + 1;
        int new_count = old_count;
        fprintf(fp, "@@ -%d,%d +%d,%d @@\n", from, old_count, from, new_count);

        size_t k = i;
        for (int ln = from; ln <= to; ln++) {
            const char *orig = (ln >= 1 && ln <= total_lines && f->lines[ln - 1].raw)
                               ? f->lines[ln - 1].raw : "";
            if (k <= j && entries[k].lineno == ln) {
                fprintf(fp, "-%s\n", orig);
                fprintf(fp, "+%s\n", entries[k].new_line);
                k++;
            } else {
                fprintf(fp, " %s\n", orig);
            }
        }
        hunks++;
        i = j + 1;
    }

    free(entries);
    return hunks;
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
