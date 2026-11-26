#!/usr/bin/env bash
# Selftest of tools/t3_kw1_check.py (kill channel witness kw1). Desk only: no engine, no display.
#   tools/t3_kw1_selftest.sh
# No real run folder of pose v4 exists yet, so EVERY fixture here is STUB-MADE: the real harness/zz_p1_puppet_kill4.script
# runs in the offline fake engine (wt-render-split/perf-harness/killchannel-draft/kill4_stub.lua, mode `fixture`), and the
# real writer logic (tools/t2_kill_pose.py, its Writer class imported as a module) drives it in lockstep, 50 ms a step,
# on a virtual clock: the writer writes pose.txt, reads snap.txt, then the fake engine runs one 50 ms frame. The run
# folder gets the layout tools/t3_kw1.sh + tools/t2_xray_kw.sh produce; the files no engine can make here (xray.log,
# t2_xray.log, restarts.csv, ...) are written as stub-made stand-ins and say so. Track: a synthetic straight line.
# Folders (fresh, under out/kw1_selftest/r-<time>-<pid>/; nothing is deleted):
#   good            must give PASS
#   mut<n>[x]       copies of good with ONE change each; must FAIL check <n> and no other check
#   nv1_* nv2_*     stub runs that meet a NO VERDICT rule; must give NO VERDICT
#   miss_<file>     good without one input file; must FAIL and name the file
# Exit 0 only if all of that holds. Last line: KW1 SELFTEST: PASS|FAIL (...)
set -u
cd "$(dirname "$0")/.."; P=$PWD
for f in tools/t3_kw1_check.py tools/t2_kill_pose.py harness/zz_p1_puppet_kill4.script \
         wt-render-split/perf-harness/killchannel-draft/kill4_stub.lua /usr/bin/lua5.1; do
  [ -e "$f" ] || { echo "KW1 SELFTEST: FAIL (missing $f)"; exit 1; }
done
D="$P/out/kw1_selftest/r-$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$D" ] && { echo "KW1 SELFTEST: FAIL ($D exists)"; exit 1; }
mkdir -p "$D" || { echo "KW1 SELFTEST: FAIL (cannot make $D)"; exit 1; }
echo "selftest dir $D"
echo "checker $(sha256sum tools/t3_kw1_check.py | cut -c1-16)  writer $(sha256sum tools/t2_kill_pose.py | cut -c1-16)  script $(sha256sum harness/zz_p1_puppet_kill4.script | cut -c1-16)  stub $(sha256sum wt-render-split/perf-harness/killchannel-draft/kill4_stub.lua | cut -c1-16)"
python3 -I - "$P" "$D" <<'PYEOF'
import sys, os, re, shutil, subprocess
P, D = sys.argv[1:3]
sys.path.insert(0, os.path.join(P, 'tools'))
import t2_kill_pose as kp
LUA, STUB = '/usr/bin/lua5.1', os.path.join(P, 'wt-render-split/perf-harness/killchannel-draft/kill4_stub.lua')
SCRIPT, CHK = os.path.join(P, 'harness/zz_p1_puppet_kill4.script'), os.path.join(P, 'tools/t3_kw1_check.py')
U0, SESS, RUN_W = 1795000000000, '1795000000000', 100.0
TRACK = [[i * 0.4, i * 0.04, 0.0, 0.0] for i in range(0, 600)]   # synthetic: 0.1 m/s along x from the origin
STUB_NOTE = 'STUB-MADE (kw1 checker selftest): no engine ran; kill4_stub.lua fake engine + t2_kill_pose.Writer in lockstep'
bad = []

def wfile(p, text):
    os.makedirs(os.path.dirname(p), exist_ok=True); open(p, 'w').write(text)

def make_fixture(name, scen, events):
    """events: list of (writer time s, command) sent once to the fake engine."""
    O = os.path.join(D, name); shm = os.path.join(D, '_shm_' + name); os.makedirs(shm)
    pr = subprocess.Popen([LUA, STUB, 'fixture', SCRIPT, shm, scen], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, bufsize=1)
    def cmd(c):
        pr.stdin.write(c + '\n'); pr.stdin.flush(); r = pr.stdout.readline().strip()
        if not r.startswith('ok '): raise RuntimeError(f'{name}: fake engine said {r!r} to {c!r}')
        return int(r.split()[1])
    r = pr.stdout.readline().strip()
    if not r.startswith('ready '): raise RuntimeError(f'{name}: fake engine did not start: {r!r}')
    now = int(r.split()[1]); sp = os.path.join(shm, 'snap.txt')
    s = kp.parse_snapshot(open(sp, 'rb').read())
    if not s or s['t_track'] < 0: raise RuntimeError(f'{name}: no snapshot with track time >= 0 after the boot')
    wr = kp.Writer({'aw': 'keep', 'pick': '30'}, SESS, s['level'], TRACK)
    pw = [kp.POSE_COLS]; ws = [kp.SNAP_COLS]; u_first = U0 + now; ev = list(events)
    while True:
        u = U0 + now; w = (u - u_first) / 1000
        if w > RUN_W: break
        n, body, kl = wr.pose(w, u)
        open(os.path.join(shm, 'pose.txt'), 'wb').write(body)
        pw.append(f"{u},{n},T,{SESS},{wr.t0 + w:.2f},{w:.3f},{kp.kcol(kl)}")
        if wr.pick_row is not None: ws.append(wr.pick_row)
        try: raw = open(sp, 'rb').read()
        except OSError: raw = None
        row = wr.read(raw, w, u)
        if row is not None: ws.append(row)
        while ev and w >= ev[0][0]: cmd(ev.pop(0)[1])
        now = cmd('tick')
    cmd('finish'); pr.wait()
    wfile(f'{O}/pose_written.csv', '\n'.join(pw) + '\n'); wfile(f'{O}/writer_snaps.csv', '\n'.join(ws) + '\n')
    wfile(f'{O}/pose_written.csv.lvl', s['level'] + '\n')
    wfile(f'{O}/STUB_MADE.txt', STUB_NOTE + f'\nscenario {scen}, events {events}\n')
    wfile(f'{O}/preflight.log', f'{STUB_NOTE}\nslice end (exit 0)\n'); wfile(f'{O}/pose_writer.log', STUB_NOTE + '\n')
    wfile(f'{O}/writer_events.csv', f'utc_ms,event,detail\n{u_first},first_write,session {SESS} pick 30\n')
    wfile(f'{O}/t2_xray.out', STUB_NOTE + '\n')
    R = f'{O}/run'; B = f'{R}/boot0'
    wfile(f'{R}/t2_xray.log', f'{STUB_NOTE}\nboot 0: run_id 1 engine_s 125\nboot 0 end: tb_run rc=0 reason=engine run ended\nend: ran to the end; boots 1; unplanned restarts 0; stall no; planned kill no; rc=0\n')
    wfile(f'{R}/restarts.csv', 'utc_ms,boot,reason,planned\n'); wfile(f'{R}/lost.csv', 'utc_ms,age_ms,state,policy\n')
    wfile(f'{R}/freeze.csv', 'utc_ms,event,state,pid,game_ms\n')
    os.makedirs(B, exist_ok=True); shutil.copyfile(os.path.join(shm, 'meta_t2.csv'), f'{B}/meta_t2.csv')
    if os.path.exists(os.path.join(shm, 'hits_t2.csv')): shutil.copyfile(os.path.join(shm, 'hits_t2.csv'), f'{B}/hits_t2.csv')
    wfile(f'{B}/tb_run.log', STUB_NOTE + '\n'); wfile(f'{B}/watch.log', STUB_NOTE + '\n')
    wfile(f'{B}/tb/xray.log', f'{STUB_NOTE}\n* stub-made stand-in for the engine log\n'); wfile(f'{B}/tb/conditions.txt', STUB_NOTE + '\n')
    wfile(f'{B}/watch/written.csv', 'utc_ms,seq\n'); wfile(f'{B}/watch/torn.csv', 'utc_ms,header_seq,end_seq\n')
    wfile(f'{B}/watch/resources.csv', 'utc_ms,mem_available_kb,swap_kb,load1\n')
    print(f'STUB-MADE fixture {name} (scenario {scen}, events {events}): {len(pw) - 1} poses, {len(ws) - 1} writer rows')
    return O

def run_check(O):
    p = subprocess.run([sys.executable, '-I', CHK, O, f'out={O}.report.txt'], capture_output=True, text=True)
    fails = {int(m.group(1)) for m in re.finditer(r'^(\d+) .*: FAIL \(', p.stdout, re.M)}
    passes = {int(m.group(1)) for m in re.finditer(r'^(\d+) .*: PASS \(', p.stdout, re.M)}
    v = re.search(r'^KW1 X SIDE: (.*)$', p.stdout, re.M)
    return p.returncode, fails, passes, v.group(1) if v else None, p.stdout

# ---- the stub-made run folders ---------------------------------------------------------------------------------------
good = make_fixture('good', 'good', [])
nvs = {'nv1_nomonster': make_fixture('nv1_nomonster', 'nomonster', []),
       'nv2_fight': make_fixture('nv2_fight', 'good', [(33.0, 'hurt 201 0.55')]),    # S hurt by "the world" in its 0.250 step
       'nv2_seen': make_fixture('nv2_seen', 'good', [(33.0, 'wkill 301 202')])}      # M killed by "the world" before its death step
rc, fails, passes, v, txt = run_check(good)
ok = rc == 0 and v == 'PASS' and not fails and passes == set(range(1, 13))
print(f'good: {v} rc {rc} -> {"OK" if ok else "BAD"}')
if not ok: bad.append('good'); print(txt)

# ---- mutants: one change each ----------------------------------------------------------------------------------------
def rows_of(O):
    L = open(f'{O}/writer_snaps.csv').read().split('\n'); h = L[0].split(',')
    return h, [l.split(',') for l in L[1:] if l]
def put_rows(O, h, R): open(f'{O}/writer_snaps.csv', 'w').write('\n'.join([','.join(h)] + [','.join(r) for r in R]) + '\n')
def meta(O): return open(f'{O}/run/boot0/meta_t2.csv').read().split('\n')
def put_meta(O, L): open(f'{O}/run/boot0/meta_t2.csv', 'w').write('\n'.join(L))
def poses(O): return [l.split(',') for l in open(f'{O}/pose_written.csv').read().split('\n')[1:] if l]
def picks(O):
    h, R = rows_of(O); ev = ' '.join(r[h.index('event')] for r in R)
    return {m.group(1): (m.group(2), float(m.group(3))) for m in re.finditer(r'PICK_([SM])=(\d+):[^:]+:([\d.]+)', ev)}
def snaprows(h, R): return [r for r in R if 'PICK' not in r[h.index('event')]]

def m1(O): open(f'{O}/run/boot0/tb/xray.log', 'a').write('FATAL ERROR (stub-made mutant)\n')
def m2(O):   # first KAPPLY of S: health after off by 0.001
    s = picks(O)['S'][0]; L = meta(O); i = next(i for i, l in enumerate(L) if l.startswith('KAPPLY,') and l.split(',')[3] == s)
    f = L[i].split(','); f[7] = f'{float(f[7]) + 0.001:.4f}'; L[i] = ','.join(f); put_meta(O, L)
def m2x(O):  # a third part-hit KAPPLY for S, early (pose at 10 s), outside every repeat window
    s, h = picks(O)['S']; pw = poses(O); n = next(p[1] for p in pw if float(p[5]) >= 10.0); L = meta(O)
    i = next(i for i, l in enumerate(L) if l.startswith('P2,' + n + ','))
    L.insert(i + 1, f'KAPPLY,0,{n},{s},0.100,0.100,{h:.4f},{h - 0.1:.4f},hit'); put_meta(O, L)
def m3(O):   # a further KAPPLY for S while its 1.000 total is repeated
    s, h = picks(O)['S']; pw = poses(O)
    n = [p[1] for p in pw if f'k:{s}:' in p[6] and ':1.000:1' in p[6]][3]; L = meta(O)
    i = max(i for i, l in enumerate(L) if l.startswith('KAPPLY,') and l.split(',')[3] == s)
    L.insert(i + 1, f'KAPPLY,0,{n},{s},0.000,1.000,0.0000,0.0000,hit'); put_meta(O, L)
def m4(O):   # S: the first snapshot after the 0.250 hit shows a drop of only 0.1
    h, R = rows_of(O); c, k = h.index('s_health'), h.index('s_k'); e = h.index('event')
    i = next(i for i, r in enumerate(R) if r[k] == '0.250' and 'PICK' not in r[e])
    j = max(j for j in range(i) if 'PICK' not in R[j][e]); R[i][c] = f'{float(R[j][c]) - 0.1:.4f}'; put_rows(O, h, R)
def m5(O):   # S: the 0.250 confirmation read 2.5 s late
    s = picks(O)['S'][0]; u0 = next(int(p[0]) for p in poses(O) if f'k:{s}:' in p[6] and ':0.250:0' in p[6])
    h, R = rows_of(O); k, u = h.index('s_k'), h.index('utc_ms')
    for r in R:
        if r[k] == '0.250' and int(r[u]) < u0 + 2500: r[k] = ''
    put_rows(O, h, R)
def m6(O):   # a second KDEAD online for M
    m = picks(O)['M'][0]; L = meta(O); i = next(i for i, l in enumerate(L) if re.match(rf'KDEAD,\d+,\d+,{m},online$', l))
    L.insert(i + 1, L[i]); put_meta(O, L)
def m7(O):   # M alive again 5 s after its dead line was read
    h, R = rows_of(O); k, a, u = h.index('m_k'), h.index('m_alive'), h.index('utc_ms')
    t = next(int(r[u]) for r in R if r[k].endswith(';dead')); i = next(i for i, r in enumerate(R) if int(r[u]) >= t + 5000)
    R[i][a] = '1'; put_rows(O, h, R)
def m8(O):   # S: the dead line still there 13 s after the last pose that carried the line
    s = picks(O)['S'][0]; lp = max(int(p[0]) for p in poses(O) if f'k:{s}:' in p[6])
    h, R = rows_of(O); k, u = h.index('s_k'), h.index('utc_ms'); dv = [r[k] for r in R if r[k].endswith(';dead')][-1]
    for r in R:
        if lp < int(r[u]) <= lp + 13000: r[k] = dv
    put_rows(O, h, R)
def m9(O):   # KWHO for S names killer 5
    s = picks(O)['S'][0]; L = meta(O); i = next(i for i, l in enumerate(L) if re.match(rf'KWHO,\d+,{s},', l))
    L[i] = re.sub(r',[-\d]+$', ',5', L[i]); put_meta(O, L)
def m10(O):  # actor health 0.99 in one snapshot
    h, R = rows_of(O); a = h.index('actor_health'); sr = snaprows(h, R); sr[len(sr) // 2][a] = '0.9900'; put_rows(O, h, R)
def m11(O):  # the r line goes back by 5 in one snapshot
    h, R = rows_of(O); q = h.index('r_seq'); sr = [r for r in snaprows(h, R) if r[q]]; r = sr[len(sr) // 2]
    r[q] = str(int(r[q]) - 5); put_rows(O, h, R)
def m12(O):  # a counter off: kback 1 in the last T2SUM
    L = meta(O); i = max(i for i, l in enumerate(L) if l.startswith('T2SUM,')); L[i] = L[i].replace(',kback=0,', ',kback=1,'); put_meta(O, L)
MUT = [('mut1', 1, m1), ('mut2', 2, m2), ('mut2x', 2, m2x), ('mut3', 3, m3), ('mut4', 4, m4), ('mut5', 5, m5),
       ('mut6', 6, m6), ('mut7', 7, m7), ('mut8', 8, m8), ('mut9', 9, m9), ('mut10', 10, m10), ('mut11', 11, m11), ('mut12', 12, m12)]
nmut = 0
for name, n, f in MUT:
    O = os.path.join(D, name); shutil.copytree(good, O); f(O)
    open(f'{O}/STUB_MADE.txt', 'a').write(f'MUTANT {name}: made from good by {f.__name__} (must fail check {n} only)\n')
    rc, fails, passes, v, txt = run_check(O)
    ok = rc == 1 and v == 'FAIL' and fails == {n}
    print(f'{name} (stub-made, from good): fails {sorted(fails)}, verdict {v} rc {rc} -> {"OK" if ok else "BAD (must fail exactly check %d)" % n}')
    if ok: nmut += 1
    else: bad.append(name); print(txt)
for name, O in nvs.items():
    rc, fails, passes, v, txt = run_check(O)
    ok = rc == 3 and v == 'NO VERDICT'
    fired = re.findall(r'^(NV\d) .*: FIRED', txt, re.M)
    print(f'{name} (stub-made): verdict {v} rc {rc}, fired {fired}, failing checks {sorted(fails)} -> {"OK" if ok else "BAD"}')
    if not ok: bad.append(name); print(txt)
nmiss = 0
for rel in ('pose_written.csv', 'writer_snaps.csv', 'run/t2_xray.log', 'run/restarts.csv', 'run/boot0/meta_t2.csv', 'run/boot0/tb/xray.log'):
    name = 'miss_' + rel.replace('/', '_'); O = os.path.join(D, name)
    shutil.copytree(good, O, ignore=lambda d, fs, rel=rel: [x for x in fs if os.path.join(d, x) == os.path.join(good, rel)])
    rc, fails, passes, v, txt = run_check(O)
    ok = not os.path.exists(os.path.join(O, rel)) and rc == 1 and v == 'FAIL' and f'MISSING {rel} ' in txt and f'missing {rel}' in txt
    print(f'{name} (stub-made, from good): verdict {v} rc {rc}, named {"MISSING " + rel in txt} -> {"OK" if ok else "BAD"}')
    if ok: nmiss += 1
    else: bad.append(name); print(txt)
print(f'KW1 SELFTEST: {"PASS" if not bad else "FAIL"} (all fixtures STUB-MADE; good PASS {"good" not in bad}; mutants failing exactly their check {nmut}/{len(MUT)}; NO VERDICT {sum(1 for n in nvs if n not in bad)}/{len(nvs)}; missing-file folders named {nmiss}/6{"; bad: " + " ".join(bad) if bad else ""})')
sys.exit(0 if not bad else 1)
PYEOF
