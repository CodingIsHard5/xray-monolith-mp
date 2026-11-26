#!/usr/bin/env bash
# Selftest of tools/t3_kj2_check.py (kill channel joint run kj2, X side). A copy of kj1/t3_kj1_selftest.sh with the kj2
# additions marked (kj2). Desk only: no engine, no Wine, no display, nothing under /dev/shm, no git. Never deletes anything.
#   bash kj2/t3_kj2_selftest.sh
# 1. kj2/t3_kj2_mkfix.py builds the fixture run folders into a FRESH directory kj2/selftest-runs/<utc time>-<pid>/fixtures
#    (synthetic ones as kj1's, the simulation following the kill5 reader; plus (kj2) three folders that are COPIES of the
#    real kj1 run folder, one unchanged and two edited ON THE COPY) with fixtures.tsv, the expected outcome of each folder.
# 2. Runs the checker (python3 -I) on every folder, report to <dir>/reports/<name>.txt, and compares:
#    good*      PASS, exit 0, all 15 checks PASS, no NO VERDICT rule fired, a 14.<key> row for EVERY one of the 19
#               T2SUM keys and the 2 log rows (log.KCONF, log.KNOCONF), each PASS, and no other 14 row
#    mutant     FAIL, exit 1, the failing checks are EXACTLY the expected set, every other check PASS, no NO VERDICT rule
#    counter    FAIL, exit 1, only check 14 fails; exactly the named 14 row FAILS, every other row PASSES
#    nv         NO VERDICT, exit 3, the fired rules exactly the expected set (and the failing checks where given)
#    nvfail     FAIL, exit 1, rules fired as expected but a failing check lies outside their untested set
#    real (kj2) verdict, exit code, failing checks and fired rules EXACTLY as fixtures.tsv says, all 15 check lines present
#    missing / empty / unreadable / unparsed   FAIL, exit 1, the report names the file, nothing on stderr
# 3. (kj2) The OLD checker tools/t3_kj1_check.py, run unmodified on the copy real_kj1, must give NO VERDICT (exit 3) with
#    NV3 FIRED naming S 19425 snap 804->805 and M 23183 snap 963->964, while the new one (step 2) gives NV3 clear: the
#    proof that the fix changed something. The copy's required files must be byte-equal to the real folder's, and every
#    file of the real folder must have the same sha256 after the selftest as before it.
# 4. (kj2) Checker mutants: patched copies of t3_kj2_check.py (NV3 exemption removed; check 15 removed; exemption widened to
#    all falls; exemption widened to every fall after the KDEAD) are run on their listed fixtures; each must give at least
#    one outcome that differs from fixtures.tsv (caught). A mutant not caught makes the selftest FAIL.
# 5. Refuse-overwrite probe and counters-only mode, as kj1.
# Prints one line per folder and a last line `SELFTEST: PASS|FAIL (...)`.
set -u
K="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$K/../../.." && pwd)"
CHK="$ROOT/tools/t3_kj2_check.py"; OLD="$ROOT/tools/t3_kj1_check.py"
REAL="${KJ1_REAL:-/home/caden/Projects/ITRZone/out/runs/20261126-152750-t2kj1-xray}"
for f in "$CHK" "$OLD" "$K/t3_kj2_mkfix.py"; do [ -f "$f" ] || { echo "SELFTEST: FAIL (missing $f)"; exit 1; }; done
[ -d "$REAL" ] || { echo "SELFTEST: FAIL (missing the real kj1 run folder $REAL)"; exit 1; }
D="${KJ2_ST_ROOT:-$K/selftest-runs}/$(date -u +%Y%m%d-%H%M%S)-$$"
[ -e "$D" ] && { echo "SELFTEST: FAIL ($D exists)"; exit 1; }
mkdir -p "$D/reports" "$D/mutants" || { echo "SELFTEST: FAIL (cannot make $D)"; exit 1; }
exec 3>&1 4>&2 > >(tee "$D/selftest_output.txt") 2>&1; TEEP=$!
echo "selftest dir $D"
python3 -I - "$REAL" "$D/real_before.sha256" <<'PYEOF' || { echo "SELFTEST: FAIL (cannot hash the real folder)"; exit 1; }
import sys, os, hashlib
R, out = sys.argv[1:3]; L = []
for d, _, fs in sorted(os.walk(R)):
    for f in sorted(fs):
        p = os.path.join(d, f); L.append(f'{hashlib.sha256(open(p, "rb").read()).hexdigest()}  {os.path.relpath(p, R)}')
open(out, 'x').write('\n'.join(L) + '\n'); print(f'real folder: {len(L)} files hashed before the selftest')
PYEOF
python3 -I "$K/t3_kj2_mkfix.py" "$D/fixtures" "" "$REAL" || { echo "SELFTEST: FAIL (fixture maker failed)"; exit 1; }
python3 -I - "$K" "$D" "$CHK" "$OLD" "$REAL" <<'PYEOF'
import sys, os, re, subprocess, hashlib
K, D, CHK, OLD, REAL = sys.argv[1:6]
FX = os.path.join(D, 'fixtures'); REP = os.path.join(D, 'reports'); MUT = os.path.join(D, 'mutants')
h = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()[:16]
print(f'checker {h(CHK)}  old checker {h(OLD)}  mkfix {h(os.path.join(K, "t3_kj2_mkfix.py"))}  selftest {h(os.path.join(K, "t3_kj2_selftest.sh"))}')
L = open(os.path.join(FX, 'fixtures.tsv')).read().rstrip('\n').split('\n')
head = L[0].split('\t'); rows = [dict(zip(head, l.split('\t'))) for l in L[1:]]
ALL = set(range(1, 16)); bad = []; count = {}
KEYS = set('last_seq torn holds paused near guards lost epochs khits kdead_online kdead_offline kowed kstale kback krefused '
           'kclamped kver kdead_fail ktabfail'.split()) | {'log.KCONF', 'log.KNOCONF'}   # typed here on purpose (kill5 T2SUM + kj2 log rows)

def run(chk, O, out):
    p = subprocess.run([sys.executable, '-I', chk, O, f'out={out}'], capture_output=True, text=True)
    return p, p.stdout

def judge(r, p, txt, out, prefix='KJ2'):
    kind = r['kind']
    checks = {int(n): st for n, st in re.findall(r'^(\d+) .*?: (PASS|FAIL) \(', txt, re.M)}
    fails = {n for n, st in checks.items() if st == 'FAIL'}
    nvf = set(re.findall(r'^(NV\d+) .*?: FIRED \(', txt, re.M))
    rows14 = re.findall(r'^14\.(\S+) .*: (PASS|FAIL|NOT JUDGED) \(', txt, re.M)
    r14 = dict(rows14); k14 = {k for k, st in rows14 if st != 'PASS'}
    v = re.search(rf'^{prefix} X SIDE: (.*)$', txt, re.M); v = v.group(1) if v else None
    why = []
    if v != r['verdict']: why.append(f'verdict {v} != {r["verdict"]}')
    if p.returncode != int(r['rc']): why.append(f'rc {p.returncode} != {r["rc"]}')
    if not os.path.isfile(out) or open(out).read() != txt: why.append('report file missing or differs from the output')
    if p.stderr.strip(): why.append('stderr: ' + p.stderr.strip()[-200:])
    want_nv = set(x for x in r['nvs'].split(',') if x)
    if kind in ('nv', 'nvfail') and r['fails'] != '*':
        want = {int(x) for x in r['fails'].split(',') if x}
        if fails != want: why.append(f'failing {sorted(fails)} != {sorted(want)}')
    if kind in ('nvfail', 'nv', 'real') and nvf != want_nv: why.append(f'NO VERDICT fired {sorted(nvf)} != {sorted(want_nv)}')
    if kind in ('good', 'mutant', 'counter', 'real'):
        if set(checks) != ALL: why.append(f'check lines {sorted(checks)}')
        want = set() if kind == 'good' else {int(x) for x in r['fails'].split(',') if x}
        if fails != want: why.append(f'failing {sorted(fails)} != {sorted(want)}')
        if kind != 'real' and nvf: why.append(f'NO VERDICT rules fired {sorted(nvf)}')
    if kind in ('good', 'real'):
        if len(rows14) != len(r14): why.append('a 14 key appears in two rows')
        if set(r14) != KEYS: why.append(f'14 rows missing {sorted(KEYS - set(r14))} extra {sorted(set(r14) - KEYS)}')
        if kind == 'good' and k14: why.append(f'14 keys not PASS {sorted(k14)}')
    if kind == 'counter':
        key = re.match(r'14\.(\S+)', r['must']).group(1)
        if len(rows14) != len(r14): why.append('a 14 key appears in two rows')
        if set(r14) != KEYS | {key}: why.append(f'14 rows missing {sorted((KEYS | {key}) - set(r14))} extra {sorted(set(r14) - KEYS - {key})}')
        if r14.get(key) != 'FAIL': why.append(f'14.{key} is {r14.get(key)}, not FAIL')
        if k14 != {key}: why.append(f'failing 14 keys {sorted(k14)} != [{key}]')
    for m in (x for x in r['must'].split('|') if x):
        if m not in txt: why.append(f'report lacks {m!r}')
    return why, v, fails, nvf, k14

for r in rows:
    name, kind = r['name'], r['kind']; O = os.path.join(FX, name); out = os.path.join(REP, name + '.txt')
    p, txt = run(CHK, O, out)
    why, v, fails, nvf, k14 = judge(r, p, txt, out)
    ok = not why; count[kind] = count.get(kind, (0, 0)); count[kind] = (count[kind][0] + ok, count[kind][1] + 1)
    extra = f', failing 14 keys {sorted(k14)}' if k14 else ''
    print(f'{name} ({kind}): verdict {v} rc {p.returncode}, failing {sorted(fails)}, NV fired {sorted(nvf)}{extra} -> '
          f'{"OK" if ok else "BAD: " + "; ".join(why)}')
    if not ok: bad.append(name)

# ---- (kj2) the OLD checker on the copy of the real kj1 run: NO VERDICT via NV3 (the defect the exemption removes) ----------
out = os.path.join(REP, 'old_checker_real_kj1.txt')
p, txt = run(OLD, os.path.join(FX, 'real_kj1'), out)
nv3 = re.search(r'^NV3 .*?: FIRED \((.*)$', txt, re.M)
why = []
if p.returncode != 3 or not re.search(r'^KJ1 X SIDE: NO VERDICT$', txt, re.M): why.append(f'rc {p.returncode}, verdict line {re.findall(r"^KJ1 X SIDE: .*$", txt, re.M)}')
if not nv3 or 'S 19425 snap 804->805' not in nv3.group(1) or 'M 23183 snap 963->964' not in nv3.group(1): why.append('NV3 not FIRED on both death steps')
if set(re.findall(r'^(NV\d+) .*?: FIRED \(', txt, re.M)) != {'NV3'}: why.append('fired rules are not exactly NV3')
newtxt = open(os.path.join(REP, 'real_kj1.txt')).read() if os.path.isfile(os.path.join(REP, 'real_kj1.txt')) else ''
if not re.search(r'^NV3 .*?: clear \(', newtxt, re.M): why.append('the NEW checker does not show NV3 clear on the same copy')
if p.stderr.strip(): why.append('stderr: ' + p.stderr.strip()[-200:])
print(f'old_checker_real_kj1 (proof): old checker rc {p.returncode}, NV3 {"FIRED" if nv3 else "not fired"}; new checker NV3 '
      f'{"clear" if "NV3" in newtxt and re.search(r"^NV3 .*?: clear", newtxt, re.M) else "NOT clear"} -> {"OK" if not why else "BAD: " + "; ".join(why)}')
if why: bad.append('old_checker_real_kj1')
# the copy is a byte copy of the real folder (its required files)
REQ = ('t2_xray.log', 'restarts.csv', 'stall.csv', 'boot0/meta_t2.csv', 'boot0/tb/xray.log', 'boot0/watch/snaps_all.txt', 'boot0/watch/poses_all.txt')
diff = [f for f in REQ if h(os.path.join(REAL, f)) != h(os.path.join(FX, 'real_kj1', f))]
print(f'real_kj1 copy byte-equal to {REAL} in the 7 required files: {"OK" if not diff else "BAD: differ " + str(diff)}')
if diff: bad.append('real_copy')

# ---- (kj2) checker mutants ------------------------------------------------------------------------------------------------
MUTS = [
    ('mut_nv3_noexempt', "            ds = death_step(i)\n", "            ds = {'T': None, 'step': None}\n", ['good', 'real_kj1', 'real_kj1_kill5like']),
    ('mut_no15', "        return bool(snaps) and not bad and not stale, (", "        return True, (", ['m15_dead_beside', 'm15_stale', 'real_kj1']),
    ('mut_widen_all', "        return ds['step'] is not None and (p['seq'], q['seq']) == ds['step']\n", "        return True\n",
     ['nv3_fight', 'nv3_nokdead', 'nv3_after_kdead_alive', 'nv2_hurt_before_A']),
    ('mut_widen_after_kdead', "        return ds['step'] is not None and (p['seq'], q['seq']) == ds['step']\n",
     "        return ds['T'] is not None and p['tx'] >= ds['T']\n", ['nv3_after_kdead_alive']),
]
byname = {r['name']: r for r in rows}
src = open(CHK).read(); nm_caught = 0
for nm, old, new, fx in MUTS:
    if src.count(old) != 1: print(f'{nm}: patch does not apply once ({src.count(old)}) -> NOT CAUGHT'); bad.append(nm); continue
    mp = os.path.join(MUT, nm + '.py'); open(mp, 'x').write(src.replace(old, new))
    hits = []
    for f in fx:
        out = os.path.join(REP, f'{nm}__{f}.txt'); p, txt = run(mp, os.path.join(FX, f), out)
        if p.stderr.strip(): hits.append(f'{f}: stderr (not a catch)'); continue
        why = judge(byname[f], p, txt, out)[0]
        if why: hits.append(f'{f}: {why[0]}')
    caught = [x for x in hits if 'not a catch' not in x]
    print(f'{nm}: {"CAUGHT by " + " | ".join(caught) if caught else "NOT CAUGHT (" + "; ".join(hits) + ")"}')
    if caught: nm_caught += 1
    else: bad.append(nm)
count['checker-mutant'] = (nm_caught, len(MUTS))

# ---- refuse overwrite --------------------------------------------------------------------------------------------------------
out = os.path.join(REP, 'good.txt')
if not os.path.isfile(out):
    print('refuse_overwrite: good.txt missing, probe not possible -> BAD'); bad.append('refuse_overwrite')
else:
    before = open(out, 'rb').read()
    p = subprocess.run([sys.executable, '-I', CHK, os.path.join(FX, 'good'), f'out={out}'], capture_output=True, text=True)
    ok = p.returncode == 2 and 'REFUSED' in p.stdout and open(out, 'rb').read() == before
    print(f'refuse_overwrite: rc {p.returncode} -> {"OK" if ok else "BAD"}')
    if not ok: bad.append('refuse_overwrite')
# ---- counters-only mode ------------------------------------------------------------------------------------------------------
W = lambda n, f: os.path.join(FX, n, 'boot0', 'watch', f)
META = os.path.join(FX, 'good', 'boot0', 'meta_t2.csv')
for nm, ps, ss, want_rc, musts in (
        ('co_good', W('good', 'poses_all.txt'), W('good', 'snaps_all.txt'), 0,
         ['KJ2 COUNTERS ONLY: PASS', '14.khits = 5, expected 5', '14.last_seq = 3201, expected 3201 .. 3201', '14.log.KCONF lines = 2']),
        ('co_badposes', W('bad_poserecord', 'poses_all.txt'), W('good', 'snaps_all.txt'), 1,
         ['KJ2 COUNTERS ONLY: FAIL', 'UNPARSED poses file ' + W('bad_poserecord', 'poses_all.txt')]),
        ('co_badsnaps', W('good', 'poses_all.txt'), W('bad_snaprecord', 'snaps_all.txt'), 1,
         ['KJ2 COUNTERS ONLY: FAIL', 'UNPARSED snaps file ' + W('bad_snaprecord', 'snaps_all.txt')]),
        ('co_final_nor', W('good', 'poses_all.txt'), W('m14_final_nor', 'snaps_all.txt'), 1,
         ['KJ2 COUNTERS ONLY: FAIL', 'NONE: the final snapshot has no r line, so the bound FAILS'])):
    out = os.path.join(REP, nm + '.txt')
    p = subprocess.run([sys.executable, '-I', CHK, 'counters-only', META, f'poses={ps}', f'snaps={ss}', f'out={out}'],
                       capture_output=True, text=True)
    why = [f'rc {p.returncode} != {want_rc}'] if p.returncode != want_rc else []
    if p.stderr.strip(): why.append('stderr: ' + p.stderr.strip()[-200:])
    why += [f'report lacks {m!r}' for m in musts if m not in p.stdout]
    if want_rc == 0 and re.search(r'^14\.\S+ .*: (FAIL|NOT JUDGED) \(', p.stdout, re.M): why.append('a 14 row not PASS')
    print(f'{nm} (counters-only): rc {p.returncode} -> {"OK" if not why else "BAD: " + "; ".join(why)}')
    count['counters-only'] = count.get('counters-only', (0, 0)); count['counters-only'] = (count['counters-only'][0] + (not why), count['counters-only'][1] + 1)
    if why: bad.append(nm)
open(os.path.join(D, 'bad.txt'), 'x').write(' '.join(bad) + '\n')
summ = '; '.join(f'{k} {a}/{b}' for k, (a, b) in count.items())
summ = (f'{len(rows)} folders: {summ}; refuse-overwrite {"ok" if "refuse_overwrite" not in bad else "BAD"}; old checker on the real copy '
        f'{"NO VERDICT via NV3 (ok)" if "old_checker_real_kj1" not in bad else "BAD"}{"; bad: " + " ".join(bad) if bad else ""}')
open(os.path.join(D, 'summary.txt'), 'x').write(summ + '\n'); print('SUMMARY ' + summ)
sys.exit(0 if not bad else 1)
PYEOF
rc=$?
# (kj2) the real folder must be unchanged: every file's sha256 as before
python3 -I - "$REAL" "$D/real_before.sha256" "$D/real_after.sha256" <<'PYEOF'
import sys, os, hashlib
R, before, after = sys.argv[1:4]; L = []
for d, _, fs in sorted(os.walk(R)):
    for f in sorted(fs):
        p = os.path.join(d, f); L.append(f'{hashlib.sha256(open(p, "rb").read()).hexdigest()}  {os.path.relpath(p, R)}')
open(after, 'x').write('\n'.join(L) + '\n')
same = open(before).read() == open(after).read()
print(f'real folder unchanged after the selftest ({len(L)} files, sha256 each): {"OK" if same else "BAD"}'); sys.exit(0 if same else 1)
PYEOF
rrc=$?
if [ $rc -eq 0 ] && [ $rrc -eq 0 ]; then res=PASS; else res=FAIL; fi
echo "SELFTEST: $res ($(cat "$D/summary.txt" 2>/dev/null || echo 'no summary'); real folder unchanged $([ $rrc -eq 0 ] && echo ok || echo BAD); reports in $D/reports)"
exec 1>&3 2>&4
wait "$TEEP"; trc=$?
if [ "$trc" -ne 0 ] || ! tail -n 1 "$D/selftest_output.txt" 2>/dev/null | grep -q '^SELFTEST: '; then
  echo "SELFTEST: FAIL (the full output could not be written to $D/selftest_output.txt: tee exit $trc)" >&2; exit 1
fi
[ "$res" = PASS ] && exit 0 || exit 1
