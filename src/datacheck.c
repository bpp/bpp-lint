#define _POSIX_C_SOURCE 200809L

#include "datacheck.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- small helpers ---------- */

static const bpp_line_t *find_line(const bpp_file_t *f, const char *key) {
    for (size_t i = 0; i < f->n; i++)
        if (f->lines[i].key && bpp_strieq(f->lines[i].key, key)) return &f->lines[i];
    return NULL;
}

static int first_int(const char *v) {
    if (!v) return 0;
    while (*v && isspace((unsigned char) *v)) v++;
    return atoi(v);
}

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

/* Directory portion of `path` without the trailing slash; "." if none. */
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

static char *path_join(const char *dir, const char *name) {
    if (name[0] == '/' || strcmp(dir, ".") == 0) return bpp_strdup(name);
    return bpp_asprintf("%s/%s", dir, name);
}

static int can_open(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

/* Absolute form of `path`: realpath when it exists, else cwd-joined. */
static char *absolutize(const char *path) {
    char *rp = realpath(path, NULL);
    if (rp) return rp;
    if (path[0] == '/') return bpp_strdup(path);
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return bpp_strdup(path);
    if (path[0] == '.' && path[1] == '/') path += 2;   /* drop "./" */
    return bpp_asprintf("%s/%s", cwd, path);
}

/* Fill the resolution fields of `df` for the control-file line `key`. */
static int resolve_one(const bpp_file_t *f, const char *cfile_path,
                       const char *key, bpp_data_file_t *df)
{
    df->line = find_line(f, key);
    if (!df->line) return 0;
    df->name = first_token_dup(df->line->value);
    if (!df->name) return 0;                 /* empty value: BPP004 covers it */

    char *dir = path_dirname(cfile_path);
    if (!dir) return -1;
    df->path_ctl = path_join(dir, df->name);
    df->path_cwd = bpp_strdup(df->name);
    free(dir);
    if (!df->path_ctl || !df->path_cwd) return -1;

    df->ctl_exists = can_open(df->path_ctl);
    df->cwd_exists = can_open(df->path_cwd);
    if (strcmp(df->path_ctl, df->path_cwd) == 0) {
        df->same = 1;
    } else if (df->ctl_exists && df->cwd_exists) {
        char *a = realpath(df->path_ctl, NULL), *b = realpath(df->path_cwd, NULL);
        df->same = (a && b && strcmp(a, b) == 0);
        free(a); free(b);
    } else {
        df->same = 0;
    }
    return 0;
}

/* Which path to load from: the control-file-relative one, falling back to
 * the cwd-relative one BPP would use when only that exists. */
static const char *load_path(bpp_data_file_t *df) {
    if (df->ctl_exists || !df->cwd_exists) return df->path_ctl;
    df->used_cwd = 1;
    return df->path_cwd;
}

/* ---------- load ---------- */

int bpp_data_load(const bpp_file_t *f, const char *cfile_path, bpp_data_t *d) {
    if (resolve_one(f, cfile_path, "seqfile",  &d->seq)  != 0) return -1;
    if (resolve_one(f, cfile_path, "imapfile", &d->imap) != 0) return -1;

    const bpp_line_t *st = find_line(f, "species&tree");
    d->n_species = (st && st->value) ? first_int(st->value) : 0;
    d->species   = bpp_species_tree_names(f, &d->n_species_names);

    /* Read the loci exactly as BPP will: nloci of them (0 = all). Anything
     * after locus nloci is only counted, never a reason to fail. */
    const bpp_line_t *nl = find_line(f, "nloci");
    int max_loci = (nl && nl->value) ? first_int(nl->value) : 0;
    if (max_loci < 0) max_loci = 0;

    if (d->seq.name) {
        const char *p = load_path(&d->seq);
        d->seq.abs = absolutize(p);
        if (d->seq.ctl_exists || d->seq.cwd_exists) {
            if (bpp_alignment_load(&d->al, p, max_loci) == 0) d->seq.loaded = 1;
            else d->seq.parse_failed = 1;
        }
    }
    if (d->imap.name) {
        const char *p = load_path(&d->imap);
        d->imap.abs = absolutize(p);
        if (d->imap.ctl_exists || d->imap.cwd_exists) {
            if (bpp_imap_load(&d->map, p) == 0) {
                d->imap.loaded = 1;
                d->imap_species = bpp_imap_species_list(&d->map, &d->n_imap_species);
            } else {
                d->imap.parse_failed = 1;
            }
        }
    }

    /* distinct sequence tags across loci */
    if (d->seq.loaded) {
        size_t cap = 64, n = 0;
        const char **seen = malloc(cap * sizeof(*seen));
        if (!seen) return -1;
        for (size_t li = 0; li < d->al.n; li++) {
            const bpp_locus_t *L = &d->al.loci[li];
            for (int s = 0; s < L->nseqs; s++) {
                const char *nm = L->seqs[s].name;
                size_t k;
                for (k = 0; k < n; k++) if (strcmp(seen[k], nm) == 0) break;
                if (k < n) continue;
                if (n == cap) {
                    cap *= 2;
                    const char **np = realloc(seen, cap * sizeof(*seen));
                    if (!np) { free(seen); return -1; }
                    seen = np;
                }
                seen[n++] = nm;
            }
        }
        d->n_distinct_tags = (int) n;
        free(seen);
    }
    return 0;
}

void bpp_data_free(bpp_data_t *d) {
    if (!d) return;
    free(d->seq.name);  free(d->seq.path_ctl);  free(d->seq.path_cwd);  free(d->seq.abs);
    free(d->imap.name); free(d->imap.path_ctl); free(d->imap.path_cwd); free(d->imap.abs);
    if (d->seq.loaded)  bpp_alignment_free(&d->al);
    if (d->imap.loaded) bpp_imap_free(&d->map);
    free(d->imap_species);
    bpp_names_free(d->species, d->n_species_names);
    memset(d, 0, sizeof *d);
}

/* ---------- checks ---------- */

/* A comma-separated list capped at `max` items; counts the overflow. */
typedef struct { char *buf; int n; int max; } namelist_t;

static void namelist_add(namelist_t *nl, const char *s) {
    nl->n++;
    if (nl->n > nl->max) return;
    char *nb = nl->buf ? bpp_asprintf("%s, %s", nl->buf, s) : bpp_strdup(s);
    free(nl->buf);
    nl->buf = nb;
}

static char *namelist_text(const namelist_t *nl) {
    if (!nl->buf) return bpp_strdup("");
    if (nl->n > nl->max) return bpp_asprintf("%s, ... (%d in total)", nl->buf, nl->n);
    return bpp_strdup(nl->buf);
}

/* Join an array of names with ", " (caller frees). */
static char *join_names(char *const *names, size_t n) {
    char *buf = bpp_strdup("");
    for (size_t i = 0; i < n && buf; i++) {
        char *nb = i ? bpp_asprintf("%s, %s", buf, names[i]) : bpp_strdup(names[i]);
        free(buf);
        buf = nb;
    }
    return buf;
}

static int in_names(char *const *names, size_t n, const char *s) {
    for (size_t i = 0; i < n; i++) if (strcmp(names[i], s) == 0) return 1;
    return 0;
}

/* BPP150 / BPP151 for one data file. `what` is "seqfile" / "Imapfile". */
static int check_file_access(const bpp_data_file_t *df, const char *what,
                             bpp_diag_list_t *out)
{
    int errors = 0;
    if (!df->name) return 0;
    int lineno = df->line->lineno, col = df->line->val_col;

    if (!df->ctl_exists && !df->cwd_exists) {
        char *where = df->same
            ? bpp_asprintf("looked for %s", df->abs)
            : bpp_asprintf("looked next to the control file (%s) and relative to the "
                           "current directory (%s); BPP uses the latter",
                           df->path_ctl, df->path_cwd);
        bpp_diag_add(out, SEV_ERROR, lineno, col, "BPP150",
                     bpp_asprintf("cannot open %s '%s' (%s); BPP aborts (\"Unable to open file (%s)\")",
                                  what, df->name, where, df->name),
                     bpp_asprintf("check the path; BPP resolves it relative to the directory "
                                  "it is run from, not the control file's"));
        free(where);
        return 1;
    }
    if (df->parse_failed) {
        int is_seq = strcmp(what, "seqfile") == 0;
        bpp_diag_add(out, SEV_ERROR, lineno, col, "BPP150",
                     is_seq
                         ? bpp_asprintf("%s '%s' (%s) is not a readable BPP sequence file: %s",
                                        what, df->name, df->abs, bpp_alignment_last_error())
                         : bpp_asprintf("%s '%s' (%s) is not a readable Imap file "
                                        "(expected '<individual> <species>' lines)",
                                        what, df->name, df->abs),
                     is_seq
                         ? bpp_asprintf("BPP's format: a '<nseqs> <length>' header per locus, then "
                                        "'<label> <sequence>' with the sequence allowed to wrap "
                                        "onto following lines")
                         : NULL);
        errors++;
    }
    if (!df->same) {
        char *msg;
        if (df->ctl_exists && !df->cwd_exists) {
            msg = bpp_asprintf("%s '%s' exists next to the control file (%s) but BPP opens it "
                               "relative to the current directory, where it does not exist; "
                               "BPP aborts (\"Unable to open file (%s)\") unless run from the "
                               "control file's directory", what, df->name, df->abs, df->name);
        } else if (!df->ctl_exists && df->cwd_exists) {
            msg = bpp_asprintf("%s '%s' is not next to the control file (%s) but exists relative "
                               "to the current directory (%s), which is what BPP opens; "
                               "bpp-lint used that file", what, df->name, df->path_ctl, df->abs);
        } else {
            char *cwdabs = realpath(df->path_cwd, NULL);
            msg = bpp_asprintf("%s '%s' names a different file next to the control file (%s) "
                               "than relative to the current directory (%s); BPP opens the "
                               "latter, bpp-lint checked the former", what, df->name, df->abs,
                               cwdabs ? cwdabs : df->path_cwd);
            free(cwdabs);
        }
        bpp_diag_add(out, SEV_WARNING, lineno, col, "BPP151", msg,
                     bpp_asprintf("run bpp from the control file's directory, or write the path "
                                  "relative to where bpp will run"));
    }
    return errors;
}

/* Line index of the species&tree counts line (first content line after the
 * header), or -1. Mirrors lint.c's next_content_line. */
static int counts_line_index(const bpp_file_t *f) {
    const bpp_line_t *header = find_line(f, "species&tree");
    if (!header) return -1;
    for (size_t i = (size_t)(header - f->lines) + 1; i < f->n; i++) {
        const char *p = f->lines[i].raw;
        if (!p || bpp_is_blank(p)) continue;
        while (*p && isspace((unsigned char) *p)) p++;
        if (*p == '*' || *p == '#') continue;
        if (f->lines[i].key != NULL) return -1;
        return (int) i;
    }
    return -1;
}

int bpp_data_check(const bpp_file_t *f, const bpp_data_t *d, bpp_diag_list_t *out) {
    int errors = 0;
    int multi = d->n_species > 1;   /* BPP parses the Imap only then: method.c:3550 */

    /* --- BPP150 / BPP151: can the files be opened, and from where ---
     * The seqfile is opened unconditionally, even with usedata = 0
     * (method.c:3296 phylip_open precedes any usedata test). */
    errors += check_file_access(&d->seq, "seqfile", out);
    if (multi) errors += check_file_access(&d->imap, "Imapfile", out);

    /* --- BPP152: nloci vs loci present (method.c:3308-3313). The loader
     * stored at most nloci loci; n_extra / trailing_unparsed describe what
     * follows them, which BPP never reads. --- */
    const bpp_line_t *nl = find_line(f, "nloci");
    if (d->seq.loaded && nl && nl->value) {
        int want = first_int(nl->value);
        int have = (int) d->al.n;
        if (want > have) {
            bpp_diag_add(out, SEV_ERROR, nl->lineno, nl->val_col, "BPP152",
                         bpp_asprintf("'nloci = %d' but seqfile '%s' contains only %d loc%s; "
                                      "BPP aborts (\"Expected %d loci but found only %d\")",
                                      want, d->seq.name, have, have == 1 ? "us" : "i", want, have),
                         bpp_asprintf("set nloci = %d (or 0 to use every locus in the file)", have));
            errors++;
        } else if (want == 0) {
            bpp_diag_add(out, SEV_INFO, nl->lineno, nl->val_col, "BPP152",
                         bpp_asprintf("'nloci = 0': BPP will use all %d loci in '%s'",
                                      have, d->seq.name),
                         NULL);
        } else if (d->al.n_extra > 0 || d->al.trailing_unparsed) {
            bpp_diag_add(out, SEV_INFO, nl->lineno, nl->val_col, "BPP152",
                         d->al.n_extra > 0
                             ? bpp_asprintf("'nloci = %d' but seqfile '%s' contains %zu loci%s; "
                                            "BPP uses only the first %d",
                                            want, d->seq.name, d->al.n + d->al.n_extra,
                                            d->al.trailing_unparsed
                                                ? " (followed by content that is not a locus)" : "",
                                            want)
                             : bpp_asprintf("'nloci = %d': seqfile '%s' has content after locus %d "
                                            "that is not a further locus (%s); BPP stops reading "
                                            "at nloci and never sees it",
                                            want, d->seq.name, want, bpp_alignment_last_error()),
                         NULL);
        }
    }

    /* --- BPP157: every sequence label needs a '^' species tag (stree.c:1222) --- */
    if (multi && d->seq.loaded) {
        namelist_t missing = { NULL, 0, 4 };
        const char *first = NULL; int first_locus = 0;
        size_t cap = 64, n = 0;
        const char **seen = malloc(cap * sizeof(*seen));   /* distinct labels */
        for (size_t li = 0; seen && li < d->al.n; li++) {
            const bpp_locus_t *L = &d->al.loci[li];
            for (int s = 0; s < L->nseqs; s++) {
                if (L->seqs[s].has_tag) continue;
                const char *nm = L->seqs[s].name;
                size_t k;
                for (k = 0; k < n; k++) if (strcmp(seen[k], nm) == 0) break;
                if (k < n) continue;
                if (n == cap) {
                    cap *= 2;
                    const char **np = realloc(seen, cap * sizeof(*seen));
                    if (!np) break;
                    seen = np;
                }
                seen[n++] = nm;
                if (!first) { first = nm; first_locus = (int) li; }
                namelist_add(&missing, nm);
            }
        }
        free(seen);
        if (missing.n > 0) {
            char *list = namelist_text(&missing);
            bpp_diag_add(out, SEV_ERROR, d->seq.line->lineno, d->seq.line->val_col, "BPP157",
                         bpp_asprintf("%d sequence label%s in seqfile '%s' %s no '^' species tag (%s); "
                                      "BPP aborts (\"Cannot find species tag on sequence %s of locus %d\")",
                                      missing.n, missing.n == 1 ? "" : "s", d->seq.name,
                                      missing.n == 1 ? "has" : "have", list, first, first_locus),
                         bpp_asprintf("with more than one species every label must be "
                                      "'<anything>^<individual>', where <individual> is an Imap entry"));
            free(list);
            errors++;
        }
        free(missing.buf);
    }

    /* --- BPP153: every tag must have an Imap entry (stree.c:1240-1242) --- */
    if (multi && d->seq.loaded && d->imap.loaded) {
        namelist_t missing = { NULL, 0, 5 };
        const char *first = NULL;
        /* distinct tags only, so each individual is reported once */
        size_t cap = 64, n = 0;
        const char **seen = malloc(cap * sizeof(*seen));
        for (size_t li = 0; seen && li < d->al.n; li++) {
            const bpp_locus_t *L = &d->al.loci[li];
            for (int s = 0; s < L->nseqs; s++) {
                if (!L->seqs[s].has_tag) continue;
                const char *tag = L->seqs[s].name;
                if (bpp_imap_lookup(&d->map, tag)) continue;
                size_t k;
                for (k = 0; k < n; k++) if (strcmp(seen[k], tag) == 0) break;
                if (k < n) continue;
                if (n == cap) {
                    cap *= 2;
                    const char **np = realloc(seen, cap * sizeof(*seen));
                    if (!np) break;
                    seen = np;
                }
                seen[n++] = tag;
                if (!first) first = tag;
                namelist_add(&missing, tag);
            }
        }
        free(seen);
        if (missing.n > 0) {
            char *list = namelist_text(&missing);
            bpp_diag_add(out, SEV_ERROR, d->imap.line->lineno, d->imap.line->val_col, "BPP153",
                         bpp_asprintf("%d sequence tag%s in seqfile '%s' %s no entry in Imap '%s' (%s); "
                                      "BPP aborts (\"Cannot find a mapping to species for tag %s inside file %s\")",
                                      missing.n, missing.n == 1 ? "" : "s", d->seq.name,
                                      missing.n == 1 ? "has" : "have", d->imap.name, list,
                                      first, d->imap.name),
                         bpp_asprintf("add an '<individual> <species>' line to the Imap for each tag "
                                      "(matching is case-sensitive)"));
            free(list);
            errors++;
        }
        free(missing.buf);
    }

    /* --- BPP154: Imap species vs species&tree names (mapping.c:58-65) --- */
    if (multi && d->imap.loaded && d->species && d->n_species_names > 0) {
        /* Imap species that are not a species-tree tip: fatal in BPP. */
        namelist_t extra = { NULL, 0, 5 };
        const char *first = NULL;
        for (size_t i = 0; i < d->n_imap_species; i++) {
            if (in_names(d->species, (size_t) d->n_species_names, d->imap_species[i])) continue;
            if (!first) first = d->imap_species[i];
            namelist_add(&extra, d->imap_species[i]);
        }
        if (extra.n > 0) {
            char *list = namelist_text(&extra);
            char *tree = join_names(d->species, (size_t) d->n_species_names);
            bpp_diag_add(out, SEV_ERROR, d->imap.line->lineno, d->imap.line->val_col, "BPP154",
                         bpp_asprintf("Imap '%s' maps individuals to species %s, which %s not in "
                                      "'species&tree' (%s); BPP aborts (\"Cannot find node with "
                                      "population label %s\")",
                                      d->imap.name, list, extra.n == 1 ? "is" : "are", tree, first),
                         bpp_asprintf("species names must match exactly (case-sensitive) between "
                                      "the Imap and the species&tree header and Newick"));
            free(list); free(tree);
            errors++;
        }
        free(extra.buf);

        /* Tree species with no Imap individuals: BPP runs (verified against
         * 4.8.7), the population simply has no sequences -- warn. */
        namelist_t empty = { NULL, 0, 5 };
        for (int i = 0; i < d->n_species_names; i++) {
            if (in_names(d->imap_species, d->n_imap_species, d->species[i])) continue;
            namelist_add(&empty, d->species[i]);
        }
        if (empty.n > 0) {
            char *list = namelist_text(&empty);
            char *imsp = join_names(d->imap_species, d->n_imap_species);
            const bpp_line_t *st = find_line(f, "species&tree");
            bpp_diag_add(out, SEV_WARNING, st->lineno, st->val_col, "BPP154",
                         bpp_asprintf("species %s in 'species&tree' %s no individuals in Imap '%s' "
                                      "(Imap species: %s); BPP runs, but the population has no data",
                                      list, empty.n == 1 ? "has" : "have", d->imap.name, imsp),
                         bpp_asprintf("check for a misspelt species name in the Imap or the tree"));
            free(list); free(imsp);
        }
        free(empty.buf);
    }

    /* --- BPP156: per-species counts vs Imap individuals (informational) ---
     * Inference reads the counts (cfile.c:2143-2148) but never uses them;
     * only --simulate does (simulate.c). */
    if (multi && d->imap.loaded && d->species && d->n_species_names == d->n_species) {
        int ci = counts_line_index(f);
        if (ci >= 0) {
            const bpp_line_t *cl = &f->lines[ci];
            char *ctoks[256];
            int nc = bpp_tokenise_value(cl->raw, ctoks, 256);
            if (nc == d->n_species) {
                namelist_t diffs = { NULL, 0, 6 };
                for (int k = 0; k < d->n_species; k++) {
                    /* distinct individuals mapped to species k */
                    int indiv = 0;
                    for (size_t i = 0; i < d->map.n; i++) {
                        if (strcmp(d->map.entries[i].species, d->species[k]) != 0) continue;
                        size_t j;
                        for (j = 0; j < i; j++)
                            if (strcmp(d->map.entries[j].individual, d->map.entries[i].individual) == 0 &&
                                strcmp(d->map.entries[j].species, d->species[k]) == 0) break;
                        if (j == i) indiv++;
                    }
                    char *endp = NULL;
                    long declared = strtol(ctoks[k], &endp, 10);
                    if (!endp || *endp != '\0') continue;    /* BPP132 covers it */
                    if (declared != indiv) {
                        char *item = bpp_asprintf("%s: %ld declared, %d in Imap",
                                                  d->species[k], declared, indiv);
                        namelist_add(&diffs, item);
                        free(item);
                    }
                }
                if (diffs.n > 0) {
                    char *list = namelist_text(&diffs);
                    bpp_diag_add(out, SEV_INFO, cl->lineno, 1, "BPP156",
                                 bpp_asprintf("'species&tree' per-species counts differ from the "
                                              "Imap (%s); inference ignores these counts, only "
                                              "--simulate uses them", list),
                                 NULL);
                    free(list);
                }
                free(diffs.buf);
            }
            if (nc > 0) bpp_tokens_free(ctoks, nc);
        }
    }

    return errors;
}
