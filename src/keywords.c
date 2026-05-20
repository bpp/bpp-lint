#include "keywords.h"
#include "lex.h"

#include <stddef.h>

/*
 * Keyword catalogue for BPP 4.x with legacy 2.x/3.x entries.
 *
 * Sources:
 *   - bpp 4.8.7 src/cfile.c       (lines ~2995-3492, inference parser)
 *   - bpp 4.8.7 src/cfile_sim.c   (lines ~1297-1556, simulator parser)
 *   - bpp 4.8.7 ChangeLog.md      (rename/removal history)
 *   - iBPP src/bpp.c GetOptions   (BPP 2.1 keyword list, for legacy detection)
 *
 * Ordering: valid 4.x keywords first (so the "did you mean" suggester
 * surfaces them preferentially), then legacy entries.
 */
static const bpp_keyword_t kw_table[] = {

    /* Field order: name, status, mode, replacement, note, since_version, default_value.
     * `default_value` is non-NULL only for keywords with a meaningful BPP-side
     * default; "must-set" keywords (seqfile, nloci, jobname, ...) keep NULL. */

    /* ===== Valid BPP 4.x inference-mode keywords ===== */
    { "seed",                KW_VALID, MODE_BOTH, NULL, NULL, 0, "-1" },
    { "arch",                KW_VALID, MODE_BOTH, NULL, NULL, 0, "auto" },
    { "nloci",               KW_VALID, MODE_INFER, NULL, NULL, 0, NULL },
    { "print",               KW_VALID, MODE_INFER, NULL,
      "4-5 boolean bits, or -1 for summary-only.", 0, "1 0 0 0 0" },
    { "model",               KW_VALID, MODE_BOTH, NULL,
      "Inference: name (JC69, HKY, GTR, ...). --simulate: integer code.", 0, "JC69" },
    { "clock",               KW_VALID, MODE_BOTH, NULL, NULL, 0, "1 (strict)" },
    { "phase",               KW_VALID, MODE_BOTH, NULL, NULL, 0, "0 ... 0 (all phased)" },
    { "burnin",              KW_VALID, MODE_INFER, NULL, NULL, 0, "100" },
    { "wprior",              KW_VALID, MODE_INFER, NULL, NULL, 480, NULL },
    { "seqfile",             KW_VALID, MODE_BOTH, NULL, NULL, 0, NULL },
    { "jobname",             KW_VALID, MODE_INFER, NULL,
      "Required since v4.8.0; replaces outfile/mcmcfile.", 480, NULL },
    { "usedata",             KW_VALID, MODE_INFER, NULL,
      "0=prior only, 1=likelihood+prior, 2=fixed gene trees (v4.8.4+).", 0, "1" },
    { "nsample",             KW_VALID, MODE_INFER, NULL, NULL, 0, NULL },
    { "scaling",             KW_VALID, MODE_INFER, NULL, NULL, 0, "0" },
    { "threads",             KW_VALID, MODE_INFER, NULL, NULL, 0, "1 1 1" },
    { "imapfile",            KW_VALID, MODE_BOTH, NULL, NULL, 0, NULL },
    { "datefile",            KW_VALID, MODE_BOTH, NULL, NULL, 0, NULL },
    { "tauprior",            KW_VALID, MODE_INFER, NULL,
      "'<dist> a b' with dist = invgamma|gamma. Bare numeric => invgamma.", 0, NULL },
    { "heredity",            KW_VALID, MODE_INFER, NULL, NULL, 0, "0" },
    { "finetune",            KW_VALID, MODE_INFER, NULL,
      "v4.8.1+: '<0|1> key:value ...'. Positional form is deprecated.", 481, "1 (auto)" },
    { "sampfreq",            KW_VALID, MODE_INFER, NULL, NULL, 0, "10" },
    { "phiprior",            KW_VALID, MODE_INFER, NULL, NULL, 0, NULL },
    { "geneflow",            KW_VALID, MODE_INFER, NULL, NULL, 0, "0" },
    { "cleandata",           KW_VALID, MODE_INFER, NULL, NULL, 0, "0" },
    { "locusrate",           KW_VALID, MODE_BOTH, NULL,
      "Inference: '0' | '1 a_mubar b_mubar a_mui [prior]' | '2 <file>' | '3 a b'. Syntax changed in v4.1.4.", 414, "0" },
    { "migration",           KW_VALID, MODE_BOTH, NULL,
      "'<N>' header + N rows: 'source target [a b] [a_w] [pa pb]'.", 0, "0" },
    { "traitfile",           KW_VALID, MODE_INFER, NULL, NULL, 0, NULL },
    { "thetaprior",          KW_VALID, MODE_INFER, NULL,
      "'invgamma a b [e]' | 'gamma a b' | 'beta p q lo hi'. v4.8.2+ requires invgamma alpha > 2.", 482, NULL },
    { "checkpoint",          KW_VALID, MODE_INFER, NULL, NULL, 0, NULL },
    { "alphaprior",          KW_VALID, MODE_INFER, NULL, NULL, 0, NULL },
    { "thetamodel",          KW_VALID, MODE_INFER, NULL,
      "linked-none | linked-all | linked-inner | linked-msci | linked-mscm.", 0, "linked-none" },
    { "printlocus",          KW_VALID, MODE_BOTH, NULL, NULL, 0, NULL },
    { "speciestree",         KW_VALID, MODE_INFER, NULL, NULL, 0, "0 (fixed)" },
    { "loadbalance",         KW_VALID, MODE_INFER, NULL,
      "zigzag | none.", 0, "zigzag" },
    { "species&tree",        KW_VALID, MODE_BOTH, NULL,
      "Block of 2-3 lines: header, counts, Newick (omit for single species).", 0, NULL },
    { "constraintfile",      KW_VALID, MODE_INFER, NULL, NULL, 0, NULL },
    { "bayesfactorbeta",     KW_VALID, MODE_INFER, NULL, NULL, 0, "1" },
    { "debug_migration",     KW_VALID, MODE_INFER, NULL, NULL, 0, "0" },
    { "speciesmodelprior",   KW_VALID, MODE_INFER, NULL,
      "0=labeled histories, 1=rooted trees, 2=uniformSLH, 3=uniformSRooted.", 0, "1 (uniform rooted)" },
    { "speciesdelimitation", KW_VALID, MODE_INFER, NULL,
      "'0' | '1 0 epsilon' | '1 1 alpha m'.", 0, "0 (fixed)" },

    /* ===== Valid BPP 4.x --simulate-mode keywords ===== */
    { "qrates",              KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },
    { "seqerr",              KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },
    { "treefile",            KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },
    { "seqdates",            KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },
    { "basefreqs",           KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },
    { "concatfile",          KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },
    { "loci&length",         KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },
    { "modelparafile",       KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },
    { "alpha_siterate",      KW_VALID_SIM, MODE_SIM, NULL, NULL, 0, NULL },

    /* ===== Legacy / removed / renamed ===== */

    /* Hard errors emitted by BPP 4.x itself (these tokens are recognised but
     * the parser aborts with help text). */
    { "outfile",             KW_RENAMED, MODE_INFER, "jobname",
      "v4.8.0+; jobname is a filename prefix (no extension).", 480, NULL },
    { "mcmcfile",            KW_RENAMED, MODE_INFER, "jobname",
      "v4.8.0+; jobname covers the MCMC output file.", 480, NULL },
    { "diploid",             KW_RENAMED, MODE_BOTH, "phase",
      "v4.2.2+; same value format.", 422, NULL },
    { "migprior",            KW_REPARAMETERISED, MODE_INFER, "wprior",
      "v4.8.0+; wprior uses w = 4M/theta. Re-derive numeric values from your theta scale.", 480, NULL },

    /* Silently rejected by BPP 4.x because they no longer appear in any
     * length-bucket of the parser. The linter recognises them by name. */
    { "uniformrootedtrees",  KW_RENAMED, MODE_INFER, "speciesmodelprior",
      "Replaced in 3.x; integer values map directly.", 300, NULL },
    { "gammaprior",          KW_RENAMED, MODE_INFER, "phiprior",
      "v4.1.1+; Beta(a,b) prior on phi.", 411, NULL },
    { "alpha_locusrate",     KW_REMOVED, MODE_SIM, "locusrate",
      "v4.2.1+ (simulation); rewrite with the multi-argument locusrate syntax.", 421, NULL },

    /* Tokens that BPP 4.x still tokenises but immediately aborts. */
    { "sequenceerror",       KW_UNIMPLEMENTED, MODE_INFER, NULL,
      "BPP 3.x genotyping-error model; not ported to 4.x. Delete the keyword and its matrix block.", 0, NULL },

    /* iBPP-only extensions that may appear in legacy files but were never
     * part of mainline BPP. Flag them so the user knows they will not work
     * with stock BPP 4.x. */
    { "ntraits",             KW_REMOVED, MODE_INFER, NULL,
      "iBPP-specific (combined trait/sequence analysis); not in mainline BPP.", 0, NULL },
    { "nindt",               KW_REMOVED, MODE_INFER, NULL,
      "iBPP-specific; not in mainline BPP.", 0, NULL },
    { "useseqdata",          KW_REMOVED, MODE_INFER, NULL,
      "iBPP-specific; not in mainline BPP.", 0, NULL },
    { "usetraitdata",        KW_REMOVED, MODE_INFER, NULL,
      "iBPP-specific; not in mainline BPP.", 0, NULL },
    { "nu0",                 KW_REMOVED, MODE_INFER, NULL,
      "iBPP-specific; not in mainline BPP.", 0, NULL },
    { "kappa0",              KW_REMOVED, MODE_INFER, NULL,
      "iBPP-specific; not in mainline BPP.", 0, NULL },

    /* Sentinel */
    { NULL, KW_UNKNOWN, 0, NULL, NULL, 0, NULL }
};

const bpp_keyword_t *bpp_keyword_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; kw_table[i].name; i++) {
        if (bpp_strieq(kw_table[i].name, name)) return &kw_table[i];
    }
    return NULL;
}

const bpp_keyword_t *bpp_keyword_at(int i) {
    if (i < 0) return NULL;
    int n = 0;
    while (kw_table[n].name) n++;
    if (i >= n) return NULL;
    return &kw_table[i];
}

const bpp_keyword_t *bpp_keyword_suggest(const char *name, kw_mode_t mode, int max_distance) {
    if (!name) return NULL;
    const bpp_keyword_t *best = NULL;
    int best_d = max_distance + 1;
    for (int i = 0; kw_table[i].name; i++) {
        const bpp_keyword_t *k = &kw_table[i];
        /* only suggest currently-valid keywords */
        if (k->status != KW_VALID && k->status != KW_VALID_SIM) continue;
        if ((k->mode & mode) == 0) continue;
        int d = bpp_levenshtein(name, k->name);
        if (d < 0) continue;
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    return (best_d <= max_distance) ? best : NULL;
}
