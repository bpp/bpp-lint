# Releasing bpp-lint

Releases are **tag-driven**. Pushing a tag matching `v*` runs
`.github/workflows/release.yml`, which builds binaries for four targets
(Linux x86_64, macOS arm64, macOS x86_64, Windows x86_64), publishes a GitHub
Release with `SHA256SUMS`, and then bumps the Homebrew formula in
[`bpp/homebrew-tap`](https://github.com/bpp/homebrew-tap).

## Cut a release

1. **Bump the version string** in `src/main.c` to match the tag:

   ```c
   #define BPP_LINT_VERSION "0.2.1"
   ```

   This is easy to forget and there is no auto-derivation, so `--version` will
   drift if you skip it. Build and confirm:

   ```bash
   make && ./bpp-lint --version      # -> bpp-lint 0.2.1
   ```

2. **Commit and push** the bump to `main`:

   ```bash
   git commit -am "Bump version string to 0.2.1"
   git push origin main
   ```

3. **Tag and push** (annotated tag; summarise the changes in the message):

   ```bash
   git tag -a v0.2.1 -m "v0.2.1 — <summary>"
   git push origin v0.2.1
   ```

   Pre-release tags containing a `-` (e.g. `v0.3.0-rc1`) are marked as
   pre-releases and **skip** the Homebrew bump.

4. **Watch the workflow:**

   ```bash
   gh run watch "$(gh run list --workflow=release.yml --limit 1 \
       --json databaseId -q '.[0].databaseId')"
   ```

## Homebrew auto-bump (the `HOMEBREW_TAP_TOKEN` secret)

The final `bump-homebrew` job **pushes the formula bump directly** to
`bpp/homebrew-tap` (clone → rewrite `url`/`sha256` → commit → push to `main`).
It authenticates with the repository secret **`HOMEBREW_TAP_TOKEN`**.

> **Why not `mislav/bump-homebrew-formula-action`?** We used it originally, but
> it returns `unexpected HTTP 303` against this tap: the action tries to *fork*
> `bpp/homebrew-tap`, which fails because the token owner already has push
> access within the org. The direct-push step avoids that entirely.

### Create the token

Use a **classic PAT** owned by an account with **push access to
`bpp/homebrew-tap`**:

1. <https://github.com/settings/tokens> → *Generate new token (classic)*.
2. Scope: check **`repo`**. Set an expiration.
3. Generate and copy.

(A fine-grained PAT with Contents: write on the tap also works for the
direct-push step, but classic `repo` is simplest.)

### Store the secret

Use `printf` (no trailing newline) or the web UI:

```bash
printf '%s' 'PASTE_TOKEN' | gh secret set HOMEBREW_TAP_TOKEN --repo bpp/bpp-lint
gh secret list --repo bpp/bpp-lint | grep HOMEBREW_TAP_TOKEN
```

### Test without a new release

```bash
gh run rerun <release-run-id> --failed --repo bpp/bpp-lint
```

### Troubleshooting

- **`403` / auth failures on `git push`** — the token is expired, mistyped, or
  its account lacks push access to `bpp/homebrew-tap`. Regenerate a classic
  `repo` PAT from an account with write on the tap.
- **`fatal: could not read Username`** — the `HOMEBREW_TAP_TOKEN` secret is
  empty. Re-set it (see above; use `printf` to avoid a trailing newline).
- **historical `unexpected HTTP 303`** — came from the old
  `mislav/bump-homebrew-formula-action` trying to fork the org-owned tap; the
  direct-push job no longer has this problem.

## Manual formula bump (fallback when the token is broken)

If the auto-bump fails, update the formula by hand. The formula builds from the
**source tarball**, so its `sha256` is the checksum of the GitHub archive, not
of the release binaries:

```bash
VER=0.2.1
curl -sL -o src.tar.gz \
  "https://github.com/bpp/bpp-lint/archive/refs/tags/v${VER}.tar.gz"
shasum -a 256 src.tar.gz          # -> the sha256 for the formula

git clone git@github.com:bpp/homebrew-tap.git
# Edit Formula/bpp-lint.rb: set url to .../v${VER}.tar.gz and sha256 to the above
cd homebrew-tap
git commit -am "bpp-lint ${VER}"
git push origin main
```

Verify:

```bash
brew update-reset && brew upgrade bpp/tap/bpp-lint
bpp-lint --version                # -> bpp-lint 0.2.1
```
