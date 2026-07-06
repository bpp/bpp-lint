# BPP control-file syntax spec

`bpp-syntax.json` is the **canonical, machine-readable definition of the BPP
control-file syntax**. It is the single source of truth consumed by:

- **bpp-lint** — its keyword table *and* its value checking are *generated*
  from this spec, not hand-maintained: `gen_keywords_c.py` projects
  `bpp-syntax.json` into `src/keywords_gen.c` — both the `kw_table[]` the linter
  compiles in and a per-keyword typed **slot list** compiled from each
  `value.grammar`, which the linter walks to catch wrong type, wrong argument
  count, out-of-range values, and bad enum choices (diagnostics BPP016–019) —
  plus its awareness of earlier-release syntax for migrating old control files
  up to the latest release;
- **bpp-manual** — the control-file variable tables and per-variable syntax are
  audited (and can be generated) against it;
- **bpp-agent** — the agent's grounding / tool schema reference it.

It lives here (not in the BPP source repo) because it is *generated* from the
source and would otherwise be redundant, bloat that repo, and be irrelevant to
compilation.

## How it is built — two layers

`generate.py` scans the BPP source at a **set of release tags** and merges two
layers:

1. **Auto-derived (every release).** The control-file keyword set and each
   keyword's context (`inference` / `simulation` / `both`) are extracted from
   `src/cfile.c` (inference parser) and `src/cfile_sim.c` (simulation parser) at
   each tag — the stable `strncasecmp(token,"...")` dispatch. Diffing across
   releases yields each keyword's `introduced_in` / `removed_in` / `present_in`.
   This is what lets bpp-lint recognise old keywords and migrate old control
   files **up to the latest release**.

2. **Authored (`enrich.json`, latest release only).** Value grammar, defaults,
   and deprecation / rename / reparameterisation notes are hand-authored and
   overlaid. Value grammars describe **only the latest release**: bpp-lint
   validates a control file against the latest release and upgrades older files
   to it, so per-release value grammars are unnecessary. The cross-release layer
   only needs keyword-level history (which keyword replaces which) to drive that
   migration.

`releases.json` records exactly which release tags (and commit SHAs) produced
the spec, so it is reproducible and auditable.

## Regenerating

```
python generate.py --bpp /path/to/bpp        # defaults to ~/repos/bpp
```

Do **not** hand-edit `bpp-syntax.json` — edit `enrich.json` (authored facts) or
re-run `generate.py` (source-derived facts). A CI check should re-run
`generate.py` against the pinned release set and assert `bpp-syntax.json` is
unchanged, so the spec never drifts from the parser: adding a keyword in a
future release fails CI until the spec is regenerated.

## Files

| File | Source | Contents |
|------|--------|----------|
| `bpp-syntax.json` | generated | the merged canonical spec (committed artifact) |
| `releases.json`   | generated | provenance: tag → version → commit SHA |
| `generate.py`     | authored  | the spec generator (source tags + enrich.json → bpp-syntax.json) |
| `gen_keywords_c.py` | authored | projects bpp-syntax.json → `../src/keywords_gen.c` (bpp-lint's keyword table) |
| `enrich.json`     | authored  | value grammars (latest release), defaults, deprecations |

## bpp-lint's keyword table is generated

`../src/keywords_gen.c` (the `kw_table[]` bpp-lint compiles in) is produced from
`bpp-syntax.json` by `gen_keywords_c.py` and committed, so the ordinary `make`
build needs no Python. `../src/keywords.c` keeps only the stable lookup API
(`bpp_keyword_find` / `_at` / `_suggest` / `_slots`) and references the generated
tables via `extern`. Field mapping: spec `status`/`context` →
`kw_status_t`/`kw_mode_t`, `superseded_by` → `replacement`, `default` →
`default_value`, deprecated `note` → the fix hint. Regenerate with `make gen`;
`make check-gen` (run by `make test`) fails if the committed table drifts from
the spec.

### Value grammars → slot lists

`gen_keywords_c.py` also compiles each keyword's `value.grammar` mini-language
into a flat list of typed slots (`kw_slot_t`) emitted into `keywords_gen.c`, and
lint.c's `check_value_generic()` walks a value's tokens against them. The
grammar atoms map to slot types (`b`→bool, `d`→int, `+d`→uint, `f`→float,
`s`→string); `[x]` marks trailing optional slots, `x*` a trailing repetition,
`(0|1)` a bounded leading slot; `enum`/`values`/`range`/`constraints` become
enum lists and numeric bounds. Grammars that need real parsing (top-level
alternation with differing arities, Newick trees, multi-line blocks) or that
have a bespoke check in lint.c (`print`, `thetaprior`, `tauprior`, `phiprior`,
`finetune`, `locusrate`, `clock`) compile to no slots and are skipped by the
generic checker. Of the 49 live keywords, 38 currently carry a slot profile.

## Keyword record shape

```json
{
  "name": "thetaprior",
  "context": "inference",
  "current": true,
  "introduced_in": "4.0.0",
  "present_in": ["4.8.6", "4.8.7"],
  "value": { "grammar": "dist f f [flag]", "dist": ["invgamma","gamma","beta"],
             "constraints": ["invgamma alpha > 2 (since 4.8.2)"] },
  "default": null,
  "required": "always",
  "summary": "Prior on theta (population size)"
}
```

Deprecated / renamed keywords carry the migration target:

```json
{ "name": "outfile", "status": "renamed", "superseded_by": "jobname",
  "since": "4.8.0", "note": "BPP 4.8.0 merged outfile/mcmcfile into jobname." }
```
