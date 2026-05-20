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

/* ---------- main parser ---------- */

static int is_blank(const char *s) {
    while (*s) {
        if (!isspace((unsigned char) *s)) return 0;
        s++;
    }
    return 1;
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

int bpp_alignment_load(bpp_alignment_t *al, const char *path) {
    al->loci = NULL;
    al->n = al->cap = 0;

    line_reader_t r;
    if (lr_open(&r, path) != 0) return -1;

    while (1) {
        /* skip blanks until a header */
        int ns = 0, ln_ = 0;
        int found = 0;
        while (1) {
            long len = lr_getline(&r);
            if (len < 0) { goto done; }
            if (is_blank(r.buf)) continue;
            if (parse_header(r.buf, &ns, &ln_)) { found = 1; break; }
            /* Found a non-blank, non-header line outside of a locus -> error */
            lr_close(&r);
            bpp_alignment_free(al);
            return -1;
        }
        if (!found) break;

        if (al_reserve(al, 1) != 0) { lr_close(&r); bpp_alignment_free(al); return -1; }
        bpp_locus_t *L = &al->loci[al->n++];
        L->nseqs  = ns;
        L->length = ln_;
        L->seqs   = calloc((size_t) ns, sizeof(bpp_seq_t));
        if (!L->seqs) { lr_close(&r); bpp_alignment_free(al); return -1; }

        int got = 0;
        while (got < ns) {
            long llen = lr_getline(&r);
            if (llen < 0) {
                lr_close(&r); bpp_alignment_free(al); return -1;
            }
            if (is_blank(r.buf)) continue;
            /* Parse "<label> <sequence>" — split on first whitespace. */
            const char *p = r.buf;
            while (*p && isspace((unsigned char) *p)) p++;
            const char *lbls = p;
            while (*p && !isspace((unsigned char) *p)) p++;
            size_t lblen = (size_t)(p - lbls);
            while (*p && isspace((unsigned char) *p)) p++;
            const char *seqs = p;
            /* sequence may have embedded spaces (PHYLIP interleaved style);
             * compact by stripping all whitespace. */
            size_t seqcap = (size_t) ln_ + 8;
            char *seq = malloc(seqcap);
            if (!seq) { lr_close(&r); bpp_alignment_free(al); return -1; }
            size_t sw = 0;
            for (const char *q = seqs; *q; q++) {
                if (isspace((unsigned char) *q)) continue;
                if (sw + 1 >= seqcap) {
                    seqcap *= 2;
                    char *np = realloc(seq, seqcap);
                    if (!np) { free(seq); lr_close(&r); bpp_alignment_free(al); return -1; }
                    seq = np;
                }
                seq[sw++] = *q;
            }
            seq[sw] = '\0';

            char *label = malloc(lblen + 1);
            if (!label) { free(seq); lr_close(&r); bpp_alignment_free(al); return -1; }
            memcpy(label, lbls, lblen);
            label[lblen] = '\0';

            L->seqs[got].name = strip_caret(label);
            free(label);
            L->seqs[got].seq  = seq;
            got++;
        }
    }
done:
    lr_close(&r);
    return 0;
}

void bpp_alignment_free(bpp_alignment_t *al) {
    if (!al) return;
    for (size_t i = 0; i < al->n; i++) {
        for (int j = 0; j < al->loci[i].nseqs; j++) {
            free(al->loci[i].seqs[j].name);
            free(al->loci[i].seqs[j].seq);
        }
        free(al->loci[i].seqs);
    }
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
