#ifndef BPP_LINT_SEQFILE_H
#define BPP_LINT_SEQFILE_H

#include <stddef.h>

#include "imap.h"

/* A single sequence within a locus. */
typedef struct {
    char *name;     /* original line label, without leading '^' */
    char *seq;      /* nucleotide string, length == locus_len */
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
    size_t       n;
    size_t       cap;
} bpp_alignment_t;

/* Read a BPP sequence file. Returns 0 on success, -1 on I/O or format error.
 * Each locus is:
 *   <nseqs> <length>           (whitespace-separated, single line)
 *   <nseqs lines>              '^<name> <sequence>' (case-insensitive)
 * Blank lines between loci are tolerated. */
int  bpp_alignment_load(bpp_alignment_t *al, const char *path);
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
