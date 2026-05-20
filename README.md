# bpp-lint

A linter for [BPP](https://github.com/bpp/bpp) (Bayesian Phylogenetics
& Phylogeography) control files. Targets BPP 4.x syntax, flags errors
and stylistic problems, auto-fixes files written for BPP 2.x / 3.x, and
sanity-checks priors against the underlying sequence data.

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
| `0xx` | Lexical / structural problems                    |
| `01x` | Value-format problems                            |
| `02x` | Legacy / renamed / removed keywords (auto-fixable) |
| `1xx` | Completeness and context                         |
| `11x` | Prior sanity (`--check-priors`)                  |
| `12x` | Cross-keyword consistency (mirrors `check_validity()` in `cfile.c`) |

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
| `--color=WHEN`    | `auto` (default), `always`, or `never`                            |
| `--version`       | Print version and exit                                            |
| `-h, --help`      | Full help                                                         |

## Examples

The `examples/` directory contains three fixtures:

- `modern-4x.bpp.ctl` — clean BPP 4.x file; only `[103]` default-value
  notes when linted.
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

## Exit codes

| Code | Meaning                                                          |
|------|------------------------------------------------------------------|
| `0`  | No errors (warnings may still be present)                        |
| `1`  | At least one error reported, or `--diff` would change the file   |
| `2`  | Invocation error (bad arguments, missing file, I/O failure)      |

## License

See [LICENSE](LICENSE).
