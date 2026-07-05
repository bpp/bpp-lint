#!/usr/bin/env python3
"""
generate.py -- build spec/bpp-syntax.json from the BPP source at a set of
release tags. This is the canonical control-file syntax spec that bpp-lint,
bpp-manual, and the bpp-agent grounding all consume (see spec/README.md).

Two layers:
  1. AUTO (dense, across every release): the control-file KEYWORD set and each
     keyword's CONTEXT (inference / simulation / both) are extracted from each
     release's src/cfile.c (inference parser) and src/cfile_sim.c (simulation
     parser) -- the strncasecmp(token,"...") dispatch, which is stable across
     the 4.x line. Diffing across releases yields each keyword's history
     (introduced_in / removed_in / present_in). This drives bpp-lint's ability
     to recognise and MIGRATE older control files up to the latest release.
  2. AUTHORED (sparse, latest release only): value grammar, defaults, and
     rename/reparameterisation notes live in enrich.json and are overlaid here.
     Value grammars describe the LATEST release only -- bpp-lint validates
     against the latest release and migrates older files up to it, so it never
     needs per-release value grammars.

Provenance (tag -> commit SHA) is written to releases.json so the spec is
reproducible; a CI check re-runs this and asserts bpp-syntax.json is unchanged.

Run:  python generate.py [--bpp PATH_TO_BPP_REPO] [--out .]
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

# strncasecmp(token, "keyword", N) -- the stable dispatch pattern in cfile*.c
KW_RE = re.compile(r'strncasecmp\s*\(\s*token\s*,\s*"([A-Za-z&_][A-Za-z0-9&_]*)"')


def git(bpp, *args):
    return subprocess.run(["git", "-C", str(bpp), *args],
                          capture_output=True, text=True, check=True).stdout


def show(bpp, tag, path):
    r = subprocess.run(["git", "-C", str(bpp), "show", f"{tag}:{path}"],
                       capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def extract(src):
    return set(m.group(1) for m in KW_RE.finditer(src)) if src else set()


def ver_tuple(tag):
    m = re.match(r'v?(\d+)\.(\d+)\.(\d+)', tag)
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)


def ver_str(tag):
    return re.match(r'v?(\d+\.\d+\.\d+)', tag).group(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bpp", default=str(Path.home() / "repos" / "bpp"),
                    help="path to the BPP source git repo")
    ap.add_argument("--out", default=str(Path(__file__).parent),
                    help="directory to write bpp-syntax.json / releases.json")
    args = ap.parse_args()
    bpp = Path(args.bpp)
    out = Path(args.out)

    tags = [t for t in git(bpp, "tag").split() if re.match(r'v?\d+\.\d+\.\d+', t)]
    tags.sort(key=ver_tuple)
    if not tags:
        sys.exit("no version tags found in bpp repo")

    releases, per_tag = {}, {}
    for tag in tags:
        sha = git(bpp, "rev-list", "-n", "1", tag).strip()
        infer = extract(show(bpp, tag, "src/cfile.c"))
        sim = extract(show(bpp, tag, "src/cfile_sim.c"))
        ctx = {}
        for k in infer | sim:
            ctx[k] = ("both" if k in infer and k in sim
                      else "inference" if k in infer else "simulation")
        per_tag[tag] = ctx
        releases[tag] = {"tag": tag, "version": ver_str(tag), "commit": sha}

    latest = tags[-1]
    all_kw = sorted({k for ctx in per_tag.values() for k in ctx})
    keywords = {}
    for k in all_kw:
        present = [t for t in tags if k in per_tag[t]]
        last = present[-1]
        rec = {
            "name": k,
            "context": per_tag[last][k],
            "current": k in per_tag[latest],
            "introduced_in": ver_str(present[0]),
            "present_in": [ver_str(t) for t in present],
        }
        if not rec["current"]:
            nxt = tags.index(last) + 1
            rec["removed_in"] = ver_str(tags[nxt]) if nxt < len(tags) else None
        keywords[k] = rec

    # overlay authored value grammars / defaults / notes (latest release)
    enrich_path = out / "enrich.json"
    if enrich_path.exists():
        enrich = json.loads(enrich_path.read_text())
        for k, extra in enrich.get("keywords", {}).items():
            keywords.setdefault(k, {"name": k}).update(extra)

    spec = {
        "spec_version": "0.1.0",
        "generated_from": {
            "repo": "bpp",
            "latest_release": ver_str(latest),
            "releases_scanned": [ver_str(t) for t in tags],
        },
        "note": ("Auto-generated keyword set / context / cross-release history "
                 "from BPP release tags; value grammars (latest release) are "
                 "authored in enrich.json and overlaid. Do not hand-edit "
                 "bpp-syntax.json -- run generate.py."),
        "keywords": keywords,
    }
    (out / "bpp-syntax.json").write_text(json.dumps(spec, indent=2) + "\n")
    (out / "releases.json").write_text(json.dumps(releases, indent=2) + "\n")

    # human-readable changelog of keyword additions/removals across releases
    print(f"scanned {len(tags)} releases: {ver_str(tags[0])} .. {ver_str(latest)}",
          file=sys.stderr)
    print(f"{len(keywords)} keywords total; "
          f"{sum(1 for v in keywords.values() if v['current'])} in {ver_str(latest)}\n",
          file=sys.stderr)
    prev = set()
    for t in tags:
        cur = set(per_tag[t])
        added = sorted(cur - prev)
        removed = sorted(prev - cur)
        if added or removed:
            print(f"  {ver_str(t):8s}  +{added}  -{removed}", file=sys.stderr)
        prev = cur


if __name__ == "__main__":
    main()
