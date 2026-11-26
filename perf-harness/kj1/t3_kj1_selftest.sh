#!/usr/bin/env bash
# Selftest of kj1/t3_kj1_check.py (kill channel joint run kj1, X side). Desk only: no engine, no Wine, no display,
# nothing under /dev/shm, no git. Never deletes anything (no rm anywhere).
#   bash kj1/t3_kj1_selftest.sh
# 1. kj1/t3_kj1_mkfix.py builds the fixture run folders into a FRESH directory kj1/selftest-runs/<utc time>-<pid>/fixtures
#    (meta/T2SUM/logs derived from the REAL kw1 folder out/kw1/run; kept snapshot and pose files synthetic; each folder's
#    README.txt says which is which) with fixtures.tsv, the expected outcome of each folder.
# 2. Runs the checker (python3 -I) on every folder, report to <dir>/reports/<name>.txt, and compares:
#    good*      PASS, exit 0, all 14 checks PASS, no NO VERDICT rule fired, a 14.<key> row for EVERY one of the 19
#               T2SUM keys (a missing row is a mismatch), each PASS, and no other 14 row
#    mutant     FAIL, exit 1, the failing checks are EXACTLY the expected set (one check, or the stated multi set), every
#               other check PASS, no NO VERDICT rule fired
#    counter    FAIL, exit 1, only check 14 fails; the 14 rows are the 19 keys (plus the named key if it is an unknown
#               one), the named key's row FAILS and every other row PASSES
#    nv         NO VERDICT, exit 3, the fired NO VERDICT rules are exactly the expected set (and the failing checks
#               exactly the expected set where fixtures.tsv gives one instead of '*')
#    nvfail     FAIL, exit 1, NO VERDICT rules fired as expected, but a failing check lies outside their untested set:
#               failing checks and fired rules exactly as expected, and the report says so
#    missing / empty / unreadable / unparsed   FAIL, exit 1, the report names the file (MISSING/EMPTY/UNREADABLE/UNPARSED
#               line and the check lines), nothing on stderr (no traceback)
#    plus: the checker refuses to overwrite an existing report (exit 2, file unchanged; if good's report is missing the
#    probe is recorded as failed); counters-only mode on good's meta file with good's poses=/snaps= (PASS, khits and
#    last_seq judged PASS), and with a supplied file that does not parse or whose final snapshot has no r line (FAIL,
#    the file named).
# If selftest_output.txt cannot be written completely (tee fails), the selftest exits 1 and says so on stderr.
# A deliberately bad folder that PASSES, or any other mismatch, makes the selftest FAIL (exit 1).
# Prints one line per folder and a last line `SELFTEST: PASS|FAIL (...)`.
set -u
K="$(cd "$(dirname "$0")" && pwd)"
for f in "$K/t3_kj1_check.py" "$K/t3_kj1_mkfix.py"; do [ -f "$f" ] || { echo "SELFTEST: FAIL (missing $f)"; exit 1; }; done
D="${KJ1_ST_ROOT:-$K/selftest-runs}/$(date -u +%Y%m%d-%H%M%S)-$$"   # KJ1_ST_ROOT: another parent folder (default kj1/selftest-runs)
[ -e "$D" ] && { echo "SELFTEST: FAIL ($D exists)"; exit 1; }
mkdir -p "$D/reports" || { echo "SELFTEST: FAIL (cannot make $D)"; exit 1; }
exec 3>&1 4>&2 > >(tee "$D/selftest_output.txt") 2>&1; TEEP=$!   # the full output is also kept beside the run
echo "selftest dir $D"
python3 -I "$K/t3_kj1_mkfix.py" "$D/fixtures" || { echo "SELFTEST: FAIL (fixture maker failed)"; exit 1; }
python3 -I - "$K" "$D" <<'PYEOF'
import sys, os, re, subprocess, hashlib
K, D = sys.argv[1:3]
CHK = os.path.join(K, 't3_kj1_check.py'); FX = os.path.join(D, 'fixtures'); REP = os.path.join(D, 'reports')
h = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()[:16]
print(f'checker {h(CHK)}  mkfix {h(os.path.join(K, "t3_kj1_mkfix.py"))}  selftest {h(os.path.join(K, "t3_kj1_selftest.sh"))}')
L = open(os.path.join(FX, 'fixtures.tsv')).read().rstrip('\n').split('\n')
head = L[0].split('\t'); rows = [dict(zip(head, l.split('\t'))) for l in L[1:]]
ALL = set(range(1, 15)); bad = []; count = {}
KEYS = set('last_seq torn holds paused near guards lost epochs khits kdead_online kdead_offline kowed kstale kback krefused '
           'kclamped kver kdead_fail ktabfail'.split())   # the T2SUM keys (kill4 line 703 and the P1 keys), typed here on purpose
for r in rows:
    name, kind = r['name'], r['kind']; O = os.path.join(FX, name); out = os.path.join(REP, name + '.txt')
    p = subprocess.run([sys.executable, '-I', CHK, O, f'out={out}'], capture_output=True, text=True)
    txt = p.stdout
    checks = {int(n): st for n, st in re.findall(r'^(\d+) .*?: (PASS|FAIL) \(', txt, re.M)}
    fails = {n for n, st in checks.items() if st == 'FAIL'}
    nvf = set(re.findall(r'^(NV\d+) .*?: FIRED \(', txt, re.M))
    rows14 = re.findall(r'^14\.(\S+) .*: (PASS|FAIL|NOT JUDGED) \(', txt, re.M)
    r14 = dict(rows14); k14 = {k for k, st in rows14 if st != 'PASS'}
    v = re.search(r'^KJ1 X SIDE: (.*)$', txt, re.M); v = v.group(1) if v else None
    why = []
    if v != r['verdict']: why.append(f'verdict {v} != {r["verdict"]}')
    if p.returncode != int(r['rc']): why.append(f'rc {p.returncode} != {r["rc"]}')
    if not os.path.isfile(out) or open(out).read() != txt: why.append('report file missing or differs from the output')
    if p.stderr.strip(): why.append('stderr: ' + p.stderr.strip()[-200:])
    want_nv = set(x for x in r['nvs'].split(',') if x)
    if kind in ('nv', 'nvfail') and r['fails'] != '*':
        want = {int(x) for x in r['fails'].split(',') if x}
        if fails != want: why.append(f'failing {sorted(fails)} != {sorted(want)}')
    if kind == 'nvfail' and nvf != want_nv: why.append(f'NO VERDICT fired {sorted(nvf)} != {sorted(want_nv)}')
    if kind in ('good', 'mutant', 'counter'):
        if set(checks) != ALL: why.append(f'check lines {sorted(checks)}')
        want = set() if kind == 'good' else {int(x) for x in r['fails'].split(',')}
        if fails != want: why.append(f'failing {sorted(fails)} != {sorted(want)}')
        if nvf: why.append(f'NO VERDICT rules fired {sorted(nvf)}')
    if kind == 'good':   # the FULL key set, each PASS; a missing row is a mismatch
        if len(rows14) != len(r14): why.append('a 14 key appears in two rows')
        if set(r14) != KEYS: why.append(f'14 rows missing {sorted(KEYS - set(r14))} extra {sorted(set(r14) - KEYS)}')
        if k14: why.append(f'14 keys not PASS {sorted(k14)}')
    if kind == 'counter':   # exactly the named key FAILS, every other key PASSES, no key missing
        key = re.match(r'14\.(\S+)', r['must']).group(1)
        if len(rows14) != len(r14): why.append('a 14 key appears in two rows')
        if set(r14) != KEYS | {key}: why.append(f'14 rows missing {sorted((KEYS | {key}) - set(r14))} extra {sorted(set(r14) - KEYS - {key})}')
        if r14.get(key) != 'FAIL': why.append(f'14.{key} is {r14.get(key)}, not FAIL')
        if k14 != {key}: why.append(f'failing 14 keys {sorted(k14)} != [{key}]')
    if kind == 'nv' and nvf != want_nv: why.append(f'NO VERDICT fired {sorted(nvf)} != {sorted(want_nv)}')
    for m in (x for x in r['must'].split('|') if x):
        if m not in txt: why.append(f'report lacks {m!r}')
    ok = not why; count[kind] = count.get(kind, (0, 0)); count[kind] = (count[kind][0] + ok, count[kind][1] + 1)
    extra = f', failing 14 keys {sorted(k14)}' if k14 else ''
    print(f'{name} ({kind}): verdict {v} rc {p.returncode}, failing {sorted(fails)}, NV fired {sorted(nvf)}{extra} -> '
          f'{"OK" if ok else "BAD: " + "; ".join(why)}')
    if not ok: bad.append(name)
# the checker must refuse to overwrite a report (good's report must exist for the probe to mean anything)
out = os.path.join(REP, 'good.txt')
if not os.path.isfile(out):
    print('refuse_overwrite: good.txt missing, probe not possible -> BAD'); bad.append('refuse_overwrite')
else:
    before = open(out, 'rb').read()
    p = subprocess.run([sys.executable, '-I', CHK, os.path.join(FX, 'good'), f'out={out}'], capture_output=True, text=True)
    ok = p.returncode == 2 and 'REFUSED' in p.stdout and open(out, 'rb').read() == before
    print(f'refuse_overwrite: rc {p.returncode} -> {"OK" if ok else "BAD"}')
    if not ok: bad.append('refuse_overwrite')
# counters-only mode with supplied poses=/snaps= files
W = lambda n, f: os.path.join(FX, n, 'boot0', 'watch', f)
META = os.path.join(FX, 'good', 'boot0', 'meta_t2.csv')
for nm, ps, ss, want_rc, musts in (
        ('co_good', W('good', 'poses_all.txt'), W('good', 'snaps_all.txt'), 0,
         ['KJ1 COUNTERS ONLY: PASS', '14.khits = 5, expected 5', '14.last_seq = 3201, expected 3201 .. 3201']),
        ('co_badposes', W('bad_poserecord', 'poses_all.txt'), W('good', 'snaps_all.txt'), 1,
         ['KJ1 COUNTERS ONLY: FAIL', 'UNPARSED poses file ' + W('bad_poserecord', 'poses_all.txt')]),
        ('co_badsnaps', W('good', 'poses_all.txt'), W('bad_snaprecord', 'snaps_all.txt'), 1,
         ['KJ1 COUNTERS ONLY: FAIL', 'UNPARSED snaps file ' + W('bad_snaprecord', 'snaps_all.txt')]),
        ('co_final_nor', W('good', 'poses_all.txt'), W('m14_final_nor', 'snaps_all.txt'), 1,
         ['KJ1 COUNTERS ONLY: FAIL', 'NONE: the final snapshot has no r line, so the bound FAILS'])):
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
summ = '; '.join(f'{k} {a}/{b}' for k, (a, b) in count.items())
print(f'SELFTEST: {"PASS" if not bad else "FAIL"} ({len(rows)} folders: {summ}; refuse-overwrite {"ok" if "refuse_overwrite" not in bad else "BAD"}'
      f'{"; bad: " + " ".join(bad) if bad else ""}; reports in {REP})')
sys.exit(0 if not bad else 1)
PYEOF
rc=$?; exec 1>&3 2>&4
wait "$TEEP"; trc=$?   # tee's own status: non-zero if it could not open or write selftest_output.txt
if [ "$trc" -ne 0 ] || ! tail -n 1 "$D/selftest_output.txt" 2>/dev/null | grep -q '^SELFTEST: '; then
  echo "SELFTEST: FAIL (the full output could not be written to $D/selftest_output.txt: tee exit $trc)" >&2; exit 1
fi
exit $rc
