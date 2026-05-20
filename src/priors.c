#include "priors.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- IUPAC distance table ----------
 * Verbatim port of bpps `unphasedDistances` (src/PriorFunc.js). Values are
 * mismatch probabilities for unphased diploid genotypes; symmetric, indexed
 * by a 16-entry IUPAC index. Missing data (-, ?, N) yields a sentinel that
 * causes the calling code to skip the site. */

enum {
    I_A = 0, I_C, I_G, I_T,
    I_R, I_Y, I_S, I_W, I_K, I_M,
    I_B, I_D, I_H, I_V,
    I_MISSING,
    I_UNKNOWN,
    I_COUNT
};

static int char_to_idx(char ch) {
    switch (tolower((unsigned char) ch)) {
    case 'a': return I_A;
    case 'c': return I_C;
    case 'g': return I_G;
    case 't': case 'u': return I_T;
    case 'r': return I_R;
    case 'y': return I_Y;
    case 's': return I_S;
    case 'w': return I_W;
    case 'k': return I_K;
    case 'm': return I_M;
    case 'b': return I_B;
    case 'd': return I_D;
    case 'h': return I_H;
    case 'v': return I_V;
    case 'n': case '-': case '?': return I_MISSING;
    default: return I_UNKNOWN;
    }
}

static double iupac_table[I_COUNT][I_COUNT];
static int    iupac_initialized = 0;

/* Set table[i][j] = table[j][i] = d (symmetric). */
static void set_pair(int i, int j, double d) {
    iupac_table[i][j] = iupac_table[j][i] = d;
}

static void iupac_init(void) {
    if (iupac_initialized) return;

    /* Default: 1.0 (any unrecognised pair is a full mismatch). */
    for (int i = 0; i < I_COUNT; i++)
        for (int j = 0; j < I_COUNT; j++)
            iupac_table[i][j] = 1.0;

    /* Diagonals for unambiguous nucleotides: 0 (identical). */
    set_pair(I_A, I_A, 0.0);
    set_pair(I_C, I_C, 0.0);
    set_pair(I_G, I_G, 0.0);
    set_pair(I_T, I_T, 0.0);

    /* Mixed unambiguous: 1. (already default — but explicit for clarity.) */
    set_pair(I_A, I_C, 1.0);
    set_pair(I_A, I_G, 1.0);
    set_pair(I_A, I_T, 1.0);
    set_pair(I_C, I_G, 1.0);
    set_pair(I_C, I_T, 1.0);
    set_pair(I_G, I_T, 1.0);

    /* Unambiguous vs 2-letter ambiguity. */
    set_pair(I_A, I_R, 0.5);   set_pair(I_A, I_Y, 1.0);
    set_pair(I_A, I_S, 1.0);   set_pair(I_A, I_W, 0.5);
    set_pair(I_A, I_K, 1.0);   set_pair(I_A, I_M, 0.5);
    set_pair(I_C, I_R, 1.0);   set_pair(I_C, I_Y, 0.5);
    set_pair(I_C, I_S, 0.5);   set_pair(I_C, I_W, 1.0);
    set_pair(I_C, I_K, 1.0);   set_pair(I_C, I_M, 0.5);
    set_pair(I_G, I_R, 0.5);   set_pair(I_G, I_Y, 1.0);
    set_pair(I_G, I_S, 0.5);   set_pair(I_G, I_W, 1.0);
    set_pair(I_G, I_K, 0.5);   set_pair(I_G, I_M, 1.0);
    set_pair(I_T, I_R, 1.0);   set_pair(I_T, I_Y, 0.5);
    set_pair(I_T, I_S, 1.0);   set_pair(I_T, I_W, 0.5);
    set_pair(I_T, I_K, 0.5);   set_pair(I_T, I_M, 1.0);

    /* Unambiguous vs 3-letter ambiguity. */
    set_pair(I_A, I_B, 1.0);   set_pair(I_A, I_D, 1.0/3.0);
    set_pair(I_A, I_H, 1.0/3.0); set_pair(I_A, I_V, 1.0/3.0);
    set_pair(I_C, I_B, 1.0/3.0); set_pair(I_C, I_D, 1.0);
    set_pair(I_C, I_H, 1.0/3.0); set_pair(I_C, I_V, 1.0/3.0);
    set_pair(I_G, I_B, 1.0/3.0); set_pair(I_G, I_D, 1.0/3.0);
    set_pair(I_G, I_H, 1.0);   set_pair(I_G, I_V, 1.0/3.0);
    set_pair(I_T, I_B, 1.0/3.0); set_pair(I_T, I_D, 1.0/3.0);
    set_pair(I_T, I_H, 1.0/3.0); set_pair(I_T, I_V, 1.0);

    /* 2-letter ambiguity vs 2-letter ambiguity. */
    set_pair(I_R, I_R, 0.5);
    set_pair(I_R, I_Y, 1.0);
    set_pair(I_R, I_S, 0.25); set_pair(I_R, I_W, 0.25);
    set_pair(I_R, I_K, 0.25); set_pair(I_R, I_M, 0.25);
    set_pair(I_Y, I_Y, 0.5);
    set_pair(I_Y, I_S, 0.25); set_pair(I_Y, I_W, 0.25);
    set_pair(I_Y, I_K, 0.25); set_pair(I_Y, I_M, 0.25);
    set_pair(I_S, I_S, 0.5);
    set_pair(I_S, I_W, 1.0);
    set_pair(I_S, I_K, 0.25); set_pair(I_S, I_M, 0.25);
    set_pair(I_W, I_W, 0.5);
    set_pair(I_W, I_K, 0.25); set_pair(I_W, I_M, 0.25);
    set_pair(I_K, I_K, 0.5);
    set_pair(I_K, I_M, 1.0);
    set_pair(I_M, I_M, 0.5);

    /* 2-letter vs 3-letter ambiguity. Verbatim from bpps; note that some
     * entries (e.g. 'hr' = 0.25) don't follow the obvious |intersection| /
     * (|s1|*|s2|) formula. We preserve them as-is for bpps parity. */
    set_pair(I_B, I_R, 1.0/6.0); set_pair(I_B, I_Y, 1.0/3.0);
    set_pair(I_B, I_S, 1.0/3.0); set_pair(I_B, I_W, 1.0/6.0);
    set_pair(I_B, I_K, 1.0/3.0); set_pair(I_B, I_M, 1.0/6.0);
    set_pair(I_D, I_R, 1.0/3.0); set_pair(I_D, I_Y, 1.0/6.0);
    set_pair(I_D, I_S, 1.0/6.0); set_pair(I_D, I_W, 1.0/3.0);
    set_pair(I_D, I_K, 1.0/3.0); set_pair(I_D, I_M, 1.0/6.0);
    set_pair(I_H, I_R, 0.25);    /* bpps quirk; expected 1/6 */
    set_pair(I_H, I_Y, 1.0/3.0); set_pair(I_H, I_S, 1.0/6.0);
    set_pair(I_H, I_W, 1.0/3.0); set_pair(I_H, I_K, 1.0/6.0);
    set_pair(I_H, I_M, 1.0/3.0);
    set_pair(I_V, I_R, 1.0/3.0); set_pair(I_V, I_Y, 1.0/6.0);
    set_pair(I_V, I_S, 1.0/3.0); set_pair(I_V, I_W, 1.0/6.0);
    set_pair(I_V, I_K, 1.0/6.0); set_pair(I_V, I_M, 1.0/3.0);

    /* 3-letter vs 3-letter ambiguity. */
    set_pair(I_B, I_B, 1.0/3.0); set_pair(I_B, I_D, 2.0/9.0);
    set_pair(I_B, I_H, 2.0/9.0); set_pair(I_B, I_V, 2.0/9.0);
    set_pair(I_D, I_D, 1.0/3.0); set_pair(I_D, I_H, 2.0/9.0);
    set_pair(I_D, I_V, 2.0/9.0);
    set_pair(I_H, I_H, 1.0/3.0); set_pair(I_H, I_V, 2.0/9.0);
    set_pair(I_V, I_V, 1.0/3.0);

    iupac_initialized = 1;
}

/* ---------- pairwise distance ---------- */

double bpp_pairwise_distance(const char *s1, const char *s2, int len) {
    iupac_init();
    double sum = 0.0;
    int    informative = 0;
    for (int i = 0; i < len; i++) {
        int a = char_to_idx(s1[i]);
        int b = char_to_idx(s2[i]);
        if (a == I_MISSING || b == I_MISSING) continue;
        if (a == I_UNKNOWN || b == I_UNKNOWN) continue;
        sum += iupac_table[a][b];
        informative++;
    }
    return informative > 0 ? sum / informative : 0.0;
}

/* Average pairwise distance within a single bucket of sequences. */
static double avg_within(const char **seqs, int n, int len) {
    if (n < 2) return 0.0;
    double sum = 0.0;
    long   pairs = 0;
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            sum += bpp_pairwise_distance(seqs[i], seqs[j], len);
            pairs++;
        }
    }
    return pairs > 0 ? sum / (double) pairs : 0.0;
}

/* Maximum pairwise distance over ALL sequences (across species) at one locus. */
static double max_at_locus(const bpp_locus_group_t *G) {
    /* Collect all sequence pointers in a temporary flat array. */
    int total = 0;
    for (int k = 0; k < G->n_species; k++) total += G->counts[k];
    if (total < 2) return 0.0;
    const char **all = malloc((size_t) total * sizeof(const char *));
    if (!all) return 0.0;
    int idx = 0;
    for (int k = 0; k < G->n_species; k++) {
        for (int s = 0; s < G->counts[k]; s++) all[idx++] = G->buckets[k][s];
    }
    double max_dist = 0.0;
    for (int i = 0; i < total - 1; i++) {
        for (int j = i + 1; j < total; j++) {
            double d = bpp_pairwise_distance(all[i], all[j], G->length);
            if (d > max_dist) max_dist = d;
        }
    }
    free(all);
    return max_dist;
}

/* Between-species summary statistics used by both the fallback theta
 * estimator AND the coalescent-corrected tau estimator.
 *
 *   coal_var       : average per-pair coalescent variance = Var(D) - mean/L
 *                    (averaged over species pairs that have >= 2 loci).
 *                    sqrt(coal_var) estimates the ancestral theta.
 *   pair_mean_avg  : the average over species pairs of the per-pair mean
 *                    distance across loci. Used as a fallback "mean" when
 *                    coal_var <= 0.
 *   pair_mean_max  : the MAX over species pairs of that per-pair mean. This
 *                    is the deepest expected between-species divergence
 *                    (smoother than the raw maxDistance: averages out
 *                    mutation noise within each species pair).
 *   pairs_used     : number of species pairs that contributed (>= 2 loci).
 */
typedef struct {
    double coal_var;
    double pair_mean_avg;
    double pair_mean_max;
    int    pairs_used;
} between_stats_t;

/* For each between-species pair, compute the mean and variance of pairwise
 * distances across loci. Aggregate across pairs into the four summary stats
 * above. */
static void compute_between_stats(const bpp_seq_grouping_t *g, between_stats_t *out)
{
    out->coal_var      = 0;
    out->pair_mean_avg = 0;
    out->pair_mean_max = 0;
    out->pairs_used    = 0;

    int S = g->n_species;
    typedef struct { double d; int len; } point_t;
    typedef struct { point_t *pts; int n; int cap; } pair_data_t;
    int n_pairs = S * (S - 1) / 2;
    if (n_pairs <= 0) return;

    pair_data_t *pd = calloc((size_t) n_pairs, sizeof(*pd));
    if (!pd) return;

    for (int li = 0; li < g->n_loci; li++) {
        const bpp_locus_group_t *G = &g->loci[li];
        int present = 0;
        for (int k = 0; k < S; k++) if (G->counts[k] > 0) present++;
        if (present < 2) continue;

        int pk = 0;
        for (int s1 = 0; s1 < S - 1; s1++) {
            for (int s2 = s1 + 1; s2 < S; s2++, pk++) {
                int n1 = G->counts[s1], n2 = G->counts[s2];
                if (n1 == 0 || n2 == 0) continue;
                double sum = 0.0;
                int    cnt = 0;
                for (int i = 0; i < n1; i++) {
                    for (int j = 0; j < n2; j++) {
                        sum += bpp_pairwise_distance(G->buckets[s1][i],
                                                     G->buckets[s2][j], G->length);
                        cnt++;
                    }
                }
                if (cnt == 0) continue;
                double avg = sum / cnt;
                if (pd[pk].n >= pd[pk].cap) {
                    int nc = pd[pk].cap ? pd[pk].cap * 2 : 8;
                    point_t *np = realloc(pd[pk].pts, (size_t) nc * sizeof(point_t));
                    if (!np) goto cleanup;
                    pd[pk].pts = np;
                    pd[pk].cap = nc;
                }
                pd[pk].pts[pd[pk].n].d   = avg;
                pd[pk].pts[pd[pk].n].len = G->length;
                pd[pk].n++;
            }
        }
    }

    double total_cv   = 0.0;
    double total_mean = 0.0;
    double max_pmean  = 0.0;
    int    used       = 0;
    for (int p = 0; p < n_pairs; p++) {
        if (pd[p].n < 2) continue;
        double m = 0.0;
        for (int i = 0; i < pd[p].n; i++) m += pd[p].pts[i].d;
        m /= pd[p].n;
        double v = 0.0;
        for (int i = 0; i < pd[p].n; i++) {
            double dx = pd[p].pts[i].d - m;
            v += dx * dx;
        }
        v /= (pd[p].n - 1);    /* sample variance */
        double avg_len = 0.0;
        for (int i = 0; i < pd[p].n; i++) avg_len += pd[p].pts[i].len;
        avg_len /= pd[p].n;
        double mut_var  = (avg_len > 0) ? m / avg_len : 0.0;
        double coal_var = v - mut_var;
        total_cv   += coal_var;
        total_mean += m;
        if (m > max_pmean) max_pmean = m;
        used++;
    }
    if (used > 0) {
        out->coal_var      = total_cv   / used;
        out->pair_mean_avg = total_mean / used;
        out->pair_mean_max = max_pmean;
        out->pairs_used    = used;
    }

cleanup:
    for (int p = 0; p < n_pairs; p++) free(pd[p].pts);
    free(pd);
}

int bpp_compute_prior_means(const bpp_seq_grouping_t *g,
                            double *theta_mean, double *tau_mean)
{
    if (!g || g->n_loci <= 0) {
        *theta_mean = 0;
        *tau_mean   = 0;
        return -1;
    }
    iupac_init();

    /* priorMeanTheta = avg of within-species pairwise distance, averaged
     * across (locus, species) pairs that have >= 2 sequences. */
    double sum = 0.0;
    int    n_species_obs = 0;
    for (int li = 0; li < g->n_loci; li++) {
        const bpp_locus_group_t *G = &g->loci[li];
        for (int k = 0; k < G->n_species; k++) {
            if (G->counts[k] > 0) {
                sum += avg_within(G->buckets[k], G->counts[k], G->length);
                n_species_obs++;
            }
        }
    }
    double pmt = (n_species_obs > 0) ? sum / n_species_obs : 0.0;

    /* Between-species statistics: always computed, used for both the theta
     * fallback (when within-species variation is missing) and the coalescent
     * correction to the tau estimate (see below). */
    between_stats_t bs = {0};
    compute_between_stats(g, &bs);

    /* Theta fallback when within-species variation is absent (single sample
     * per species). */
    if (!(pmt > 0)) {
        if (bs.coal_var > 0)            pmt = sqrt(bs.coal_var);
        else if (bs.pair_mean_avg > 0)  pmt = bs.pair_mean_avg / 10.0;
        else                            pmt = 0.0;
    }

    /* Tau estimate. The bpps default (max raw pairwise distance) overestimates
     * tau substantially because the raw distance E[D] = 2*tau + theta_anc
     * (coalescent component) + outlier noise. When we have enough between-
     * species data to estimate theta_anc, refine:
     *
     *     tau_est = max(0, max_pair_mean - theta_anc) / 2
     *
     * where max_pair_mean = largest per-species-pair mean distance (deepest
     * expected divergence, smoother than raw maxDistance). Fall back to the
     * raw maxDistance heuristic when the correction is not computable or
     * comes out non-positive. */
    double tau_raw = 0.0;
    for (int li = 0; li < g->n_loci; li++) {
        double m = max_at_locus(&g->loci[li]);
        if (m > tau_raw) tau_raw = m;
    }

    double tau = tau_raw;
    if (bs.pair_mean_max > 0) {
        /* theta_anc: prefer the coalescent-variance estimate when valid
         * (specifically an ancestral-population estimate); otherwise borrow
         * the within-species theta, which is a reasonable proxy when the
         * variance estimator gives a non-positive result (sparse loci,
         * small samples — common). */
        double theta_anc = 0.0;
        if      (bs.coal_var > 0) theta_anc = sqrt(bs.coal_var);
        else if (pmt > 0)         theta_anc = pmt;

        double tau_corr = (bs.pair_mean_max - theta_anc) / 2.0;
        if (tau_corr > 0) tau = tau_corr;
        /* Otherwise fall back to raw — happens when the between-species
         * signal is dominated by coalescent / ambiguity noise and tau is
         * genuinely tiny relative to ancestral N. */
    }

    *theta_mean = pmt;
    *tau_mean   = tau;
    return 0;
}
