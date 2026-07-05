# BPP control-file syntax spec

`bpp-syntax.json` is the **canonical, machine-readable definition of the BPP
control-file syntax**. It is the single source of truth consumed by:

- **bpp-lint** — its keyword table and value checks, plus its awareness of
  earlier-release syntax for migrating old control files up to the latest
  release;
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
| `generate.py`     | authored  | the generator |
| `enrich.json`     | authored  | value grammars (latest release), defaults, deprecations |

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
