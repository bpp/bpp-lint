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
    {          "speciestree", KW_VALID          , MODE_INFER, NULL, "Species-tree estimation switch + optional move tuning (A01/A11).", 0, "0" },
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

/* ===== Value grammars: typed slot lists compiled from
 * spec value.grammar, walked by lint.c's generic value checker.
 * 38 of 49 live keywords have a
 * reducible grammar; the rest (alternation / Newick / multi-line /
 * bespoke-checked) are validated elsewhere or not at all. ===== */

static const kw_slot_t slots_alphaprior[] = { { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_UINT, .optional = 1 }, { .type = VT_END } };
static const char *const enum_arch[] = { "cpu", "sse", "avx", "avx2", "neon", NULL };
static const kw_slot_t slots_arch[] = { { .type = VT_STRING, .enums = enum_arch }, { .type = VT_END } };
static const kw_slot_t slots_basefreqs[] = { { .type = VT_UINT, .has_min = 1, .min = 0, .has_max = 1, .max = 1 }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_END } };
static const kw_slot_t slots_bayesfactorbeta[] = { { .type = VT_FLOAT, .has_min = 1, .min = 0, .min_excl = 1 }, { .type = VT_END } };
static const kw_slot_t slots_burnin[] = { { .type = VT_UINT }, { .type = VT_END } };
static const kw_slot_t slots_checkpoint[] = { { .type = VT_UINT }, { .type = VT_UINT, .optional = 1 }, { .type = VT_END } };
static const kw_slot_t slots_cleandata[] = { { .type = VT_BOOL }, { .type = VT_END } };
static const kw_slot_t slots_concatfile[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_constraintfile[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_datefile[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_debug_migration[] = { { .type = VT_UINT }, { .type = VT_END } };
static const kw_slot_t slots_geneflow[] = { { .type = VT_BOOL }, { .type = VT_END } };
static const kw_slot_t slots_imapfile[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_jobname[] = { { .type = VT_STRING }, { .type = VT_END } };
static const char *const enum_loadbalance[] = { "zigzag", "none", NULL };
static const kw_slot_t slots_loadbalance[] = { { .type = VT_STRING, .enums = enum_loadbalance }, { .type = VT_END } };
static const kw_slot_t slots_loci_length[] = { { .type = VT_UINT, .has_min = 1, .min = 1 }, { .type = VT_UINT, .has_min = 1, .min = 1 }, { .type = VT_END } };
static const char *const enum_model[] = { "JC69", "K80", "F81", "HKY", "T92", "TN93", "F84", "GTR", "DAYHOFF", "LG", "DCMUT", "JTT", "MTREV", "WAG", "RTREV", "CPREV", "VT", "BLOSUM62", "MTMAM", "MTART", "MTZOA", "PMB", "HIVB", "HIVW", "JTTDCMUT", "FLU", "STMTREV", "Custom", NULL };
static const kw_slot_t slots_model[] = { { .type = VT_STRING, .enums = enum_model }, { .type = VT_STRING, .optional = 1 }, { .type = VT_END } };
static const kw_slot_t slots_modelparafile[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_nloci[] = { { .type = VT_UINT }, { .type = VT_END } };
static const kw_slot_t slots_nsample[] = { { .type = VT_UINT, .has_min = 1, .min = 1 }, { .type = VT_END } };
static const kw_slot_t slots_phase[] = { { .type = VT_BOOL, .repeat = 1 }, { .type = VT_END } };
static const kw_slot_t slots_printlocus[] = { { .type = VT_UINT }, { .type = VT_UINT, .repeat = 1 }, { .type = VT_END } };
static const kw_slot_t slots_qrates[] = { { .type = VT_UINT, .has_min = 1, .min = 0, .has_max = 1, .max = 1 }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_END } };
static const kw_slot_t slots_sampfreq[] = { { .type = VT_UINT, .has_min = 1, .min = 1 }, { .type = VT_END } };
static const kw_slot_t slots_scaling[] = { { .type = VT_BOOL }, { .type = VT_END } };
static const kw_slot_t slots_seed[] = { { .type = VT_INT }, { .type = VT_END } };
static const kw_slot_t slots_seqDates[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_seqerr[] = { { .type = VT_UINT }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_END } };
static const kw_slot_t slots_seqfile[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_speciesdelimitation[] = { { .type = VT_BOOL }, { .type = VT_INT, .optional = 1 }, { .type = VT_FLOAT, .optional = 1 }, { .type = VT_FLOAT, .optional = 1 }, { .type = VT_END } };
static const kw_slot_t slots_speciesmodelprior[] = { { .type = VT_UINT, .has_min = 1, .min = 0, .has_max = 1, .max = 3 }, { .type = VT_END } };
static const kw_slot_t slots_speciestree[] = { { .type = VT_BOOL }, { .type = VT_FLOAT, .repeat = 1 }, { .type = VT_END } };
static const char *const enum_thetamodel[] = { "linked-none", "linked-all", "linked-inner", "linked-msci", "linked-mscm", NULL };
static const kw_slot_t slots_thetamodel[] = { { .type = VT_STRING, .enums = enum_thetamodel }, { .type = VT_END } };
static const kw_slot_t slots_threads[] = { { .type = VT_UINT, .has_min = 1, .min = 1 }, { .type = VT_UINT, .optional = 1, .has_min = 1, .min = 1 }, { .type = VT_UINT, .optional = 1, .has_min = 1, .min = 1 }, { .type = VT_END } };
static const kw_slot_t slots_traitfile[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_treefile[] = { { .type = VT_STRING }, { .type = VT_END } };
static const kw_slot_t slots_usedata[] = { { .type = VT_INT, .has_min = 1, .min = 0, .has_max = 1, .max = 2 }, { .type = VT_END } };
static const kw_slot_t slots_wprior[] = { { .type = VT_FLOAT }, { .type = VT_FLOAT }, { .type = VT_END } };

const kw_valuespec_t kw_value_table[] = {
    { "alphaprior", slots_alphaprior },
    { "arch", slots_arch },
    { "basefreqs", slots_basefreqs },
    { "bayesfactorbeta", slots_bayesfactorbeta },
    { "burnin", slots_burnin },
    { "checkpoint", slots_checkpoint },
    { "cleandata", slots_cleandata },
    { "concatfile", slots_concatfile },
    { "constraintfile", slots_constraintfile },
    { "datefile", slots_datefile },
    { "debug_migration", slots_debug_migration },
    { "geneflow", slots_geneflow },
    { "imapfile", slots_imapfile },
    { "jobname", slots_jobname },
    { "loadbalance", slots_loadbalance },
    { "loci&length", slots_loci_length },
    { "model", slots_model },
    { "modelparafile", slots_modelparafile },
    { "nloci", slots_nloci },
    { "nsample", slots_nsample },
    { "phase", slots_phase },
    { "printlocus", slots_printlocus },
    { "qrates", slots_qrates },
    { "sampfreq", slots_sampfreq },
    { "scaling", slots_scaling },
    { "seed", slots_seed },
    { "seqDates", slots_seqDates },
    { "seqerr", slots_seqerr },
    { "seqfile", slots_seqfile },
    { "speciesdelimitation", slots_speciesdelimitation },
    { "speciesmodelprior", slots_speciesmodelprior },
    { "speciestree", slots_speciestree },
    { "thetamodel", slots_thetamodel },
    { "threads", slots_threads },
    { "traitfile", slots_traitfile },
    { "treefile", slots_treefile },
    { "usedata", slots_usedata },
    { "wprior", slots_wprior },
    { NULL, NULL }
};
