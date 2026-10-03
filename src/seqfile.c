#define _POSIX_C_SOURCE 200809L

#include "seqfile.h"
#include "imap.h"
#include "lex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- file reader: line-by-line with growable buffer ---------- */

typedef struct {
    FILE  *fp;
    char  *buf;
    size_t cap;
    int    eof;
} line_reader_t;

static int lr_open(line_reader_t *r, const char *path) {
    r->fp = fopen(path, "r");
    if (!r->fp) return -1;
    r->buf = NULL;
    r->cap = 0;
    r->eof = 0;
    return 0;
}

static void lr_close(line_reader_t *r) {
    if (r->fp) fclose(r->fp);
    free(r->buf);
    r->fp = NULL;
    r->buf = NULL;
    r->cap = 0;
}

/* Read one line (without newline). Returns the length, or -1 on EOF with no
 * data. The line is owned by the reader and reused on the next call. */
static long lr_getline(line_reader_t *r) {
    if (r->eof) return -1;
    size_t used = 0;
    int c;
    while ((c = fgetc(r->fp)) != EOF && c != '\n') {
        if (used + 1 >= r->cap) {
            size_t nc = r->cap ? r->cap * 2 : 256;
            char *p = realloc(r->buf, nc);
            if (!p) return -1;
            r->buf = p;
            r->cap = nc;
        }
        r->buf[used++] = (char) c;
    }
    if (c == EOF) {
        r->eof = 1;
        if (used == 0) return -1;
    }
    if (r->cap == 0) {
        r->buf = malloc(1);
        r->cap = 1;
    }
    if (used > 0 && r->buf[used - 1] == '\r') used--;   /* CRLF files */
    r->buf[used] = '\0';
    return (long) used;
}

/* ---------- alignment array growth ---------- */

static int al_reserve(bpp_alignment_t *al, size_t need) {
    if (al->n + need <= al->cap) return 0;
    size_t nc = al->cap ? al->cap * 2 : 4;
    while (nc < al->n + need) nc *= 2;
    bpp_locus_t *p = realloc(al->loci, nc * sizeof(*p));
    if (!p) return -1;
    al->loci = p;
    al->cap = nc;
    return 0;
}

/* ---------- main parser ----------
 *
 * Mirrors BPP's phylip_parse_sequential / phylip_parse_multisequential
 * (bpp-4.8.7 phylip.c:386-680): a locus is a header '<nseqs> <length>' (and
 * nothing else on the line), then nseqs sequences. A sequence starts on a
 * non-blank line with its label (up to the first space or tab), and its data
 * continues on that line and as many following lines as needed until exactly
 * `length` legal characters have been read; blank lines inside are skipped.
 * Characters are classified like pll_map_fasta (maps.c:173): letters, digits,
 * '-' and '?' are sequence data; '.' and control characters are fatal;
 * everything else (spaces, punctuation) is stripped. Loci are separated by
 * any number of blank lines. */

static void locus_free(bpp_locus_t *L) {
    if (!L->seqs) return;
    for (int j = 0; j < L->nseqs; j++) {
        free(L->seqs[j].name);
        free(L->seqs[j].seq);
    }
    free(L->seqs);
    L->seqs = NULL;
    L->nseqs = 0;
}

static char last_error[256];

const char *bpp_alignment_last_error(void) { return last_error; }

#define set_error(...) snprintf(last_error, sizeof last_error, __VA_ARGS__)

static int is_blank(const char *s) {
    while (*s) {
        if (!isspace((unsigned char) *s)) return 0;
        s++;
    }
    return 1;
}

/* pll_map_fasta classes: 1 legal, 2 fatal, 0 stripped. */
static int seqchar_class(unsigned char c) {
    if (c < 0x20) return (c >= 9 && c <= 13) ? 0 : 2;
    if (c == '.') return 2;
    if (c == '-' || c == '?') return 1;
    if (isdigit(c)) return 1;
    if (isalpha(c)) return (c == 'j' || c == 'o') ? 0 : 1;
    return 0;
}

/* Try to parse a line as a locus header "<nseqs> <length>". Returns 1 if
 * matched (sets *ns and *ln), 0 otherwise. */
static int parse_header(const char *line, int *ns, int *ln) {
    const char *p = line;
    while (*p && isspace((unsigned char) *p)) p++;
    if (!isdigit((unsigned char) *p)) return 0;
    char *end1 = NULL;
    long v1 = strtol(p, &end1, 10);
    if (end1 == p) return 0;
    while (*end1 && isspace((unsigned char) *end1)) end1++;
    if (!isdigit((unsigned char) *end1)) return 0;
    char *end2 = NULL;
    long v2 = strtol(end1, &end2, 10);
    if (end2 == end1) return 0;
    while (*end2 && isspace((unsigned char) *end2)) end2++;
    if (*end2 != '\0') return 0;
    if (v1 <= 0 || v2 <= 0) return 0;
    *ns = (int) v1;
    *ln = (int) v2;
    return 1;
}

/* Given a sequence label that may start with '^' (BPP convention), return
 * a freshly-allocated copy of the post-'^' substring. If no '^' is present,
 * the whole label is returned. */
static char *strip_caret(const char *label) {
    const char *q = strchr(label, '^');
    const char *start = q ? q + 1 : label;
    return bpp_strdup(start);
}

/* Append the legal characters of `p` to seq (capacity `len`+1, `*got` filled
 * so far). Returns 0, or -1 on a fatal character / overlong sequence. */
static int consume_seqchars(const char *p, char *seq, int len, int *got,
                            long lineno, const char *label) {
    for (; *p; p++) {
        int cls = seqchar_class((unsigned char) *p);
        if (cls == 0) continue;
        if (cls == 2) {
            set_error("illegal character '%c' in sequence '%s' (line %ld)",
                      *p, label, lineno);
            return -1;
        }
        if (*got >= len) {
            set_error("sequence '%s' is longer than the declared length %d (line %ld)",
                      label, len, lineno);
            return -1;
        }
        seq[(*got)++] = *p;
    }
    return 0;
}

int bpp_alignment_load(bpp_alignment_t *al, const char *path, int max_loci) {
    al->loci = NULL;
    al->n = al->cap = 0;
    al->n_extra = 0;
    al->trailing_unparsed = 0;
    last_error[0] = '\0';
    bpp_locus_t scratch = {0};   /* receives loci beyond max_loci */

    line_reader_t r;
    if (lr_open(&r, path) != 0) {
        snprintf(last_error, sizeof last_error, "cannot open");
        return -1;
    }
    long lineno = 0;
    long len;
    int have_line = 0;   /* r.buf holds an unconsumed line */

#define NEXT_LINE() (have_line ? (have_line = 0, (long) strlen(r.buf)) \
                                : ((len = lr_getline(&r)) >= 0 ? (lineno++, len) : -1))
    /* Beyond max_loci a problem is not an error (BPP stops reading at nloci):
     * note it and stop. Within the stored loci it is fatal. */
#define FAIL() do { \
        if (extra_mode) { al->trailing_unparsed = 1; locus_free(&scratch); goto done; } \
        lr_close(&r); bpp_alignment_free(al); return -1; } while (0)

    while (1) {
        int extra_mode = max_loci > 0 && al->n >= (size_t) max_loci;

        /* skip blanks until a header */
        int ns = 0, ln_ = 0, found = 0;
        while (NEXT_LINE() >= 0) {
            if (is_blank(r.buf)) continue;
            if (parse_header(r.buf, &ns, &ln_)) { found = 1; break; }
            set_error("expected a locus header '<nseqs> <length>' at line %ld, got '%.60s'",
                      lineno, r.buf);
            FAIL();
        }
        if (!found) break;

        bpp_locus_t *L;
        if (extra_mode) {
            L = &scratch;
        } else {
            if (al_reserve(al, 1) != 0) FAIL();
            L = &al->loci[al->n++];
        }
        L->nseqs  = ns;
        L->length = ln_;
        L->seqs   = calloc((size_t) ns, sizeof(bpp_seq_t));
        if (!L->seqs) FAIL();

        for (int got_seqs = 0; got_seqs < ns; got_seqs++) {
            /* label line: first non-blank line */
            long l;
            while ((l = NEXT_LINE()) >= 0 && is_blank(r.buf)) ;
            if (l < 0) {
                set_error("locus %zu: found %d sequence(s) but expected %d",
                          al->n, got_seqs, ns);
                FAIL();
            }
            const char *p = r.buf;
            while (*p && isspace((unsigned char) *p)) p++;
            const char *lbls = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            size_t lblen = (size_t)(p - lbls);
            char *label = malloc(lblen + 1);
            if (!label) FAIL();
            memcpy(label, lbls, lblen);
            label[lblen] = '\0';

            char *seq = malloc((size_t) ln_ + 1);
            if (!seq) { free(label); FAIL(); }
            int got = 0;
            if (consume_seqchars(p, seq, ln_, &got, lineno, label) != 0) {
                free(label); free(seq); FAIL();
            }
            /* continuation lines until the declared length is reached */
            while (got < ln_) {
                if (NEXT_LINE() < 0) {
                    set_error("sequence '%s' has %d characters but expected %d",
                              label, got, ln_);
                    free(label); free(seq); FAIL();
                }
                if (consume_seqchars(r.buf, seq, ln_, &got, lineno, label) != 0) {
                    free(label); free(seq); FAIL();
                }
            }
            seq[got] = '\0';

            L->seqs[got_seqs].name    = strip_caret(label);
            L->seqs[got_seqs].has_tag = strchr(label, '^') != NULL;
            L->seqs[got_seqs].seq     = seq;
            free(label);
        }
        if (extra_mode) { al->n_extra++; locus_free(&scratch); }
    }
done:
#undef NEXT_LINE
#undef FAIL
    lr_close(&r);
    return 0;
}

void bpp_alignment_free(bpp_alignment_t *al) {
    if (!al) return;
    for (size_t i = 0; i < al->n; i++) locus_free(&al->loci[i]);
    free(al->loci);
    al->loci = NULL;
    al->n = al->cap = 0;
}

/* ---------- group by species ---------- */

int bpp_group_by_species(const bpp_alignment_t *al,
                         const bpp_imap_t *imap,
                         char *const *species_names, int n_species,
                         bpp_seq_grouping_t *out)
{
    out->n_loci    = (int) al->n;
    out->n_species = n_species;
    out->loci      = calloc((size_t) out->n_loci, sizeof(bpp_locus_group_t));
    if (!out->loci && out->n_loci > 0) return -1;

    for (int li = 0; li < out->n_loci; li++) {
        const bpp_locus_t *L = &al->loci[li];
        bpp_locus_group_t *G = &out->loci[li];
        G->length    = L->length;
        G->n_species = n_species;
        G->counts    = calloc((size_t) n_species, sizeof(int));
        G->buckets   = calloc((size_t) n_species, sizeof(const char **));
        if (!G->counts || !G->buckets) return -1;

        /* Pass 1: count sequences per species. */
        for (int s = 0; s < L->nseqs; s++) {
            const char *sp = bpp_imap_lookup(imap, L->seqs[s].name);
            if (!sp) continue;
            for (int k = 0; k < n_species; k++) {
                if (strcmp(species_names[k], sp) == 0) { G->counts[k]++; break; }
            }
        }
        /* Allocate per-species pointer arrays. */
        for (int k = 0; k < n_species; k++) {
            if (G->counts[k] > 0) {
                G->buckets[k] = malloc((size_t) G->counts[k] * sizeof(const char *));
                if (!G->buckets[k]) return -1;
            }
        }
        /* Pass 2: fill them. */
        int *cursor = calloc((size_t) n_species, sizeof(int));
        if (!cursor) return -1;
        for (int s = 0; s < L->nseqs; s++) {
            const char *sp = bpp_imap_lookup(imap, L->seqs[s].name);
            if (!sp) continue;
            for (int k = 0; k < n_species; k++) {
                if (strcmp(species_names[k], sp) == 0) {
                    G->buckets[k][cursor[k]++] = L->seqs[s].seq;
                    break;
                }
            }
        }
        free(cursor);
    }
    return 0;
}

void bpp_seq_grouping_free(bpp_seq_grouping_t *g) {
    if (!g || !g->loci) return;
    for (int li = 0; li < g->n_loci; li++) {
        bpp_locus_group_t *G = &g->loci[li];
        if (G->buckets) {
            for (int k = 0; k < G->n_species; k++) free((void *) G->buckets[k]);
            free(G->buckets);
        }
        free(G->counts);
    }
    free(g->loci);
    g->loci = NULL;
    g->n_loci = g->n_species = 0;
}
