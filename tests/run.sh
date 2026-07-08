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

# duplicate-key structural check (BPP005): keyword assigned twice.
echo "-- duplicate-key check --"
dtmp="$(mktemp)"
printf 'seqfile = a\nseqfile = b\njobname = r\nnloci = 5\nspecies&tree = 1 A\n' > "$dtmp"
dout="$("$BIN" --json "$dtmp" 2>/dev/null)"
printf '%s' "$dout" | grep -q '"code": "BPP005"' && ok "BPP005 fires on duplicate seqfile" || bad "BPP005 duplicate key"
rm -f "$dtmp"

# 9. JSON schema carries suggestion (note) + structured default (for the editor).
echo "-- json suggestion / structured default --"
if [[ $HAVE_PY -eq 1 ]]; then
    stmp="$(mktemp)"
    printf 'seqfile=a\njobname=r\nnloci=1\nnsample=1\nmodel = HKX\ntauprior=invgamma 3 0.03\nthetaprior=invgamma 3 0.002\nspecies&tree=1 A\n' > "$stmp"
    sout="$("$BIN" --json "$stmp" 2>/dev/null)"
    rm -f "$stmp"
    printf '%s' "$sout" | python3 -c '
import json,sys
d=json.load(sys.stdin)
diags=d["diagnostics"]
enum=[x for x in diags if x["code"]=="BPP019"]
ok_sugg = bool(enum) and enum[0].get("suggestion") and "one of" in enum[0]["suggestion"]
defs=[x for x in diags if x["code"]=="BPP103" and x.get("default")]
ok_def = any(x["default"].get("keyword") and x["default"].get("value") is not None for x in defs)
sys.exit(0 if (ok_sugg and ok_def) else 1)
' && ok "suggestion + structured default present" || bad "suggestion + structured default present"
else
    ok "suggestion/default (skipped: no python3)"
fi
# valid file must carry none of the generic value codes
mout="$("$BIN" --json "$EX/modern-4x.bpp.ctl" 2>/dev/null)"
if printf '%s' "$mout" | grep -qE '"code": "BPP(005|01[6-9])"'; then
    bad "no value-check/duplicate false positives on modern-4x"
else
    ok "no value-check/duplicate false positives on modern-4x"
fi

# Regression: real control-file forms from the official BPP examples that BPP
# 4.8.7 accepts but bpp-lint used to falsely reject (differential-corpus finds).
echo "-- differential-corpus regressions --"
rtmp="$(mktemp)"
# speciestree with SPR/SNL move-tuning floats (frogs A01/A11)
printf 'seqfile=x\nImapfile=m\njobname=r\nnloci=1\nnsample=1\nthetaprior=invgamma 3 0.01\ntauprior=invgamma 3 0.02\nspeciesmodelprior=1\nspecies&tree=2 A B\n((A,B));\nspeciestree=1 0.4 0.2 0.1\n' > "$rtmp"
"$BIN" "$rtmp" 2>&1 | grep -qi 'speciestree.*expect' && bad "speciestree tuning floats accepted" || ok "speciestree tuning floats accepted"
# speciesdelimitation rjMCMC algorithm-1 form '1 1 a m' (frogs A10/A11)
printf 'seqfile=x\nImapfile=m\njobname=r\nnloci=1\nnsample=1\nthetaprior=invgamma 3 0.01\ntauprior=invgamma 3 0.02\nspeciesmodelprior=1\nspecies&tree=2 A B\n((A,B));\nspeciesdelimitation=1 1 2 1\n' > "$rtmp"
"$BIN" "$rtmp" 2>&1 | grep -qi 'speciesdelimitation.*expect' && bad "speciesdelimitation '1 1 a m' accepted" || ok "speciesdelimitation '1 1 a m' accepted"
# single-species analysis (yu2001): no tauprior needed
printf 'seqfile=x\njobname=r\nnloci=1\nnsample=1\nthetaprior=invgamma 3 0.01\nspecies&tree=1 A\n' > "$rtmp"
"$BIN" "$rtmp" 2>&1 | grep -qi "'tauprior' is required" && bad "single-species needs no tauprior" || ok "single-species needs no tauprior"
# but multi-species without tauprior MUST still be flagged (no over-fix)
printf 'seqfile=x\nImapfile=m\njobname=r\nnloci=1\nnsample=1\nthetaprior=invgamma 3 0.01\nspecies&tree=2 A B\n((A,B));\n' > "$rtmp"
"$BIN" "$rtmp" 2>&1 | grep -qi "'tauprior' is required" && ok "multi-species still requires tauprior" || bad "multi-species still requires tauprior"
rm -f "$rtmp"

# Regression: legacy syntax forms that make BPP 4.8.7 HARD-ABORT on startup must
# be reported as ERRORS (not warnings, and not silently accepted). Each form
# below was confirmed to abort the real bpp 4.8.7 binary via differential test.
echo "-- legacy-abort severity (differential vs bpp 4.8.7) --"
ltmp="$(mktemp)"
VBODY='seqfile=x
imapfile=m
jobname=r
nloci=1
nsample=1
species&tree=2 A B
2 2
((A,B));'
assert_err() { # <body> <code> <label>
    printf '%s\n' "$1" > "$ltmp"
    if [[ $HAVE_PY -eq 1 ]]; then
        "$BIN" --json "$ltmp" 2>/dev/null | python3 -c '
import json,sys
d=json.load(sys.stdin)
hit=[x for x in d["diagnostics"] if x["code"]==sys.argv[1] and x["severity"]=="error"]
sys.exit(0 if hit else 1)' "$2" && ok "$3" || bad "$3"
    else
        ok "$3 (skipped: no python3)"
    fi
}
assert_err "$VBODY
thetaprior=invgamma 3 0.002
tauprior=invgamma 3 0.04
finetune = 1: 5 0.001 0.001 0.3" BPP013 "BPP013 finetune colon-form is an error"
assert_err "$VBODY
thetaprior=invgamma 3 0.002
tauprior=invgamma 3 0.04
finetune = 1 5 0.001 0.3" BPP013 "BPP013 finetune bare-positional is an error"
assert_err "$VBODY
thetaprior=2 0.2
tauprior=invgamma 3 0.04" BPP011 "BPP011 thetaprior invgamma alpha<=2 is an error"
assert_err "$VBODY
thetaprior=invgamma 3 0.002
tauprior=1 0.03" BPP024 "BPP024 tauprior invgamma alpha<=1 is an error"
# a valid modern file must trip NONE of these three legacy-abort codes
printf '%s\n' "$VBODY
thetaprior=invgamma 3 0.002
tauprior=invgamma 3 0.04
finetune = 1 Gage:5 Gspr:0.001 tau:0.001 mix:0.3" > "$ltmp"
if [[ $HAVE_PY -eq 1 ]]; then
    "$BIN" --json "$ltmp" 2>/dev/null | python3 -c '
import json,sys
d=json.load(sys.stdin)
sys.exit(1 if any(x["code"] in ("BPP011","BPP013","BPP024") for x in d["diagnostics"]) else 0)' \
      && ok "no legacy-abort false positive on modern finetune/priors" \
      || bad "no legacy-abort false positive on modern finetune/priors"
else
    ok "modern finetune/priors (skipped: no python3)"
fi
rm -f "$ltmp"

echo
echo "== $pass passed, $fail failed =="
[[ $fail -eq 0 ]]
