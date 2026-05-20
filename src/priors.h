#ifndef BPP_LINT_PRIORS_H
#define BPP_LINT_PRIORS_H

#include "seqfile.h"

/* Pairwise distance between two nucleotide strings, using the IUPAC
 * unphased-diploid distance table from bpps. Returns proportion of mismatch
 * over non-missing sites. */
double bpp_pairwise_distance(const char *s1, const char *s2, int len);

/* Per-bpps PRIOR_CALCULATION_NOTES.md:
 *   priorMeanTheta = average within-species pairwise distance across all
 *                    (species, locus); falls back to coalescent-variance
 *                    estimator when no within-species variation is present.
 *   priorRootAge   = maximum pairwise distance across all sequences/loci.
 *
 * On success, fills *theta_mean and *tau_mean and returns 0. */
int bpp_compute_prior_means(const bpp_seq_grouping_t *g,
                            double *theta_mean,
                            double *tau_mean);

#endif
