#!/usr/bin/env bash
# Selftest of kj4/kj4p_check.py (kj4p, X-only emission preflight). Desk only: no engine, no Wine, no display, no box claim.
#   nice -n 19 bash kj4/kj4p_selftest.sh
# Fixtures:
#  - STUB-MADE run folders: the REAL reader harness/zz_p1_puppet_kill7.script (read only, sha256 pinned below) runs in the
#    Lua 5.1 fake engine kj3/kill7h_stub.lua (mode `fixture`, unchanged), driven in lockstep 50 ms a step by the REAL writer
#    logic (tools/t2_kill_pose.Writer, imported read only, pick=99999: no k line, standing at kj4/kj4p_track_start.csv)
#    and by this file's stand-ins for the runner (emtest.txt made empty before the boot, `surge` / `psi` appended at play 70 /
#    310 s on a 1 s poll, emforce.csv) and for the watcher (snaps_all.txt / poses_all.txt). The fake managers in the stub
#    are a model of the scripts (kj3/NOTES.md Stage 2), not the engine. Files no engine can make here (xray.log,
#    t2_xray.log, restarts.csv) are written as stand-ins and say so (STUB_MADE.txt).
#  - MADE mutants: one file of `good` changed (each is written, judged, and then overwritten by the next mutant of the
#    same file in one work folder per file, to keep the disk use small; the unchanged files are symlinks to `good`).
#    Each must FAIL with exactly its listed failing checks. NO VERDICT fixtures must give NO VERDICT with the listed rules.
#  - REAL bytes: the kj2 X run folder (kill5, no e lines), read in place: as it is -> REFUSED naming emforce.csv; through a
#    folder of symlinks plus a made emforce.csv -> FAIL naming the missing e lines (checks 2, 13).
#  - missing / empty required files -> REFUSED naming the file; CRLF copy of good -> PASS; the checker writes nothing.
#  - kj4/kj4p_score.sh in every state (bash, after the Python part): fresh, re-run, interrupted (report without .rc),
#    .rc without report, checker hash mismatch, usage; its registered hash = the checker on disk.
#  - kj4/kj4p_run.sh's functions, sourced with KJ4P_FUNCS_ONLY=1 against stub tools (no claim, no engine, no display):
#    title check states, display check, exclusive out dir, pins, watchdog precedence, finish gives back only what was
#    taken, TERM during want / claim, own-engine kill on harmless sleep processes, and the order of the main body.
# Output dir: kj4/runs/kj4p-st-<time>-<pid>/ (nothing deleted). Exit 0 only if every expectation holds.
# Last line: KJ4P SELFTEST: PASS|FAIL (...)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; P="$(cd "$HERE/../../.." && pwd)"
SCRIPT="$P/harness/zz_p1_puppet_kill7.script"; SCRIPT_SHA=0d5e59279775498e54155bc7a1ef9ecc7d8fa4f9132f7a77827fb30e3d6d9b54
for f in "$HERE/kj4p_check.py" "$HERE/kill7h_stub.lua" "$HERE/kj4p_track_start.csv" "$P/tools/t2_kill_pose.py" "$SCRIPT" /usr/bin/lua5.1; do
  [ -e "$f" ] || { echo "KJ4P SELFTEST: FAIL (missing $f)"; exit 1; }
done
[ "$(sha256sum "$SCRIPT" | cut -d' ' -f1)" = "$SCRIPT_SHA" ] || { echo "KJ4P SELFTEST: FAIL (reader is not 7d7c173d)"; exit 1; }
D="$HERE/runs/kj4p-st-$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$D" ] && { echo "KJ4P SELFTEST: FAIL ($D exists)"; exit 1; }
mkdir -p "$D" || { echo "KJ4P SELFTEST: FAIL (cannot make $D)"; exit 1; }
echo "selftest dir $D  start $(date '+%F %T')"
echo "checker $(sha256sum "$HERE/kj4p_check.py" | cut -c1-16)  reader $(cut -c1-16 <<<"$SCRIPT_SHA")  stub $(sha256sum "$HERE/kill7h_stub.lua" | cut -c1-16)  writer $(sha256sum "$P/tools/t2_kill_pose.py" | cut -c1-16)"
python3 -I - "$P" "$HERE" "$D" <<'PYEOF'
import sys, os, re, subprocess
P, HERE, D = sys.argv[1:4]
sys.path.insert(0, os.path.join(P, 'tools'))
import t2_kill_pose as kp
LUA, STUB = '/usr/bin/lua5.1', os.path.join(HERE, 'kill7h_stub.lua')
SCRIPT, CHK = os.path.join(P, 'harness/zz_p1_puppet_kill7.script'), os.path.join(HERE, 'kj4p_check.py')
TRACK = kp.load_track(os.path.join(HERE, 'kj4p_track_start.csv'))
KJ2 = '/home/caden/Projects/ITRZone/out/runs/20261126-172548-t2kj2-xray'
U0, SESS = 1795000000000, '1795000000000'
SURGE_AT, PSI_AT, RUN_W = 70, 310, 452.0
NOTE = 'STUB-MADE (kj4p checker selftest): no engine ran; kill7h_stub.lua fake engine + t2_kill_pose.Writer in lockstep'
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
    v = re.search(r'^KJ4P: (.*)$', p.stdout, re.M)
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
def vort_mid(t, n, val):   # set s of vortex n in its middle snapshot to val
    recs = [(a, b) for a, b in snap_recs(t) if re.search(rf'^v,{n},', t[a:b], re.M)]
    if len(recs) < 3: raise RuntimeError('vortex too short for a middle sample')
    a, b = recs[len(recs) // 2]
    return t[:a] + re.sub(rf'^(v,{n},[^,]+,[^,]+,[^,]+,)[-0-9.]+$', lambda m: m.group(1) + val, t[a:b], count=1, flags=re.M) + t[b:]
# the real callback-registration block, read in place from the kj2 X run's engine log (CRLF kept: the checker strips CR)
def real_block():
    L = open(os.path.join(KJ2, 'boot0/tb/xray.log'), 'rb').read().decode('utf-8', 'replace').split('\n')
    a = next(k for k, l in enumerate(L) if l.startswith('![axr_main callback_set]') and "doesn't exist" in l)
    seps = [k for k in range(a + 1, a + 30) if L[k].startswith('~ -')]
    return '\n'.join(L[a:seps[1] + 1]) + '\n', a + 1
REAL_BLOCK, REAL_AT = real_block()
FR = re.search(r'^\t\.\.\. liz_[^\n]*\(line: 44\) in function [^\n]*$', REAL_BLOCK, re.M).group(0)
NOISE = REAL_BLOCK.replace(FR, "\t... zz_p1_puppet.script (line: 44) in function 'on_game_start'\r", 1)   # its frame names the bridge
AFTER = "~ STACK TRACEBACK:\n\n\t... zz_p1_puppet.script (line: 812) in function 'update'\n~ " + '-' * 72 + "\n"   # a later traceback
_k = REAL_BLOCK.rstrip('\n').rfind('\n') + 1   # start of the closing delimiter line
SECOND = REAL_BLOCK[:_k] + "~ STACK TRACEBACK:\r\n\r\n\t... zz_p1_puppet.script (line: 812) in function 'update'\r\n" + REAL_BLOCK[_k:]   # a 2nd traceback inside
GOOD_OUT = open(os.path.join(D, 'reports', 'good.txt')).read()
D_SURGE = int(re.search(r"surge: runner appended .* EFORCE 'started' at X \d+, (-?\d+) ms after", GOOD_OUT).group(1))
MUT = [   # name, file, transform, expected failing checks
    ('m1_rc', 't2_xray.log', lambda t: resub(t, r'rc=0$', 'rc=6', n=0), ['1']),
    ('m1_kpose', 'boot0/watch/poses_all.txt', lambda t: resub(t, r'^(end,1)$', r'k,0,actor,0.500,0\n\1'), ['1']),
    ('m1_lua', 'boot0/tb/xray.log', lambda t: t + '! [LUA] ...gameengine/run\\gamedata\\scripts\\zz_p1_puppet.script:812: attempt to index a nil value\n', ['1']),
    ('m2_extra', M, lambda t: resub(t, r'^(SUMMARY,)', r'EFORCE,19000,unexpected,unknown\n\1'), ['2']),
    ('m2_early', 'emforce.csv', lambda t: resub(t, r'^(\d+)(,[^,]+,surge,\d+)$', lambda m: f'{int(m.group(1)) + D_SURGE + 600}{m.group(2)}'), ['2']),
    ('m1_lua_after', 'boot0/tb/xray.log', lambda t: t + REAL_BLOCK + AFTER, ['1']),
    ('m1_lua_second', 'boot0/tb/xray.log', lambda t: t + SECOND, ['1']),
    ('m1_lua_noise', 'boot0/tb/xray.log', lambda t: t + NOISE + '! [LUA] ...gameengine/run\\gamedata\\scripts\\zz_p1_puppet.script:812: attempt to index a nil value\n', ['1']),
    ('m6s_x', M, lambda t: resub(t, r'^EEND,(\d+),surge,', lambda m: f'EEND,{int(m.group(1)) + 10000},surge,'), ['6s']),
    ('m9b', S, lambda t: vort_mid(t, first_v, '99.00'), ['9']),
    ('m10c', M, lambda t: resub(t, r'^SUMMARY,hits=\d+,cancelled=\d+,', 'SUMMARY,'), ['10']),
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
    ('nv5_novortex', S, lambda t: resub(t, r'^v,[^\n]*\n', '', n=0), ['NV5']),
]
nmut = 0
# NV4 (two files): the psi force moved to 1 s before the surge's EEND (X), its runner append moved by the same time, so
# check 2's delay is unchanged; 3p then fails (ESTART long after EFORCE), inside NV4's untested set
efp = int(re.search(r'^EFORCE,(\d+),psi,', G[M], re.M).group(1)); ees = int(re.search(r'^EEND,(\d+),surge,', G[M], re.M).group(1))
dl = efp - (ees - 1000); R4 = os.path.join(D, 'nv4_overlap', 'run')
for n in REQ:
    os.makedirs(os.path.dirname(os.path.join(R4, n)), exist_ok=True)
    if n not in (M, 'emforce.csv'): os.symlink(os.path.join(GOOD, n), os.path.join(R4, n))
wfile(os.path.join(R4, M), resub(G[M], r'^EFORCE,\d+,psi,', f'EFORCE,{efp - dl},psi,'))
wfile(os.path.join(R4, 'emforce.csv'), resub(G['emforce.csv'], r'^(\d+)(,[^,]+,psi,\d+)$', lambda m: f'{int(m.group(1)) - dl}{m.group(2)}'))
expect('nv4_overlap', R4, 3, 'NO VERDICT', None, ['NV4'])
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
# stray pose payload: the first #pose wrapper line removed, its payload kept
PO = 'boot0/watch/poses_all.txt'
R = work(PO); wfile(os.path.join(R, PO), resub(G[PO], r'^#pose,[^\n]*\n', ''))
expect('unparsed_pstray', R, 1, 'FAIL', None, None, [r'^UNPARSED stray pose line: seq,', r'^verdict: unparsable input'])
# PASS fixtures for the review fixes (each must still PASS):
#  p2_stale: the P2 lines of the 300 ms before (and at) the surge EFORCE say a pose 400 ms older; the 10 s minimum map
#            gives the SAME surge delay as good (the newest-P2 map would move it 400 ms early)
efn = int(re.search(r'^EFORCE,(\d+),surge,', G[M], re.M).group(1))
def stale(m):
    f = m.group(0).split(','); now = int(f[3])
    if efn - 300 <= now <= efn: f[2] = str(int(f[2]) - 400)
    return ','.join(f)
R = work(M); wfile(os.path.join(R, M), re.sub(r'^P2,[^\n]*$', stale, G[M], flags=re.M))
expect('p2_stale', R, 0, 'PASS', [], [], [rf"surge: runner appended .* at X {efn}, {D_SURGE} ms after"])
#  lua_noise_ok: a registration diagnostic whose FRAME names the bridge is still noise
R = work('boot0/tb/xray.log'); wfile(os.path.join(R, 'boot0/tb/xray.log'), G['boot0/tb/xray.log'] + NOISE)
expect('lua_noise_ok', R, 0, 'PASS', [], [])
#  F5 on EEND: good reports both events; with EEND psi after the last snapshot, F5 psi is unavailable (6p fails: X span)
if not re.search(r'^reported: F5 surge: online stalkers before \d+, after \(EEND \+ 2 s\)', GOOD_OUT, re.M): print('BAD  f5_good'); bad.append('f5_good')
else: print('ok   f5_good: F5 surge reported against EEND + 2 s')
last_tx = max(int(m.group(1)) for m in re.finditer(r'^seq,\d+,(\d+),', G[S], re.M))
R = work(M); wfile(os.path.join(R, M), resub(G[M], r'^EEND,\d+,psi,', f'EEND,{last_tx + 60000},psi,'))
expect('f5_unavail', R, 1, 'FAIL', ['6p'], None, [r'^reported: F5 psi: unavailable'])

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

print(f'checker part: {"PASS" if not bad else "FAIL"} (fixtures: 2 STUB-MADE runs; good PASS {"good" not in bad}; '
      f'mutants failing exactly their checks {nmut}/{len(MUT)}; made NO VERDICT {sum(1 for n in NVM if n[0] not in bad) + ("nv23_refused" not in bad) + ("nv4_overlap" not in bad)}/{len(NVM) + 2}; '
      f'missing-file REFUSED {nmiss}/{len(REQ)}; real kj2 bytes {"ok" if not {"real_kj2_inplace", "real_kj2_links", "real"} & set(bad) else "BAD"}'
      f'{"; bad: " + " ".join(bad) if bad else ""})')
sys.exit(1 if bad else 0)
PYEOF
pyrc=$?
GOOD="$D/good/run"; sbad=""; dbad=""; ns=0; nd=0; nsb=0; ndb=0
sok() { if [ "$2" = 0 ]; then echo "ok   score $1"; ns=$((ns + 1)); else echo "BAD  score $1"; sbad="$sbad|$1"; nsb=$((nsb + 1)); fi; }
dok() { if [ "$2" = 0 ]; then echo "ok   driver $1"; nd=$((nd + 1)); else echo "BAD  driver $1"; dbad="$dbad|$1"; ndb=$((ndb + 1)); fi; }
t() { "$@" >/dev/null 2>&1; echo $?; }   # exit code of a command, its output dropped
# ---------------------------------------------------------------------------------------------------- kj4p_score.sh states
SC="$HERE/kj4p_score.sh"; SD="$D/score"; mkdir -p "$SD/alt"
reg=$(sed -n 's/^CHK_SHA=\([0-9a-f]\{64\}\)$/\1/p' "$SC"); cur=$(sha256sum "$HERE/kj4p_check.py" | cut -c1-64)
[ -n "$reg" ] && [ "$reg" = "$cur" ]; sok "registered-hash-is-the-checker (${reg:0:8} = ${cur:0:8})" $?
r=$(t bash "$SC"); [ "$r" = 2 ]; sok "usage-refused (rc $r)" $?
r=$(t bash "$SC" "$GOOD" "$SD/rep1.txt")
[ "$r" = 0 ] && [ "$(cat "$SD/rep1.txt.rc" 2>/dev/null)" = 0 ] && [ "$(tail -1 "$SD/rep1.txt")" = "KJ4P: PASS" ]; sok "fresh: report + rc 0 (rc $r)" $?
h1=$(sha256sum "$SD/rep1.txt" "$SD/rep1.txt.rc")
r=$(t bash "$SC" "$GOOD" "$SD/rep1.txt"); [ "$r" = 2 ] && [ "$(sha256sum "$SD/rep1.txt" "$SD/rep1.txt.rc")" = "$h1" ]; sok "re-run refused, report and rc unchanged (rc $r)" $?
printf 'partial report of an interrupted score\n' > "$SD/rep2.txt"; h2=$(sha256sum < "$SD/rep2.txt")
r=$(t bash "$SC" "$GOOD" "$SD/rep2.txt"); [ "$r" = 2 ] && [ ! -e "$SD/rep2.txt.rc" ] && [ "$(sha256sum < "$SD/rep2.txt")" = "$h2" ]; sok "interrupted (report, no rc): refused, no rc made, report unchanged (rc $r)" $?
echo 0 > "$SD/rep3.txt.rc"
r=$(t bash "$SC" "$GOOD" "$SD/rep3.txt"); [ "$r" = 2 ] && [ ! -e "$SD/rep3.txt" ] && [ "$(cat "$SD/rep3.txt.rc")" = 0 ]; sok "rc without report: refused, no report made (rc $r)" $?
cp "$SC" "$SD/alt/kj4p_score.sh"; { cat "$HERE/kj4p_check.py"; echo "# changed"; } > "$SD/alt/kj4p_check.py"
r=$(t bash "$SD/alt/kj4p_score.sh" "$GOOD" "$SD/rep4.txt"); [ "$r" = 2 ] && [ ! -e "$SD/rep4.txt" ] && [ ! -e "$SD/rep4.txt.rc" ]; sok "checker hash mismatch: refused, nothing made (rc $r)" $?
# ---------------------------------------------------------------------------------------------------- kj4p_run.sh functions
DV="$D/drv"; mkdir -p "$DV/tools" "$DV/bin" "$DV/x/.X11-unix" "$DV/notools"
cat > "$DV/tools/protected-title-check.sh" <<EOS
#!/bin/bash
exit \$(cat "$DV/tcode" 2>/dev/null || echo 1)
EOS
cat > "$DV/tools/quiet-required.sh" <<EOS
#!/bin/bash
# stub: check prints the fake flag ($DV/held); claim modes in $DV/cmode: ok (flag + receipt), commit_sig (flag written,
# then killed by TERM before any receipt), refuse (rc 2, nothing written); every other verb is only logged
if [ "\$1" = check ]; then [ -s "$DV/held" ] && { cat "$DV/held"; exit 0; }; echo clear; exit 1; fi
echo "\$*" >> "$DV/qr.log"; [ -e "$DV/qsleep_\$1" ] && sleep "\$(cat "$DV/qsleep_\$1")"
if [ "\$1" = claim ]; then case "\$(cat "$DV/cmode" 2>/dev/null)" in
  commit_sig) echo "HELD: 2026-11-27 00:00:00 \$2 \$3 \$4" > "$DV/held"; kill -TERM \$\$; sleep 5 ;;
  refuse) echo "REFUSED: stub" >&2; exit 2 ;;
  *) echo "HELD: 2026-11-27 00:00:00 \$2 \$3 \$4" > "$DV/held"; echo "claimed: stub"; echo "claim-id: c1795.42.7"; exit 0 ;;
esac; fi
exit \$(cat "$DV/qcode_\$1" 2>/dev/null || echo 0)
EOS
cat > "$DV/bin/pgrep" <<EOS
#!/bin/bash
[ "\$1" = -x ] && [ "\$2" = sober ] && exit \$(cat "$DV/pcode" 2>/dev/null || echo 1)
exec /usr/bin/pgrep "\$@"
EOS
mkdir -p "$DV/fk/tools"
cat > "$DV/fk/tools/tb_run.sh" <<'EOS'
#!/bin/bash
# fake launcher: starts a fake engine (argv0 fakewine) after $1 s, as tb_run.sh starts wine after Xvfb + openbox
sleep "$1"; exec -a fakewine sleep 30
EOS
chmod +x "$DV/tools/protected-title-check.sh" "$DV/tools/quiet-required.sh" "$DV/bin/pgrep" "$DV/fk/tools/tb_run.sh"
# each test: a fresh subshell that sources the driver's functions only and sets the variables the main body would set;
# the candidate lists are the fake processes' (argv0 fakewine), never the box's real Wine / Xvfb processes
SETV='PATH="$DV/bin:$PATH"; T="$DV/tools"; W=engineperf; XROOT="$DV/x"; DISP=:4242; P="$DV"; K="$HERE"; SHM="$DV/shm"; WAITMIN=1
  TOKEN=tok1; WHAT="EXCLUSIVE: test tok1"; PW=""; XP=""; WD=""; rc=0; WANTED=0; CLAIMED=0; CLAIMTRY=0; RELEASED=0; KILLED_LAUNCHER=0
  LAUNCHED=0; SIGPEND=0; RID=""; O="$DV/o"; KJ4P_POLL_S=1; KJ4P_SCAN_S=0.4; mkdir -p "$O"
  wine_pids() { local d; for d in /proc/[0-9]*; do [ "$(tr "\0" "\n" 2>/dev/null < $d/cmdline | head -1)" = fakewine ] && echo "${d#/proc/}"; done; }
  xsrv_pids() { local d; for d in /proc/[0-9]*; do [ "$(tr "\0" "\n" 2>/dev/null < $d/cmdline | head -1)" = fakexvfb ] && echo "${d#/proc/}"; done; }
  LOCKF="$DV/state/driver.lock"'
fx() { ( KJ4P_FUNCS_ONLY=1; . "$HERE/kj4p_run.sh"; eval "$SETV"; eval "$1" ) >> "$DV/fx.log" 2>&1; }
fxb() { ( KJ4P_FUNCS_ONLY=1; . "$HERE/kj4p_run.sh"; eval "$SETV"; eval "$1" ) >> "$DV/fx.log" 2>&1 & FXP=$!; }   # FXP = the subshell that holds the traps
setq() { local v; for v in want claim withdraw release; do echo 0 > "$DV/qsleep_$v"; echo 0 > "$DV/qcode_$v"; done; : > "$DV/qr.log"; : > "$DV/held"; echo ok > "$DV/cmode"; }   # stub reset (no file removed)
q() { tr '\n' '|' < "$DV/qr.log"; }
fakes() { for d in /proc/[0-9]*; do [ "$(tr '\0' '\n' 2>/dev/null < $d/cmdline | head -1)" = fakewine ] && echo "${d#/proc/}"; done; }
ts() { echo "$1" > "$DV/tcode"; echo "$2" > "$DV/pcode"; fx 'title_state'; echo $?; }
[ "$(ts 0 1)" = 0 ] && [ "$(ts 1 1)" = 1 ] && [ "$(ts 1 0)" = 0 ] && [ "$(ts 2 1)" = 2 ] && [ "$(ts 1 3)" = 2 ]; dok "title_state: playing 0, clear 1, helper rc 2 -> 2, pgrep error -> 2" $?
fx 'T="$DV/notools"; title_state'; [ $? = 2 ]; dok "title_state: helper missing -> 2 (not clear)" $?
fx 'DISP=:0; disp_free'; a=$?; fx 'DISP=:x1; disp_free'; b=$?; fx 'disp_free'; c=$?; : > "$DV/x/.X11-unix/X4244"; fx 'DISP=:4244; disp_free'; d=$?
: > "$DV/x/.X4245-lock"; fx 'DISP=:4245; disp_free'; e=$?
[ $a = 1 ] && [ $b = 1 ] && [ $c = 0 ] && [ $d = 1 ] && [ $e = 1 ]; dok "disp_free: :0 / not :<n> / socket / lock refused, free ok ($a$b$c$d$e)" $?
mkdir -p "$DV/ro"; chmod 555 "$DV/ro"
fx 'O="$DV/new1"; make_out'; a=$?; fx 'O="$DV/new1"; make_out'; b=$?; fx 'O="$DV/ro/sub"; make_out'; c=$?; fx 'O="$DV/nop/sub"; make_out'; d=$?
chmod 755 "$DV/ro"
[ $a = 0 ] && [ -d "$DV/new1" ] && [ $b = 1 ] && [ $c = 1 ] && [ $d = 1 ] && [ ! -e "$DV/ro/sub" ]; dok "make_out: new ok; exists / parent not writable / parent missing refused ($a$b$c$d)" $?
echo pinned > "$DV/pinf"; ps=$(sha256sum "$DV/pinf" | cut -c1-64)
fx "pin '$DV/pinf' $ps"; a=$?; fx "pin '$DV/pinf' ${ps/a/b}0"; b=$?; fx "pin '$DV/nofile' $ps"; c=$?; fx "pin '$DV/pinf' PIN_ME_X"; d=$?
[ $a = 0 ] && [ $b = 1 ] && [ $c = 1 ] && [ $d = 1 ]; dok "pin: match ok; other hash / missing / PIN_ME refused ($a$b$c$d)" $?
fx 'O="$DV/o_fr"; mkdir -p "$O"; [ "$(final_rc 3)" = 3 ] && [ "$(final_rc 0)" = 0 ] && echo stop > "$O/.title_stop" && [ "$(final_rc 3)" = 4 ] && [ "$(final_rc 0)" = 4 ]'
dok "final_rc: the watchdog marker wins (3 -> 4, 0 -> 4), else unchanged" $?
# pass 2 #4: a stop marker written while finish reaps the watchdog, on the normal and on the cancel path, gives 4
mkdir -p "$DV/o_late1" "$DV/o_late2"
fx 'O="$DV/o_late1"; bash -c "trap \"echo stop > $DV/o_late1/.title_stop; exit 0\" TERM; while :; do sleep 0.1; done" & WD=$!; sleep 0.3; rc=0; finish'; a=$?
fx 'O="$DV/o_late2"; bash -c "trap \"echo stop > $DV/o_late2/.title_stop; exit 0\" TERM; while :; do sleep 0.1; done" & WD=$!; sleep 0.3; on_sig'; b=$?
fx 'O="$DV/o_late3"; mkdir -p "$O"; sleep 5 & WD=$!; rc=0; finish'; c=$?
[ $a = 4 ] && [ $b = 4 ] && [ $c = 0 ]; dok "finish: a marker written while the watchdog is reaped gives 4 (normal $a, cancel $b); none keeps the status ($c)" $?
setq; fx 'finish'; a=$?; [ ! -s "$DV/qr.log" ] && [ $a = 0 ]; dok "finish with nothing taken: no withdraw, no release" $?
setq; fx 'WANTED=1; rc=2; finish'; a=$?; [ "$(q)" = "withdraw engineperf|" ] && [ $a = 2 ]; dok "finish after a want only: withdraw only" $?
setq; fx 'CLAIMTRY=1; echo "claim-id: c9.9.9" > "$O/claim_calls.txt"; finish'; [ "$(q)" = "release engineperf c9.9.9|" ]; dok "finish after a claim with a receipt: release by that id only" $?
echo 1 > "$DV/tcode"; echo 1 > "$DV/pcode"
CL="claim engineperf 30 EXCLUSIVE: test tok1"
setq; fx 'O="$DV/o_a1"; mkdir -p "$O"; trap finish EXIT; trap on_sig TERM INT; acquire; finish'
[ "$(q)" = "want engineperf EXCLUSIVE|$CL|withdraw engineperf|release engineperf c1795.42.7|" ]; dok "acquire: want, claim, withdraw; finish releases by the receipt id" $?
# pass 2 #3: a claim committed by a helper that dies (TERM) before its receipt; a refused claim beside another engineperf claim
setq; echo commit_sig > "$DV/cmode"; fx 'O="$DV/o_a2"; mkdir -p "$O"; held_call CLAIMED claim $W 30 "$WHAT"; echo "rc $?" >> "$O/rc"; finish'
[ "$(q)" = "$CL|release engineperf|" ] && grep -q 'rc 143' "$DV/o_a2/rc"; dok "claim committed, helper killed before its receipt (rc 143): found by its token, released" $?
setq; echo refuse > "$DV/cmode"; echo "HELD: 2026-11-27 00:00:00 engineperf 60 EXCLUSIVE: another engineperf job" > "$DV/held"
fx 'O="$DV/o_a3"; mkdir -p "$O"; held_call CLAIMED claim $W 30 "$WHAT"; finish'; [ "$(q)" = "$CL|" ]; dok "claim refused while ANOTHER engineperf claim is held: nothing released" $?
setq; echo "HELD: 2026-11-27 00:00:00 engineperf 60 EXCLUSIVE: another engineperf job" > "$DV/held"
fx 'O="$DV/o_a4"; mkdir -p "$O"; WAITMIN=0; trap finish EXIT; trap on_sig TERM INT; acquire; rc=$?; finish'; [ "$(q)" = "want engineperf EXCLUSIVE|withdraw engineperf|" ]; dok "an engineperf claim already held: no claim beside it (would renew it), queue left, nothing released" $?
# amendment 1 (2026-11-27): replay of the 08:37 handoff. The releasing worker writes a 5-minute pre-claim in OUR name; the
# driver must claim over it (renew it with its own text), not wait beside it until it lapses.
setq; echo "HELD: 2026-11-27 08:37:00 engineperf 5 EXCLUSIVE handoff pre-claim (claim within 5 min)" > "$DV/held"
fx 'O="$DV/o_a5"; mkdir -p "$O"; WAITMIN=0; trap finish EXIT; trap on_sig TERM INT; acquire; echo "rc $?" > "$O/rc"; finish'
case "$(q)" in "want engineperf EXCLUSIVE|$CL|"*) grep -q '^rc 0$' "$DV/o_a5/rc" ;; *) false ;; esac; dok "handoff pre-claim in our name: claimed over it, not waited out (replay of 2026-11-27 08:37)" $?
setq; echo 2 > "$DV/qsleep_want"
fxb 'O="$DV/o_t1"; mkdir -p "$O"; trap finish EXIT; trap on_sig TERM INT; acquire; finish'; sleep 0.7; kill -TERM $FXP; wait $FXP; a=$?
[ $a = 143 ] && [ "$(q)" = "want engineperf EXCLUSIVE|withdraw engineperf|" ]; dok "TERM during want: exit 143 ($a), leaves the queue, claims nothing, releases nothing" $?
setq; echo 2 > "$DV/qsleep_claim"
fxb 'O="$DV/o_t2"; mkdir -p "$O"; trap finish EXIT; trap on_sig TERM INT; acquire; finish'; sleep 0.7; kill -TERM $FXP; wait $FXP; a=$?
[ "$(q)" = "want engineperf EXCLUSIVE|$CL|withdraw engineperf|release engineperf c1795.42.7|" ]; dok "TERM during a claim that succeeds: released by its id" $?
setq; echo refuse > "$DV/cmode"; echo 2 > "$DV/qsleep_claim"
fxb 'O="$DV/o_t3"; mkdir -p "$O"; trap finish EXIT; trap on_sig TERM INT; acquire; finish'; sleep 0.7; kill -TERM $FXP; wait $FXP; a=$?
[ "$(q)" = "want engineperf EXCLUSIVE|$CL|withdraw engineperf|" ]; dok "TERM during a claim that fails: no release" $?
setq; echo 2 > "$DV/tcode"
fx 'O="$DV/o_t4"; mkdir -p "$O"; trap finish EXIT; trap on_sig TERM INT; acquire; a=$?; [ $a = 2 ] && rc=2; finish'
[ "$(q)" = "want engineperf EXCLUSIVE|withdraw engineperf|" ]; dok "title check failing while queued: refused, no claim, queue left" $?
echo 1 > "$DV/tcode"
# own-process kill on three fake engines (argv0 fakewine): ours by display, ours by run id, not ours
mkdir -p "$DV/o/run"; echo "2026-11-27 00:00:00.000Z boot 0: run_id 777 engine_s 465 load 0 0 0" > "$DV/o/run/t2_xray.log"
alive=$( { env DISPLAY=:4242 bash -c "exec -a fakewine bash -c 'sleep 20; :'" >/dev/null & p1=$!; env DISPLAY=:4243 bash -c "exec -a fakewine bash -c 'sleep 20; :' x -t2_run 777" >/dev/null & p2=$!
  env DISPLAY=:4243 bash -c "exec -a fakewine bash -c 'sleep 20; :' x -t2_run 7770" >/dev/null & p3=$!
  sleep 0.3; fx 'stop_run'; echo "sr $?" >> "$DV/o/sr"
  for x in $p1 $p2 $p3; do kill -0 $x 2>/dev/null && printf 1 || printf 0; done; kill $p3; wait $p1 $p2 $p3; } 2>/dev/null )   # job notices dropped
[ "$alive" = 001 ]; dok "stop_run: kills our display's and our run id's process, leaves the other (alive $alive)" $?
# pass 2 #5: the launcher is stopped BEFORE it can start an engine; an engine that appears after the first scan is caught
# by the rescan; the claim is given back only after both, and kept if something of this run cannot be stopped
mkdir -p "$DV/out" "$DV/run/appdata"; echo pristine > "$DV/out/user.ltx.orig"; echo changed > "$DV/run/appdata/user.ltx"
setq; mkdir -p "$DV/o_l1/run"; cp "$DV/o/run/t2_xray.log" "$DV/o_l1/run/"; echo "claim-id: c5.5.5" > "$DV/o_l1/claim_calls.txt"
fx 'O="$DV/o_l1"; env BENCH_DISPLAY=:4242 "$DV/fk/tools/tb_run.sh" 1.0 -t2_run 777 >/dev/null 2>&1 & sleep 0.2; LAUNCHED=1; CLAIMTRY=1; finish'; sleep 1.3
n=$(fakes | while read -r x; do tr '\0' '\n' 2>/dev/null < /proc/$x/environ | grep -qx 'BENCH_DISPLAY=:4242' && echo $x; done | grep -c .)
lk=$(grep -n 'LAUNCHER of this run' "$DV/o_l1/preflight.log" | head -1 | cut -d: -f1); rl=$(grep -n 'released claim c5.5.5' "$DV/o_l1/preflight.log" | cut -d: -f1)
[ "$n" = 0 ] && [ -n "$lk" ] && [ -n "$rl" ] && [ "$lk" -lt "$rl" ] && [ "$(q)" = "release engineperf c5.5.5|" ]; dok "launcher killed before its engine start, no engine of this run 1.3 s later, released after (launcher line $lk < release line $rl)" $?
grep -q 'tb_run pid' "$DV/o_l1/.restore_user_ltx.done" 2>/dev/null && cmp -s "$DV/out/user.ltx.orig" "$DV/run/appdata/user.ltx"; dok "launcher KILL: marker written first, user.ltx put back from it" $?
# pass 3 #5: the watchdog wrote the marker (killed tb_run) and was stopped before it put user.ltx back: finish does it
echo changed > "$DV/run/appdata/user.ltx"; mkdir -p "$DV/o_r1"
fx 'O="$DV/o_r1"; bash -c "echo killed > $DV/o_r1/.restore_user_ltx; sleep 30" & WD=$!; sleep 0.3; finish'
cmp -s "$DV/out/user.ltx.orig" "$DV/run/appdata/user.ltx" && [ -e "$DV/o_r1/.restore_user_ltx.done" ] && [ ! -e "$DV/o_r1/.restore_user_ltx" ]; dok "marker left by a stopped watchdog: parent finish puts user.ltx back" $?
# pass 3 #1: this run's Xvfb that takes 0.5 s to exit is waited for; one that cannot be stopped keeps the claim
setq; mkdir -p "$DV/o_x1/run" "$DV/o_x2/run"; echo "claim-id: c7.7.7" > "$DV/o_x2/claim_calls.txt"
fx 'O="$DV/o_x1"; env DISPLAY=:4242 bash -c "exec -a fakexvfb bash -c '"'"'trap \"sleep 0.5; exit 0\" TERM; while :; do sleep 0.1; done'"'"'" >/dev/null 2>&1 & sleep 0.3; stop_run'; a=$?
n=$(for d in /proc/[0-9]*; do [ "$(tr '\0' '\n' 2>/dev/null < $d/cmdline | head -1)" = fakexvfb ] && echo x; done | grep -c .)
[ $a = 0 ] && [ "$n" = 0 ]; dok "stop_run waits for this run's Xvfb (exits 0.5 s after TERM): none left, rc $a" $?
env DISPLAY=:4242 bash -c "exec -a fakexvfb bash -c 'while :; do sleep 0.1; done'" >/dev/null 2>&1 & px=$!; sleep 0.3
fx "O=\"\$DV/o_x2\"; STUCK=$px; kill() { local a o=(); for a in \"\$@\"; do [ \"\$a\" = \"\$STUCK\" ] || o+=(\"\$a\"); done; builtin kill \"\${o[@]}\"; }; LAUNCHED=1; CLAIMTRY=1; finish"
k=0; kill -0 $px 2>/dev/null && k=1; { kill -KILL $px; wait $px; } 2>/dev/null
[ $k = 1 ] && [ ! -s "$DV/qr.log" ] && grep -q 'LEFT ALIVE.*'"$px" "$DV/o_x2/preflight.log" && grep -q 'CLAIM KEPT' "$DV/o_x2/preflight.log"; dok "an Xvfb of this run that cannot be stopped: LEFT ALIVE, claim kept, nothing released" $?
# pass 3 #2: a second kj4p driver cannot take the lock while the first holds it; it can once the first is gone
fxb 'take_lock && { echo got > "$DV/lk1"; sleep 2; }'; sleep 0.5
fx 'take_lock'; a=$?; grep -q 'REFUSED: another kj4p driver holds' "$DV/fx.log"; b=$?; wait $FXP; fx 'take_lock'; c=$?
[ -e "$DV/lk1" ] && [ $a = 1 ] && [ $b = 0 ] && [ $c = 0 ]; dok "driver lock: second instance refused by name while the first holds it ($a), taken after ($c)" $?
setq; mkdir -p "$DV/o_l2/run"; cp "$DV/o/run/t2_xray.log" "$DV/o_l2/run/"
fx 'O="$DV/o_l2"; ( sleep 0.1; exec env BENCH_DISPLAY=:4242 bash -c "exec -a fakewine sleep 30" ) >/dev/null 2>&1 & LAUNCHED=1; finish'; sleep 0.2
n=$(fakes | while read -r x; do tr '\0' '\n' 2>/dev/null < /proc/$x/environ | grep -qx 'BENCH_DISPLAY=:4242' && echo $x; done | grep -c .)
[ "$n" = 0 ] && grep -q 'WINE processes of this run' "$DV/o_l2/preflight.log"; dok "an engine of this run that appears after the first scan is caught by the rescan" $?
setq; mkdir -p "$DV/o_l3"; echo "claim-id: c6.6.6" > "$DV/o_l3/claim_calls.txt"
fx 'O="$DV/o_l3"; stop_run() { return 1; }; LAUNCHED=1; CLAIMTRY=1; finish'; [ ! -s "$DV/qr.log" ] && grep -q 'CLAIM KEPT' "$DV/o_l3/preflight.log"; dok "a process of this run left alive: the claim is kept, not released" $?
# watchdog: playing / check failed -> marker, runner TERMed, stop; clear -> go on
for code in 0 2; do echo $code > "$DV/tcode"; sleep 20 & xp=$!
  mkdir -p "$DV/o_w$code"; fx "O=\"\$DV/o_w$code\"; XP=$xp; watch_once"; a=$?; k=0; kill -0 $xp 2>/dev/null && k=1; kill $xp 2>/dev/null; wait $xp 2>/dev/null
  [ $a = 1 ] && [ -e "$DV/o_w$code/.title_stop" ] && [ $k = 0 ]; dok "watch_once title rc $code: stop marker, runner TERMed and gone before the scan" $?; done
echo 1 > "$DV/tcode"; mkdir -p "$DV/o_wc"; fx 'O="$DV/o_wc"; watch_once'; a=$?; [ $a = 0 ] && [ ! -e "$DV/o_wc/.title_stop" ]; dok "watch_once clear: goes on, no marker" $?
# source guard and the order of the main body (static: line numbers in the file)
r=$(t env KJ4P_FUNCS_ONLY=1 bash "$HERE/kj4p_run.sh" "$DV/should_not_exist"); [ "$r" = 2 ] && [ ! -e "$DV/should_not_exist" ]; dok "KJ4P_FUNCS_ONLY=1 run as a program: refused, nothing made" $?
ln_() { grep -n -F -- "$1" "$HERE/kj4p_run.sh" | grep -v '^[0-9]*: *#' | tail -1 | cut -d: -f1; }
L1=$(ln_ 'make_out || exit 2'); L2=$(ln_ 'trap finish EXIT; trap on_sig TERM INT'); L3=$(ln_ 'acquire || {'); L4=$(ln_ 'pins_ok >> "$O/preflight.log"')
L5=$(ln_ 'SESS1=$(utc); LAUNCHED=1'); L6=$(ln_ 'python3 -I tools/t2_kill_pose.py'); L7=$(ln_ 'tools/t2_xray_kj4.sh "$O/run"')
[ -n "$L1" ] && [ -n "$L7" ] && [ "$L1" -lt "$L2" ] && [ "$L2" -lt "$L3" ] && [ "$L3" -lt "$L4" ] && [ "$L4" -lt "$L5" ] && [ "$L5" -lt "$L6" ] && [ "$L6" -lt "$L7" ]
dok "main body order: out dir ($L1) < traps ($L2) < claim ($L3) < pins again ($L4) < launch ($L5-$L7)" $?
kk=$(grep -o -- 'kill -KILL [^ ;]*' "$HERE/kj4p_run.sh" | LC_ALL=C sort -u | tr '\n' ' ')
[ "$kk" = 'kill -KILL "$XP" kill -KILL $PW kill -KILL $e kill -KILL $l kill -KILL $x ' ] && ! grep -v '^ *#' "$HERE/kj4p_run.sh" | grep -q 'wineserver -k'
dok "kill -KILL only on this run's own lists / children ($kk); no wineserver -k" $?
# kj4p: the REAL engine run of kj3p r1 (2026-11-27, kill6 reader, read in place, never written): the checker must give
# the verdict of record on real engine bytes: FAIL with exactly these failing checks (the psi half degraded by the
# kill6 `M == sm` fault). A missing folder fails by name.
REAL=$P/out/kj3p-r1/run; rbad=""
if [ -d "$REAL/boot0" ]; then
  ro=$(python3 -I "$HERE/kj4p_check.py" "$REAL" 2>&1); rr=$?
  [ "$rr" = 1 ] && grep -qx 'verdict: failing checks 3p, 4p, 4n, 5p, 6p, 7p, 9, 11' <<<"$ro" && grep -qx 'KJ4P: FAIL' <<<"$ro" \
    && echo "ok   real kj3p-r1 run folder: FAIL 3p 4p 4n 5p 6p 7p 9 11 (as scored on 2026-11-27)" \
    || { echo "BAD  real kj3p-r1 run folder: rc $rr, $(grep '^verdict' <<<"$ro" | head -1)"; rbad=1; }
else echo "BAD  real_fixture_missing: $REAL/boot0"; rbad=1; fi
echo "driver offline tests: $nd passed${dbad:+, BAD:$dbad}; score states: $ns passed${sbad:+, BAD:$sbad}"
fin=PASS; [ "$pyrc" = 0 ] && [ -z "$sbad" ] && [ -z "$dbad" ] && [ -z "$rbad" ] || fin=FAIL
echo "end $(date '+%F %T')"
echo "KJ4P SELFTEST: $fin (checker part rc $pyrc; score-once states $ns/$((ns + nsb)); driver functions offline $nd/$((nd + ndb)))"
[ "$fin" = PASS ]
