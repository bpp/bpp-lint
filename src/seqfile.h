#ifndef BPP_LINT_SEQFILE_H
#define BPP_LINT_SEQFILE_H

#include <stddef.h>

#include "imap.h"

/* A single sequence within a locus. */
typedef struct {
    char *name;     /* species tag: the text after '^', or the whole label if
                     * the label has no '^' */
    char *seq;      /* nucleotide string, length == locus_len */
    int   has_tag;  /* 1 if the label contained a '^' (BPP requires it for
                     * multi-species runs: stree.c:1222 "Cannot find species
                     * tag on sequence") */
} bpp_seq_t;

/* One locus. */
typedef struct {
    int        nseqs;
    int        length;
    bpp_seq_t *seqs;
} bpp_locus_t;

/* Whole alignment. */
typedef struct {
    bpp_locus_t *loci;
    size_t       n;            /* loci stored (at most max_loci when that is > 0) */
    size_t       cap;
    size_t       n_extra;      /* further complete loci found after the first
                                * max_loci (parsed and discarded, not stored) */
    int          trailing_unparsed; /* content after the stored/extra loci that
                                * is not a locus; BPP never reads it when it
                                * stops at nloci, so it is not an error */
} bpp_alignment_t;

/* Read a BPP sequence file (sequential PHYLIP, as BPP's phylip.c reads it).
 * Returns 0 on success, -1 on I/O or format error. Each locus is:
 *   <nseqs> <length>           (whitespace-separated, single line)
 *   <nseqs sequences>          '<label> <sequence>'; the sequence may wrap
 *                              onto following lines until <length> characters
 *                              (letters, digits, '-', '?') have been read
 * Blank lines between loci (and inside wrapped sequences) are tolerated. */
/* `max_loci` > 0 mirrors BPP's nloci: exactly that many loci are read and
 * stored (phylip_parse_multisequential stops there, phylip.c:656), and
 * whatever follows is only counted into n_extra / trailing_unparsed. 0 reads
 * every locus. */
int  bpp_alignment_load(bpp_alignment_t *al, const char *path, int max_loci);

/* Why the last bpp_alignment_load failed (empty if it succeeded). */
const char *bpp_alignment_last_error(void);
void bpp_alignment_free(bpp_alignment_t *al);

/* Grouped view: one bucket per (locus, species) pair. All sequence pointers
 * alias data owned by the underlying bpp_alignment_t; do not free them. */
typedef struct {
    int            length;       /* locus length */
    int            n_species;
    int           *counts;       /* [n_species] */
    const char  ***buckets;      /* [n_species][counts[k]] */
} bpp_locus_group_t;

typedef struct {
    int                 n_loci;
    int                 n_species;
    bpp_locus_group_t  *loci;    /* [n_loci] */
} bpp_seq_grouping_t;

int  bpp_group_by_species(const bpp_alignment_t *al,
                          const bpp_imap_t *imap,
                          char *const *species_names, int n_species,
                          bpp_seq_grouping_t *out);

void bpp_seq_grouping_free(bpp_seq_grouping_t *g);

#endif
