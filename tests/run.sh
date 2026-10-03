#!/usr/bin/env bash
# bpp-lint test harness. Invoked by `make test`; also runnable directly.
# Focus: lock in the --json contract (design §2) plus exit-code behavior, so
# the machine-readable output the bpp-agent loop depends on cannot regress.
set -u

# Resolve repo root from this script's location so paths work regardless of cwd.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BPP_LINT_BIN:-$ROOT/bpp-lint}"   # `make debug` points this at the sanitizer build
EX="$ROOT/examples"
FX="$ROOT/tests/fixtures/data"

# The examples/ control files name data files (frogs.txt ...) that are not in
# the repo, so the data-consistency pass (15x) would add BPP150 errors to every
# one of them. Those tests are about syntax: run them with --no-data-checks.
# The 15x pass has its own fixtures and tests below.
NODATA=--no-data-checks

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
    out="$("$BIN" --json $NODATA "$f" 2>/dev/null)"
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
out="$("$BIN" --json $NODATA "$EX/modern-4x.bpp.ctl" 2>/dev/null)"; rc=$?
check "exit code" 0 "$rc"
check "status"    "valid" "$(jget "$out" status)"
check "n_species" "4" "$(jget "$out" n_species)"
check "errors=0"  "0" "$(count_field "$out" errors)"

# 3. Known-bad file: invalid status, exit 1, has errors + a fixable BPP020.
echo "-- invalid control file (legacy-3x) --"
out="$("$BIN" --json $NODATA "$EX/legacy-3x.bpp.ctl" 2>/dev/null)"; rc=$?
check "exit code" 1 "$rc"
check "status"    "invalid" "$(jget "$out" status)"
[[ "$(count_field "$out" errors)" -gt 0 ]] && ok "errors > 0" || bad "errors > 0"
printf '%s' "$out" | grep -q '"code": "BPP020"' && ok "carries BPP020" || bad "carries BPP020"
printf '%s' "$out" | grep -q '"fixable": true'  && ok "has a fixable item" || bad "has a fixable item"

# 4. analysis_type is derived correctly (cross-checks is A10).
echo "-- analysis_type derivation --"
out="$("$BIN" --json $NODATA "$EX/cross-checks.bpp.ctl" 2>/dev/null)"
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

# wrong-mode detection (BPP006): a --simulate control file linted without -s
# must get ONE clear diagnostic, not a cascade of bogus BPP100/101/103s for
# inference-only requirements the file never intended to satisfy.
echo "-- wrong-mode (--simulate file linted without -s) check --"
mtmp="$(mktemp)"
printf 'seed=-1\nseqfile=x\ntreefile=t\nspecies&tree=2 A B\n   1 1\n   (A,B);\nloci&length=5 500\n' > "$mtmp"
mout="$("$BIN" --json "$mtmp" 2>/dev/null)"
printf '%s' "$mout" | grep -q '"code": "BPP006"' && ok "BPP006 fires on --simulate file without -s" || bad "BPP006 wrong-mode detection"
printf '%s' "$mout" | grep -q '"code": "BPP100"' && bad "BPP100 cascade suppressed when BPP006 fires" || ok "BPP100 cascade suppressed when BPP006 fires"
# same file WITH -s must be clean of BPP006 (no false positive in the right mode)
sout="$("$BIN" --json -s "$mtmp" 2>/dev/null)"
printf '%s' "$sout" | grep -q '"code": "BPP006"' && bad "no BPP006 false positive with --simulate" || ok "no BPP006 false positive with --simulate"
rm -f "$mtmp"

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
mout="$("$BIN" --json $NODATA "$EX/modern-4x.bpp.ctl" 2>/dev/null)"
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

# --- --template control-file scaffolding (0.3.3) ---------------------------
echo "-- --template scaffolding --"
ttmp="$(mktemp)"

# bare template carries the analysis-type switches and marks required fields
tpl="$("$BIN" --template A00 2>/dev/null)"
printf '%s' "$tpl" | grep -q "speciesdelimitation = 0" && ok "template A00 sets speciesdelimitation=0" || bad "template A00 speciesdelimitation"
printf '%s' "$tpl" | grep -q "speciestree = 0" && ok "template A00 sets speciestree=0" || bad "template A00 speciestree"
printf '%s' "$tpl" | grep -qE "REQUIRED.*seqfile|seqfile.*REQUIRED" && ok "template marks seqfile REQUIRED" || bad "template seqfile REQUIRED"

# A10 flips speciesdelimitation on
"$BIN" --template A10 2>/dev/null | grep -q "speciesdelimitation = 1" && ok "template A10 sets speciesdelimitation=1" || bad "template A10 speciesdelimitation"

# bad type is rejected
"$BIN" --template ZZZ >/dev/null 2>&1 && bad "template rejects bad type" || ok "template rejects bad type"

# drip loop: with seqfile+imapfile the still-missing required fields are flagged
"$BIN" --template A00 --seqfile s.txt --imapfile m.txt --out "$ttmp" >/dev/null 2>&1
if [[ $HAVE_PY -eq 1 ]]; then
    j="$("$BIN" --json $NODATA "$ttmp" 2>/dev/null)"
    st="$(jget "$j" status)"
    check "template+seqfile still invalid (drip)" "invalid" "$st"
    printf '%s' "$j" | python3 -c 'import json,sys; d=json.load(sys.stdin); sys.exit(0 if any(x["code"]=="BPP100" for x in d["diagnostics"]) else 1)' \
        && ok "drip reports BPP100 for remaining required fields" \
        || bad "drip reports BPP100 for remaining required fields"
else
    ok "drip loop (skipped: no python3)"
fi

# full fill -> valid (species-tree WITH the "species&tree = " label, as bpp-tree
# emits it -- the prefix must not be doubled)
"$BIN" --template A00 --seqfile s.txt --imapfile m.txt \
    --species-tree "$(printf 'species&tree = 2  A B\n                       2 2\n                       (A,B);')" \
    --nloci 5 --thetaprior 'invgamma 3 0.002' --tauprior 'invgamma 3 0.04' \
    --out "$ttmp" >/dev/null 2>&1
check "species&tree prefix not doubled" "0" "$(grep -c 'species&tree = species&tree' "$ttmp" | tr -d ' ')"
if [[ $HAVE_PY -eq 1 ]]; then
    check "fully-specified template lints valid" "valid" "$(jget "$("$BIN" --json $NODATA "$ttmp" 2>/dev/null)" status)"
else
    ok "fully-specified template (skipped: no python3)"
fi
rm -f "$ttmp"

# --species-tree-file: block read from a file (bpp-tree's .stree) reaches valid
stf="$(mktemp)"; ttmp2="$(mktemp)"
printf 'species&tree = 2  A B\n   2 2\n   (A,B);\n' > "$stf"
"$BIN" --template A00 --seqfile s.txt --imapfile m.txt --species-tree-file "$stf" \
    --nloci 5 --thetaprior 'invgamma 3 0.002' --tauprior 'invgamma 3 0.04' \
    --out "$ttmp2" >/dev/null 2>&1
if [[ $HAVE_PY -eq 1 ]]; then
    check "--species-tree-file yields a valid control file" "valid" "$(jget "$("$BIN" --json $NODATA "$ttmp2" 2>/dev/null)" status)"
    check "block from file, prefix not doubled" "0" "$(grep -c 'species&tree = species&tree' "$ttmp2" | tr -d ' ')"
else
    ok "--species-tree-file (skipped: no python3)"
fi
rm -f "$stf" "$ttmp2"

# --- combined --template + --suggest-priors: one-shot valid control file (0.3.5) ---
echo "-- --template + --suggest-priors (one-shot valid) --"
sdir="$(mktemp -d)"
# 4 species x 2 individuals, within- and between-species variation -> theta/tau derivable
printf '8 32\n^Chi1  ACGTACGTATGTACGTACGTACGTACGTACGT\n^Chi2  ACGTACGTACGTACGTACGTACGTACGTACGT\n^Jap1  ACGTACGAACGTACGTACGTACGTAGGAACGT\n^Jap2  ACGTACGTACGTACGTACGTACGTAGGAAGGT\n^Kor1  ACGCACGTACGCGCGTACGCACGTACGTACGT\n^Kor2  ACGCACGTACGCACGTACGCACGTACGTACGT\n^Tai1  TCGTACGCACGTCTGTACGTACGTCCGTAGGT\n^Tai2  TCGTACGTACGTCTGTACGTACGTCCGTAGGT\n' > "$sdir/seqs.txt"
printf 'Chi1 Chinese\nChi2 Chinese\nJap1 Japanese\nJap2 Japanese\nKor1 Korean\nKor2 Korean\nTai1 Taiwanese\nTai2 Taiwanese\n' > "$sdir/map.imap"
printf 'species&tree = 4  Chinese Japanese Korean Taiwanese\n   2 2 2 2\n   (((Chinese,Japanese),Korean),Taiwanese);\n' > "$sdir/tree.stree"
cout="$sdir/a.ctl"
"$BIN" --template A00 --seqfile "$sdir/seqs.txt" --imapfile "$sdir/map.imap" \
    --species-tree-file "$sdir/tree.stree" --nloci 1 --jobname t --suggest-priors \
    --out "$cout" >/dev/null 2>&1
check "combined template+suggest-priors exits 0" "0" "$?"
grep -q 'thetaprior = invgamma' "$cout" && ok "combined fills thetaprior from data" || bad "combined fills thetaprior from data"
grep -q 'tauprior = invgamma'   "$cout" && ok "combined fills tauprior from data"   || bad "combined fills tauprior from data"
if [[ $HAVE_PY -eq 1 ]]; then
    check "combined template+suggest-priors -> valid in ONE call" "valid" "$(jget "$("$BIN" --json "$cout" 2>/dev/null)" status)"
else
    ok "combined valid (skipped: no python3)"
fi

# requires --seqfile + --imapfile
"$BIN" --template A00 --suggest-priors --out "$sdir/x.ctl" >/dev/null 2>&1
check "combined without --seqfile/--imapfile errors" "2" "$?"

# invariant data -> theta not derivable -> placeholder + nonzero exit (still honest)
printf '4 8\n^A1 ACGTACGT\n^A2 ACGTACGT\n^B1 ACGTACGT\n^B2 ACGTACGT\n' > "$sdir/inv.txt"
printf 'A1 A\nA2 A\nB1 B\nB2 B\n' > "$sdir/inv.imap"
printf 'species&tree = 2  A B\n   2 2\n   (A,B);\n' > "$sdir/inv.stree"
"$BIN" --template A00 --seqfile "$sdir/inv.txt" --imapfile "$sdir/inv.imap" \
    --species-tree-file "$sdir/inv.stree" --nloci 1 --suggest-priors --out "$sdir/inv.ctl" >/dev/null 2>&1
[[ "$?" -ne 0 ]] && ok "invariant data -> nonzero exit (theta underivable)" || bad "invariant data nonzero exit"
grep -q 'thetaprior = ???' "$sdir/inv.ctl" && ok "invariant data leaves thetaprior placeholder" || bad "invariant data thetaprior placeholder"
rm -rf "$sdir"

# --- speciesdelimitation arity (0.4.0): BPP's parse_speciesdelimitation needs
# '0', '1 0 e' or '1 1 a m' exactly; '1', '1 0' and '1 1 2' are "Erroneous
# format" aborts that 0.3.5 let through. Fixtures in tests/fixtures/data/.
echo "-- speciesdelimitation arity (fixtures) --"
# fx_case <file> <expected rc> <expected status> <code that must be present> [severity]
fx_case() {
    local f="$1" want_rc="$2" want_status="$3" code="$4" sev="${5:-}"
    local out rc
    out="$(cd "$FX" && "$BIN" --json "$f" 2>/dev/null)"; rc=$?
    check "$f exit code" "$want_rc" "$rc"
    check "$f status" "$want_status" "$(jget "$out" status)"
    if [[ -n "$code" ]]; then
        if [[ $HAVE_PY -eq 1 && -n "$sev" ]]; then
            printf '%s' "$out" | python3 -c '
import json,sys
d=json.load(sys.stdin)
sys.exit(0 if any(x["code"]==sys.argv[1] and x["severity"]==sys.argv[2] for x in d["diagnostics"]) else 1)' "$code" "$sev" \
                && ok "$f carries $code ($sev)" || bad "$f carries $code ($sev)"
        else
            printf '%s' "$out" | grep -q "\"code\": \"$code\"" && ok "$f carries $code" || bad "$f carries $code"
        fi
    fi
}
fx_absent() { # <file> <code that must NOT appear>
    local out
    out="$(cd "$FX" && "$BIN" --json "$1" 2>/dev/null)"
    printf '%s' "$out" | grep -q "\"code\": \"$2\"" && bad "$1 has no $2" || ok "$1 has no $2"
}
fx_case ok.ctl             0 valid   ""
fx_case sd_bare.ctl        1 invalid BPP017 error
fx_case sd_short.ctl       1 invalid BPP017 error
fx_case sd_alg1_short.ctl  1 invalid BPP017 error
fx_case sd_ok.ctl          0 valid   ""
# bare '1' is auto-fixable to '1 0 2'; '1 0' is not (a user parameter is present)
sdout="$(cd "$FX" && "$BIN" --json sd_bare.ctl 2>/dev/null)"
printf '%s' "$sdout" | grep -q '"suggested_fix": "speciesdelimitation = 1 0 2"' \
    && ok "sd_bare fix is 'speciesdelimitation = 1 0 2'" || bad "sd_bare fix is 'speciesdelimitation = 1 0 2'"
sdout="$(cd "$FX" && "$BIN" --json sd_short.ctl 2>/dev/null)"
printf '%s' "$sdout" | grep -q '"fixable": true' && bad "sd_short is not auto-fixable" || ok "sd_short is not auto-fixable"
# a bad rjMCMC algorithm number is BPP019 (no form matches)
atmp="$(mktemp)"
sed 's/^speciesdelimitation = 0/speciesdelimitation = 1 2 0.5\nspeciesmodelprior = 1/' "$FX/ok.ctl" > "$atmp"
"$BIN" --json $NODATA "$atmp" 2>/dev/null | grep -q '"code": "BPP019"' && ok "algorithm 2 -> BPP019" || bad "algorithm 2 -> BPP019"
# other alternation-shaped keywords (clock / heredity / locusrate) follow cfile.c
printf 'clock = 2 1 1\n' >> "$atmp";    "$BIN" --json $NODATA "$atmp" 2>/dev/null | grep -q "'clock = 2 1 1' expects" && ok "clock '2 1 1' needs 4 values" || bad "clock '2 1 1' needs 4 values"
sed -i 's/^clock = 2 1 1$/clock = 4 1/' "$atmp"; "$BIN" --json $NODATA "$atmp" 2>/dev/null | grep -q "'clock" && bad "clock '4 1' accepted" || ok "clock '4 1' accepted"
rm -f "$atmp"
# exit code must agree with --json status for value errors (was 0 in 0.3.x)
vtmp="$(mktemp)"
printf 'seqfile = x.txt\njobname = r\nnloci = 2.5\nnsample = 1\nthetaprior = invgamma 3 0.01\nspecies&tree = 1 A\n' > "$vtmp"
"$BIN" $NODATA "$vtmp" >/dev/null 2>&1; check "value error (BPP016) exits 1" 1 "$?"
rm -f "$vtmp"

# --- data-consistency pass (15x, 0.4.0): control file vs seqfile / Imapfile.
# Every case reproduced against BPP 4.8.7 (see BPP-LINT-FIXES.md).
echo "-- data-consistency checks (fixtures) --"
fx_case nofile.ctl            1 invalid BPP150 error     # Unable to open file
fx_case nloci.ctl             1 invalid BPP152 error     # Expected 3 loci but found only 2
fx_case imap_missing_tag.ctl  1 invalid BPP153 error     # Cannot find a mapping to species for tag a1
fx_case imap_extra_sp.ctl     1 invalid BPP154 error     # Cannot find node with population label D
fx_case tree_sp_noimap.ctl    1 invalid BPP154 error     # Cannot find node with population label C
fx_case phase_ones.ctl        1 invalid BPP155 error     # Number of digits in 'phase' ...
fx_case phase_zeros.ctl       0 valid   BPP155 warning   # all-zero phase is discarded by BPP
fx_case counts.ctl            0 valid   BPP156 note      # counts ignored by inference
fx_case nocaret.ctl     1 invalid BPP157 error     # Cannot find species tag on sequence
fx_case tree_sp_nodata.ctl 0 valid BPP154 warning  # tree species without Imap individuals: BPP runs
fx_absent ok.ctl BPP15
# case 13: a relative path that resolves from the control file's directory but
# not from the cwd -> warning only (lint exit 0, --check-priors exit 0)
relout="$(cd "$FX" && "$BIN" --json sub/rel.ctl 2>/dev/null)"; relrc=$?
check "sub/rel.ctl exit code" 0 "$relrc"
check "sub/rel.ctl status" "valid" "$(jget "$relout" status)"
printf '%s' "$relout" | grep -q '"code": "BPP151"' && ok "sub/rel.ctl warns BPP151" || bad "sub/rel.ctl warns BPP151"
(cd "$FX" && "$BIN" --check-priors sub/rel.ctl >/dev/null 2>&1); check "sub/rel.ctl --check-priors exit code" 0 "$?"
# --no-data-checks silences the pass (and data: null)
nd="$(cd "$FX" && "$BIN" --json --no-data-checks nofile.ctl 2>/dev/null)"; ndrc=$?
check "--no-data-checks: nofile.ctl exit code" 0 "$ndrc"
printf '%s' "$nd" | grep -q '"data": null' && ok "--no-data-checks: data is null" || bad "--no-data-checks: data is null"
# --simulate never opens the (output) data files
printf 'seed=-1\nseqfile=nowhere.txt\nImapfile=nowhere.imap\ntreefile=t\nspecies&tree=2 A B\n   1 1\n   (A,B);\nloci&length=5 500\n' > "$FX/.sim.ctl"
(cd "$FX" && "$BIN" --json -s .sim.ctl 2>/dev/null) | grep -q '"code": "BPP150"' && bad "--simulate skips data checks" || ok "--simulate skips data checks"
rm -f "$FX/.sim.ctl"
# JSON data object (the MCP server reads these)
if [[ $HAVE_PY -eq 1 ]]; then
    (cd "$FX" && "$BIN" --json ok.ctl 2>/dev/null) | python3 -c '
import json,sys,os
d=json.load(sys.stdin)["data"]
ok = (d["n_loci"]==2 and d["n_sequences"]==6 and d["species"]==["A","B","C"]
      and os.path.isabs(d["seqfile"]) and d["seqfile"].endswith("/tiny.txt")
      and os.path.isabs(d["imapfile"]) and d["imapfile"].endswith("/tiny.imap"))
sys.exit(0 if ok else 1)' && ok "json data object (paths, n_loci, n_sequences, species)" || bad "json data object"
else
    ok "json data object (skipped: no python3)"
fi

# --- --template A10/A11 write a complete speciesdelimitation (0.4.0) ---
echo "-- --template speciesdelimitation --"
"$BIN" --template A10 2>/dev/null | grep -q "speciesdelimitation = 1 0 2" && ok "template A10 writes '1 0 2'" || bad "template A10 writes '1 0 2'"
"$BIN" --template A11 2>/dev/null | grep -q "speciesdelimitation = 1 0 2" && ok "template A11 writes '1 0 2'" || bad "template A11 writes '1 0 2'"
"$BIN" --template A11 --speciesdelimitation "1 1 2 1" 2>/dev/null | grep -q "speciesdelimitation = 1 1 2 1" && ok "--speciesdelimitation override" || bad "--speciesdelimitation override"
# full A10 + A11 files over the fixture data must lint valid WITH the data pass
printf 'species&tree = 3  A B C\n   2 2 2\n   ((A,B),C);\n' > "$FX/.tree.stree"
for T in A10 A11; do
    (cd "$FX" && "$BIN" --template "$T" --seqfile tiny.txt --imapfile tiny.imap --species-tree-file .tree.stree \
        --nloci 2 --jobname "tpl_$T" --burnin 10 --sampfreq 1 --nsample 20 --suggest-priors --out "tpl_$T.ctl" >/dev/null 2>&1)
    check "template $T + --suggest-priors exits 0" 0 "$?"
    check "template $T lints valid (data checks on)" "valid" "$(jget "$(cd "$FX" && "$BIN" --json "tpl_$T.ctl" 2>/dev/null)" status)"
done
rm -f "$FX/.tree.stree"

# --- oracle: if bpp is on PATH, every fixture lint calls valid must get past
# BPP's parser and data loading. Each file is run from its own directory (the
# control-file-directory resolution bpp-lint uses), in a scratch copy so BPP's
# output files never land in the repo. Skipped quietly without bpp.
if command -v bpp >/dev/null 2>&1; then
    echo "-- oracle: bpp accepts every lint-valid fixture --"
    odir="$(mktemp -d)"; cp -r "$FX"/. "$odir"/
    for f in "$odir"/*.ctl "$odir"/sub/*.ctl; do
        n="${f#$odir/}"
        st="$(jget "$(cd "$(dirname "$f")" && "$BIN" --json "$(basename "$f")" 2>/dev/null)" status)"
        [[ "$st" == "valid" ]] || continue
        bout="$(cd "$(dirname "$f")" && timeout 120 bpp --cfile "$(basename "$f")" 2>&1)"; brc=$?
        if [[ $brc -eq 0 ]] && ! printf '%s' "$bout" | grep -qE 'Erroneous format|Unable to open|Cannot find|Expected [0-9]+ loci|Number of digits'; then
            ok "bpp runs $n"
        else
            bad "bpp rejects lint-valid $n: $(printf '%s' "$bout" | grep -E 'Erroneous format|Unable to open|Cannot find|Expected [0-9]+ loci|Number of digits|rror' | head -1)"
        fi
    done
    # and every fixture lint calls INVALID must indeed be rejected by bpp
    for f in "$odir"/*.ctl; do
        n="${f#$odir/}"
        st="$(jget "$(cd "$odir" && "$BIN" --json "$(basename "$f")" 2>/dev/null)" status)"
        [[ "$st" == "invalid" ]] || continue
        (cd "$odir" && timeout 120 bpp --cfile "$(basename "$f")" >/dev/null 2>&1) && bad "bpp accepts lint-invalid $n" || ok "bpp rejects $n"
    done
    rm -rf "$odir"
fi
rm -f "$FX"/tpl_A1[01].ctl

echo
echo "== $pass passed, $fail failed =="
[[ $fail -eq 0 ]]
