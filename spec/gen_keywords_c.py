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


# ---------- value grammar -> slot list (for generic value checking) ----------
#
# Compile the spec's value.grammar mini-language into a flat list of typed
# slots the C validator can walk. Grammars that need real parsing (top-level
# alternation with differing arities, Newick trees, multi-line blocks) or that
# have a bespoke check in lint.c compile to None and are skipped.

ATOM_TYPE = {"b": "VT_BOOL", "d": "VT_INT", "+d": "VT_UINT",
             "f": "VT_FLOAT", "s": "VT_STRING"}

# Keywords with a bespoke check in lint.c (check_*), or a semantically
# conditional grammar the flat slot checker must not second-guess. These are
# skipped by the generic checker even when their grammar looks reducible.
CUSTOM_CHECK = {"print", "thetaprior", "tauprior", "phiprior",
                "finetune", "locusrate", "clock"}


def grammar_tokens(g):
    toks, i = [], 0
    while i < len(g):
        c = g[i]
        if c.isspace():
            i += 1
        elif c in "[]*":
            toks.append(c)
            i += 1
        else:
            j = i
            while j < len(g) and not g[j].isspace() and g[j] not in "[]*":
                j += 1
            toks.append(g[i:j])
            i = j
    return toks


def apply_constraints(val, slots):
    numeric = [s for s in slots if s["type"] in ("VT_INT", "VT_UINT", "VT_FLOAT")]

    def set_min(s, lo, excl=False):
        s["has_min"] = True
        s["min"] = float(lo)
        if excl:
            s["min_excl"] = True

    def set_range(s, lo, hi):
        s["has_min"], s["min"] = True, float(lo)
        s["has_max"], s["max"] = True, float(hi)

    enum = val.get("enum")
    if enum:
        for s in slots:
            if s["type"] == "VT_STRING":
                s["enums"] = list(enum)
                break

    values = val.get("values")
    if isinstance(values, list) and values and all(isinstance(x, int) for x in values) and numeric:
        set_range(numeric[0], min(values), max(values))

    rng = val.get("range")
    if isinstance(rng, list) and len(rng) == 2 and numeric:
        set_range(numeric[0], rng[0], rng[1])

    for c in val.get("constraints") or []:
        cl = c.lower()
        spread = any(w in cl for w in ("each", "both", "all"))
        targets = numeric if spread else numeric[:1]
        if re.search(r'>\s*0', cl) or "positive" in cl:
            for s in targets:
                set_min(s, 0, excl=True) if s["type"] == "VT_FLOAT" else set_min(s, 1)
        m = re.search(r'>=\s*(\d+(?:\.\d+)?)', cl)
        if m and (spread or len(numeric) == 1):
            for s in targets:
                set_min(s, m.group(1))


def compile_grammar(name, rec):
    if name in CUSTOM_CHECK:
        return None
    val = rec.get("value") or {}
    g = val.get("grammar")
    if not g:
        return None
    g = g.strip()
    slots = []

    # leading integer-literal choice, e.g. "(0|1) f f f f" -> one bounded slot
    m = re.match(r'\(\s*(-?\d+)\s*\|\s*(-?\d+)\s*\)\s*(.*)$', g)
    if m:
        lo, hi = sorted((int(m.group(1)), int(m.group(2))))
        slots.append({"type": "VT_UINT" if lo >= 0 else "VT_INT",
                      "has_min": True, "min": float(lo),
                      "has_max": True, "max": float(hi)})
        g = m.group(3).strip()

    # anything left that needs real parsing -> bail out (skip generic check)
    if re.search(r'[()|,;]', g) or re.search(r'\b(t|dist|int)\b', g):
        return None

    in_opt = False
    for t in grammar_tokens(g):
        if t == "[":
            in_opt = True
        elif t == "]":
            in_opt = False
        elif t == "*":
            if slots:
                slots[-1]["repeat"] = True
        elif t in ATOM_TYPE:
            slots.append({"type": ATOM_TYPE[t], "optional": in_opt})
        else:
            return None  # unknown atom/literal
    if not slots:
        return None
    apply_constraints(val, slots)
    return slots


def cident(name):
    return re.sub(r'[^0-9A-Za-z]', "_", name)


def slot_c(s, enum_ref):
    parts = [f".type = {s['type']}"]
    if s.get("optional"):
        parts.append(".optional = 1")
    if s.get("repeat"):
        parts.append(".repeat = 1")
    if s.get("has_min"):
        parts += [".has_min = 1", f".min = {s['min']:g}"]
    if s.get("min_excl"):
        parts.append(".min_excl = 1")
    if s.get("has_max"):
        parts += [".has_max = 1", f".max = {s['max']:g}"]
    if s.get("enums"):
        parts.append(f".enums = {enum_ref}")
    return "{ " + ", ".join(parts) + " }"


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

    # ---- value grammar slot tables ----
    profiles = []  # (name, ident, slots)
    for name, rec in kws.items():
        if not rec.get("current"):
            continue
        slots = compile_grammar(name, rec)
        if slots:
            profiles.append((name, cident(name), slots))

    L.append("/* ===== Value grammars: typed slot lists compiled from")
    L.append(" * spec value.grammar, walked by lint.c's generic value checker.")
    L.append(f" * {len(profiles)} of {len(valid_infer) + len(valid_sim)} live keywords have a")
    L.append(" * reducible grammar; the rest (alternation / Newick / multi-line /")
    L.append(" * bespoke-checked) are validated elsewhere or not at all. ===== */")
    L.append("")
    for name, ident, slots in profiles:
        enum_ref = "NULL"
        for s in slots:
            if s.get("enums"):
                enum_ref = f"enum_{ident}"
                items = ", ".join(f'"{e}"' for e in s["enums"])
                L.append(f"static const char *const {enum_ref}[] = {{ {items}, NULL }};")
        row = ", ".join(slot_c(s, enum_ref) for s in slots)
        L.append(f"static const kw_slot_t slots_{ident}[] = {{ {row}, {{ .type = VT_END }} }};")
    L.append("")
    L.append("const kw_valuespec_t kw_value_table[] = {")
    for name, ident, _ in profiles:
        L.append(f'    {{ {cstr(name)}, slots_{ident} }},')
    L.append("    { NULL, NULL }")
    L.append("};")
    L.append("")

    Path(args.out).write_text("\n".join(L))
    print(f"wrote {args.out}: {len(valid_infer)} valid, {len(valid_sim)} sim, "
          f"{len(deprecated)} deprecated ({len(entries)} total); "
          f"{len(profiles)} value grammars")


if __name__ == "__main__":
    main()
