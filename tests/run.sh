#!/usr/bin/env bash
# bpp-lint test harness. Invoked by `make test`; also runnable directly.
# Focus: lock in the --json contract (design §2) plus exit-code behavior, so
# the machine-readable output the bpp-agent loop depends on cannot regress.
set -u

# Resolve repo root from this script's location so paths work regardless of cwd.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/bpp-lint"
EX="$ROOT/examples"

if [[ ! -x "$BIN" ]]; then
    echo "FATAL: $BIN not built — run 'make' first" >&2
    exit 2
fi
HAVE_PY=0
if command -v python3 >/dev/null 2>&1; then HAVE_PY=1; fi

pass=0 fail=0
ok()   { printf '  ok   %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  FAIL %s\n' "$1"; fail=$((fail+1)); }

# check <desc> <expected> <actual>
check() {
    if [[ "$2" == "$3" ]]; then ok "$1"; else bad "$1 (expected '$2', got '$3')"; fi
}

# Extract a top-level string/number field from JSON. Prefers python3 (robust);
# falls back to grep so the suite still runs on a bare box.
jget() { # jget <json> <field>
    if [[ $HAVE_PY -eq 1 ]]; then
        printf '%s' "$1" | python3 -c 'import json,sys; print(json.load(sys.stdin)[sys.argv[1]])' "$2" 2>/dev/null
    else
        printf '%s' "$1" | grep -oE "\"$2\"[[:space:]]*:[[:space:]]*(\"[^\"]*\"|[0-9]+)" \
            | head -1 | sed -E "s/.*:[[:space:]]*//; s/^\"//; s/\"$//"
    fi
}
count_field() { # count_field <json> <errors|warnings|notes>
    if [[ $HAVE_PY -eq 1 ]]; then
        printf '%s' "$1" | python3 -c 'import json,sys; print(json.load(sys.stdin)["counts"][sys.argv[1]])' "$2" 2>/dev/null
    else
        printf '%s' "$1" | grep -oE "\"$2\"[[:space:]]*:[[:space:]]*[0-9]+" | head -1 | grep -oE '[0-9]+'
    fi
}

echo "== bpp-lint tests =="

# 1. Every example must emit strictly-parseable JSON under --json.
echo "-- strict JSON parseability --"
for f in "$EX"/*.ctl; do
    name="$(basename "$f")"
    out="$("$BIN" --json "$f" 2>/dev/null)"
    if [[ $HAVE_PY -eq 1 ]]; then
        if printf '%s' "$out" | python3 -m json.tool >/dev/null 2>&1; then
            ok "parse $name"
        else
            bad "parse $name (invalid JSON)"
        fi
    else
        # crude structural check when python3 is unavailable
        [[ "$out" == "{"* && "$out" == *"}"* && "$out" == *'"status"'* ]] \
            && ok "parse $name (no python3, structural only)" \
            || bad "parse $name"
    fi
done

# 2. Known-good file: valid status, exit 0, zero errors.
echo "-- valid control file (modern-4x) --"
out="$("$BIN" --json "$EX/modern-4x.bpp.ctl" 2>/dev/null)"; rc=$?
check "exit code" 0 "$rc"
check "status"    "valid" "$(jget "$out" status)"
check "n_species" "4" "$(jget "$out" n_species)"
check "errors=0"  "0" "$(count_field "$out" errors)"

# 3. Known-bad file: invalid status, exit 1, has errors + a fixable BPP020.
echo "-- invalid control file (legacy-3x) --"
out="$("$BIN" --json "$EX/legacy-3x.bpp.ctl" 2>/dev/null)"; rc=$?
check "exit code" 1 "$rc"
check "status"    "invalid" "$(jget "$out" status)"
[[ "$(count_field "$out" errors)" -gt 0 ]] && ok "errors > 0" || bad "errors > 0"
printf '%s' "$out" | grep -q '"code": "BPP020"' && ok "carries BPP020" || bad "carries BPP020"
printf '%s' "$out" | grep -q '"fixable": true'  && ok "has a fixable item" || bad "has a fixable item"

# 4. analysis_type is derived correctly (cross-checks is A10).
echo "-- analysis_type derivation --"
out="$("$BIN" --json "$EX/cross-checks.bpp.ctl" 2>/dev/null)"
check "cross-checks -> A10" "A10" "$(jget "$out" analysis_type)"

# 5. --check-priors advertises the request in prior_check.
echo "-- prior_check flag --"
out="$("$BIN" --json --check-priors "$EX/modern-4x.bpp.ctl" 2>/dev/null)"
printf '%s' "$out" | grep -q '"requested": true' && ok "prior_check.requested=true" || bad "prior_check.requested=true"

# 6. Mutual-exclusivity guards exit 2.
echo "-- guards --"
"$BIN" --json --fix            "$EX/modern-4x.bpp.ctl" >/dev/null 2>&1; check "--json --fix"            2 "$?"
"$BIN" --json --diff           "$EX/modern-4x.bpp.ctl" >/dev/null 2>&1; check "--json --diff"           2 "$?"
"$BIN" --json --suggest-priors "$EX/modern-4x.bpp.ctl" >/dev/null 2>&1; check "--json --suggest-priors" 2 "$?"

# 7. Missing file: exit 2, and must NOT print JSON to stdout.
echo "-- missing file --"
out="$("$BIN" --json "$EX/does-not-exist.ctl" 2>/dev/null)"; rc=$?
check "exit code" 2 "$rc"
[[ -z "$out" ]] && ok "no JSON on stdout" || bad "no JSON on stdout (got output)"

# 8. Grammar-driven value checks: type / arity / range / enum (BPP016-019),
#    and NO false positives on the valid modern example.
echo "-- grammar-driven value checks --"
tmp="$(mktemp)"
cat > "$tmp" <<'CTL'
seqfile = x.txt
jobname = run1
nloci = 2.5
cleandata = 3
usedata = 5
threads = 1 2 3 4 5
model = HKX
species&tree = 1 A
CTL
vout="$("$BIN" --json "$tmp" 2>/dev/null)"
has() { printf '%s' "$vout" | grep -q "\"code\": \"$1\""; }
has BPP016 && ok "BPP016 wrong type (nloci=2.5)"   || bad "BPP016 wrong type"
has BPP017 && ok "BPP017 wrong arity (threads)"    || bad "BPP017 wrong arity"
has BPP018 && ok "BPP018 out of range (usedata=5)" || bad "BPP018 out of range"
has BPP019 && ok "BPP019 bad enum (model=HKX)"     || bad "BPP019 bad enum"
rm -f "$tmp"
# valid file must carry none of the generic value codes
mout="$("$BIN" --json "$EX/modern-4x.bpp.ctl" 2>/dev/null)"
if printf '%s' "$mout" | grep -qE '"code": "BPP01[6-9]"'; then
    bad "no value-check false positives on modern-4x"
else
    ok "no value-check false positives on modern-4x"
fi

echo
echo "== $pass passed, $fail failed =="
[[ $fail -eq 0 ]]
