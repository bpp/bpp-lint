/* GENERATED FILE -- DO NOT EDIT.
 *
 * Projected from spec/bpp-syntax.json (BPP 4.8.7) by
 * spec/gen_keywords_c.py.  Edit spec/enrich.json (authored facts) or
 * re-run spec/generate.py (source-derived facts), then `make gen`.
 *
 * The lookup API (bpp_keyword_find / _at / _suggest) lives in
 * src/keywords.c and references this table via extern.
 */
#include "keywords.h"

#include <stddef.h>

/* Field order: name, status, mode, replacement, note, since_version,
 * default_value.  Valid inference/both keywords first, then valid
 * simulation keywords, then deprecated/legacy -- so the "did you mean"
 * suggester surfaces live keywords first. */
const bpp_keyword_t kw_table[] = {

    /* ===== Valid BPP 4.x inference-mode keywords ===== */
    {           "alphaprior", KW_VALID          , MODE_INFER, NULL, "Gamma prior for among-site rate variation; optional 3rd value = number of rate categories (default 4).", 0, NULL },
    {                 "arch", KW_VALID          , MODE_BOTH , NULL, "Force a SIMD instruction set (default: auto-detect).", 0, "auto" },
    {      "bayesfactorbeta", KW_VALID          , MODE_INFER, NULL, "Power-posterior beta for marginal-likelihood (Bayes factor) estimation.", 0, "1" },
    {               "burnin", KW_VALID          , MODE_INFER, NULL, "Number of burn-in MCMC iterations.", 0, "100" },
    {           "checkpoint", KW_VALID          , MODE_INFER, NULL, "Write a checkpoint at iteration `initial`, then every `step` iterations.", 0, NULL },
    {            "cleandata", KW_VALID          , MODE_INFER, NULL, "Remove sites with ambiguities/gaps before analysis.", 0, "0" },
    {                "clock", KW_VALID          , MODE_BOTH , NULL, "Molecular-clock model.", 0, "1" },
    {       "constraintfile", KW_VALID          , MODE_INFER, NULL, "File of topological constraints on the species tree.", 0, NULL },
    {             "datefile", KW_VALID          , MODE_BOTH , NULL, "Tip-dating file of sequence sampling dates.", 0, NULL },
    {      "debug_migration", KW_VALID          , MODE_INFER, NULL, "Developer debug option (undocumented).", 0, "0" },
    {             "finetune", KW_VALID          , MODE_INFER, NULL, "MCMC proposal step sizes; leading bit enables auto-tuning.", 0, "1" },
    {             "geneflow", KW_VALID          , MODE_INFER, NULL, "Enable gene-flow (MSC-M/MSC-I) estimation.", 0, "0" },
    {             "heredity", KW_VALID          , MODE_INFER, NULL, "Per-locus heredity multipliers (e.g. autosome vs. sex chromosome).", 0, "0" },
    {             "imapfile", KW_VALID          , MODE_BOTH , NULL, "Individual-to-species map file.", 0, NULL },
    {              "jobname", KW_VALID          , MODE_INFER, NULL, "Output filename prefix (no extension). Replaces outfile/mcmcfile (v4.8.0).", 0, NULL },
    {          "loadbalance", KW_VALID          , MODE_INFER, NULL, "Thread work-distribution strategy. NOTE: source default is 'zigzag'; the manual says 'none'.", 0, "zigzag" },
    {            "locusrate", KW_VALID          , MODE_BOTH , NULL, "Among-locus substitution-rate variation.", 0, "0" },
    {            "migration", KW_VALID          , MODE_BOTH , NULL, "MSC-M migration bands between populations.", 0, "0" },
    {                "model", KW_VALID          , MODE_BOTH , NULL, "Substitution model (DNA or amino-acid), or 'Custom <file>'.", 0, "JC69" },
    {                "nloci", KW_VALID          , MODE_INFER, NULL, "Number of loci to analyse (0 = all).", 0, NULL },
    {              "nsample", KW_VALID          , MODE_INFER, NULL, "Number of MCMC samples to collect.", 0, NULL },
    {                "phase", KW_VALID          , MODE_BOTH , NULL, "Per-species phase-resolution flags. (Renamed from diploid.)", 0, "0" },
    {             "phiprior", KW_VALID          , MODE_INFER, NULL, "Beta(a,b) prior on phi (introgression probability); no distribution keyword.", 0, NULL },
    {                "print", KW_VALID          , MODE_INFER, NULL, "Which MCMC quantities to write; first bit (MCMC samples) must be 1.", 0, "1 0 0 0 0" },
    {           "printlocus", KW_VALID          , MODE_BOTH , NULL, "Print per-locus gene trees for the listed loci.", 0, NULL },
    {             "sampfreq", KW_VALID          , MODE_INFER, NULL, "MCMC sampling frequency (every Nth iteration).", 0, "10" },
    {              "scaling", KW_VALID          , MODE_INFER, NULL, "Scale conditional likelihoods to avoid numerical underflow.", 0, "0" },
    {                 "seed", KW_VALID          , MODE_BOTH , NULL, "Random-number seed; -1 = draw a random seed.", 0, "-1" },
    {              "seqfile", KW_VALID          , MODE_BOTH , NULL, "Sequence alignment file (BPP/PHYLIP format).", 0, NULL },
    {         "species&tree", KW_VALID          , MODE_BOTH , NULL, "Species count, names, per-species sample counts, and the guide tree.", 0, NULL },
    {  "speciesdelimitation", KW_VALID          , MODE_INFER, NULL, "Species-delimitation switch and rjMCMC settings (A10/A11).", 0, "0" },
    {    "speciesmodelprior", KW_VALID          , MODE_INFER, NULL, "Prior on species models; integer 0-3, default 1 (uniform rooted).", 0, "1" },
    {          "speciestree", KW_VALID          , MODE_INFER, NULL, "Species-tree estimation switch (A01/A11).", 0, "0" },
    {             "tauprior", KW_VALID          , MODE_INFER, NULL, "Prior on tau (divergence times); invgamma (default) or gamma.", 0, NULL },
    {           "thetamodel", KW_VALID          , MODE_INFER, NULL, "How theta parameters are shared across populations.", 0, "linked-none" },
    {           "thetaprior", KW_VALID          , MODE_INFER, NULL, "Prior on theta (population size); invgamma (default) or gamma.", 482, NULL },
    {              "threads", KW_VALID          , MODE_INFER, NULL, "Number of threads, with optional starting core index and step.", 0, "1 1 1" },
    {            "traitfile", KW_VALID          , MODE_INFER, NULL, "Morphological/quantitative trait data file (iBPP trait analysis).", 0, NULL },
    {              "usedata", KW_VALID          , MODE_INFER, NULL, "Whether the sequence likelihood is used.", 0, "1" },
    {               "wprior", KW_VALID          , MODE_INFER, NULL, "Gamma prior on migration rate w (MSC-M). Replaces migprior (v4.8.0).", 0, NULL },

    /* ===== Valid BPP 4.x --simulate-mode keywords ===== */
    {       "alpha_siterate", KW_VALID_SIM      , MODE_SIM  , NULL, "Among-site rate variation to simulate (--simulate).", 0, NULL },
    {            "basefreqs", KW_VALID_SIM      , MODE_SIM  , NULL, "GTR base frequencies to simulate under (--simulate only).", 0, NULL },
    {           "concatfile", KW_VALID_SIM      , MODE_SIM  , NULL, "Output file for the concatenated alignment (--simulate).", 0, NULL },
    {          "loci&length", KW_VALID_SIM      , MODE_SIM  , NULL, "Number of loci and sites per locus to simulate (--simulate).", 0, NULL },
    {        "modelparafile", KW_VALID_SIM      , MODE_SIM  , NULL, "Output file for per-locus model parameters (--simulate).", 0, NULL },
    {               "qrates", KW_VALID_SIM      , MODE_SIM  , NULL, "GTR exchangeability rates to simulate under (--simulate only).", 0, NULL },
    {             "seqDates", KW_VALID_SIM      , MODE_SIM  , NULL, "Sequence sampling-dates file, the --simulate analogue of datefile.", 0, NULL },
    {               "seqerr", KW_VALID_SIM      , MODE_SIM  , NULL, "Sequencing-error model to simulate: read depth plus base-error and two Dirichlet concentration parameters (--simulate).", 0, NULL },
    {             "treefile", KW_VALID_SIM      , MODE_SIM  , NULL, "Output file for the simulated trees (--simulate).", 0, NULL },

    /* ===== Deprecated / renamed / removed / foreign ===== */
    {      "alpha_locusrate", KW_REMOVED        , MODE_SIM  , "locusrate", "Valid in v4.1.x; since v4.2.1 the --simulate parser aborts telling you to use 'locusrate' (new multi-argument syntax). The keyword is still tokenised in every release, but only to emit that error -- so the auto-derived 'present_in' does NOT mean it is usable.", 421, NULL },
    {              "diploid", KW_RENAMED        , MODE_INFER, "phase", "v4.8.7 parser aborts on 'diploid' with a rename message; use phase (same syntax).", 429, NULL },
    {             "mcmcfile", KW_RENAMED        , MODE_INFER, "jobname", "v4.8.7 parser aborts on 'mcmcfile'; use jobname.", 480, NULL },
    {             "migprior", KW_REPARAMETERISED, MODE_INFER, "wprior", "Reparameterised, not a pure rename: wprior uses w = 4M/theta, so numeric values must be re-derived from your theta scale. v4.8.7 parser aborts on 'migprior'.", 480, NULL },
    {              "outfile", KW_RENAMED        , MODE_INFER, "jobname", "v4.8.7 parser aborts on 'outfile'; use jobname (outfile/mcmcfile merged into jobname in 4.8.0).", 480, NULL },
    {        "sequenceerror", KW_UNIMPLEMENTED  , MODE_INFER, NULL, "Recognised by the parser but aborts at runtime ('Not implemented'); a dead BPP 3.x keyword. bpp-lint flags this (BPP023).", 0, NULL },
    {   "uniformrootedtrees", KW_RENAMED        , MODE_INFER, "speciesmodelprior", "BPP 3.x keyword; integer values map directly to speciesmodelprior. Predates the scanned tags.", 300, NULL },
    {           "gammaprior", KW_RENAMED        , MODE_INFER, "phiprior", "Beta(a,b) prior on phi; renamed to phiprior in v4.1.1 (before the earliest scanned tag v4.1.3).", 411, NULL },
    {              "ntraits", KW_REMOVED        , MODE_INFER, NULL, "iBPP-specific (combined trait/sequence analysis); not in mainline BPP.", 0, NULL },
    {                "nindt", KW_REMOVED        , MODE_INFER, NULL, "iBPP-specific; not in mainline BPP.", 0, NULL },
    {           "useseqdata", KW_REMOVED        , MODE_INFER, NULL, "iBPP-specific; not in mainline BPP.", 0, NULL },
    {         "usetraitdata", KW_REMOVED        , MODE_INFER, NULL, "iBPP-specific; not in mainline BPP.", 0, NULL },
    {                  "nu0", KW_REMOVED        , MODE_INFER, NULL, "iBPP-specific; not in mainline BPP.", 0, NULL },
    {               "kappa0", KW_REMOVED        , MODE_INFER, NULL, "iBPP-specific; not in mainline BPP.", 0, NULL },

    /* Sentinel */
    { NULL, KW_UNKNOWN, 0, NULL, NULL, 0, NULL }
};
