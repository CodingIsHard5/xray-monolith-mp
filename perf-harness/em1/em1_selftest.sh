#!/usr/bin/env bash
# Selftest of em1/em1_check.py (em1 X side, DRAFT). Desk only: no engine, no Wine, no display, no box claim, no network.
#   grep -q EXCLUSIVE ~/.claude/overseer/QUIET-REQUIRED && echo HOLD     (must not print HOLD)
#   nice -n 19 bash em1/em1_selftest.sh
# Fixtures:
#  - ONE STUB-MADE run folder `good`: the REAL reader harness/zz_p1_puppet_kill7.script (read only, sha256 pinned below) in
#    the Lua 5.1 fake engine em1/em1_stub.lua (= kj4/kill7h_stub.lua + one fixture command `lag <ms>`), in lockstep 50 ms a
#    step with this file's stand-ins for itrzone's rig (Rig below: M = the nearest monster with health >= 0.9 at w 30, total
#    0.300; at w 50 total = 0.300 + M's snapshot health - 0.005, dead 1: the kcjp short), for the runner (emtest.txt empty
#    before the boot, surge / psi appended at play 70 / 310 on a 1 s poll, emforce.csv) and for the watcher. The fake kill
#    takes effect 250 ms after the call (lag 250): M stays alive just above 0 for a few snapshots (the pending path).
#    The actor STANDS in the stub (one-row track); the moving actor is exercised by MADE edits of the actor line (m9_*).
#    Files no engine can make here (xray.log, t2_xray.log, restarts.csv) are stand-ins (STUB_MADE.txt says so).
#  - MADE fixtures: one file of `good` changed (the others symlinks); each must give exactly its listed failing checks /
#    fired rules / verdict. Every check id has at least one failing fixture; every NO VERDICT rule a firing one.
#  - CHECKER MUTANTS: for every check id, a copy of the checker whose check always passes; for every NO VERDICT rule, a
#    copy whose rule never fires; plus three bound mutants (V_DY back to 0.05, no gap rule, no 10-of-12 rule). Each is run
#    on the fixture(s) that fail that check / fire that rule and must give a DIFFERENT outcome (verdict or failing set).
#  - REAL bytes, read in place (never written): out/kj4p-r1/run (kj4p, X-only, real engine: must give NO VERDICT, nvk1
#    only, every emission check PASS incl. 9 (it FAILED 9 under kj4p's +/-0.05), the kill checks failing exactly as
#    "no k line at all"); the kj2 X run (ITRZone, read only): in place -> FAIL naming emforce.csv; through a folder of
#    symlinks + a header-only emforce.csv -> FAIL with the exact per-check outcome listed below (which kill checks real
#    kj2 bytes PASS and which they cannot).
#  - missing / empty required files -> FAIL by name; CRLF copy of good -> PASS; unparsable rows -> FAIL; the checker
#    writes nothing; then em1/em1_axr_step_test.sh (the runner's new step).
# Output dir: em1/selftest-runs/em1-st-<time>-<pid>/ (nothing deleted; ~25 MB). Last line: EM1 SELFTEST: PASS|FAIL (...)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; P="$(cd "$HERE/../../.." && pwd)"
SCRIPT="$P/harness/zz_p1_puppet_kill7.script"; SCRIPT_SHA=0d5e59279775498e54155bc7a1ef9ecc7d8fa4f9132f7a77827fb30e3d6d9b54
for f in "$HERE/em1_check.py" "$HERE/em1_stub.lua" "$HERE/../kj4/kj4p_track_start.csv" "$P/tools/t2_kill_pose.py" "$SCRIPT" /usr/bin/lua5.1; do
  [ -e "$f" ] || { echo "EM1 SELFTEST: FAIL (missing $f)"; exit 1; }
done
[ "$(sha256sum "$SCRIPT" | cut -d' ' -f1)" = "$SCRIPT_SHA" ] || { echo "EM1 SELFTEST: FAIL (reader is not 0d5e5927)"; exit 1; }
D="$HERE/selftest-runs/em1-st-$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$D" ] && { echo "EM1 SELFTEST: FAIL ($D exists)"; exit 1; }
mkdir -p "$D" || { echo "EM1 SELFTEST: FAIL (cannot make $D)"; exit 1; }
echo "selftest dir $D  start $(date '+%F %T')"
echo "checker $(sha256sum "$HERE/em1_check.py" | cut -c1-16)  reader $(cut -c1-16 <<<"$SCRIPT_SHA")  stub $(sha256sum "$HERE/em1_stub.lua" | cut -c1-16)"
python3 -I - "$P" "$HERE" "$D" <<'PYEOF'
import sys, os, re, subprocess
P, HERE, D = sys.argv[1:4]
sys.path.insert(0, os.path.join(P, 'tools'))
import t2_kill_pose as kp
LUA, STUB = '/usr/bin/lua5.1', os.path.join(HERE, 'em1_stub.lua')
SCRIPT, CHK = os.path.join(P, 'harness/zz_p1_puppet_kill7.script'), os.path.join(HERE, 'em1_check.py')
TRACK = kp.load_track(os.path.join(HERE, '..', 'kj4', 'kj4p_track_start.csv'))
KJ2 = '/home/caden/Projects/ITRZone/out/runs/20261126-172548-t2kj2-xray'
KJ4P = os.path.join(P, 'out/kj4p-r1/run')
U0, SESS = 1795000000000, '1795000000000'
SURGE_AT, PSI_AT, RUN_W = 70, 310, 432.0          # play end ~431 >= 425 (RUN_S 445 - 15 - 5)
HIT_W, KILL_W, KCJP, LAG_MS, HOLD_S = 30.0, 50.0, 0.005, 250, 5.0
NOTE = 'STUB-MADE (em1 checker selftest): no engine ran; em1_stub.lua fake engine + the em1 rig stand-in in lockstep'
REQ = ['t2_xray.log', 'restarts.csv', 'emforce.csv', 'boot0/meta_t2.csv', 'boot0/hits_t2.csv', 'boot0/tb/xray.log',
       'boot0/watch/snaps_all.txt', 'boot0/watch/poses_all.txt']
bad = []; caught = []; ncm = 0

def wfile(p, text, mode='w'):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    if os.path.islink(p): raise RuntimeError(f'refusing to write through a symlink: {p}')
    with open(p, mode) as f: f.write(text)

class Rig:
    """itrzone's rig with kcj=30 kcjo=1 kcjd=20 kcjp=0.005, as a stand-in (NOT itrzone's code): w = its own clock."""
    def __init__(s, lvl):
        s.lvl, s.n, s.M, s.lethal, s.dead_w, s.dropped, s.newest = lvl, 0, None, None, None, False, None
    def lines(s, w):
        if w < HIT_W or s.newest is None: return []
        if s.M is None:
            c = [(o[4], i, o[0]) for i, o in s.newest['o'].items() if o[1] == 'm' and o[3] == 1 and o[2] >= 0.9]
            s.M = (min(c)[1], min(c)[2]) if c else '-'
        if s.M == '-' or s.dropped: return []
        i, sec = s.M
        if w < KILL_W: return [f'k,{i},{sec},0.300,0']
        if s.lethal is None: s.lethal = f'{0.3 + s.newest["o"][i][2] - KCJP:.3f}'
        return [f'k,{i},{sec},{s.lethal},1']
    def pose(s, w, u):
        if s.dead_w is not None and w - s.dead_w >= HOLD_S: s.dropped = True
        s.n += 1; x, y, z = TRACK[0][1:4]
        body = (f'seq,{s.n},{u},play,{s.lvl},keep,{SESS},4\np,{x:.3f},{y + 0.3:.3f},{z:.3f},0.0000,1.0,0\n'
                + ''.join(l + '\n' for l in s.lines(w)) + f'end,{s.n}\n')
        return body
    def read(s, raw, w):
        q = kp.parse_snapshot(raw)
        if q: s.newest = q
        if q and s.M not in (None, '-') and s.dead_w is None and q['k'].get(s.M[0], '').endswith(';dead'): s.dead_w = w
        return q

def make_stub(name, force):
    O = os.path.join(D, name); R = os.path.join(O, 'run'); B = os.path.join(R, 'boot0'); shm = os.path.join(O, '_shm')
    os.makedirs(shm); os.makedirs(os.path.join(B, 'watch')); os.makedirs(os.path.join(B, 'tb'))
    open(os.path.join(shm, 'emtest.txt'), 'w').close()
    pr = subprocess.Popen([LUA, STUB, 'fixture', SCRIPT, shm, 'good'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, bufsize=1)
    def cmd(c):
        pr.stdin.write(c + '\n'); pr.stdin.flush(); r = pr.stdout.readline().strip()
        if not r.startswith('ok '): raise RuntimeError(f'{name}: fake engine said {r!r} to {c!r}')
        return int(r.split()[1])
    r = pr.stdout.readline().strip()
    if not r.startswith('ready '): raise RuntimeError(f'{name}: fake engine did not start: {r!r}')
    now = int(r.split()[1]); cmd(f'lag {LAG_MS}'); sp = os.path.join(shm, 'snap.txt')
    s = kp.parse_snapshot(open(sp, 'rb').read())
    if not s or s['t_track'] < 0: raise RuntimeError(f'{name}: no snapshot with track time >= 0 after the boot')
    rig = Rig(s['level']); rig.newest = s
    snaps = open(os.path.join(B, 'watch/snaps_all.txt'), 'w'); poses = open(os.path.join(B, 'watch/poses_all.txt'), 'w')
    ef = ['utc_ms,wall_utc,kind,ms_after_play0']; u_first = U0 + now; play0 = None; play0_line = ''; todo = list(force)
    last_seq = None
    while True:
        u = U0 + now; w = (u - u_first) / 1000
        if w > RUN_W: break
        body = rig.pose(w, u)
        open(os.path.join(shm, 'pose.txt'), 'w').write(body); poses.write(f'#pose,{u},{rig.n}\n{body}')
        if play0 is None and w >= 1.0:
            play0_line = body.split('\n')[0]; play0 = int(play0_line.split(',')[2])
        if play0 is not None and int(w * 1000) % 1000 < 50:
            el = u - play0
            while todo and el >= todo[0][0] * 1000:
                k = todo.pop(0)[1]; open(os.path.join(shm, 'emtest.txt'), 'a').write(k + '\n')
                ef.append(f'{u},2026-11-27T00:00:00.000Z,{k},{el}')
        now = cmd('tick')
        raw = open(sp, 'rb').read(); q = rig.read(raw, w)
        if q and q['seq'] != last_seq:
            last_seq = q['seq']; snaps.write(f'#snap,{U0 + now},{q["seq"]}\n' + raw.decode())
    cmd('finish'); pr.wait(); snaps.close(); poses.close()
    os.rename(os.path.join(shm, 'meta_t2.csv'), os.path.join(B, 'meta_t2.csv'))
    os.rename(os.path.join(shm, 'hits_t2.csv'), os.path.join(B, 'hits_t2.csv'))
    wfile(os.path.join(R, 'emforce.csv'), '\n'.join(ef) + '\n')
    wfile(os.path.join(O, 'STUB_MADE.txt'), f'{NOTE}\nforced {force}; rig M {rig.M} lethal total {rig.lethal}; kill lag {LAG_MS} ms\n')
    wfile(os.path.join(R, 't2_xray.log'), f'2026-11-27 00:00:00.000Z {NOTE}\n2026-11-27 00:00:00.000Z boot 0: run_id 1 engine_s 445\n'
          f'2026-11-27 00:00:01.000Z first play pose read: {play0_line} (stop planned 0 s after its utc_ms; 0 = none)\n'
          '2026-11-27 00:08:00.000Z boot 0 end: tb_run rc=0 reason=engine run ended\n'
          '2026-11-27 00:08:00.000Z end: ran to the end; boots 1; unplanned restarts 0; stall no; planned kill no; rc=0\n')
    wfile(os.path.join(R, 'restarts.csv'), 'utc_ms,boot,reason,planned\n')
    wfile(os.path.join(B, 'tb/xray.log'), f'{NOTE}\n* loading script surge_manager.script\n* loading script psi_storm_manager.script\n'
          '* loading script zz_p1_puppet.script\n')
    print(f'STUB-MADE {name}: forced {force}, play0 {play0}, rig M {rig.M} lethal {rig.lethal}', flush=True)
    return R

def run_check(R, chk=CHK):
    p = subprocess.run([sys.executable, '-I', chk, R], capture_output=True, text=True)
    fails = {m.group(1) for m in re.finditer(r'^(\w+) .*: FAIL \(', p.stdout, re.M)}
    fired = {m.group(1) for m in re.finditer(r'^(nv[ek]\d) .*: FIRED', p.stdout, re.M)}
    v = re.search(r'^EM1 X SIDE: (.*)$', p.stdout, re.M)
    return p.returncode, fails, fired, (v.group(1) if v else None), p.stdout

def expect(name, R, code, verdict, fails=None, fired=None, also=None):
    rc, f, nv, v, out = run_check(R)
    wfile(os.path.join(D, 'reports', name + '.txt'), out)
    ok = rc == code and v == verdict and (fails is None or f == set(fails)) and (fired is None or nv == set(fired))
    if also: ok = ok and all(re.search(a, out, re.M) for a in also)
    print(f'{"ok  " if ok else "BAD "} {name}: rc {rc} verdict {v} failing {sorted(f)} fired {sorted(nv)}'
          + ('' if ok else f'   (want rc {code} {verdict} failing {sorted(fails or [])} fired {sorted(fired or [])} {also or ""})'), flush=True)
    if not ok: bad.append(name)
    return (rc, f, nv, v)

# ---- checker mutants (made once): check X always passes / rule X never fires / three bound mutants
CT = open(CHK).read(); MDIR = os.path.join(D, 'checker_mutants'); os.makedirs(MDIR)
A1, A2 = "        res[cid] = (text, bool(ok), nums)\n", "        nv.append((k, t, f, d))\n"
assert CT.count(A1) == 1 and CT.count(A2) == 1
def cmut(name, a, b):
    t = CT.replace(a, b, 1)
    if t == CT: raise RuntimeError(f'checker mutant {name}: anchor missing')
    p = os.path.join(MDIR, f'm_{name}.py'); open(p, 'w').write(t); return p
CMUT = {}
for cid in re.findall(r"^ORDER_ID = \[(.*?)\]", CT, re.M | re.S)[0].replace('\n', ' ').replace("'", '').split(','):
    cid = cid.strip()
    if cid: CMUT['chk_' + cid] = cmut('chk_' + cid, A1, f"        res[cid] = (text, bool(ok) or cid == {cid!r}, nums)\n")
for k in ('nve1', 'nve2', 'nve3', 'nve4', 'nve5', 'nvk1', 'nvk2', 'nvk3', 'nvk6', 'nvk7'):
    CMUT['nv_' + k] = cmut('nv_' + k, A2, f"        nv.append((k, t, f and k != {k!r}, d))\n")
CMUT['b_vdy'] = cmut('b_vdy', 'V_DY = 0.50 ', 'V_DY = 0.05 ')
CMUT['b_nogap'] = cmut('b_nogap', 'V_GAP_MS = 250 ', 'V_GAP_MS = 10 ** 9 ')
CMUT['b_nofrac'] = cmut('b_nofrac', 'V_JUDGED = (10, 12)', 'V_JUDGED = (0, 12)')
def mut_caught(mname, R, base):
    """the mutant must give a different outcome than the real checker on R (base = (rc, fails, fired, verdict))"""
    global ncm
    rc, f, nv, v, _ = run_check(R, CMUT[mname]); ok = (rc, f, nv, v) != base; ncm += 1
    if ok: caught.append(mname)
    else: bad.append('mutant ' + mname); print(f'BAD  checker mutant {mname} NOT caught on {os.path.relpath(R, D)}: {v} {sorted(f)} {sorted(nv)}', flush=True)

# ---------------------------------------------------------------------------------------------------- the stub run
GOOD = make_stub('good', [(SURGE_AT, 'surge'), (PSI_AT, 'psi')])
listing = lambda R: sorted((os.path.relpath(os.path.join(a, f), R), os.lstat(os.path.join(a, f)).st_size, os.lstat(os.path.join(a, f)).st_mtime_ns)
                           for a, _, fs in os.walk(R) for f in fs)
before = listing(GOOD)
expect('good', GOOD, 0, 'PASS', [], [], [r'^kp .*: PASS \(.*snapshots from the kill call .* listing M: [1-9]'])
if listing(GOOD) != before: print('BAD  checker changed the run folder'); bad.append('writes')
else: print('ok   the checker wrote nothing into the run folder')
GOOD_OUT = open(os.path.join(D, 'reports', 'good.txt')).read()
rd = lambda R, n: open(os.path.join(R, n)).read()
G = {n: rd(GOOD, n) for n in REQ}
S, M_, PO = 'boot0/watch/snaps_all.txt', 'boot0/meta_t2.csv', 'boot0/watch/poses_all.txt'
MID = re.search(r'^reported: kill channel: M (\d+);', GOOD_OUT, re.M).group(1)

WORK = {}
def work(fname, tag=''):
    key = (fname, tag)
    if key in WORK: return WORK[key]
    R = os.path.join(D, 'mut_' + re.sub(r'\W', '_', fname) + tag, 'run')
    for n in REQ:
        os.makedirs(os.path.dirname(os.path.join(R, n)), exist_ok=True)
        if n != fname: os.symlink(os.path.join(GOOD, n), os.path.join(R, n))
    WORK[key] = R; return R
def snap_recs(t):
    idx = [m.start() for m in re.finditer(r'^#snap,', t, re.M)] + [len(t)]
    return list(zip(idx, idx[1:]))
def edit_snap(t, pred, fn, count=1):
    out, last, done = [], 0, 0
    for a, b in snap_recs(t):
        rec = t[a:b]
        if (count is None or done < count) and pred(rec): out.append(t[last:a]); out.append(fn(rec)); last = b; done += 1
    if not done: raise RuntimeError('mutant matched nothing')
    out.append(t[last:]); return ''.join(out)
def et(rec):
    m = re.search(r'^e,(\w+),([-0-9.]+),', rec, re.M); return (m.group(1), float(m.group(2))) if m else (None, None)
def near(kind, t0): return lambda rec: et(rec)[0] == kind and abs(et(rec)[1] - t0) < 0.051
def one(t, a, b):
    if t.count(a) < 1: raise RuntimeError(f'mutant anchor missing {a[:50]!r}')
    return t.replace(a, b, 1)
def resub(t, pat, fn, n=1):
    t2, k = re.subn(pat, fn, t, count=n, flags=re.M)
    if not k: raise RuntimeError(f'mutant pattern matched nothing {pat!r}')
    return t2
tx_of = lambda rec: int(re.search(r'^seq,\d+,(\d+),', rec, re.M).group(1))
u_of = lambda rec: int(re.search(r'^#snap,(\d+),', rec, re.M).group(1))
def mo(rec):   # M's o line fields after `o,<id>,` (sec kind x y z yaw health alive ...) or None
    m = re.search(rf'^o,{MID},([^\n]*)$', rec, re.M); return m.group(1).split(',') if m else None
def set_mo(rec, h=None, alive=None, x=None):
    def f(m):
        g = m.group(0).split(',')
        if h is not None: g[8] = h
        if alive is not None: g[9] = alive
        if x is not None: g[4] = x
        return ','.join(g)
    return resub(rec, rf'^o,{MID},[^\n]*$', f)
def first_v_recs(t):   # vortex n -> index (in snap_recs) of its first snapshot
    out = {}
    for j, (a, b) in enumerate(snap_recs(t)):
        for m in re.finditer(r'^v,(\d+),', t[a:b], re.M): out.setdefault(int(m.group(1)), j)
    return out
def vline(rec, n): return [float(x) for x in re.search(rf'^v,{n},([^\n]*)$', rec, re.M).group(1).split(',')]
def set_actor(rec, x=None, y=None, z=None):
    def f(m):
        g = m.group(0).split(',')
        if x is not None: g[1] = f'{x:.3f}'
        if y is not None: g[2] = f'{y:.3f}'
        if z is not None: g[3] = f'{z:.3f}'
        return ','.join(g)
    return resub(rec, r'^a,[^\n]*$', f)
def on_rec(t, j, fn):
    a, b = snap_recs(t)[j]; return t[:a] + fn(t[a:b]) + t[b:]
def fold(t, js, fn):
    for j in js: t = on_rec(t, j, fn)
    return t
def drop_recs(t, js):
    R_ = snap_recs(t); keep = [t[a:b] for j, (a, b) in enumerate(R_) if j not in js]
    return t[:R_[0][0]] + ''.join(keep)

FV = first_v_recs(G[S]); VN = sorted(FV); first_v = VN[0]; NV_ = len(VN)
KAPS = [l for l in G[M_].split('\n') if l.startswith(f'KAPPLY,') and l.split(',')[3] == MID]
KD = [l for l in G[M_].split('\n') if l.startswith('KDEAD,') and l.split(',')[3] == MID][0]
TA, TK = int(KAPS[0].split(',')[1]), int(KD.split(',')[1])
SR = snap_recs(G[S])
recs_txt = [G[S][a:b] for a, b in SR]
J_after_hit = next(j for j, r in enumerate(recs_txt) if tx_of(r) >= TA)
J_pend = [j for j, r in enumerate(recs_txt) if tx_of(r) >= TK and mo(r) and mo(r)[7] == '1']
J_mid = next(j for j, r in enumerate(recs_txt) if tx_of(r) >= (TA + TK) // 2 and mo(r))
J_dead1 = next(j for j, r in enumerate(recs_txt) if re.search(rf'^k,{MID},[^,]+,dead$', r, re.M))
U_dead1 = u_of(recs_txt[J_dead1])
J_late = max(j for j, r in enumerate(recs_txt) if mo(r) and u_of(r) >= U_dead1 + 2500 and not re.search(rf'^k,{MID},', r, re.M))
NEI = next(i for i, sec in re.findall(r'^o,(\d+),([^,]+),m,', recs_txt[J_after_hit], re.M) if i != MID)
print(f'ok   good: M {MID}, {NV_} vortices, pending snapshots {len(J_pend)}, neighbour {NEI}', flush=True)
if not J_pend: bad.append('no pending snapshot in good')
D_SURGE = int(re.search(r"surge: runner appended .* EFORCE 'started' at X \d+, (-?\d+) ms after", GOOD_OUT).group(1))
p0 = int(re.search(r'first play pose read: seq,\d+,(\d+),', G['t2_xray.log']).group(1))
def shift_play0(t, d): return t.replace(f',{p0},play,', f',{p0 + d},play,', 1)
def ndrop(n_):   # how many vortices to make unjudged so that fewer than 10 of 12 are judged
    return n_ // 6 + 1
gapv = [n for n in VN[1:] if FV[n] >= 3][:ndrop(NV_)]
def gap_before(t, ns_, dy=None):
    js = set()
    for n in ns_: js |= {FV[n] - 1, FV[n] - 2}
    if dy is not None:
        for n in ns_:
            vx, vy, vz, _ = vline(recs_txt[FV[n]], n); t = on_rec(t, FV[n], lambda r: set_actor(r, y=vy - 20 - dy))
    return drop_recs(t, js)
def m9_actor(n, dy=None, r=None):
    j = FV[n]; vx, vy, vz, _ = vline(recs_txt[j], n)
    if dy is not None: return lambda t: on_rec(t, j, lambda rec: set_actor(rec, y=vy - 20 - dy))
    return lambda t: on_rec(t, j, lambda rec: set_actor(rec, x=vx - r, z=vz))

MUT = [   # name, file, transform, expected failing checks  (all must FAIL with exactly these, no rule fired)
    ('m1_rc', 't2_xray.log', lambda t: resub(t, r'rc=0$', 'rc=6', n=0), ['1']),
    ('m1_lua', 'boot0/tb/xray.log', lambda t: t + '! [LUA] ...gameengine/run\\gamedata\\scripts\\zz_p1_puppet.script:812: attempt to index a nil value\n', ['1']),
    ('m1_end', 't2_xray.log', lambda t: shift_play0(t, 10000), ['1', 'kd']),
    ('m2_extra', M_, lambda t: resub(t, r'^(SUMMARY,)', r'EFORCE,19000,unexpected,unknown\n\1'), ['2']),
    ('m2_early', 'emforce.csv', lambda t: resub(t, r'^(\d+)(,[^,]+,surge,\d+)$', lambda m: f'{int(m.group(1)) + D_SURGE + 600}{m.group(2)}'), ['2']),
    ('m2_late', 'emforce.csv', lambda t: resub(t, r'^(\d+,[^,]+,surge,)\d+$', r'\g<1>73000'), ['2']),
    ('m2_luaev', 'boot0/tb/xray.log', lambda t: t + '! [LUA] ...gameengine/run\\gamedata\\scripts\\surge_manager.script:1065: attempt to perform arithmetic on a nil value\n', ['2']),
    ('m3s', M_, lambda t: resub(t, r'^ESTART,(\d+),surge,', lambda m: f'ESTART,{int(m.group(1)) + 1500},surge,'), ['3s']),
    ('m3p', M_, lambda t: resub(t, r'^(ESTART,\d+,psi,)[-0-9.]+$', r'\g<1>1.50'), ['3p']),
    ('m4s', S, lambda t: edit_snap(t, near('surge', 100.0), lambda r: resub(r, r'^(e,surge,[^,]+,\d+,)10$', r'\g<1>6')), ['4s']),
    ('m4p', S, lambda t: edit_snap(t, near('psi', 50.0), lambda r: resub(r, r'^(e,psi,[^,]+,\d+,)10$', r'\g<1>6')), ['4p']),
    ('m4n', S, lambda t: one(t, '\ne,none,0.00,0,6\n', '\ne,none,0.00,0,10\n'), ['4n']),
    ('m5s', S, lambda t: edit_snap(t, near('surge', 60.0), lambda r: resub(r, r'^e,surge,([-0-9.]+),', lambda m: f'e,surge,{float(m.group(1)) + 0.8:.2f},')), ['5s']),
    ('m5p', S, lambda t: edit_snap(t, near('psi', 20.0), lambda r: resub(r, r'^e,psi,([-0-9.]+),', lambda m: f'e,psi,{float(m.group(1)) + 0.8:.2f},')), ['5p']),
    ('m6s', M_, lambda t: resub(t, r'^(EEND,\d+,surge,)[-0-9.]+$', r'\g<1>225.00'), ['6s']),
    ('m6p', M_, lambda t: resub(t, r'^(EEND,\d+,psi,)[-0-9.]+$', r'\g<1>99.00'), ['6p']),
    ('m7s', M_, lambda t: resub(t, r'^EPHASE,\d+,surge,5,[^\n]*\n', ''), ['7s']),
    ('m7p', M_, lambda t: resub(t, r'^(EPHASE,\d+,psi,3,[^\n]*\n)', r'\1\1'), ['7p']),
    ('m8', S, lambda t: resub(t, r'^w,1,([-0-9.]+)$', lambda m: f'w,1,{float(m.group(1)) - 20:.3f}', n=0), ['8']),
    ('m9_dy', S, m9_actor(first_v, dy=0.55), ['9']),
    ('m9_r', S, m9_actor(first_v, r=152.0), ['9']),
    ('m9_frac', S, lambda t: gap_before(t, gapv), ['9']),
    ('m10', S, lambda t: edit_snap(t, near('surge', 150.0), lambda r: resub(r, r'^(a,[^,]+,[^,]+,[^,]+,[^,]+,)1\.0000$', r'\g<1>0.9500')), ['10']),
    ('m10b', M_, lambda t: resub(t, r'^(VITALS,min_health=[^,]+,max_radiation=)0\.0000', r'\g<1>0.1000'), ['10']),
    ('m11', M_, lambda t: resub(t, r'^(T2SUM,.*,eerr=)0$', r'\g<1>1'), ['11', 'k14']),
    ('m11b', M_, lambda t: resub(t, r'^(SUMMARY,)', r'EERR,99999,time factor unreadable\n\1'), ['11']),
    ('m11c', S, lambda t: edit_snap(t, lambda r: et(r)[0] == 'none' and '\nns,-1,-1\n' not in r and tx_of(r) > TK + 20000, lambda r: resub(r, r'^ns,\d+,\d+$', 'ns,-1,-1')), ['11']),
    ('m12', M_, lambda t: resub(t, r'^(SUMMARY,)', r'ENOTREADY,15001\n\1'), ['12']),
    ('m13', S, lambda t: edit_snap(t, near('surge', 30.5), lambda r: resub(r, r'^(e,[^\n]*)\n(ns,[^\n]*)$', r'\2\n\1')), ['13']),
    ('m13_k', S, lambda t: on_rec(t, J_mid, lambda r: resub(r, rf'^(e,[^\n]*\n)((?:[^\n]*\n)*?)(k,{MID},[^\n]*\n)', r'\3\1\2')), ['13']),
    ('mk2', M_, lambda t: one(t, KAPS[0], KAPS[0].replace(',0.7000,', ',0.6900,')), ['k2']),
    ('mk3', M_, lambda t: resub(t, r'^(SUMMARY,)', f'KAPPLY,{TA + 5000},1,{NEI},0.100,0.100,0.5000,0.4000,hit\n' + r'\1'), ['k3']),
    ('mk6', S, lambda t: on_rec(t, J_after_hit, lambda r: resub(r, rf'^(o,{NEI},(?:[^,\n]*,){{6}})[-0-9.]+', r'\g<1>0.2000')), ['k6']),
    ('mkp_band', S, lambda t: fold(t, J_pend, lambda r: set_mo(r, h='0.0300')), ['kp']),
    ('mkp_dead', S, lambda t: on_rec(t, J_pend[0], lambda r: resub(r, rf'^(k,{MID},[0-9.]+)$', r'\1,dead')), ['kp', 'k15']),
    ('mkp_lethal', M_, lambda t: one(t, KAPS[-1], re.sub(r',[0-9.]+,hit$', ',0.0000,hit', KAPS[-1])), ['kp']),
    ('mkd', 't2_xray.log', lambda t: shift_play0(t, -7000), ['kd']),
    ('mk7', S, lambda t: fold(t, [j for j, r in enumerate(recs_txt) if re.search(rf'^k,{MID},[^,]+,dead$', r, re.M) and u_of(r) <= U_dead1 + 2100],
                              lambda r: resub(r, rf'^(k,{MID},[0-9.]+),dead$', r'\1')), ['k7']),
    ('mk8', M_, lambda t: resub(t, rf'^(KWHO,\d+,{MID},)0$', r'\g<1>5'), ['k8']),
    ('mk11', S, lambda t: on_rec(t, J_late, lambda r: set_mo(r, alive='1')), ['k11']),
    ('mk13', S, lambda t: on_rec(t, J_mid, lambda r: resub(r, r'^r,[^\n]*\n', '')), ['k13']),
    ('mk14', M_, lambda t: resub(t, r'^(T2SUM,.*,khits=)2,', r'\g<1>3,'), ['k14']),
    ('mk14_kconf', M_, lambda t: resub(t, rf'^(KCONF,[^\n]*)$', r'\1\n\1'), ['k14']),
    ('mk15_dup', S, lambda t: on_rec(t, J_mid, lambda r: resub(r, rf'^(o,{NEI},[^\n]*\n)', r'\1\1')), ['k15']),
    ('mk15_stale', S, lambda t: on_rec(t, J_mid, lambda r: resub(r, rf'^(k,{MID},[0-9.]+)$', r'\1,stale')), ['k15']),
]
PASSM = [   # MADE fixtures that must still PASS (bounds just inside; rules that excuse)
    ('p9_dy_ok', S, m9_actor(first_v, dy=0.45)),
    ('p9_r_ok', S, m9_actor(first_v, r=151.0)),
    ('p9_gap_unjudged', S, lambda t: gap_before(t, gapv[:1], dy=0.9)),
    ('p6_far', S, lambda t: fold(on_rec(t, J_after_hit, lambda r: resub(r, rf'^(o,{NEI},(?:[^,\n]*,){{6}})[-0-9.]+', r'\g<1>0.2000')),
                                 [J_after_hit - 1, J_after_hit], lambda r: resub(r, rf'^(o,{NEI},[^,]*,[^,]*,)[-0-9.]+', r'\g<1>60.000'))),
]
NVM = [   # name, file, transform, rules that must fire, failing checks (exact; all inside)
    ('nve1_pre', S, lambda t: edit_snap(t, lambda r: et(r)[0] == 'none' and re.search(r'^seq,\d+,\d+,1[0-9]\.', r, re.M),
                                        lambda r: one(r, '\ne,none,0.00,0,6\n', '\ne,psi,5.00,1,10\n')), ['nve1'], None),
    ('nve2_ref', M_, lambda t: resub(t, r'^(EFORCE,\d+,surge,)started$', r'\1notstarted'), ['nve1', 'nve2'], None),
    ('nve3_ref', M_, lambda t: resub(t, r'^(EFORCE,\d+,psi,)started$', r'\1notstarted'), ['nve1', 'nve3'], None),
    ('nve5_novortex', S, lambda t: resub(t, r'^v,[^\n]*\n', '', n=0), ['nve5'], ['9']),
    ('nvk1_stalker', PO, lambda t: resub(t, r'^(end,40)$', r'k,201,stalker_bandit,0.100,0\n\1'), ['nvk1'], ['k14']),
    ('nvk2_hurt', M_, lambda t: one(t, KAPS[0], KAPS[0].replace(',1.0000,0.7000,', ',0.9500,0.6500,')), ['nvk2'], []),
    ('nvk3_fall', S, lambda t: on_rec(t, J_mid, lambda r: set_mo(r, h='0.6000')), ['nvk3'], []),
    ('nvk7_nopend', S, lambda t: drop_recs(t, set(J_pend)), ['nvk7'], ['kp']),
]
for name, fn_, tr, want in MUT:
    R = work(fn_); wfile(os.path.join(R, fn_), tr(G[fn_]))
    base = expect(name, R, 1, 'FAIL', want, [])
    for cid in want:
        if 'chk_' + cid not in caught: mut_caught('chk_' + cid, R, base)
    if name == 'm9_frac': mut_caught('b_nofrac', R, base)
for name, fn_, tr in PASSM:
    R = work(fn_); wfile(os.path.join(R, fn_), tr(G[fn_]))
    base = expect(name, R, 0, 'PASS', [], [])
    if name == 'p9_dy_ok': mut_caught('b_vdy', R, base)
    if name == 'p9_gap_unjudged': mut_caught('b_nogap', R, base)
for name, fn_, tr, fired, fl in NVM:
    R = work(fn_); wfile(os.path.join(R, fn_), tr(G[fn_]))
    base = expect(name, R, 3, 'NO VERDICT', fl, fired)
    for k in fired:
        if 'nv_' + k not in caught: mut_caught('nv_' + k, R, base)
# nvk6 (two edits in one file): M's o line removed from the newest snapshot read at or before M's first dead pose
dp = re.search(rf'^#pose,(\d+),\d+\n(?:[^#][^\n]*\n)*?k,{MID},[^,]+,[0-9.]+,1\n', G[PO], re.M); U_dp = int(dp.group(1))
J6 = max(j for j, r in enumerate(recs_txt) if u_of(r) <= U_dp)
R = work(S, '_nvk6'); wfile(os.path.join(R, S), on_rec(G[S], J6, lambda r: resub(r, rf'^o,{MID},[^\n]*\n', '')))
base = expect('nvk6_unlisted', R, 3, 'NO VERDICT', None, ['nvk6'])
mut_caught('nv_nvk6', R, base)
# k4 (two files): M falls 0.1 in J_mid and a K line of mine (KDEAD ...,failed) lies between it and the snapshot before
T4 = (tx_of(recs_txt[J_mid - 1]) + tx_of(recs_txt[J_mid])) // 2; R = os.path.join(D, 'mk4', 'run')
for n in REQ:
    os.makedirs(os.path.dirname(os.path.join(R, n)), exist_ok=True)
    if n not in (M_, S): os.symlink(os.path.join(GOOD, n), os.path.join(R, n))
wfile(os.path.join(R, M_), one(G[M_], KD, f'KDEAD,{T4},1,{MID},failed,alive\n' + KD))
wfile(os.path.join(R, S), on_rec(G[S], J_mid, lambda r: set_mo(r, h='0.6000')))
base = expect('mk4', R, 1, 'FAIL', ['k4', 'kd'], [])
mut_caught('chk_k4', R, base)
# nve4 (two files): the psi force moved to 1 s before the surge's EEND, its runner append moved by the same time
efp = int(re.search(r'^EFORCE,(\d+),psi,', G[M_], re.M).group(1)); ees = int(re.search(r'^EEND,(\d+),surge,', G[M_], re.M).group(1))
dl = efp - (ees - 1000); R4 = os.path.join(D, 'nve4_overlap', 'run')
for n in REQ:
    os.makedirs(os.path.dirname(os.path.join(R4, n)), exist_ok=True)
    if n not in (M_, 'emforce.csv'): os.symlink(os.path.join(GOOD, n), os.path.join(R4, n))
wfile(os.path.join(R4, M_), resub(G[M_], r'^EFORCE,\d+,psi,', f'EFORCE,{efp - dl},psi,'))
wfile(os.path.join(R4, 'emforce.csv'), resub(G['emforce.csv'], r'^(\d+)(,[^,]+,psi,\d+)$', lambda m: f'{int(m.group(1)) - dl}{m.group(2)}'))
base = expect('nve4_overlap', R4, 3, 'NO VERDICT', None, ['nve4'])
mut_caught('nv_nve4', R4, base)
# a stall.csv beside good (no stop in em1): 1 fails
R = os.path.join(D, 'm1_stall', 'run')
for n in REQ: os.makedirs(os.path.dirname(os.path.join(R, n)), exist_ok=True); os.symlink(os.path.join(GOOD, n), os.path.join(R, n))
wfile(os.path.join(R, 'stall.csv'), 'utc_ms,event,seq,track_s\n1,STOP,1,1\n20001,CONT,1,1\n')
expect('m1_stall', R, 1, 'FAIL', ['1'], [])
# a NO VERDICT that does not hide an unexplained failure: nvk7 fixture + check 10 failing -> FAIL
R = work(M_, '_nvfail'); wfile(os.path.join(R, M_), resub(G[M_], r'^(VITALS,min_health=[^,]+,max_radiation=)0\.0000', r'\g<1>0.1000'))
R2 = os.path.join(D, 'nvfail', 'run')
for n in REQ:
    os.makedirs(os.path.dirname(os.path.join(R2, n)), exist_ok=True)
    if n not in (M_, S): os.symlink(os.path.join(GOOD, n), os.path.join(R2, n))
wfile(os.path.join(R2, M_), open(os.path.join(R, M_)).read()); wfile(os.path.join(R2, S), drop_recs(G[S], set(J_pend)))
expect('nvfail_outside', R2, 1, 'FAIL', ['10', 'kp'], ['nvk7'], [r'^verdict: NO VERDICT rule\(s\) nvk7 fired, but failing check\(s\) 10 are outside'])
# unparsable inputs
R = work(M_, '_unp'); wfile(os.path.join(R, M_), resub(G[M_], r'^(EFORCE,\d+,surge,started)$', r'\1,x'))
expect('unparsed_eforce', R, 1, 'FAIL', None, None, [r'^UNPARSED meta line \d+: EFORCE has 5 fields', r'^verdict: unparsable input'])
wfile(os.path.join(R, M_), resub(G[M_], rf'^KCONF,(\d+),{MID},', rf'KCONF,\1,{MID}x,'))
expect('unparsed_kconf', R, 1, 'FAIL', None, None, [r'^UNPARSED meta line \d+: KCONF id', r'^verdict: unparsable input'])
R = work(PO, '_unp'); wfile(os.path.join(R, PO), resub(G[PO], r'^#pose,[^\n]*\n', ''))
expect('unparsed_pstray', R, 1, 'FAIL', None, None, [r'^UNPARSED stray pose line: seq,', r'^verdict: unparsable input'])
# missing / empty required files: FAIL by name, no check computed
for n in REQ:
    R = os.path.join(D, 'miss_' + re.sub(r'\W', '_', n), 'run')
    for x in REQ:
        if x != n: os.makedirs(os.path.dirname(os.path.join(R, x)), exist_ok=True); os.symlink(os.path.join(GOOD, x), os.path.join(R, x))
    expect('miss_' + n, R, 1, 'FAIL', set(), set(), [rf'^MISSING {re.escape(n)} '])
for n in (M_, S, 'emforce.csv', 'boot0/hits_t2.csv'):
    R = os.path.join(D, 'empty_' + re.sub(r'\W', '_', n), 'run')
    for x in REQ:
        os.makedirs(os.path.dirname(os.path.join(R, x)), exist_ok=True)
        if x != n: os.symlink(os.path.join(GOOD, x), os.path.join(R, x))
    wfile(os.path.join(R, n), '')
    if n == 'boot0/hits_t2.csv': expect('empty_hits_ok', R, 0, 'PASS', [], [])
    else: expect('empty_' + n, R, 1, 'FAIL', set(), set(), [rf'^EMPTY {re.escape(n)} '])
# CRLF copy of good
R = os.path.join(D, 'crlf', 'run')
for n in REQ: wfile(os.path.join(R, n), G[n].replace('\n', '\r\n'))
expect('good_crlf', R, 0, 'PASS', [], [])

# ---------------------------------------------------------------------------------------------------- REAL bytes
KILLF = ['k2', 'k4', 'k6', 'kp', 'kd', 'k7', 'k8', 'k11', 'k14']
if os.path.isdir(KJ4P):
    b4 = listing(KJ4P)
    expect('real_kj4p_r1', KJ4P, 3, 'NO VERDICT', KILLF, ['nvk1'],
           [r'^9 F2 vortices.*: PASS \(12 vortices.*judged for y/distance 12 of 12', r'^1 one boot.*: PASS \(.*last snapshot at play 449\.3 s',
            r'^reported: vortex 8: .*y - actor y - 20 -0\.064'])
    if listing(KJ4P) != b4: print('BAD  real kj4p folder changed'); bad.append('kj4p_changed')
else: print(f'BAD  real kj4p folder missing: {KJ4P}'); bad.append('kj4p_missing')
if os.path.isdir(KJ2):
    expect('real_kj2_inplace', KJ2, 1, 'FAIL', set(), set(), [r'^MISSING emforce\.csv '])
    R = os.path.join(D, 'real_kj2_links', 'run')
    for x in REQ + ['stall.csv']:
        if x == 'emforce.csv': continue
        os.makedirs(os.path.dirname(os.path.join(R, x)), exist_ok=True); os.symlink(os.path.join(KJ2, x), os.path.join(R, x))
    wfile(os.path.join(R, 'emforce.csv'), 'utc_ms,wall_utc,kind,ms_after_play0\n')
    EMF = ['1', '2', '3s', '3p', '4s', '4p', '4n', '5s', '5p', '6s', '6p', '7s', '7p', '8', '9', '11', '13']
    expect('real_kj2_links', R, 1, 'FAIL', EMF + ['kp', 'kd', 'k7', 'k14'], ['nvk1', 'nvk7'],
           [r'^k2 .*: PASS \(M 23183: pose total 0\.300', r'^k15 .*: PASS \(1949 snapshots, 180 dead lines', r'^k13 .*: PASS',
            r'^nvk3 .*: clear \(none; exempt death step: \(950, 951\)', r'^k14\.eerr absent', r'^k7 .*lag 11839 ms'])
else: print(f'BAD  kj2 folder missing: {KJ2}'); bad.append('kj2_missing')

nc = len([m for m in CMUT if m in caught])
print(f'checker mutants: {ncm} runs, {len(set(caught))} distinct caught of {len(CMUT)} made; not exercised: '
      f'{sorted(set(CMUT) - set(caught))}', flush=True)
if set(CMUT) - set(caught): bad.append('mutants not exercised: ' + ','.join(sorted(set(CMUT) - set(caught))))
print(f'PYTHON PART: {"PASS" if not bad else "FAIL"} (bad: {bad})', flush=True)
sys.exit(0 if not bad else 1)
PYEOF
prc=$?
bash "$HERE/em1_axr_step_test.sh" "$D/axr" > "$D/axr_step_test.txt" 2>&1; arc=$?
tail -1 "$D/axr_step_test.txt"
echo "end $(date '+%F %T')"
[ $prc = 0 ] && [ $arc = 0 ] && r=PASS || r=FAIL
echo "EM1 SELFTEST: $r (python part rc $prc; axr step test rc $arc; dir $D)"
[ $r = PASS ]
