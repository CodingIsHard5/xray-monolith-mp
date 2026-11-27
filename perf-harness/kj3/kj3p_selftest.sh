#!/usr/bin/env bash
# Selftest of kj3/kj3p_check.py (kj3p, X-only emission preflight). Desk only: no engine, no Wine, no display, no box claim.
#   nice -n 19 bash kj3/kj3p_selftest.sh
# Fixtures:
#  - STUB-MADE run folders: the REAL reader harness/zz_p1_puppet_kill6.script (read only, sha256 pinned below) runs in the
#    Lua 5.1 fake engine kj3/kill6h_stub.lua (mode `fixture`, unchanged), driven in lockstep 50 ms a step by the REAL writer
#    logic (tools/t2_kill_pose.Writer, imported read only, pick=99999: no k line, standing at kj3/kj3p_track_start.csv)
#    and by this file's stand-ins for the runner (emtest.txt made empty before the boot, `surge` / `psi` appended at play 70 /
#    310 s on a 1 s poll, emforce.csv) and for the watcher (snaps_all.txt / poses_all.txt). The fake managers in the stub
#    are a model of the scripts (kj3/NOTES.md Stage 2), not the engine. Files no engine can make here (xray.log,
#    t2_xray.log, restarts.csv) are written as stand-ins and say so (STUB_MADE.txt).
#  - MADE mutants: one file of `good` changed (each is written, judged, and then overwritten by the next mutant of the
#    same file in one work folder per file, to keep the disk use small; the unchanged files are symlinks to `good`).
#    Each must FAIL with exactly its listed failing checks. NO VERDICT fixtures must give NO VERDICT with the listed rules.
#  - REAL bytes: the kj2 X run folder (kill5, no e lines), read in place: as it is -> REFUSED naming emforce.csv; through a
#    folder of symlinks plus a made emforce.csv -> FAIL naming the missing e lines (checks 2, 13).
#  - missing / empty required files -> REFUSED naming the file; CRLF copy of good -> PASS; the checker writes nothing;
#    the score-once redirection (set -C) refuses a second write.
# Output dir: kj3/runs/kj3p-st-<time>-<pid>/ (nothing deleted). Exit 0 only if every expectation holds.
# Last line: KJ3P SELFTEST: PASS|FAIL (...)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; P="$(cd "$HERE/../../.." && pwd)"
SCRIPT="$P/harness/zz_p1_puppet_kill6.script"; SCRIPT_SHA=7d7c173dae815a0d7673acffb9d911413a18708f9beb7914afc1dc4685d4c8e9
for f in "$HERE/kj3p_check.py" "$HERE/kill6h_stub.lua" "$HERE/kj3p_track_start.csv" "$P/tools/t2_kill_pose.py" "$SCRIPT" /usr/bin/lua5.1; do
  [ -e "$f" ] || { echo "KJ3P SELFTEST: FAIL (missing $f)"; exit 1; }
done
[ "$(sha256sum "$SCRIPT" | cut -d' ' -f1)" = "$SCRIPT_SHA" ] || { echo "KJ3P SELFTEST: FAIL (reader is not 7d7c173d)"; exit 1; }
D="$HERE/runs/kj3p-st-$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$D" ] && { echo "KJ3P SELFTEST: FAIL ($D exists)"; exit 1; }
mkdir -p "$D" || { echo "KJ3P SELFTEST: FAIL (cannot make $D)"; exit 1; }
echo "selftest dir $D  start $(date '+%F %T')"
echo "checker $(sha256sum "$HERE/kj3p_check.py" | cut -c1-16)  reader $(cut -c1-16 <<<"$SCRIPT_SHA")  stub $(sha256sum "$HERE/kill6h_stub.lua" | cut -c1-16)  writer $(sha256sum "$P/tools/t2_kill_pose.py" | cut -c1-16)"
python3 -I - "$P" "$HERE" "$D" <<'PYEOF'
import sys, os, re, subprocess
P, HERE, D = sys.argv[1:4]
sys.path.insert(0, os.path.join(P, 'tools'))
import t2_kill_pose as kp
LUA, STUB = '/usr/bin/lua5.1', os.path.join(HERE, 'kill6h_stub.lua')
SCRIPT, CHK = os.path.join(P, 'harness/zz_p1_puppet_kill6.script'), os.path.join(HERE, 'kj3p_check.py')
TRACK = kp.load_track(os.path.join(HERE, 'kj3p_track_start.csv'))
KJ2 = '/home/caden/Projects/ITRZone/out/runs/20261126-172548-t2kj2-xray'
U0, SESS = 1795000000000, '1795000000000'
SURGE_AT, PSI_AT, RUN_W = 70, 310, 452.0
NOTE = 'STUB-MADE (kj3p checker selftest): no engine ran; kill6h_stub.lua fake engine + t2_kill_pose.Writer in lockstep'
REQ = ['t2_xray.log', 'restarts.csv', 'emforce.csv', 'boot0/meta_t2.csv', 'boot0/hits_t2.csv', 'boot0/tb/xray.log',
       'boot0/watch/snaps_all.txt', 'boot0/watch/poses_all.txt']
bad = []

def wfile(p, text, mode='w'):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    if os.path.islink(p): raise RuntimeError(f'refusing to write through a symlink: {p}')
    with open(p, mode) as f: f.write(text)

def make_stub(name, force):
    """force: list of (play s, kind) the runner stand-in appends. Returns the run dir (<D>/<name>/run)."""
    O = os.path.join(D, name); R = os.path.join(O, 'run'); B = os.path.join(R, 'boot0'); shm = os.path.join(O, '_shm')
    os.makedirs(shm); os.makedirs(os.path.join(B, 'watch')); os.makedirs(os.path.join(B, 'tb'))
    open(os.path.join(shm, 'emtest.txt'), 'w').close()   # the runner makes it EMPTY before the boot (T2_*_PLAY set)
    pr = subprocess.Popen([LUA, STUB, 'fixture', SCRIPT, shm, 'good'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
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
    wr = kp.Writer({'aw': 'keep', 'pick': '99999'}, SESS, s['level'], TRACK)
    snaps = open(os.path.join(B, 'watch/snaps_all.txt'), 'w'); poses = open(os.path.join(B, 'watch/poses_all.txt'), 'w')
    ef = ['utc_ms,wall_utc,kind,ms_after_play0']; u_first = U0 + now; play0 = None; play0_line = ''; todo = list(force)
    last_seq, cur_pose = None, None
    while True:
        u = U0 + now; w = (u - u_first) / 1000
        if w > RUN_W: break
        n, body, kl = wr.pose(w, u); txt = body.decode()
        open(os.path.join(shm, 'pose.txt'), 'wb').write(body); poses.write(f'#pose,{u},{n}\n{txt}'); cur_pose = txt
        if play0 is None and w >= 1.0:   # the runner's 1 s poll reads the header of the pose current then
            h = cur_pose.split('\n')[0]; play0 = int(h.split(',')[2]); play0_line = h
        if play0 is not None and int(w * 1000) % 1000 < 50:   # runner loop: once a second
            el = u - play0
            while todo and el >= todo[0][0] * 1000:
                k = todo.pop(0)[1]; open(os.path.join(shm, 'emtest.txt'), 'a').write(k + '\n')
                ef.append(f'{u},2026-11-27T00:00:00.000Z,{k},{el}')
        now = cmd('tick')
        raw = open(sp, 'rb').read(); q = kp.parse_snapshot(raw)
        if q and q['seq'] != last_seq:
            last_seq = q['seq']; snaps.write(f'#snap,{U0 + now},{q["seq"]}\n' + raw.decode())
    cmd('finish'); pr.wait(); snaps.close(); poses.close()
    os.rename(os.path.join(shm, 'meta_t2.csv'), os.path.join(B, 'meta_t2.csv'))
    os.rename(os.path.join(shm, 'hits_t2.csv'), os.path.join(B, 'hits_t2.csv'))
    wfile(os.path.join(R, 'emforce.csv'), '\n'.join(ef) + '\n')
    wfile(os.path.join(O, 'STUB_MADE.txt'), f'{NOTE}\nforced {force}\n')
    wfile(os.path.join(R, 't2_xray.log'), f'2026-11-27 00:00:00.000Z {NOTE}\n2026-11-27 00:00:00.000Z boot 0: run_id 1 engine_s 465\n'
          f'2026-11-27 00:00:01.000Z first play pose read: {play0_line} (stop planned 0 s after its utc_ms; 0 = none)\n'
          '2026-11-27 00:08:00.000Z boot 0 end: tb_run rc=0 reason=engine run ended\n'
          '2026-11-27 00:08:00.000Z end: ran to the end; boots 1; unplanned restarts 0; stall no; planned kill no; rc=0\n')
    wfile(os.path.join(R, 'restarts.csv'), 'utc_ms,boot,reason,planned\n')
    wfile(os.path.join(B, 'tb/xray.log'), f'{NOTE}\n* loading script surge_manager.script\n* loading script psi_storm_manager.script\n'
          '* loading script zz_p1_puppet.script\n')
    print(f'STUB-MADE {name}: forced {force}, {len(ef) - 1} emforce rows, play0 {play0}', flush=True)
    return R

def run_check(R):
    p = subprocess.run([sys.executable, '-I', CHK, R], capture_output=True, text=True)
    fails = {m.group(1) for m in re.finditer(r'^(\w+) .*: FAIL \(', p.stdout, re.M)}
    fired = {m.group(1) for m in re.finditer(r'^(NV\d) .*: FIRED', p.stdout, re.M)}
    v = re.search(r'^KJ3P: (.*)$', p.stdout, re.M)
    return p.returncode, fails, fired, (v.group(1) if v else None), p.stdout

def expect(name, R, code, verdict, fails=None, fired=None, also=None):
    rc, f, nv, v, out = run_check(R)
    wfile(os.path.join(D, 'reports', name + '.txt'), out)
    ok = rc == code and v == verdict and (fails is None or f == set(fails)) and (fired is None or nv == set(fired))
    if also: ok = ok and all(re.search(a, out, re.M) for a in also)
    print(f'{"ok  " if ok else "BAD "} {name}: rc {rc} verdict {v} failing {sorted(f)} fired {sorted(nv)}'
          + ('' if ok else f'   (want rc {code} {verdict} failing {sorted(fails or [])} fired {sorted(fired or [])} {also or ""})'), flush=True)
    if not ok: bad.append(name)
    return out

# ---------------------------------------------------------------------------------------------------- stub runs
GOOD = make_stub('good', [(SURGE_AT, 'surge'), (PSI_AT, 'psi')])
NOF = make_stub('noforce', [])
listing = lambda R: sorted((os.path.relpath(os.path.join(a, f), R), os.lstat(os.path.join(a, f)).st_size, os.lstat(os.path.join(a, f)).st_mtime_ns)
                           for a, _, fs in os.walk(R) for f in fs)
before = listing(GOOD)
expect('good', GOOD, 0, 'PASS', [], [])
if listing(GOOD) != before: print('BAD  checker changed the run folder'); bad.append('writes')
else: print('ok   the checker wrote nothing into the run folder')

rd = lambda R, n: open(os.path.join(R, n)).read()
G = {n: rd(GOOD, n) for n in REQ}

# the refused-by-the-game fixture: the no-force stub run (real reader bytes, no event) + two MADE EFORCE notstarted
# lines at the X time 500 ms after two made runner appends (mapped through the run's own P2 lines)
def p2_at(meta, u):   # X time whose W estimate is u
    rows = [(int(f[3]), int(f[2])) for f in (l.split(',') for l in meta.split('\n')) if f[0] == 'P2' and len(f) > 4]
    now, pu = max((r for r in rows if r[1] <= u), key=lambda r: r[1]); return now + (u - pu)
m = rd(NOF, 'boot0/meta_t2.csv'); lg = rd(NOF, 't2_xray.log'); p0 = int(re.search(r'read: seq,\d+,(\d+),', lg).group(1))
rows = ['utc_ms,wall_utc,kind,ms_after_play0']; add = []
for k, at in (('surge', SURGE_AT), ('psi', PSI_AT)):
    u = p0 + at * 1000 + 300; rows.append(f'{u},2026-11-27T00:00:00.000Z,{k},{u - p0}'); add.append(f'EFORCE,{p2_at(m, u + 500)},{k},notstarted')
i = m.index('\nSUMMARY,'); wfile(os.path.join(NOF, 'boot0/meta_t2.csv'), m[:i + 1] + '\n'.join(add) + '\n' + m[i + 1:])
wfile(os.path.join(NOF, 'emforce.csv'), '\n'.join(rows) + '\n')
wfile(os.path.join(D, 'noforce', 'STUB_MADE.txt'), f'{NOTE}\nplus MADE: two EFORCE ...,notstarted lines and their emforce rows\n', 'a')
expect('nv23_refused', NOF, 3, 'NO VERDICT', None, ['NV2', 'NV3'], [r'^F1 ANSWER: .*forced surge: REFUSED BY THE GAME'])

# ---------------------------------------------------------------------------------------------------- made mutants
WORK = {}
def work(fname):   # one folder per mutated file: that file real, every other file a symlink to good
    if fname in WORK: return WORK[fname]
    R = os.path.join(D, 'mut_' + re.sub(r'\W', '_', fname), 'run')
    for n in REQ:
        os.makedirs(os.path.dirname(os.path.join(R, n)), exist_ok=True)
        if n != fname: os.symlink(os.path.join(GOOD, n), os.path.join(R, n))
    WORK[fname] = R; return R

def snap_recs(t):   # [(start, end, text)] of each #snap record
    idx = [m.start() for m in re.finditer(r'^#snap,', t, re.M)] + [len(t)]
    return [(a, b) for a, b in zip(idx, idx[1:])]

def edit_snap(t, pred, fn, count=1):
    """apply fn to the text of the first `count` snapshot records whose text satisfies pred (count None = all)."""
    out, last, done = [], 0, 0
    for a, b in snap_recs(t):
        rec = t[a:b]
        if (count is None or done < count) and pred(rec): out.append(t[last:a]); out.append(fn(rec)); last = b; done += 1
    if not done: raise RuntimeError('mutant matched nothing')
    out.append(t[last:]); return ''.join(out)

def et(rec):
    m = re.search(r'^e,(\w+),([-0-9.]+),', rec, re.M); return (m.group(1), float(m.group(2))) if m else (None, None)

def near(kind, t0):
    return lambda rec: et(rec)[0] == kind and abs(et(rec)[1] - t0) < 0.051

def one(t, a, b):
    if t.count(a) < 1: raise RuntimeError(f'mutant anchor missing {a[:50]!r}')
    return t.replace(a, b, 1)

def resub(t, pat, fn, n=1):
    t2, k = re.subn(pat, fn, t, count=n, flags=re.M)
    if not k: raise RuntimeError(f'mutant pattern matched nothing {pat!r}')
    return t2

S, M = 'boot0/watch/snaps_all.txt', 'boot0/meta_t2.csv'
def vort_last(t, n, k):   # remove vortex n's v lines from its last k snapshots
    recs = [(a, b) for a, b in snap_recs(t) if re.search(rf'^v,{n},', t[a:b], re.M)][-k:]
    for a, b in reversed(recs):
        t = t[:a] + re.sub(rf'^v,{n},[^\n]*\n', '', t[a:b], flags=re.M) + t[b:]
    return t
first_v = int(re.search(r'^v,(\d+),', G[S], re.M).group(1))
MUT = [   # name, file, transform, expected failing checks
    ('m1_rc', 't2_xray.log', lambda t: resub(t, r'rc=0$', 'rc=6', n=0), ['1']),
    ('m1_kpose', 'boot0/watch/poses_all.txt', lambda t: resub(t, r'^(end,1)$', r'k,0,actor,0.500,0\n\1'), ['1']),
    ('m1_lua', 'boot0/tb/xray.log', lambda t: t + '! [LUA] ...gameengine/run\\gamedata\\scripts\\zz_p1_puppet.script:812: attempt to index a nil value\n', ['1']),
    ('m2_late', 'emforce.csv', lambda t: resub(t, r'^(\d+,[^,]+,surge,)\d+$', r'\g<1>73000'), ['2']),
    ('m2_error', M, lambda t: resub(t, r'^(EFORCE,\d+,surge,)started$', r"\1error:attempt to index global 'surge_manager' (a nil value)"), ['2']),
    ('m2_luaev', 'boot0/tb/xray.log', lambda t: t + '! [LUA] ...gameengine/run\\gamedata\\scripts\\surge_manager.script:1065: attempt to perform arithmetic on a nil value\n', ['2']),
    ('m3s', M, lambda t: resub(t, r'^ESTART,(\d+),surge,', lambda m: f'ESTART,{int(m.group(1)) + 1500},surge,'), ['3s']),
    ('m3p', M, lambda t: resub(t, r'^(ESTART,\d+,psi,)[-0-9.]+$', r'\g<1>1.50'), ['3p']),
    ('m4s', S, lambda t: edit_snap(t, near('surge', 100.0), lambda r: resub(r, r'^(e,surge,[^,]+,\d+,)10$', r'\g<1>6')), ['4s']),
    ('m4p', S, lambda t: edit_snap(t, near('psi', 50.0), lambda r: resub(r, r'^(e,psi,[^,]+,\d+,)10$', r'\g<1>6')), ['4p']),
    ('m4n', S, lambda t: one(t, '\ne,none,0.00,0,6\n', '\ne,none,0.00,0,10\n'), ['4n']),
    ('m5s', S, lambda t: edit_snap(t, near('surge', 60.0), lambda r: resub(r, r'^e,surge,([-0-9.]+),', lambda m: f'e,surge,{float(m.group(1)) + 0.8:.2f},')), ['5s']),
    ('m5p', S, lambda t: edit_snap(t, near('psi', 20.0), lambda r: resub(r, r'^e,psi,([-0-9.]+),', lambda m: f'e,psi,{float(m.group(1)) + 0.8:.2f},')), ['5p']),
    ('m6s', M, lambda t: resub(t, r'^(EEND,\d+,surge,)[-0-9.]+$', r'\g<1>225.00'), ['6s']),
    ('m6p', M, lambda t: resub(t, r'^(EEND,\d+,psi,)[-0-9.]+$', r'\g<1>99.00'), ['6p']),
    ('m7s', M, lambda t: resub(t, r'^EPHASE,\d+,surge,5,[^\n]*\n', ''), ['7s']),
    ('m7p', M, lambda t: resub(t, r'^(EPHASE,\d+,psi,3,[^\n]*\n)', r'\1\1'), ['7p']),
    ('m8', S, lambda t: resub(t, r'^w,1,([-0-9.]+)$', lambda m: f'w,1,{float(m.group(1)) - 20:.3f}', n=0), ['8']),
    ('m9', S, lambda t: vort_last(t, first_v, 20), ['9']),
    ('m10', S, lambda t: edit_snap(t, near('surge', 150.0), lambda r: resub(r, r'^(a,[^,]+,[^,]+,[^,]+,[^,]+,)1\.0000$', r'\g<1>0.9500')), ['10']),
    ('m10b', M, lambda t: resub(t, r'^(VITALS,min_health=[^,]+,max_radiation=)0\.0000', r'\g<1>0.1000'), ['10']),
    ('m11', M, lambda t: resub(t, r'^(T2SUM,.*,eerr=)0$', r'\g<1>1'), ['11']),
    ('m11b', M, lambda t: resub(t, r'^(SUMMARY,)', r'EERR,99999,time factor unreadable\n\1'), ['11']),
    ('m11c', S, lambda t: edit_snap(t, lambda r: et(r)[0] == 'none' and '\nns,-1,-1\n' not in r, lambda r: resub(r, r'^ns,\d+,\d+$', 'ns,-1,-1')), ['11']),
    ('m12', M, lambda t: resub(t, r'^(SUMMARY,)', r'ENOTREADY,15001\n\1'), ['12']),
    ('m13', S, lambda t: edit_snap(t, near('surge', 30.5), lambda r: resub(r, r'^(e,[^\n]*)\n(ns,[^\n]*)$', r'\2\n\1')), ['13']),
]
NVM = [   # made NO VERDICT fixtures: name, file, transform, rules that must fire, failing checks allowed (all inside)
    ('nv1_pre', S, lambda t: edit_snap(t, lambda r: et(r)[0] == 'none' and re.search(r'^seq,\d+,\d+,1[0-9]\.', r, re.M),
                                       lambda r: one(r, '\ne,none,0.00,0,6\n', '\ne,psi,5.00,1,10\n')), ['NV1']),
    ('nv4_overlap', M, lambda t: resub(t, r'^EEND,\d+,surge,', 'EEND,%d,surge,' % (int(re.search(r'^EFORCE,(\d+),psi,', G[M], re.M).group(1)) + 1000)), ['NV4']),
    ('nv5_novortex', S, lambda t: resub(t, r'^v,[^\n]*\n', '', n=0), ['NV5']),
]
nmut = 0
for name, fn_, tr, want in MUT:
    R = work(fn_); wfile(os.path.join(R, fn_), tr(G[fn_]))
    out = expect(name, R, 1, 'FAIL', want, None)
    if name not in bad: nmut += 1
for name, fn_, tr, fired in NVM:
    R = work(fn_); wfile(os.path.join(R, fn_), tr(G[fn_]))
    expect(name, R, 3, 'NO VERDICT', None, fired)
# unparsable: an EFORCE line with an extra field
R = work(M); wfile(os.path.join(R, M), resub(G[M], r'^(EFORCE,\d+,surge,started)$', r'\1,x'))
expect('unparsed_eforce', R, 1, 'FAIL', None, None, [r'^UNPARSED meta line \d+: EFORCE has 5 fields', r'^verdict: unparsable input'])

# ---------------------------------------------------------------------------------------------------- missing / empty
nmiss = 0
for n in REQ:
    R = os.path.join(D, 'miss_' + re.sub(r'\W', '_', n), 'run')
    for x in REQ:
        if x != n: os.makedirs(os.path.dirname(os.path.join(R, x)), exist_ok=True); os.symlink(os.path.join(GOOD, x), os.path.join(R, x))
    expect('miss_' + n, R, 2, 'REFUSED', set(), set(), [rf'^REFUSED: {re.escape(n)} is missing'])
    nmiss += ('miss_' + n) not in bad
for n in (M, S, 'emforce.csv'):
    R = os.path.join(D, 'empty_' + re.sub(r'\W', '_', n), 'run')
    for x in REQ:
        os.makedirs(os.path.dirname(os.path.join(R, x)), exist_ok=True)
        if x != n: os.symlink(os.path.join(GOOD, x), os.path.join(R, x))
    wfile(os.path.join(R, n), '')
    expect('empty_' + n, R, 2, 'REFUSED', set(), set(), [rf'^REFUSED: {re.escape(n)} is empty'])

# ---------------------------------------------------------------------------------------------------- CRLF copy of good
R = os.path.join(D, 'crlf', 'run')
for n in REQ:
    t = G[n]; wfile(os.path.join(R, n), t.replace('\n', '\r\n'))
os.makedirs(os.path.join(R, 'boot0'), exist_ok=True)
expect('good_crlf', R, 0, 'PASS', [], [])

# ---------------------------------------------------------------------------------------------------- real bytes (kj2, read in place)
if os.path.isdir(KJ2):
    expect('real_kj2_inplace', KJ2, 2, 'REFUSED', set(), set(), [r'^REFUSED: emforce\.csv is missing'])
    R = os.path.join(D, 'real_kj2_links', 'run')
    for x in REQ:
        if x == 'emforce.csv': continue
        os.makedirs(os.path.dirname(os.path.join(R, x)), exist_ok=True); os.symlink(os.path.join(KJ2, x), os.path.join(R, x))
    wfile(os.path.join(R, 'emforce.csv'), 'utc_ms,wall_utc,kind,ms_after_play0\n1795732174000,MADE,surge,70030\n1795732414000,MADE,psi,310030\n')
    out = expect('real_kj2_links', R, 1, 'FAIL', None, None, [r'^13 snapshot layout.*: FAIL \(1949 snapshots', r'^2 F1 .*: FAIL .*EFORCE lines 0'])
else:
    print(f'BAD  real kj2 folder missing: {KJ2}'); bad.append('real')

# ---------------------------------------------------------------------------------------------------- score once (set -C)
rep = os.path.join(D, 'score_once_report.txt')
c1 = subprocess.run(['bash', '-c', f'set -C; python3 -I "{CHK}" "{GOOD}" > "{rep}"; echo $? > "{rep}.rc"'], capture_output=True, text=True)
first = open(rep).read()
c2 = subprocess.run(['bash', '-c', f'set -C; python3 -I "{CHK}" "{GOOD}" > "{rep}"'], capture_output=True, text=True)
okc = c1.returncode == 0 and open(rep + '.rc').read().strip() == '0' and first.rstrip().endswith('KJ3P: PASS') and c2.returncode != 0 and open(rep).read() == first
print(f'{"ok  " if okc else "BAD "} score-once: first write rc {open(rep + ".rc").read().strip()}, second refused rc {c2.returncode} ({c2.stderr.strip()[:80]}), report unchanged {open(rep).read() == first}')
if not okc: bad.append('score_once')

print(f'KJ3P SELFTEST: {"PASS" if not bad else "FAIL"} (fixtures: 2 STUB-MADE runs; good PASS {"good" not in bad}; '
      f'mutants failing exactly their checks {nmut}/{len(MUT)}; made NO VERDICT {sum(1 for n in NVM if n[0] not in bad) + ("nv23_refused" not in bad)}/{len(NVM) + 1}; '
      f'missing-file REFUSED {nmiss}/{len(REQ)}; real kj2 bytes {"ok" if not {"real_kj2_inplace", "real_kj2_links", "real"} & set(bad) else "BAD"}'
      f'{"; bad: " + " ".join(bad) if bad else ""})')
sys.exit(1 if bad else 0)
PYEOF
rc=$?
echo "end $(date '+%F %T')"
exit $rc
