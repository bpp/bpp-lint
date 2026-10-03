# bpp-lint

A linter for [BPP](https://github.com/bpp/bpp) (Bayesian Phylogenetics
& Phylogeography) control files. Targets the latest BPP 4.x syntax. It
validates a control file at three layers — **structural** (missing `=`,
unknown keyword, duplicate assignment), **value** (each keyword's value checked
against a generated grammar: type, argument count, range, allowed values), and
**semantic** (cross-keyword consistency, mirroring BPP's own `check_validity()`)
— auto-fixes files written for BPP 2.x / 3.x, and sanity-checks priors against
the underlying sequence data. A `--json` mode makes it scriptable.

## Install

### Homebrew (macOS, Linux)

```
brew install bpp/tap/bpp-lint
```

### Prebuilt binaries

Download a tarball / zip for your platform from the
[releases](https://github.com/bpp/bpp-lint/releases) page.

### Build from source

```
make
```

Produces `./bpp-lint`. C11; no external dependencies. To install:

```
make install PREFIX=/usr/local
```

## Usage

```
bpp-lint [options] <control-file>
```

Diagnostics print to stderr in compiler style:

```
file:LINE:COL: severity: message
  note: optional explanation
  fix:  optional autofix preview
```

`severity` is `error`, `warning`, or `info`. Add `--codes` to include
the BPP code (e.g. `[120]`) — useful when filtering output. Browse the
catalogue:

```
bpp-lint --list-codes
bpp-lint --explain 120
```

Codes are grouped:

| Range | Topic                                            |
|-------|--------------------------------------------------|
| `0xx` | Lexical / structural problems (missing `=`, unknown keyword, duplicate assignment) |
| `01x` | Value problems: wrong type / argument count / range / allowed value, and format |
| `02x` | Legacy / renamed / removed keywords (auto-fixable) |
| `1xx` | Completeness and context                         |
| `11x` | Prior sanity (`--check-priors`)                  |
| `12x` | Cross-keyword consistency (mirrors `check_validity()` in `cfile.c`) |
| `13x` | `species&tree` block: header, counts, Newick                        |
| `14x` | `migration` (MSC-M) block                                         |
| `15x` | Data consistency: the control file vs the `seqfile` / `Imapfile` it names (see below) |

### Data-consistency checks (`15x`)

By default bpp-lint opens the `seqfile` and `Imapfile` named in an inference
control file and checks them against it. Each check mirrors an abort that BPP
raises only *after* the control file has parsed, so a syntactically valid file
can still be refused by BPP. `--explain` quotes the BPP message each code
prevents.

| Code  | Severity          | Condition                                                        | BPP message prevented |
|-------|-------------------|------------------------------------------------------------------|-----------------------|
| `150` | error             | `seqfile` / `Imapfile` cannot be opened (or parsed)              | `Unable to open file (...)` |
| `151` | warning           | a relative data path resolves differently from the control file's directory than from the current directory (BPP uses the latter) | `Unable to open file (...)` when run elsewhere |
| `152` | error / info      | `nloci` larger than the loci in the seqfile / smaller (BPP uses the first `nloci`) | `Expected N loci but found only M` |
| `153` | error             | a `^tag` in the seqfile has no Imap entry                        | `Cannot find a mapping to species for tag ...` |
| `154` | error / warning   | an Imap species is not in `species&tree` / a tree species has no Imap individuals (BPP runs) | `Cannot find node with population label ...` |
| `155` | error / warning   | `phase` digit count ≠ species count; warning when all digits are 0 (BPP discards the line) | `Number of digits in 'phase' does not match number of species` |
| `156` | info              | per-species counts in `species&tree` differ from the Imap individuals (inference ignores the counts) | — |
| `157` | error             | a sequence label has no `^` species tag (multi-species runs)     | `Cannot find species tag on sequence ...` |

Paths are resolved relative to the control file's directory (friendlier for
editors); BPP resolves them relative to the directory it is run from, and
`151` warns whenever that makes a difference. `--no-data-checks` turns the pass
off; it never runs for `--simulate` files, where those files are outputs.

### JSON output

For editors and pipelines, `--json` emits a single machine-readable report to
stdout instead of the human-readable diagnostics:

```
bpp-lint --json foo.ctl
```

It carries top-level `status` (`"valid"` / `"invalid"`), `counts`, and a
`diagnostics[]` array where each item has `code`, `severity`, `line`, `column`,
`message`, `suggestion`, `fixable` + `suggested_fix`, and — for `[103]`
"using default" notices — a structured `default` `{keyword, value}`. Consumers
typically use `status == "valid"` as a stop condition. `--json` cannot be
combined with `--fix`, `--diff`, or `--suggest-priors`.

A top-level `data` object reports what the data-consistency pass resolved and
read (`null` when the pass did not run: `--no-data-checks` or `--simulate`):

```json
"data": {
  "seqfile": "/abs/path/tiny.txt",
  "imapfile": "/abs/path/tiny.imap",
  "n_loci": 2,
  "n_sequences": 6,
  "species": ["A", "B", "C"]
}
```

Paths are absolute, as the files were looked for. `n_loci` and `n_sequences`
(the number of distinct `^tag` labels) come from the seqfile, `species` from
the Imap; each is `null` when the corresponding file could not be read. The
exit status follows the diagnostics: any error-severity item exits 1, exactly
when `status` is `"invalid"`.

### Applying fixes

Renamed keywords (`outfile → jobname`, `diploid → phase`, …), the
single-bit legacy `print`, the third `tauprior` token, and similar
mechanical fixes can be previewed or written back:

```
bpp-lint --diff foo.ctl       # unified diff on stdout (apply with patch -p0)
bpp-lint --fix  foo.ctl       # rewrite in place; original saved as foo.ctl.bak
```

`--fix` and `--diff` are mutually exclusive. `--diff` follows gofmt
convention: exit 1 if a rewrite would be needed.

Semantic changes (`migprior → wprior` reparameterisation, removed
features like `sequenceerror`) are reported but never auto-rewritten —
they need human review.

### Prior recommendations

Given a control file with valid `seqfile` and `imapfile`, the linter
can derive prior means directly from the data:

```
bpp-lint --suggest-priors foo.ctl   # print recommended thetaprior /
                                    # tauprior lines and exit
bpp-lint --check-priors  foo.ctl    # warn (BPP110/BPP111) if existing
                                    # priors are >10x off the data
```

Theta uses the within-species pairwise distance; tau uses a coalescent
correction to the raw max-distance heuristic. See the `bpps` reference
implementation for the underlying calculation.

### Other options

| Option            | Description                                                       |
|-------------------|-------------------------------------------------------------------|
| `-s, --simulate`  | Lint as a BPP `--simulate` control file (different keyword set)   |
| `-q, --quiet`     | Suppress warnings and notes; errors only                          |
| `--no-defaults`   | Suppress `[103]` notes about keywords falling back to default     |
| `--no-data-checks`| Skip the `15x` data-consistency pass (do not open `seqfile` / `Imapfile`) |
| `--color=WHEN`    | `auto` (default), `always`, or `never`                            |
| `--version`       | Print version and exit                                            |
| `-h, --help`      | Full help                                                         |

## Examples

The `examples/` directory contains three fixtures:

- `modern-4x.bpp.ctl` — clean BPP 4.x file; only `[103]` default-value
  notes when linted (plus `[150]`, since its `frogs.txt` data files are not
  in the repo; pass `--no-data-checks` to see the syntax result alone).
- `legacy-3x.bpp.ctl` — BPP 3.x file demonstrating the rename,
  removal, and value-format diagnostics. Try `--diff` to see the
  auto-rewrite.
- `cross-checks.bpp.ctl` — four `12x` cross-keyword consistency rules
  triggered in one file.

```
./bpp-lint --codes examples/legacy-3x.bpp.ctl
./bpp-lint --diff  examples/legacy-3x.bpp.ctl
./bpp-lint --codes --no-defaults examples/cross-checks.bpp.ctl
```

## Keyword and value definitions

The keyword catalogue and every value grammar are **generated** from a canonical
machine-readable spec, [`spec/bpp-syntax.json`](spec/bpp-syntax.json), which is
itself derived from the BPP source across its release tags (plus an authored
layer for value grammars, defaults, and deprecation notes). The linter therefore
tracks the real parser instead of a hand-maintained copy that can drift.
`src/keywords_gen.c` — the table compiled into the binary — is regenerated with
`make gen`, and `make check-gen` (run by `make test`) fails if it drifts from
the spec. See [`spec/README.md`](spec/README.md) for the full design.

## Exit codes

| Code | Meaning                                                          |
|------|------------------------------------------------------------------|
| `0`  | No errors (warnings may still be present)                        |
| `1`  | At least one error reported, or `--diff` would change the file   |
| `2`  | Invocation error (bad arguments, missing file, I/O failure)      |

## License

See [LICENSE](LICENSE).
