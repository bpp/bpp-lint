#!/usr/bin/env python3
"""
gen_keywords_c.py -- generate src/keywords_gen.c (bpp-lint's keyword table)
from the canonical spec/bpp-syntax.json.

This is what makes bpp-lint "load the spec": the keyword catalogue is no longer
hand-maintained C -- it is projected from bpp-syntax.json (itself generated from
the BPP release tags + enrich.json; see spec/README.md). The stable lookup API
(bpp_keyword_find / _at / _suggest) stays hand-written in src/keywords.c and
references the generated `kw_table[]` via extern.

The generated file is committed so the ordinary `make` build needs no Python.
Regenerate with `make gen`; CI verifies it is in sync with `make check-gen`.

Run:  python gen_keywords_c.py [--spec spec/bpp-syntax.json] [--out src/keywords_gen.c]

Field mapping  (spec key -> bpp_keyword_t field):
  name            <- name
  status          <- explicit enrich `status` if present, else current+context:
                       renamed->KW_RENAMED, reparameterised->KW_REPARAMETERISED,
                       removed/foreign->KW_REMOVED, unimplemented->KW_UNIMPLEMENTED;
                       current & context simulation -> KW_VALID_SIM; else KW_VALID.
  mode            <- context: inference->MODE_INFER, simulation->MODE_SIM, both->MODE_BOTH
  replacement     <- superseded_by
  note            <- deprecated: enrich `note`; valid: `summary` (informational only --
                       lint.c uses `note` only for deprecated keywords' fix hints)
  since_version   <- `since` (or latest value_history `since`) as code M*100+m*10+p;
                       informational (lint.c does not read it)
  default_value   <- `default` stringified (NULL for must-set / no default)
"""
import argparse
import json
import re
from pathlib import Path

HERE = Path(__file__).parent

STATUS_MAP = {
    "renamed": "KW_RENAMED",
    "reparameterised": "KW_REPARAMETERISED",
    "removed": "KW_REMOVED",
    "foreign": "KW_REMOVED",          # iBPP-only extensions: unusable in mainline BPP
    "unimplemented": "KW_UNIMPLEMENTED",
}
MODE_MAP = {"inference": "MODE_INFER", "simulation": "MODE_SIM", "both": "MODE_BOTH"}


def vcode(s):
    """'4.8.0' -> 480, '3.x' -> 300, None -> 0."""
    if not s:
        return 0
    if s.strip().lower().startswith("3"):
        return 300
    m = re.match(r'(\d+)\.(\d+)\.(\d+)', s)
    if not m:
        return 0
    a, b, c = (int(x) for x in m.groups())
    return a * 100 + b * 10 + c


def cstr(s):
    """Render a Python str/None as a C string literal or NULL."""
    if s is None:
        return "NULL"
    s = str(s).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{s}"'


def classify(rec):
    """Return (status_enum, is_valid) for a spec record."""
    st = rec.get("status")
    if st in STATUS_MAP:
        return STATUS_MAP[st], False
    # no explicit deprecated status -> a live keyword in the latest release
    if rec.get("context") == "simulation":
        return "KW_VALID_SIM", True
    return "KW_VALID", True


def since_code(rec):
    if rec.get("since"):
        return vcode(rec["since"])
    vh = rec.get("value_history")
    if vh and isinstance(vh, list) and vh[-1].get("since"):
        return vcode(vh[-1]["since"])
    return 0


def note_for(rec, is_valid):
    # lint.c reads `note` only for deprecated keywords (as the fix hint); for
    # valid keywords the one-line summary keeps the table self-documenting.
    if is_valid:
        return rec.get("summary")
    return rec.get("note") or rec.get("summary")


def default_for(rec):
    d = rec.get("default")
    if d is None:
        return None
    if isinstance(d, bool):          # spec has no bool defaults today, but be safe
        return "1" if d else "0"
    return str(d)


def entry(rec):
    status, is_valid = classify(rec)
    mode = MODE_MAP.get(rec.get("context"), "MODE_INFER")
    return {
        "name": rec["name"],
        "status": status,
        "is_valid": is_valid,
        "mode": mode,
        "replacement": rec.get("superseded_by"),
        "note": note_for(rec, is_valid),
        "since": since_code(rec),
        "default": default_for(rec),
    }


def fmt(e):
    return (f'    {{ {cstr(e["name"]):>22}, {e["status"]:<18}, {e["mode"]:<10}, '
            f'{cstr(e["replacement"])}, {cstr(e["note"])}, {e["since"]}, '
            f'{cstr(e["default"])} }},')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", default=str(HERE / "bpp-syntax.json"))
    ap.add_argument("--out", default=str(HERE.parent / "src" / "keywords_gen.c"))
    args = ap.parse_args()

    spec = json.loads(Path(args.spec).read_text())
    kws = spec["keywords"]
    rel = spec.get("generated_from", {}).get("latest_release", "?")

    entries = [entry(rec) for rec in kws.values()]
    valid_infer = [e for e in entries if e["is_valid"] and e["mode"] != "MODE_SIM"]
    valid_sim = [e for e in entries if e["is_valid"] and e["mode"] == "MODE_SIM"]
    deprecated = [e for e in entries if not e["is_valid"]]

    L = []
    L.append("/* GENERATED FILE -- DO NOT EDIT.")
    L.append(" *")
    L.append(f" * Projected from spec/bpp-syntax.json (BPP {rel}) by")
    L.append(" * spec/gen_keywords_c.py.  Edit spec/enrich.json (authored facts) or")
    L.append(" * re-run spec/generate.py (source-derived facts), then `make gen`.")
    L.append(" *")
    L.append(" * The lookup API (bpp_keyword_find / _at / _suggest) lives in")
    L.append(" * src/keywords.c and references this table via extern.")
    L.append(" */")
    L.append('#include "keywords.h"')
    L.append("")
    L.append("#include <stddef.h>")
    L.append("")
    L.append("/* Field order: name, status, mode, replacement, note, since_version,")
    L.append(" * default_value.  Valid inference/both keywords first, then valid")
    L.append(" * simulation keywords, then deprecated/legacy -- so the \"did you mean\"")
    L.append(" * suggester surfaces live keywords first. */")
    L.append("const bpp_keyword_t kw_table[] = {")
    L.append("")
    L.append("    /* ===== Valid BPP 4.x inference-mode keywords ===== */")
    L += [fmt(e) for e in valid_infer]
    L.append("")
    L.append("    /* ===== Valid BPP 4.x --simulate-mode keywords ===== */")
    L += [fmt(e) for e in valid_sim]
    L.append("")
    L.append("    /* ===== Deprecated / renamed / removed / foreign ===== */")
    L += [fmt(e) for e in deprecated]
    L.append("")
    L.append("    /* Sentinel */")
    L.append("    { NULL, KW_UNKNOWN, 0, NULL, NULL, 0, NULL }")
    L.append("};")
    L.append("")

    Path(args.out).write_text("\n".join(L))
    print(f"wrote {args.out}: {len(valid_infer)} valid, {len(valid_sim)} sim, "
          f"{len(deprecated)} deprecated ({len(entries)} total)")


if __name__ == "__main__":
    main()
