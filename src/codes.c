#include "codes.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*
 * Diagnostic code catalogue. Each entry documents what the code means and,
 * for --explain, gives a fuller account of when it fires and how to address
 * it. Keep this in sync with the emit() sites in lint.c.
 */
static const bpp_code_doc_t codes[] = {

    /* 0xx: lexical / structural problems */
    { "001",
      "Unknown keyword (may include a 'did you mean' suggestion).",
      "An identifier on the left-hand side of '=' is not recognised by BPP 4.x\n"
      "and is too distant from any known keyword to be auto-corrected. If you\n"
      "see a 'did you mean' note attached, that is the linter's best guess at\n"
      "what you meant. Otherwise the keyword is likely from a third-party fork\n"
      "of BPP (e.g. iBPP) or simply mistyped." },

    { "002",
      "Line has no '=' assignment and is not a comment or known continuation.",
      "Every non-comment line in a BPP control file is either a 'key = value'\n"
      "statement or a continuation line consumed by a multi-line block\n"
      "(species&tree, migration, or — in legacy files — sequenceerror). The\n"
      "linter saw an orphan content line that fits none of these shapes." },

    { "003",
      "Keyword belongs to the other run mode.",
      "Some keywords are recognised only by the inference parser, others only\n"
      "by --simulate. This diagnostic fires when a keyword from the wrong mode\n"
      "appears. Pass --simulate (or remove it) to match the file's intent." },

    { "004",
      "Keyword has an empty value.",
      "A 'key =' line with no value on the right-hand side. BPP usually treats\n"
      "this as a parse error. Provide a value or delete the line." },

    /* 01x: value-format problems */
    { "010",
      "'print' has fewer than 4 bits (legacy single-bit form).",
      "BPP 4.x expects 4-5 boolean bits for 'print' (or -1 for summary-only\n"
      "mode). BPP 3.x accepted a single bit; that form is rejected now. The\n"
      "linter auto-pads to 5 bits via --fix. Bit meanings: samples, locusrate,\n"
      "hscalars, genetrees, qmatrix." },

    { "011",
      "'thetaprior' alpha <= 2 with implicit invgamma distribution.",
      "Since BPP v4.8.2, an invgamma prior on theta requires alpha > 2. The\n"
      "bare-numeric form ('thetaprior = a b') is implicitly invgamma. Either\n"
      "switch to a gamma prior or choose alpha > 2." },

    { "012",
      "'tauprior' has three numeric tokens (legacy form).",
      "Some BPP 3.x builds accepted a third token as a Dirichlet alpha for\n"
      "non-root taus. BPP 4.x silently ignores it. --fix drops the extra\n"
      "token." },

    { "013",
      "'finetune' uses the pre-v4.8.1 positional value list.",
      "BPP v4.8.1 replaced the positional 'finetune = 1: f f f f ...' syntax\n"
      "with a 'key:value' dictionary form. The positional form is rejected by\n"
      "current BPP releases. Rewrite as e.g. 'finetune = 1 Gage:5 Gspr:0.001\n"
      "tau:0.001 mix:0.3 lrht:0.33'. The linter cannot translate the values\n"
      "automatically because the positional-to-key mapping varied across\n"
      "BPP 3.x and early 4.x." },

    { "014",
      "'locusrate' uses the pre-v4.1.4 two-argument form.",
      "Before v4.1.4, 'locusrate = 1 <alpha>' was a valid two-argument form.\n"
      "Current BPP releases expect 'locusrate = 1 a_mubar b_mubar a_mui\n"
      "[DIR|IID]'. The new parameters cannot be derived mechanically from the\n"
      "old alpha; supply them explicitly." },

    /* 02x: legacy / removed / unimplemented keywords */
    { "020",
      "Legacy keyword renamed in a modern BPP release.",
      "Pure rename: BPP renamed the keyword between major versions, value\n"
      "format unchanged. --fix mechanically rewrites the keyword name.\n"
      "Examples: outfile -> jobname, mcmcfile -> jobname, diploid -> phase,\n"
      "gammaprior -> phiprior, uniformrootedtrees -> speciesmodelprior. The\n"
      "attached 'note:' line names the version where the rename happened." },

    { "021",
      "Legacy keyword renamed with a changed parameterisation.",
      "Both the keyword name AND the meaning of its values changed; the\n"
      "linter does not auto-rewrite. Currently this covers migprior -> wprior\n"
      "(BPP v4.8.0): wprior uses w = 4M/theta, so numeric values must be\n"
      "re-derived against your theta scale." },

    { "022",
      "Keyword removed with no direct replacement.",
      "Used for keywords that exist in BPP 2.x / 3.x or iBPP but have no\n"
      "counterpart in mainline BPP 4.x. Delete the line (or migrate to a\n"
      "fork that still supports it)." },

    { "023",
      "Keyword recognised by BPP 4.x but unimplemented; the parser aborts.",
      "BPP still tokenises the keyword (for parse compatibility with old\n"
      "files) but emits a 'Not implemented' fatal error at run time. Delete\n"
      "the keyword and any continuation block. Currently this covers\n"
      "'sequenceerror' (the BPP 3.x genotyping-error model has not been\n"
      "ported to 4.x)." },

    /* 1xx: completeness / semantics */
    { "100",
      "Required keyword is not set.",
      "BPP refuses to run without this keyword and provides no default. The\n"
      "always-required set (inference mode) is: seqfile, nloci, nsample,\n"
      "jobname, species&tree, tauprior, thetaprior. Simulation-mode set:\n"
      "seqfile, treefile, species&tree, loci&length. If a legacy alias is\n"
      "present (e.g. 'outfile' for 'jobname'), the linter treats the modern\n"
      "name as effectively set and only emits the rename error (020)." },

    { "101",
      "Conditionally-required keyword is missing.",
      "Required because another keyword is set, per the BPP manual's Table 2.\n"
      "The conditions checked are:\n"
      "  - imapfile           when species count > 1\n"
      "  - speciesmodelprior  when speciesdelimitation=1 or speciestree=1\n"
      "  - phiprior           when species&tree has an MSC-I '&phi=' node\n"
      "  - wprior             when 'migration' is set" },

    { "102",
      "Keyword is set but has no effect (or only takes effect) in this context.",
      "  - 'phase' has no effect when species count is 1\n"
      "  - 'qrates' / 'basefreqs' only apply when model = GTR\n"
      "Not a fatal error, but the line is probably either redundant or\n"
      "indicates the rest of the file is configured differently than\n"
      "intended." },

    { "103",
      "Optional keyword not set; BPP will use its built-in default.",
      "Informational. Lists the default BPP applies. Suppress with\n"
      "--no-defaults if you don't want to see these. Useful when reviewing\n"
      "an unfamiliar file or porting from an older BPP version where the\n"
      "default may have changed." },

    { "110",
      "thetaprior is implausible vs the data-derived estimate (--check-priors).",
      "Triggered when --check-priors is passed and the existing thetaprior's\n"
      "implied prior mean (b / (a-1) for invgamma(a,b)) is more than 10x off\n"
      "from the data-derived mean. The data-derived mean is the average\n"
      "within-species pairwise distance across loci, falling back to\n"
      "between-species coalescent variance when no within-species variation\n"
      "is observed. See PRIOR_CALCULATION_NOTES.md in the bpps repo." },

    /* 12x: cross-keyword consistency (mirrors check_validity() in BPP 4.8.7
     *      cfile.c, plus a few rules documented only in the manual). */
    { "120",
      "speciesmodelprior in {2, 3} is invalid in A10 (delimitation-only).",
      "When speciesdelimitation=1 and speciestree=0 (analysis A10), only\n"
      "speciesmodelprior = 0 or 1 are accepted. Values 2 (uniformSLH) and 3\n"
      "(uniformSRooted) weight delimitations by labeled-history or rooted-tree\n"
      "topology counts and are only meaningful when tree topologies are also\n"
      "being explored (A11). Source: bpp-4.8.7 cfile.c:2802-2804." },

    { "121",
      "speciesmodelprior in {2, 3} is invalid in A01 (tree-only).",
      "When speciestree=1 and speciesdelimitation=0 (analysis A01), only\n"
      "speciesmodelprior = 0 or 1 are documented as legal (BPP 4.x manual §14).\n"
      "Values 2 and 3 are intended for joint A11 analyses where both delim and\n"
      "tree are being inferred. Source: bpp-4-manual.md lines 1255-1260." },

    { "122",
      "bayesfactorbeta set to a non-default value with usedata = 0.",
      "BayesFactorBeta scales the data likelihood for marginal-likelihood\n"
      "estimation; with usedata = 0 there is no likelihood term to scale and\n"
      "BPP aborts the run. Either restore bayesfactorbeta to its default of 1\n"
      "or set usedata = 1. Source: bpp-4.8.7 cfile.c:2724-2725." },

    { "123",
      "cleandata = 1 with at least one unphased species (phase has '1').",
      "cleandata = 1 strips ambiguity characters from sequences before the\n"
      "likelihood calculation. Unphased diploid species rely on heterozygote\n"
      "ambiguity codes; the two settings cannot coexist. Either set\n"
      "cleandata = 0 or clear the offending '1' bit(s) in the phase line.\n"
      "Source: bpp-4.8.7 cfile.c:2870-2872." },

    { "124",
      "datefile (tip dating) used together with species delimitation.",
      "BPP does not currently support tip-dating combined with species\n"
      "delimitation (METHOD_10 or METHOD_11). Remove the datefile or set\n"
      "speciesdelimitation = 0. Source: bpp-4.8.7 cfile.c:2878-2879." },

    { "125",
      "datefile is set but locusrate is not '3 ...'.",
      "Tip-dating requires the locusrate = 3 parameterisation (all loci share\n"
      "the same mutation rate, estimated from sample ages). Any other value\n"
      "of locusrate (or leaving it at its default of 0) makes the dates\n"
      "unusable. Source: bpp-4.8.7 cfile.c:2881-2882; manual line 2151." },

    { "126",
      "locusrate = 3 ... is set but no datefile is provided.",
      "The 'locusrate = 3 a b' parameterisation exists specifically for\n"
      "tip-dating analyses, where 'a' and 'b' are the prior parameters on the\n"
      "shared mutation rate inferred from the sample ages. Without a datefile,\n"
      "the rate has nothing to be calibrated against. Source: BPP 4.x manual\n"
      "line 1771." },

    { "127",
      "migration block is incompatible with speciestree = 1.",
      "Species tree estimation under the MSC-M model is not implemented in\n"
      "BPP 4.x. If you need migration, fix the species tree (speciestree = 0).\n"
      "Source: bpp-4.8.7 cfile.c:2884-2887." },

    { "111",
      "tauprior is far too diffuse vs the data upper bound (--check-priors).",
      "Triggered when --check-priors is passed and the existing tauprior's\n"
      "implied mean is more than 10x the data-derived upper bound on tau.\n"
      "The bound is computed as:\n"
      "    tau_max = (max_pair_mean - theta_anc) / 2\n"
      "where max_pair_mean is the largest per-species-pair mean distance,\n"
      "and theta_anc is an estimate of the ancestral population theta (from\n"
      "between-species variance when computable, otherwise the within-\n"
      "species pairwise distance is used as a proxy). This is a coalescent\n"
      "correction to the raw maxDistance heuristic in the bpps reference,\n"
      "subtracting out the expected ancestral-coalescent contribution to\n"
      "between-species distances.\n\n"
      "Asymmetric on purpose: 'too tight' priors are not flagged. The data\n"
      "informs tau strongly enough that a tight, well-placed prior is\n"
      "often the right choice. On BPP's frogs A00 file, gamma(2, 1000)\n"
      "(prior mean 0.002) yields posterior tau ~0.0018 — within 1.5x of the\n"
      "data-derived ~0.0027." },
};

static size_t codes_n(void) {
    return sizeof(codes) / sizeof(codes[0]);
}

/* Normalise a user-supplied code spelling to the short canonical form. */
static int code_match(const char *needle, const char *canonical) {
    if (!needle || !canonical) return 0;
    /* Strip leading 'BPP' (case-insensitive). */
    if ((needle[0] == 'B' || needle[0] == 'b') &&
        (needle[1] == 'P' || needle[1] == 'p') &&
        (needle[2] == 'P' || needle[2] == 'p')) {
        needle += 3;
    }
    /* Skip leading zeros so "20" matches "020". */
    while (*needle == '0' && needle[1] != '\0') needle++;
    const char *c = canonical;
    while (*c == '0' && c[1] != '\0') c++;
    return strcmp(needle, c) == 0;
}

void bpp_codes_list(FILE *fp) {
    /* Two columns: code | summary. */
    for (size_t i = 0; i < codes_n(); i++) {
        fprintf(fp, "  %s  %s\n", codes[i].code, codes[i].summary);
    }
}

int bpp_codes_explain(const char *code, FILE *fp) {
    for (size_t i = 0; i < codes_n(); i++) {
        if (code_match(code, codes[i].code)) {
            fprintf(fp, "[%s]  %s\n\n%s\n",
                    codes[i].code, codes[i].summary, codes[i].detail);
            return 0;
        }
    }
    return -1;
}
