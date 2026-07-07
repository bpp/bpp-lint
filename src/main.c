#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <strings.h>

#include "codes.h"
#include "imap.h"
#include "lex.h"
#include "lint.h"
#include "priors.h"
#include "seqfile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BPP_LINT_VERSION "0.3.1"

static void print_usage(FILE *out, const char *argv0) {
    fprintf(out,
        "Usage: %s [options] <control-file>\n"
        "\n"
        "Lint a BPP (Bayesian Phylogenetics & Phylogeography) control file.\n"
        "Recognises BPP 4.x syntax and proposes fixes for files written for\n"
        "BPP 2.x or 3.x.\n"
        "\n"
        "Options:\n"
        "  -f, --fix         Rewrite the file in place after backing up the\n"
        "                    original to <path>.bak. Only auto-applicable\n"
        "                    fixes are written; semantic changes are reported\n"
        "                    but require human review. Mutually exclusive\n"
        "                    with --diff.\n"
        "  -d, --diff        Write a unified diff of the auto-applicable\n"
        "                    fixes to stdout. Apply with 'patch -p0' (or\n"
        "                    'git apply -p0'). Exits non-zero if any fix\n"
        "                    is needed, matching gofmt -d convention.\n"
        "  -s, --simulate    Lint as a BPP --simulate control file\n"
        "                    (different keyword set).\n"
        "  -q, --quiet       Print only errors; suppress warnings and notes.\n"
        "      --no-defaults Suppress informational warnings about optional\n"
        "                    keywords falling back to BPP's default (code 103).\n"
        "      --codes       Show diagnostic codes (e.g. '[020]') in output.\n"
        "                    Off by default; useful when filtering with grep.\n"
        "      --color=WHEN  Colorize output. WHEN is 'auto' (default), 'always',\n"
        "                    or 'never'. Auto enables color when stderr is a TTY\n"
        "                    and the NO_COLOR environment variable is unset.\n"
        "      --list-codes  Print a summary of every diagnostic code and exit.\n"
        "      --explain CODE\n"
        "                    Print a longer description of a single diagnostic\n"
        "                    code (e.g. '--explain 020') and exit.\n"
        "      --suggest-priors\n"
        "                    Read the control file's seqfile + Imap and print\n"
        "                    recommended thetaprior / tauprior lines (invgamma\n"
        "                    with alpha=3, mean = data-derived estimate).\n"
        "      --check-priors\n"
        "                    Same data-derived estimate, but compared against\n"
        "                    the control file's existing tauprior / thetaprior;\n"
        "                    warns (BPP110/BPP111) when more than ~10x off.\n"
        "      --json        Emit a machine-readable JSON report (status,\n"
        "                    diagnostics, counts) to stdout instead of the\n"
        "                    human-readable diagnostics. Consumers use\n"
        "                    status == \"valid\" as a stop condition. Cannot be\n"
        "                    combined with --fix, --diff, or --suggest-priors.\n"
        "      --version     Print version and exit.\n"
        "  -h, --help        Show this help.\n"
        "\n"
        "Exit codes:\n"
        "  0  no errors (warnings may still be present)\n"
        "  1  at least one error reported\n"
        "  2  invocation error (bad arguments, missing file, I/O failure)\n",
        argv0);
}

static int filter_quiet(bpp_diag_list_t *list) {
    /* in-place: keep only SEV_ERROR */
    size_t w = 0;
    for (size_t r = 0; r < list->n; r++) {
        if (list->items[r].severity == SEV_ERROR) {
            if (w != r) list->items[w] = list->items[r];
            w++;
        } else {
            free(list->items[r].code);
            free(list->items[r].message);
            free(list->items[r].suggestion);
            free(list->items[r].replacement_line);
        }
    }
    list->n = w;
    return 0;
}

/* ---------- helpers for the priors features ---------- */

/* Allocate a string holding the directory portion of `path` (without trailing
 * slash). Returns "." if `path` has no slash. Caller frees. */
static char *path_dirname(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return bpp_strdup(".");
    if (slash == path) return bpp_strdup("/");
    size_t n = (size_t)(slash - path);
    char *d = malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, path, n);
    d[n] = '\0';
    return d;
}

/* If `name` is absolute, return a fresh copy. Otherwise return "<dir>/<name>". */
static char *path_join(const char *dir, const char *name) {
    if (name[0] == '/') return bpp_strdup(name);
    size_t dn = strlen(dir), nn = strlen(name);
    char *out = malloc(dn + 1 + nn + 1);
    if (!out) return NULL;
    memcpy(out, dir, dn);
    out[dn] = '/';
    memcpy(out + dn + 1, name, nn);
    out[dn + 1 + nn] = '\0';
    return out;
}

/* Pull a single string value out of a parsed line (NULL-safe trim). Caller
 * frees. */
static char *first_token_dup(const char *value) {
    if (!value) return NULL;
    while (*value && isspace((unsigned char) *value)) value++;
    const char *end = value;
    while (*end && !isspace((unsigned char) *end)) end++;
    if (end == value) return NULL;
    size_t n = (size_t)(end - value);
    char *s = malloc(n + 1);
    if (!s) return NULL;
    memcpy(s, value, n);
    s[n] = '\0';
    return s;
}

/* Locate a key in a parsed control file. */
static const bpp_line_t *cfile_find(const bpp_file_t *f, const char *key) {
    for (size_t i = 0; i < f->n; i++) {
        if (f->lines[i].key && bpp_strieq(f->lines[i].key, key)) return &f->lines[i];
    }
    return NULL;
}

/* Resolve seqfile + imapfile paths against the control file's directory.
 * On success, fills *seq_out / *imap_out (caller frees) and returns 0;
 * on failure (either key missing or alloc fails), prints to stderr and
 * returns -1. */
static int resolve_data_paths(const bpp_file_t *cfile,
                              const char *cfile_path,
                              char **seq_out, char **imap_out)
{
    *seq_out = *imap_out = NULL;
    const bpp_line_t *seq_line  = cfile_find(cfile, "seqfile");
    const bpp_line_t *imap_line = cfile_find(cfile, "imapfile");
    if (!seq_line || !imap_line) {
        fprintf(stderr, "bpp-lint: priors require both 'seqfile' and 'imapfile' to be set in %s\n",
                cfile_path);
        return -1;
    }
    char *seq_name  = first_token_dup(seq_line->value);
    char *imap_name = first_token_dup(imap_line->value);
    if (!seq_name || !imap_name) {
        free(seq_name); free(imap_name);
        fprintf(stderr, "bpp-lint: seqfile or imapfile has no value\n");
        return -1;
    }
    char *dir = path_dirname(cfile_path);
    if (!dir) { free(seq_name); free(imap_name); return -1; }
    *seq_out  = path_join(dir, seq_name);
    *imap_out = path_join(dir, imap_name);
    free(dir); free(seq_name); free(imap_name);
    if (!*seq_out || !*imap_out) {
        free(*seq_out); free(*imap_out);
        *seq_out = *imap_out = NULL;
        return -1;
    }
    return 0;
}

/* Compute prior means from the data files. Returns 0 on success. */
static int compute_priors_from_files(const char *seqfile_path,
                                     const char *imapfile_path,
                                     double *theta_mean, double *tau_mean)
{
    bpp_imap_t      imap = {0};
    bpp_alignment_t al   = {0};
    bpp_seq_grouping_t g = {0};
    int rc = -1;

    if (bpp_imap_load(&imap, imapfile_path) != 0) {
        fprintf(stderr, "bpp-lint: cannot read imap file '%s'\n", imapfile_path);
        goto out;
    }
    if (bpp_alignment_load(&al, seqfile_path) != 0) {
        fprintf(stderr, "bpp-lint: cannot read sequence file '%s'\n", seqfile_path);
        goto out;
    }
    size_t n_sp = 0;
    char **species = bpp_imap_species_list(&imap, &n_sp);
    if (!species || n_sp == 0) {
        fprintf(stderr, "bpp-lint: no species found in imap '%s'\n", imapfile_path);
        free(species);
        goto out;
    }
    if (bpp_group_by_species(&al, &imap, species, (int) n_sp, &g) != 0) {
        free(species);
        fprintf(stderr, "bpp-lint: failed to group sequences by species\n");
        goto out;
    }
    free(species);
    rc = bpp_compute_prior_means(&g, theta_mean, tau_mean);
out:
    bpp_seq_grouping_free(&g);
    bpp_alignment_free(&al);
    bpp_imap_free(&imap);
    return rc;
}

/* Parse a BPP prior value as (kind, alpha, beta). Returns 0 on success.
 *   kind = 0 -> invgamma (or bare-numeric, treated as invgamma)
 *   kind = 1 -> gamma (shape-rate; BPP convention)
 *   kind = 2 -> beta (not used for sanity check; returns -2)
 */
static int parse_prior(const char *value, int *kind, double *alpha, double *beta) {
    if (!value) return -1;
    while (*value && isspace((unsigned char) *value)) value++;
    *kind = 0;
    if (strncasecmp(value, "invgamma", 8) == 0)  { value += 8; *kind = 0; }
    else if (strncasecmp(value, "gamma", 5) == 0) { value += 5; *kind = 1; }
    else if (strncasecmp(value, "beta", 4) == 0)  { return -2; }
    char *end = NULL;
    *alpha = strtod(value, &end);
    if (end == value) return -1;
    value = end;
    *beta = strtod(value, &end);
    if (end == value) return -1;
    return 0;
}

/* Sanity-check the user's prior value against a data-derived estimate.
 *
 * `factor` controls the warning threshold (e.g. 10.0 means "warn at 10x off").
 *
 * `upper_only` toggles the direction of the check:
 *   0 -> symmetric: flag if prior_mean / data_mean is > factor OR < 1/factor.
 *        Right for theta, where both too-tight and too-diffuse priors are
 *        worth flagging.
 *   1 -> upper-only: flag only if prior_mean > factor * data_mean. Right
 *        for tau, where bpps's data-derived estimate is the max raw pairwise
 *        distance — an upper bound, not an expected posterior mean. A prior
 *        well below this is normal (raw distance ~= 2*tau + theta), so only
 *        priors that are way too diffuse deserve a warning. */
static void check_one_prior(const bpp_file_t *cfile, const char *key,
                            double data_mean, const char *human_name,
                            const char *code, double factor, int upper_only,
                            bpp_diag_list_t *out)
{
    (void) human_name;
    const bpp_line_t *L = cfile_find(cfile, key);
    if (!L) return;     /* missing-required handled by completeness check */
    int kind = 0; double a = 0, b = 0;
    if (parse_prior(L->value, &kind, &a, &b) != 0) return;
    double prior_mean;
    if (kind == 0) {        /* invgamma: mean = b/(a-1) */
        if (a <= 1) return;
        prior_mean = b / (a - 1.0);
    } else {                /* gamma (shape-rate): mean = a/b */
        if (b <= 0) return;
        prior_mean = a / b;
    }
    if (data_mean <= 0) return;
    double ratio = prior_mean / data_mean;
    int flag = upper_only
                 ? (ratio > factor)
                 : (ratio > factor || ratio < 1.0 / factor);
    if (!flag) return;

    char buf[256];
    if (upper_only) {
        snprintf(buf, sizeof(buf),
                 "'%s' prior mean = %.4g; data upper bound ~%.4g (prior is %.1fx too diffuse)",
                 key, prior_mean, data_mean, ratio);
    } else {
        snprintf(buf, sizeof(buf),
                 "'%s' prior mean = %.4g; data suggests ~%.4g (off by %.1fx)",
                 key, prior_mean, data_mean,
                 ratio > 1 ? ratio : 1.0 / ratio);
    }
    bpp_diagnostic_t diag = {
        .severity            = SEV_WARNING,
        .lineno              = L->lineno,
        .column              = L->key_col,
        .code                = bpp_strdup(code),
        .message             = bpp_strdup(buf),
        .suggestion          = NULL,
        .replacement_line    = NULL,
        .replacement_lineno  = 0,
    };
    bpp_diagnostic_t *nl = realloc(out->items, (out->n + 1) * sizeof(*nl));
    if (!nl) { free(diag.code); free(diag.message); return; }
    out->items = nl;
    out->items[out->n++] = diag;
}

/* ---------- JSON report (design §2, machine-readable loop signal) ---------- */

/* Print `s` as a JSON string literal (quoted, with the mandatory escapes).
 * NULL is rendered as an empty string. */
static void json_print_escaped(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *) (s ? s : ""); *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\r': fputs("\\r", out);  break;
            case '\t': fputs("\\t", out);  break;
            default:
                if (*p < 0x20) fprintf(out, "\\u%04x", (unsigned) *p);
                else           fputc(*p, out);
        }
    }
    fputc('"', out);
}

/* Escape a substring [s, s+n) as a JSON string (keyword/value spans are short). */
static void json_print_span(FILE *out, const char *s, size_t n) {
    char buf[256];
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    json_print_escaped(out, buf);
}

/* For a BPP103 "'X' not set; using default 'Y'" warning, emit a structured
 * {"keyword","value"} so consumers never parse the human message; null else. */
static void emit_json_default(FILE *out, const bpp_diagnostic_t *d) {
    if (d->code && strcmp(d->code, "BPP103") == 0 && d->message) {
        const char *q1 = strchr(d->message, '\'');
        const char *q2 = q1 ? strchr(q1 + 1, '\'') : NULL;
        const char *mk = strstr(d->message, "using default '");
        if (q1 && q2 && mk) {
            const char *vs = mk + strlen("using default '");
            const char *ve = strrchr(vs, '\'');
            if (ve && ve > vs) {
                fputs(", \"default\": {\"keyword\": ", out);
                json_print_span(out, q1 + 1, (size_t)(q2 - (q1 + 1)));
                fputs(", \"value\": ", out);
                json_print_span(out, vs, (size_t)(ve - vs));
                fputc('}', out);
                return;
            }
        }
    }
    fputs(", \"default\": null", out);
}

static const char *severity_str(bpp_severity_t s) {
    switch (s) {
        case SEV_ERROR:   return "error";
        case SEV_WARNING: return "warning";
        default:          return "note";      /* SEV_INFO */
    }
}

/* First whitespace-separated token of `v` parsed as an int (0 if unparseable). */
static int first_int_val(const char *v) {
    if (!v) return 0;
    while (*v && isspace((unsigned char) *v)) v++;
    return atoi(v);
}

/* Derive the BPP analysis type from the speciesdelimitation/speciestree
 * switches. Returns "A00"/"A01"/"A10"/"A11", or "unknown" when it cannot be
 * determined (--simulate, or no species&tree block). Both switches default to
 * 0 in BPP, so an absent switch is treated as off (matching check_completeness). */
static const char *analysis_type_str(const bpp_file_t *f, int simulate) {
    if (simulate || !cfile_find(f, "species&tree")) return "unknown";
    const bpp_line_t *sd = cfile_find(f, "speciesdelimitation");
    const bpp_line_t *st = cfile_find(f, "speciestree");
    int sd_on = sd ? (first_int_val(sd->value) == 1) : 0;
    int st_on = st ? (first_int_val(st->value) == 1) : 0;
    if (!sd_on && !st_on) return "A00";
    if (!sd_on &&  st_on) return "A01";
    if ( sd_on && !st_on) return "A10";
    return "A11";
}

/* Emit the full lint result as JSON matching bpp-agent-design.md §2. Every
 * value comes from the real diagnostics / parsed control file — nothing is
 * synthesized. `status` is "valid" iff there are zero error-severity items. */
static void emit_json(FILE *out, const char *path, const bpp_file_t *f,
                      const bpp_diag_list_t *diags, int simulate,
                      int check_priors)
{
    size_t n_err = 0, n_warn = 0, n_note = 0;
    for (size_t i = 0; i < diags->n; i++) {
        switch (diags->items[i].severity) {
            case SEV_ERROR:   n_err++;  break;
            case SEV_WARNING: n_warn++; break;
            default:          n_note++; break;
        }
    }

    const bpp_line_t *st_line = cfile_find(f, "species&tree");
    const bpp_line_t *nl_line = cfile_find(f, "nloci");

    fputs("{\n", out);
    fputs("  \"bpp_lint_version\": ", out);
    json_print_escaped(out, BPP_LINT_VERSION);
    fputs(",\n  \"status\": ", out);
    json_print_escaped(out, n_err ? "invalid" : "valid");
    fputs(",\n  \"file\": ", out);
    json_print_escaped(out, path);
    fputs(",\n  \"analysis_type\": ", out);
    json_print_escaped(out, analysis_type_str(f, simulate));

    /* n_species / n_loci: real if present in the control file, else null. */
    if (st_line && st_line->value) fprintf(out, ",\n  \"n_species\": %d", first_int_val(st_line->value));
    else                           fputs(",\n  \"n_species\": null", out);
    if (nl_line && nl_line->value) fprintf(out, ",\n  \"n_loci\": %d", first_int_val(nl_line->value));
    else                           fputs(",\n  \"n_loci\": null", out);

    fputs(",\n  \"diagnostics\": [", out);
    for (size_t i = 0; i < diags->n; i++) {
        const bpp_diagnostic_t *d = &diags->items[i];
        fputs(i ? ",\n" : "\n", out);
        fputs("    {\"code\": ", out);
        json_print_escaped(out, d->code);
        fputs(", \"severity\": ", out);
        json_print_escaped(out, severity_str(d->severity));
        fprintf(out, ", \"line\": %d, \"column\": %d, \"message\": ", d->lineno, d->column);
        json_print_escaped(out, d->message);
        fputs(", \"suggestion\": ", out);
        if (d->suggestion) json_print_escaped(out, d->suggestion);
        else               fputs("null", out);
        fprintf(out, ", \"fixable\": %s, \"suggested_fix\": ",
                d->replacement_line ? "true" : "false");
        if (d->replacement_line) json_print_escaped(out, d->replacement_line);
        else                     fputs("null", out);
        emit_json_default(out, d);
        fputc('}', out);
    }
    fputs(diags->n ? "\n  ]" : "]", out);

    /* prior_check: mirror the diagnostics that --check-priors contributes. */
    fprintf(out, ",\n  \"prior_check\": {\"requested\": %s, \"issues\": [",
            check_priors ? "true" : "false");
    if (check_priors) {
        int first = 1;
        for (size_t i = 0; i < diags->n; i++) {
            const bpp_diagnostic_t *d = &diags->items[i];
            if (d->code && (strcmp(d->code, "BPP110") == 0 ||
                            strcmp(d->code, "BPP111") == 0)) {
                if (!first) fputs(", ", out);
                first = 0;
                fputs("{\"code\": ", out);
                json_print_escaped(out, d->code);
                fputs(", \"message\": ", out);
                json_print_escaped(out, d->message);
                fputc('}', out);
            }
        }
    }
    fputs("]}", out);

    fprintf(out, ",\n  \"counts\": {\"errors\": %zu, \"warnings\": %zu, \"notes\": %zu}\n}\n",
            n_err, n_warn, n_note);
}

int main(int argc, char **argv) {
    int do_fix       = 0;
    int do_diff      = 0;
    int do_simulate  = 0;
    int quiet        = 0;
    int show_defaults = 1;
    int show_codes   = 0;
    int do_suggest_priors = 0;
    int do_check_priors   = 0;
    int do_json           = 0;
    bpp_color_mode_t color_mode = BPP_COLOR_AUTO;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(a, "--version") == 0) {
            printf("bpp-lint %s\n", BPP_LINT_VERSION);
            return 0;
        } else if (strcmp(a, "--list-codes") == 0) {
            bpp_codes_list(stdout);
            return 0;
        } else if (strcmp(a, "--explain") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --explain requires a code argument (e.g. '020')\n", argv[0]);
                return 2;
            }
            if (bpp_codes_explain(argv[++i], stdout) != 0) {
                fprintf(stderr, "%s: unknown diagnostic code '%s'\n", argv[0], argv[i]);
                return 2;
            }
            return 0;
        } else if (strcmp(a, "-f") == 0 || strcmp(a, "--fix") == 0) {
            do_fix = 1;
        } else if (strcmp(a, "-d") == 0 || strcmp(a, "--diff") == 0) {
            do_diff = 1;
        } else if (strcmp(a, "-s") == 0 || strcmp(a, "--simulate") == 0) {
            do_simulate = 1;
        } else if (strcmp(a, "-q") == 0 || strcmp(a, "--quiet") == 0) {
            quiet = 1;
        } else if (strcmp(a, "--no-defaults") == 0) {
            show_defaults = 0;
        } else if (strcmp(a, "--suggest-priors") == 0) {
            do_suggest_priors = 1;
        } else if (strcmp(a, "--check-priors") == 0) {
            do_check_priors = 1;
        } else if (strcmp(a, "--json") == 0) {
            do_json = 1;
        } else if (strcmp(a, "--codes") == 0) {
            show_codes = 1;
        } else if (strncmp(a, "--color=", 8) == 0) {
            const char *w = a + 8;
            if      (strcmp(w, "auto")   == 0) color_mode = BPP_COLOR_AUTO;
            else if (strcmp(w, "always") == 0) color_mode = BPP_COLOR_ALWAYS;
            else if (strcmp(w, "never")  == 0) color_mode = BPP_COLOR_NEVER;
            else {
                fprintf(stderr, "%s: --color value must be auto|always|never (got '%s')\n", argv[0], w);
                return 2;
            }
        } else if (strcmp(a, "--color") == 0) {
            color_mode = BPP_COLOR_ALWAYS;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], a);
            print_usage(stderr, argv[0]);
            return 2;
        } else {
            if (path) {
                fprintf(stderr, "%s: only one input file supported (got '%s' and '%s')\n",
                        argv[0], path, a);
                return 2;
            }
            path = a;
        }
    }

    if (!path) {
        fprintf(stderr, "%s: missing control-file argument\n", argv[0]);
        print_usage(stderr, argv[0]);
        return 2;
    }

    if (do_fix && do_diff) {
        fprintf(stderr, "%s: --fix and --diff are mutually exclusive\n", argv[0]);
        return 2;
    }

    if (do_json && (do_fix || do_diff || do_suggest_priors)) {
        fprintf(stderr, "%s: --json cannot be combined with --fix, --diff, or --suggest-priors\n", argv[0]);
        return 2;
    }

    bpp_file_t file = {0};
    if (bpp_file_load(&file, path) != 0) {
        fprintf(stderr, "%s: cannot read '%s': %s\n", argv[0], path, strerror(errno));
        return 2;
    }

    /* --suggest-priors short-circuits the rest of the pipeline. */
    if (do_suggest_priors) {
        char *seq_path = NULL, *imap_path = NULL;
        if (resolve_data_paths(&file, path, &seq_path, &imap_path) != 0) {
            bpp_file_free(&file);
            return 2;
        }
        double theta_mean = 0, tau_mean = 0;
        int rc_sp = compute_priors_from_files(seq_path, imap_path, &theta_mean, &tau_mean);
        free(seq_path); free(imap_path);
        bpp_file_free(&file);
        if (rc_sp != 0) return 2;
        printf("# data-derived prior estimates (invgamma alpha=3, mean = data estimate)\n");
        printf("thetaprior = invgamma 3 %.6g\n", theta_mean * 2.0);
        printf("tauprior   = invgamma 3 %.6g\n", tau_mean   * 2.0);
        return 0;
    }

    bpp_lint_opts_t opts = {
        .simulate      = do_simulate,
        .verbose       = 0,
        .show_defaults = show_defaults,
    };
    bpp_diag_list_t diags = {0};
    int errors = bpp_lint(&file, &opts, &diags);

    if (do_check_priors) {
        char *seq_path = NULL, *imap_path = NULL;
        if (resolve_data_paths(&file, path, &seq_path, &imap_path) == 0) {
            double theta_mean = 0, tau_mean = 0;
            if (compute_priors_from_files(seq_path, imap_path,
                                          &theta_mean, &tau_mean) == 0) {
                /* theta: symmetric 10x threshold (both too-tight and too-diffuse). */
                check_one_prior(&file, "thetaprior", theta_mean,
                                "theta", "BPP110", 10.0, /*upper_only=*/0, &diags);
                /* tau: upper-only at 10x. Only flag priors that are clearly
                 * too diffuse against the data upper bound; the bpps max-
                 * distance is loose by design, so a modestly wide prior with
                 * mean at 1-5x the bound is fine. */
                check_one_prior(&file, "tauprior",   tau_mean,
                                "tau",   "BPP111", 10.0, /*upper_only=*/1, &diags);
            }
        }
        free(seq_path); free(imap_path);
    }

    int rc = (errors > 0) ? 1 : 0;

    if (do_json) {
        emit_json(stdout, path, &file, &diags, do_simulate, do_check_priors);
        goto done;
    }

    bpp_color_set(color_mode);
    bpp_codes_set(show_codes);
    if (quiet) filter_quiet(&diags);
    bpp_diag_print(&diags, path);

    if (do_diff) {
        int hunks = bpp_emit_diff(&file, &diags, path, stdout);
        if (hunks > 0) rc = 1;  /* gofmt -d convention: non-zero when rewrite needed */
        goto done;
    }

    if (do_fix) {
        int applied = bpp_apply_fixes(&file, &diags);
        if (applied > 0) {
            /* back up original */
            size_t bn = strlen(path) + 5;
            char *bak = malloc(bn);
            if (!bak) {
                fprintf(stderr, "%s: out of memory\n", argv[0]);
                rc = 2;
                goto done;
            }
            snprintf(bak, bn, "%s.bak", path);
            FILE *in  = fopen(path, "rb");
            FILE *out = fopen(bak, "wb");
            if (!in || !out) {
                fprintf(stderr, "%s: cannot create backup '%s': %s\n",
                        argv[0], bak, strerror(errno));
                if (in)  fclose(in);
                if (out) fclose(out);
                free(bak);
                rc = 2;
                goto done;
            }
            char buf[8192];
            size_t r;
            while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
                fwrite(buf, 1, r, out);
            }
            fclose(in);
            fclose(out);
            free(bak);

            if (bpp_file_write(&file, path) != 0) {
                fprintf(stderr, "%s: cannot write '%s': %s\n",
                        argv[0], path, strerror(errno));
                rc = 2;
                goto done;
            }
            fprintf(stderr, "%s: applied %d fix(es); original backed up to %s.bak\n",
                    argv[0], applied, path);
        } else {
            fprintf(stderr, "%s: --fix specified but no auto-applicable changes were available\n", argv[0]);
        }
    }

done:
    bpp_diag_list_free(&diags);
    bpp_file_free(&file);
    return rc;
}
