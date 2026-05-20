#include "lex.h"
#include "lint.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BPP_LINT_VERSION "0.1.0"

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

int main(int argc, char **argv) {
    int do_fix       = 0;
    int do_diff      = 0;
    int do_simulate  = 0;
    int quiet        = 0;
    int show_defaults = 1;
    int show_codes   = 0;
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

    bpp_file_t file = {0};
    if (bpp_file_load(&file, path) != 0) {
        fprintf(stderr, "%s: cannot read '%s': %s\n", argv[0], path, strerror(errno));
        return 2;
    }

    bpp_lint_opts_t opts = {
        .simulate      = do_simulate,
        .verbose       = 0,
        .show_defaults = show_defaults,
    };
    bpp_diag_list_t diags = {0};
    int errors = bpp_lint(&file, &opts, &diags);

    bpp_color_set(color_mode);
    bpp_codes_set(show_codes);
    if (quiet) filter_quiet(&diags);
    bpp_diag_print(&diags, path);

    int rc = (errors > 0) ? 1 : 0;

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
