#!/usr/bin/env python3
"""em1: joint emission witness + one monster kill (alive near zero, held, then killed), X-RAY SIDE checker (em1/PREREG-em1-joint-emission-witness-DRAFT.md).
DRAFT, NOT REGISTERED. Stdlib only, run with python3 -I:
  em1_check.py <run dir>
<run dir> = the folder the X runner (em1/t2_xray_em1.sh, launched by itrzone's boot script) was given. Required, by name:
  t2_xray.log  restarts.csv  emforce.csv  boot0/meta_t2.csv  boot0/hits_t2.csv (may be empty: no hit)
  boot0/tb/xray.log  boot0/watch/snaps_all.txt  boot0/watch/poses_all.txt   (tools/t1_watch_keep.py)
A missing, unreadable or (except hits_t2.csv) empty required file FAILS BY NAME (kj2's principle; kj4p refused instead):
`MISSING|EMPTY|UNREADABLE <name> (<path>)`, no check is computed, verdict FAIL, exit 1. Nothing is ever written by this
program: it prints only (score once with stdout into a no-clobber file, as kj4p_score.sh). CR is stripped everywhere.
Output: one line per check `<id> <text>: PASS|FAIL (<values>)`, the counter rows `k14.<key> ...`, one line per NO VERDICT
rule `<nv id> <text>: FIRED|clear (<values>)`, `reported:` lines, then `EM1 X SIDE: PASS|FAIL|NO VERDICT`.
Exit 0 PASS, 1 FAIL, 3 NO VERDICT, 2 usage.
Verdict: missing or unparsable input -> FAIL. Else if NO VERDICT rules fired: NO VERDICT only if EVERY failing check lies in
the union of the fired rules' untested sets (NV_UNTESTED); a failing check outside it -> FAIL, named. Else PASS only if
every check passes. Always judged (in no untested set): 1, 2, 4n, 10, 11, 12, 13, ka, k13, k15; k2 too once M has a KAPPLY.

Made from two registered checkers, parsing code reused:
  * kj4/kj4p_check.py (b0b6d70f..., the emission checks 1..13, NV1..NV4 -> here nve1..nve4; NV5 dropped), with these changes only:
    1: play end bound from RUN_S (below); the clause "no kept pose with a k line" removed (em1 has k lines); new clause
       "no stall.csv" (em1 has no stop: T2_STALL_PLAY=99999).
    9: vortex height +/-0.50 m and distance <= 151.5 m against the actor of the snapshot of the vortex's FIRST v line;
       a vortex whose first v line follows a kept-snapshot gap > 250 ms (X clock) is reported, not judged, for height and
       distance; at least 10 of every 12 vortices must be judged (plan-facts/joint-emission-witness-carried-bounds-
       2026-11-27.md, last section).
    11: the kill-channel T2SUM values moved to k14; 11 keeps the key list (20, eerr last) and the non-kill zeros.
    13: snapshot layout `a o* r? e (w|v)* ns k*` (kill7 writes k lines after ns: zz_p1_puppet_kill7.script export_b1).
  * kj2/t3_kj2_check.py (cf69899b..., kill checks 1..15, NV1..NV6) restricted to ONE monster M, no stalker, no stop:
    kept k2 k3 k4 k6 k7 (for M) k8 k11 k13 k14 k15; new ka (every KAPPLY's arithmetic, ALWAYS judged), kh (the
    "alive near zero, held, then killed" witness: itrzone's split death, kcjh=2: a dead = 0 pose with total full - kcjp at play 50
    holds M alive at the 0.0100 floor of kill7:476, then the dead = 1 pose at play 52), kd (M's kill call at play 52),
    kc (KCONF 0..2000 ms after the kill call; 0 ms PASSES: the pending path is NOT forced live, it stays proven offline
    by the kill7h stub tests and by the guard KNOCONF at 2000 ms; pending snapshots are reported only).
    dropped 5 (stalker healing), 9 and 10 (the stop), 12 (actor health: check 10 here is stricter: 1.0000 everywhere).
    NV -> nvk1 (one monster, no stalker), nvk2 (not for a death the bridge caused), nvk3 (kj2's death-step exemption),
    nvk6; NV4 dropped (no stop). k2 leaves every untested set once M has a KAPPLY (round-2 review fix B2).
    nve5 (kj4p NV5, no vortex) DROPPED: it could never decide a verdict (a storm cut short already fails 1/6p); no v line
    is a FAIL of 9.

CLOCKS. X = time_global() ms (snapshot header field 3, E*/K* field 2, P2 field 4). W = Linux realtime ms (watcher
#snap/#pose stamps, emforce.csv, the runner's play0 = the header utc of the first play pose it read: t2_xray.log line
`first play pose read: seq,<n>,<utc>,play,...`). The two cross-clock places (check 2's EFORCE delay and kd's play time of
the kill call) map X to W by kj4p's rule: W(T) = T - min(P2 now - P2 pose utc) over the P2 lines of the 10 s of X before T
(early by <= the freshest pose age + drift, ~70 ms; kj4p's docstring).
"""
import bisect, math, os, re, sys

# ================================================================ registered plan and bounds (derivations: the draft
# prereg's margin table, one row per bound; each is repeated here in a word)
RUN_S = 445                  # itrzone's choice 2026-11-27: option A (T2_RUN_S 445, T2_WALL_S 1300, T2_OVER_S 120)
WARMUP_S = 15                # play end = RUN_S - 15 s: kj2 240 -> 224.96, kj3p 465 -> 449.7, kj4p 465 -> 449.3 (offsets 15.0..15.7)
END_SLACK_S = 5              # 7x the largest measured deviation of the offset (0.7 s); above the psi storm's latest end 419
END_PLAY_S = RUN_S - WARMUP_S - END_SLACK_S   # = 425
SURGE_PLAY_S, PSI_PLAY_S = 70, 310   # the partner's plan (T2_SURGE_PLAY, T2_PSI_PLAY)
RUNNER_LATE_MS = 2500        # runner loop = sleep 1 + body (measured 807..945 ms late in kj3p/kj4p)
EF_EARLY_MS, EF_LATE_MS = 250, 2500   # EFORCE after the append: one read per 1000 ms X + frame; X->W map early <= ~70 ms
P2_WIN_MS = 10000
START_MS, START_T = 1000, 1.0
TF_EVENT, TF_NONE = 10.0, 6.0
CLOCK_SPAN = 0.5
END_T = {'surge': (220.5, 223.0), 'psi': (101.5, 104.0)}
SURGE_X = [0, 30, 47, 50, 80, 108, 120, 156, 168, 200, 212, 220, 222]   # kj3/emission-phases.md (independent copy)
PSI_X = [0, 10, 12, 30, 78, 103]
PH_REQ = {'surge': 12, 'psi': 5}
PH_LAG = 1.0
WAVE_T0 = {1: 120, 2: 168}
WAVE_LAG = 1.0
WAVE_V = (-16.5, -13.5)
WAVE_Z0 = (244.0, 250.5)     # actor z + 250 at creation, <= 2 steps of 3 m seen late; moving actor adds <= 0.07 m
V_START = (33, 77)
V_GAP = (3, 16)
V_S_FIRST = (19.0, 21.005)
V_S_LAST = (-1.0, 1.5)
V_INT = 0.02
V_DY = 0.50                  # 2.06x the 0.243 m the moving actor's y changed within 250 ms (kj2 real, t2span=4)
V_R = 151.5                  # vortex_distance_max 150 + 2 x 0.715 m (largest horizontal move within 250 ms, kj2 real)
V_GAP_MS = 250               # two p99 snapshot gaps (137 ms): a vortex first seen after a longer gap is not judged (y, r)
V_JUDGED = (10, 12)          # at least 10 of every 12 vortices judged for y and r (judged * 12 >= 10 * n)
T2SUM_KEYS = ['last_seq', 'torn', 'holds', 'paused', 'near', 'guards', 'lost', 'epochs', 'khits', 'kdead_online',
              'kdead_offline', 'kowed', 'kstale', 'kback', 'krefused', 'kclamped', 'kver', 'kdead_fail', 'ktabfail', 'eerr']
C11_ZERO = ['torn', 'lost', 'epochs', 'ktabfail', 'eerr']   # not kill-channel values: judged in 11 (always) and in k14
LEVEL = 'l01_escape'
LUA_BRIDGE = 'zz_p1_puppet'
EV_SCRIPTS = ('surge_manager', 'psi_storm_manager')
REF_KJ2 = ('kj2 X run /home/caden/Projects/ITRZone/out/runs/20261126-172548-t2kj2-xray (kill5, no e lines, 240 s): '
           'export_mean_us=795.8 export_p99_us=1558.1 export_max_us=15920.6, 1949 ticks')
# ---- kill channel (kj2's bounds, re-derived for one monster; prereg margin table)
M_HIT_PLAY, M_HOLD_PLAY, M_KILL_PLAY = 30, 50, 52   # itrzone's levers kcj=30, kcjd=20, kcjh=2 (kcjo=1: monster only)
KILL_PLAY_TOL_S = 5.0        # kj2 real: rig events at plan - 1.04..-1.09 s on this play clock (3 events); 4.6x
HOLD_FLOOR = 0.0100          # kill7:476: a dead = 0 total holds health at max(hb - d, min(hb, 0.01))
HOLD_LO, HOLD_HI = 0.0098, 0.0300   # M's health in the hold: 0.0100 - 0.0002 (print + rounding) .. 2.0x the expected top
                                    # 0.0149 (0.0100 + hb regen <= 0.0040 + <= 0.0009 per 2 s: kj2 flesh 23183, one run)
HOLD_MIN_SNAPS = 5           # expected ~18 snapshots in the 2.0 s hold (gap median 110 ms, kj2); 18 / 5 = 3.6x
KCONF_MAX_MS = 2000          # kill call -> KCONF: kill7's own KNOCONF limit; 0 ms is a PASS
TOL_H = 0.0002               # two 4-decimal roundings, doubled (kj2 row 2)
FALL = 0.02                  # a fall not caused by a hit: far over rounding + healing per snapshot, far under 0.3
CONFIRM_MS = 2000            # dead line within 2.0 s; = kill7's own KNOCONF limit
EARLY_MS = 50                # watcher read order (kj2)
DEAD_AFTER_MS = 2000
R_LAG_MS = 2000
NEAR_M = 10.0
KILLER = '0'
MINE_KDEAD = ('online', 'offline', 'failed')

KILL_ALL = {'k2', 'k3', 'k4', 'k6', 'kh', 'kd', 'kc', 'k7', 'k8', 'k11', 'k14'}   # ka: in none (ALWAYS)
NV_UNTESTED = {
    'nve1': {'3s', '3p', '4s', '4p', '5s', '5p', '6s', '6p', '7s', '7p', '8', '9'},   # an event the run did not force
    'nve2': {'3s', '4s', '5s', '6s', '7s', '8'},                                     # surge refused by the game
    'nve3': {'3p', '4p', '5p', '6p', '7p', '9'},                                     # psi storm refused by the game
    'nve4': {'3p', '4p', '5p', '6p', '7p', '9'},                                     # psi forced while the surge ran
    'nvk1': set(KILL_ALL),                       # not exactly one monster with k lines, or a stalker with k lines
    'nvk2': set(KILL_ALL),                       # M hurt by the world before its hit, or killed before its step
    'nvk3': set(KILL_ALL),                       # M fell with no K line of mine (except its death step)
    'nvk6': KILL_ALL - {'k2'},                   # M not listed at the first pose of its later step
}
REQ = ('t2_xray.log', 'restarts.csv', 'emforce.csv', 'boot0/meta_t2.csv', 'boot0/hits_t2.csv', 'boot0/tb/xray.log',
       'boot0/watch/snaps_all.txt', 'boot0/watch/poses_all.txt')
EMPTY_OK = {'boot0/hits_t2.csv'}
ORDER_ID = ['1', '2', '3s', '3p', '4s', '4p', '4n', '5s', '5p', '6s', '6p', '7s', '7p', '8', '9', '10', '11', '12', '13',
            'ka', 'k2', 'k3', 'k4', 'k6', 'kh', 'kd', 'kc', 'k7', 'k8', 'k11', 'k13', 'k14', 'k15']
ALWAYS = {'1', '2', '4n', '10', '11', '12', '13', 'ka', 'k13', 'k15'}
JUDGED_IF_KAPPLY = {'k2'}    # removed from every untested set once M has a KAPPLY (B2: a hit is the bridge's, not the world's)

_INT = re.compile(r'-?[0-9]+\Z')
_NUM = re.compile(r'-?[0-9]+(\.[0-9]+)?\Z')


def pint(s):
    if not _INT.match(s): raise ValueError(f'not an integer {s!r}')
    return int(s)


def fnum(s):
    if not _NUM.match(s): raise ValueError(f'not a number {s!r}')
    return float(s)


def read(path):
    """(lines, status): status None = ok, else 'missing' / 'unreadable: ..' / 'empty'."""
    if not os.path.isfile(path): return None, 'missing'
    try:
        with open(path, 'rb') as f: data = f.read()
    except OSError as e:
        return None, f'unreadable: {e.strerror or e}'
    lines = data.decode('utf-8', 'replace').replace('\r', '').split('\n')
    return lines, ('empty' if not data.strip() else None)


def records(lines, tag):
    recs, stray, cur = [], [], None
    for l in lines:
        if l.startswith('#' + tag + ','):
            f = l.split(','); cur = {'u': f[1], 'mseq': f[2] if len(f) > 2 else '', 'lines': [], 'mark': l}; recs.append(cur)
        elif l == '': continue
        elif cur is None: stray.append(l)
        else: cur['lines'].append(l)
    return recs, stray


def parse_snap(r):
    """kj4p's parser with kj2's o and k fields: o {id: sec kind x y z h alive}, k {id: (applied str, dead, word)}, dup."""
    L = r['lines']; h = L[0].split(',') if L else []
    if not (len(h) == 7 and h[0] == 'seq' and h[1] == r['mseq'] and L[-1] == f'end,{h[1]}' and h[6] == '4'):
        raise ValueError('header/end/version 4')
    s = {'u': pint(r['u']), 'seq': pint(h[1]), 'tx': pint(h[2]), 't': fnum(h[3]), 'level': h[4], 'run': h[5], 'a': None,
         'o': {}, 'r': None, 'e': [], 'w': [], 'v': [], 'ns': [], 'k': {}, 'dup': [], 'order': []}
    for l in L[1:-1]:
        f = l.split(','); k = f[0]; s['order'].append(k)
        if k == 'a' and len(f) == 6: fnum(f[5]); s['a'] = (fnum(f[1]), fnum(f[2]), fnum(f[3]), f[5])
        elif k == 'o' and len(f) == 12:
            o = {'sec': f[2], 'kind': f[3], 'x': fnum(f[4]), 'y': fnum(f[5]), 'z': fnum(f[6]), 'h': fnum(f[8]), 'alive': f[9]}
            if f[1] in s['o']: s['dup'].append(f'o line for id {f[1]} twice ({l!r} after the first)')   # the first is kept
            else: s['o'][f[1]] = o
        elif k == 'r' and len(f) == 3: s['r'] = (f[1], pint(f[2]))
        elif k == 'e' and len(f) == 5 and f[1] in ('none', 'surge', 'psi'):
            s['e'].append({'kind': f[1], 't': fnum(f[2]), 'ph': pint(f[3]), 'tf': f[4]})
        elif k == 'w' and len(f) == 3: s['w'].append((pint(f[1]), fnum(f[2])))
        elif k == 'v' and len(f) == 6: s['v'].append((pint(f[1]), fnum(f[2]), fnum(f[3]), fnum(f[4]), fnum(f[5])))
        elif k == 'ns' and len(f) == 3: s['ns'].append((pint(f[1]), pint(f[2])))
        elif k == 'k' and (len(f) == 3 or (len(f) == 4 and f[3] in ('dead', 'stale'))):
            fnum(f[2]); val = (f[2], len(f) == 4 and f[3] == 'dead', f[3] if len(f) == 4 else '')
            if f[1] in s['k']: s['dup'].append(f'k line for id {f[1]} twice ({l!r} after the first)')
            else: s['k'][f[1]] = val
        else: raise ValueError('line ' + l)
    return s


def parse_pose(r):
    """kj2's pose parser (k lines strict: k,<id>,<sec>,<total>,<0|1>) plus kj4p's p point."""
    L = r['lines']; h = L[0].split(',') if L else []
    if not (len(h) == 8 and h[0] == 'seq' and h[1] == r['mseq'] and L[-1] == f'end,{h[1]}' and h[7] == '4'):
        raise ValueError('pose header/end/version 4')
    p = {'u': pint(r['u']), 'seq': pint(h[1]), 'pu': pint(h[2]), 'state': h[3], 'sess': h[6], 'p': None, 'k': {}}
    for l in L[1:-1]:
        f = l.split(',')
        if f[0] == 'p' and len(f) == 7: p['p'] = (f[1], f[2], f[3])
        elif f[0] == 'k':
            if len(f) != 5 or f[4] not in ('0', '1'): raise ValueError('k line ' + l)
            p['k'][f[1]] = (f[2], f[3], fnum(f[3]), f[4])
    return p


META_SHAPE = {'EFORCE': 4, 'ESTART': 4, 'EPHASE': 5, 'EEND': 4, 'EERR': 3, 'ENOTREADY': 2, 'EREADY': 2,
              'KAPPLY': 9, 'KWHO': 4, 'KCONF': 4, 'KNOCONF': 4}


def parse_meta(lines):
    """kj4p's E* parser + kj2's K* parser in one pass; i = position in the meta file (execution order)."""
    ev = {k: [] for k in META_SHAPE}; ev['KDEAD'] = []; ev['KOTHER'] = []; bad = []
    other = {'P2': [], 'T2SUM': [], 'VITALS': [], 'SUMMARY': [], 'TICK': [], 'ON': [], 'OFF': [], 'S': []}
    for i, l in enumerate(lines):
        if not l: continue
        f = l.split(','); k = f[0]
        try:
            if k in META_SHAPE:
                if len(f) != META_SHAPE[k]: raise ValueError(f'{k} has {len(f)} fields, want {META_SHAPE[k]}')
                e = {'line': l, 'i': i, 'now': pint(f[1])}
                if k == 'EFORCE': e.update(what=f[2], res=f[3])
                elif k == 'ESTART': e.update(kind=f[2], t=fnum(f[3]))
                elif k == 'EPHASE': e.update(kind=f[2], ph=pint(f[3]), t=fnum(f[4]))
                elif k == 'EEND': e.update(kind=f[2], t=fnum(f[3]))
                elif k == 'EERR': e.update(msg=f[2])
                elif k == 'KAPPLY': e.update(n=pint(f[2]), id=f[3], d=fnum(f[4]), ap=fnum(f[5]), hb=fnum(f[6]), ha=fnum(f[7]), how=f[8])
                elif k == 'KWHO': e.update(id=f[2], killer=f[3])
                elif k in ('KCONF', 'KNOCONF'):
                    if not re.fullmatch(r'[0-9]+', f[2]): raise ValueError(f'{k} id {f[2]!r}')
                    e.update(id=f[2], ms=pint(f[3]))
                if k in ('ESTART', 'EPHASE', 'EEND') and e['kind'] not in ('surge', 'psi'): raise ValueError('kind ' + e['kind'])
                e['T'] = e['now']; ev[k].append(e)
            elif k == 'KDEAD':
                if len(f) < 5: raise ValueError(f'KDEAD has {len(f)} fields')
                ev['KDEAD'].append({'line': l, 'i': i, 'T': pint(f[1]), 'now': pint(f[1]), 'n': pint(f[2]), 'id': f[3], 'kind': f[4]})
            elif k in ('KOWED', 'KSTALE', 'KBACK', 'KREFUSED', 'KTAB'): ev['KOTHER'].append({'line': l, 'i': i})
            elif k == 'P2':
                if len(f) < 5: raise ValueError(f'P2 has {len(f)} fields')
                other['P2'].append((pint(f[3]), pint(f[2]))); ev.setdefault('P2x', []).append({'i': i, 'n': pint(f[1]), 'T': pint(f[3])})
            elif k in other: other[k].append(l)
        except ValueError as x:
            bad.append(f'meta line {i + 1}: {x} ({l[:80]!r})')
    ev.setdefault('P2x', [])
    return ev, other, bad


def kv(line):
    d = {}
    for p in line.split(',')[1:]:
        if '=' in p: a, b = p.split('=', 1); d[a] = b
    return d


def phase_of(tab, t):
    d = math.ceil(t); p = 0
    for i, x in enumerate(tab):
        if d >= x: p = i + 1
    return p


def phases_ok(tab, t, ph):
    return ph in (phase_of(tab, t - 0.005), phase_of(tab, t + 0.005))


_FRAME = re.compile(r'^\s*\.\.\. .*\(line: \d+\) in function')


def noise_frames(xlog):
    """kj4p's: indexes of the traceback FRAME lines of axr_main's callback-registration diagnostics (see kj4p_check.py)."""
    out, blk, seps, traced = set(), False, 0, False
    for i, l in enumerate(xlog):
        if l.startswith('![axr_main callback_set]') and ("doesn't exist" in l or 'nil function' in l):
            blk, seps, traced = True, 0, False; continue
        if not blk: continue
        if l.startswith('~ -'):
            seps += 1
            if seps >= 2: blk = False
            continue
        if l == '~ STACK TRACEBACK:':
            if traced: blk = False; continue
            traced = True; continue
        if l.strip() == '': continue
        if seps == 1 and _FRAME.match(l): out.add(i); continue
        blk = False
    return out


def script_errs(xlog, name):
    pat = re.compile(re.escape(name) + r'\.script(\(\d| \(line|:\d)'); nz = noise_frames(xlog)
    return [xlog[i] for i in range(len(xlog)) if pat.search(xlog[i]) and i not in nz]


def _tf_is(s, want):
    try: return abs(float(s) - want) < 1e-6
    except ValueError: return False


# ================================================================ kj2's counter table (check k14), em1 expectations
COUNTERS = [
    ('last_seq', ('last_seq',), 'seq of the last pose taken: between the r seq of the final kept snapshot and the highest kept pose seq'),
    ('torn', ('eq', '0'), 'torn pose reads'),
    ('holds', ('lines', ('HOLD',)), 'must equal the HOLD lines'),
    ('paused', ('eq', 'false'), 'no -t2_pause'),
    ('near', ('lines', ('NEAR', 'P2ring')), 'must equal NEAR lines + P2 lines with ring > 0'),
    ('guards', ('lines', ('GUARD',)), 'must equal the GUARD lines'),
    ('lost', ('eq', '0'), 'no pose LOST event'),
    ('epochs', ('eq', '0'), 'no new ITR2 session'),
    ('khits', ('khits',), 'part hits applied = distinct (id, total) rises in the kept poses (em1: M hit 0.300 + the lethal rise = 2)'),
    ('kdead_online', ('eq', '1'), 'em1: M only, killed online once'),
    ('kdead_offline', ('eq', '0'), 'no offline creature'),
    ('kowed', ('eq', '0'), 'no owed part hit'),
    ('kstale', ('eq', '0'), 'no stale row'),
    ('kback', ('eq', '0'), 'no falling total'),
    ('krefused', ('eq', '0'), 'nothing refused (itrzone sends no actor line)'),
    ('kclamped', ('eq', '0'), 'no single difference above 1.0 (M: 0.3, then <= 0.7)'),
    ('kver', ('eq', '0'), 'no pose of a wrong version'),
    ('kdead_fail', ('eq', '0'), 'no failed kill: the pending kill is confirmed within 2.0 s'),
    ('ktabfail', ('eq', '0'), 'no failed table save'),
    ('eerr', ('eq', '0'), 'emission reader errors (kill7, last key)'),
    ('log.KCONF', ('log', 'KCONF', 1), 'exactly 1 (M), after its KDEAD online, ms = its time - the KDEAD time, 0 <= ms <= 2000'),
    ('log.KNOCONF', ('log', 'KNOCONF', 0), 'none'),
]
COUNTER_KEYS = [k for k, kind, _ in COUNTERS if kind[0] != 'log']


def log_rows(ev, name, want):
    kd = {}
    for k in ev['KDEAD']:
        if k['kind'] == 'online': kd.setdefault(k['id'], k['T'])
    bad, ids = [], []
    for e in ev[name]:
        ids.append(e['id'])
        if name == 'KCONF':
            if e['id'] not in kd or kd[e['id']] > e['T']: bad.append(f'{e["line"]}: no earlier KDEAD online for {e["id"]}')
            elif e['ms'] != e['T'] - kd[e['id']] or not 0 <= e['ms'] <= CONFIRM_MS:
                bad.append(f'{e["line"]}: ms {e["ms"]}, KDEAD at {kd[e["id"]]} (expected {e["T"] - kd[e["id"]]}, 0..{CONFIRM_MS})')
    if name == 'KCONF':
        dup = sorted({i for i in ids if ids.count(i) > 1}); miss = sorted(set(kd) - set(ids))
        if dup: bad.append(f'more than one KCONF for {dup}')
        if miss: bad.append(f'KDEAD online without KCONF: {miss}')
    n = len(ev[name])
    return n, n == want and not bad, f'{n} {name} line(s); problems: {bad[:3] or "none"}'


def meta_line_counts(meta):
    c = {'HOLD': 0, 'NEAR': 0, 'GUARD': 0, 'P2ring': 0}
    for l in meta:
        f = l.split(',')
        if f[0] in c: c[f[0]] += 1
        if f[0] == 'P2' and len(f) > 12:
            try:
                if int(f[12]) > 0: c['P2ring'] += 1
            except ValueError: pass
    return c


def khits_expect(poses):
    seen = {}
    for p in poses:
        for i, (_, ts, t, _) in p['k'].items():
            st = seen.setdefault(i, [])
            prev = st[-1][1] if st else 0.0
            if t > prev + 1e-9: st.append((ts, t))
    n = sum(len(v) for v in seen.values())
    der = '; '.join(f'id {i}: ' + ', '.join(ts for ts, _ in v) for i, v in sorted(seen.items())) or 'no k lines'
    return n, f'{n} = {der}'


def last_seq_expect(poses, snaps):
    hi = max((p['seq'] for p in poses), default=None)
    fin = snaps[-1] if snaps else None
    lo = fin['r'][1] if fin and fin['r'] else None
    return lo, hi, (f'r seq of the final kept snapshot (seq {fin["seq"] if fin else None}): '
                    f'{lo if lo is not None else "NONE: no r line in the final snapshot, the bound FAILS"}; highest kept pose seq {hi}')


def counter_table(t2_lines, meta, ev, ctx):
    if not t2_lines: return False, [], 'no T2SUM line in the meta file'
    pairs, junk = [], []
    for p in t2_lines[-1].split(',')[1:]:
        if '=' in p: a, b = p.split('=', 1); pairs.append((a, b))
        else: junk.append(p)
    have, dup = {}, []
    for k, v in pairs:
        if k in have: dup.append(k)
        have[k] = v
    lc = meta_line_counts(meta); rows, bad = [], []
    for key, kind, why in COUNTERS:
        if kind[0] == 'log':
            n, st, det = log_rows(ev, kind[1], kind[2])
            rows.append(f'k14.{key} lines = {n}, expected {kind[2]} ({det}): {"PASS" if st else "FAIL"} ({why})')
            if not st: bad.append(key)
            continue
        if key not in have:
            rows.append(f'k14.{key} absent from the last T2SUM: FAIL ({why})'); bad.append(key); continue
        v = have[key]
        if kind[0] == 'eq': exp = kind[1]; st = v == exp
        elif kind[0] == 'lines':
            n = sum(lc[x] for x in kind[1]); exp = f'{n} (' + ' + '.join(f'{lc[x]} {x}' for x in kind[1]) + ')'; st = v == str(n)
        elif kind[0] == 'khits':
            n, der = ctx['khits']; exp = f'{n} ({der})'; st = v == str(n)
        else:
            lo, hi, der = ctx['last_seq']; exp = f'{lo} .. {hi} ({der})'
            st = bool(re.fullmatch(r'[0-9]+', v)) and lo is not None and hi is not None and lo <= int(v) <= hi
        rows.append(f'k14.{key} = {v}, expected {exp}: {"PASS" if st else "FAIL"} ({why})')
        if not st: bad.append(key)
    for k, v in pairs:
        if k not in COUNTER_KEYS: rows.append(f'k14.{k} = {v}, expected nothing: FAIL (no expectation for this key)'); bad.append(k)
    for k in sorted(set(dup)): rows.append(f'k14.{k} appears twice in the last T2SUM: FAIL'); bad.append(k)
    for j in junk: rows.append(f'k14.<{j}> field without key=value: FAIL'); bad.append(f'<{j}>')
    return not bad, rows, f'{len(t2_lines)} T2SUM line(s), last one judged ({len(pairs)} keys) plus the KCONF/KNOCONF rows; failing: {", ".join(bad) or "none"}'


# ================================================================ the checker
def full(run_dir):
    out, unparsed = [f'EM1 X side checker (emission witness + one monster kill: alive near zero, held, then killed), run dir {run_dir}'], []
    data, gone = {}, []
    for n in REQ:
        L, st = read(os.path.join(run_dir, n))
        if st == 'empty' and n in EMPTY_OK: st = None
        if st: gone.append(f'{st.split(":")[0].upper()} {n} ({os.path.join(run_dir, n)}{": " + st.split(": ", 1)[1] if ":" in st else ""})')
        data[n] = L
    if gone:
        return out + gone + ['verdict: a required input file is missing, empty or unreadable (named above); no check computed',
                             'EM1 X SIDE: FAIL'], 1

    rlog = [l for l in data['t2_xray.log'] if l.strip()]
    restarts = [l for l in data['restarts.csv'][1:] if l.strip()]
    xlog = data['boot0/tb/xray.log']
    hits = [l for l in data['boot0/hits_t2.csv'] if l.strip()]
    ef_rows = []
    efL = [l for l in data['emforce.csv'] if l.strip()]
    if not efL or efL[0] != 'utc_ms,wall_utc,kind,ms_after_play0':
        unparsed.append(f'emforce.csv header {efL[0] if efL else None!r}')
    else:
        for l in efL[1:]:
            f = l.split(',')
            try:
                if len(f) != 4 or f[2] not in ('surge', 'psi'): raise ValueError('fields')
                ef_rows.append({'u': pint(f[0]), 'kind': f[2], 'el': pint(f[3])})
            except ValueError: unparsed.append(f'emforce.csv row {l!r}')
    meta = [l for l in data['boot0/meta_t2.csv'] if l]
    ev, other, bad = parse_meta(meta); unparsed += bad
    srecs, sstray = records(data['boot0/watch/snaps_all.txt'], 'snap'); snaps = []
    for r in srecs:
        try: snaps.append(parse_snap(r))
        except (ValueError, IndexError) as e: unparsed.append(f'{r["mark"]}: {e}')
    unparsed += [f'stray snapshot line: {s[:60]}' for s in sstray[:3]]
    precs, pstray = records(data['boot0/watch/poses_all.txt'], 'pose'); poses = []
    for r in precs:
        try: poses.append(parse_pose(r))
        except (ValueError, IndexError) as e: unparsed.append(f'{r["mark"]}: {e}')
    unparsed += [f'stray pose line: {s[:60]}' for s in pstray[:3]]
    if not snaps: unparsed.append('no kept snapshot')
    txs = [s['tx'] for s in snaps]
    if any(b < a for a, b in zip(txs, txs[1:])): unparsed.append('snaps_all.txt: t_xray_ms goes back')
    if any(b['u'] < a['u'] for a, b in zip(snaps, snaps[1:])): unparsed.append('snaps_all.txt: read times go back')
    play0 = None
    for l in rlog:
        m = re.search(r'first play pose read: seq,\d+,(\d+),play,', l)
        if m: play0 = int(m.group(1)); break

    res = {}
    def add(cid, text, fn):
        try: ok, nums = fn()
        except Exception as e: ok, nums = False, f'error while judging: {e!r}'
        res[cid] = (text, bool(ok), nums)

    ef = {k: [e for e in ev['EFORCE'] if e['what'] == k] for k in ('surge', 'psi')}
    st = {k: [e for e in ev['ESTART'] if e['kind'] == k] for k in ('surge', 'psi')}
    en = {k: [e for e in ev['EEND'] if e['kind'] == k] for k in ('surge', 'psi')}
    esn = {k: [s for s in snaps if s['e'] and s['e'][0]['kind'] == k] for k in ('surge', 'psi')}
    started = {k: bool(ef[k]) and ef[k][0]['res'] == 'started' for k in ('surge', 'psi')}
    p2 = other['P2']
    def w_of(T):   # X -> W (kj4p's map; CLOCKS)
        offs = [now - pu for now, pu in p2 if T - P2_WIN_MS <= now <= T]
        return None if not offs else T - min(offs)

    # =========================================================== emission checks (kj4p's, changes named in the header)
    def c1():
        lua_b = script_errs(xlog, LUA_BRIDGE)
        fatal = [l for l in xlog if 'FATAL' in l]
        rc0 = bool(rlog) and rlog[-1].rstrip().endswith('rc=0')
        boots = sorted(d for d in os.listdir(run_dir) if re.match(r'boot\d+$', d))
        lv = sorted({s['level'] for s in snaps}); runs = sorted({s['run'] for s in snaps})
        stall = os.path.lexists(os.path.join(run_dir, 'stall.csv'))
        span = (snaps[-1]['u'] - play0) / 1000 if snaps and play0 else None
        ok = (rc0 and not restarts and not lua_b and not fatal and boots == ['boot0'] and lv == [LEVEL] and len(runs) == 1
              and not stall and play0 is not None and span is not None and span >= END_PLAY_S)
        return ok, (f'runner rc0 {rc0}, restart rows {len(restarts)}, boots {boots}, FATAL {len(fatal)}, Lua errors naming '
                    f'{LUA_BRIDGE} {len(lua_b)} {lua_b[:1]}, levels {lv}, run ids {len(runs)}, stall.csv present {stall} (want none: no stop), '
                    f'play0 {play0}, last snapshot at play {span if span is None else round(span, 1)} s (want >= {END_PLAY_S} = RUN_S {RUN_S} - {WARMUP_S} - {END_SLACK_S})')
    add('1', 'one boot ran to the end on Cordon: rc 0, no restart, no FATAL, no bridge Lua error, no stop, play >= end', c1)

    def c2():
        nums, ok = [], True
        kinds = [r['kind'] for r in ef_rows]
        if kinds != ['surge', 'psi']: ok = False; nums.append(f'emforce rows {kinds} (want [surge, psi])')
        allef = [(e['what'], e['res']) for e in ev['EFORCE']]
        if [w for w, _ in allef] != ['surge', 'psi']: ok = False; nums.append(f'EFORCE records {allef} (want surge then psi, one each)')
        lua_e = [l for x in EV_SCRIPTS for l in script_errs(xlog, x)]
        if lua_e: ok = False
        nums.append(f'Lua errors naming {"/".join(EV_SCRIPTS)} {len(lua_e)} {lua_e[:1]}')
        for k, plan in (('surge', SURGE_PLAY_S), ('psi', PSI_PLAY_S)):
            rows = [r for r in ef_rows if r['kind'] == k]
            if len(ef[k]) != 1: ok = False; nums.append(f'{k}: EFORCE lines {len(ef[k])} (want 1)'); continue
            e = ef[k][0]
            if e['res'] not in ('started', 'notstarted'): ok = False
            if rows:
                r = rows[0]
                if not (plan * 1000 <= r['el'] <= plan * 1000 + RUNNER_LATE_MS): ok = False
                W = w_of(e['now']); d = None if W is None else W - r['u']
                if d is None or not (-EF_EARLY_MS <= d <= EF_LATE_MS): ok = False
                nums.append(f'{k}: runner appended at play {r["el"]} ms (want {plan * 1000}..{plan * 1000 + RUNNER_LATE_MS}), '
                            f'EFORCE {e["res"]!r} at X {e["now"]}, {d} ms after the append (want -{EF_EARLY_MS}..{EF_LATE_MS})')
        return ok, '; '.join(nums)
    add('2', 'F1 forced starts: one EFORCE per runner append, in time, result started or notstarted (no error), '
             'no Lua error in the event scripts', c2)

    def c3(k):
        def f():
            if len(st[k]) != 1 or not ef[k]: return False, f'ESTART {k} lines {len(st[k])} (want 1), EFORCE {len(ef[k])}'
            dt = st[k][0]['now'] - ef[k][0]['now']; t = st[k][0]['t']
            return 0 <= dt <= START_MS and 0 <= t <= START_T, f'ESTART {dt} ms after EFORCE (want 0..{START_MS}), t {t:.2f} (want 0..{START_T})'
        return f
    add('3s', 'surge started by the forced call: one ESTART soon after EFORCE, t near 0', c3('surge'))
    add('3p', 'psi storm started by the forced call: one ESTART soon after EFORCE, t near 0', c3('psi'))

    forms = {'surge': set(), 'psi': set(), 'none': set()}
    for s in snaps:
        if s['e']: forms[s['e'][0]['kind']].add(s['e'][0]['tf'])
    def c4(k, want):
        def f():
            bad = [s['seq'] for s in snaps if s['e'] and s['e'][0]['kind'] == k and not _tf_is(s['e'][0]['tf'], want)]
            n = sum(1 for s in snaps if s['e'] and s['e'][0]['kind'] == k)
            if n == 0: return False, f'no {k} snapshot'
            return not bad, f'{n} {k} snapshots, printed forms {sorted(forms[k])}, value != {want:g} in {len(bad)} {bad[:3]}'
        return f
    add('4s', 'F2 time factor during the surge reads 10', c4('surge', TF_EVENT))
    add('4p', 'F2 time factor during the psi storm reads 10', c4('psi', TF_EVENT))
    add('4n', 'F2 time factor outside events reads 6', c4('none', TF_NONE))

    def c5(k):
        def f():
            S = esn[k]
            if len(S) < 2: return False, f'{len(S)} {k} snapshots'
            off = [s['e'][0]['t'] - s['tx'] / 1000 for s in S]; span = max(off) - min(off)
            rate = (S[-1]['e'][0]['t'] - S[0]['e'][0]['t']) / ((S[-1]['tx'] - S[0]['tx']) / 1000)
            return span <= CLOCK_SPAN, f'{len(S)} snapshots, t {S[0]["e"][0]["t"]:.2f}..{S[-1]["e"][0]["t"]:.2f}, rate {rate:.5f} t/s, spread of t - X {span:.3f} s (want <= {CLOCK_SPAN})'
        return f
    add('5s', 'F2 t runs one unit per engine second through the surge', c5('surge'))
    add('5p', 'F2 t runs one unit per engine second through the psi storm', c5('psi'))

    def c6(k):
        def f():
            lo, hi = END_T[k]
            if len(en[k]) != 1 or not st[k]: return False, f'EEND {k} lines {len(en[k])} (want 1)'
            e = en[k][0]; after = [s['seq'] for s in snaps if s['tx'] > e['now'] and s['e'] and s['e'][0]['kind'] == k]
            last = esn[k][-1]['e'][0]['t'] if esn[k] else None
            offs = [s['e'][0]['t'] - s['tx'] / 1000 for s in esn[k]]
            dev = None if not offs else e['t'] - (e['now'] / 1000 + (max(offs) + min(offs)) / 2)
            ok = (lo <= e['t'] <= hi and not after and e['now'] > st[k][0]['now'] and dev is not None and abs(dev) <= CLOCK_SPAN)
            return ok, (f'EEND t {e["t"]:.2f} (want {lo}..{hi}), last {k} snapshot t {last}, {(e["now"] - st[k][0]["now"]) / 1000:.1f} X s '
                        f'after ESTART; EEND t minus the snapshot clock at its X {dev if dev is None else round(dev, 3)} s (want '
                        f'|.| <= {CLOCK_SPAN}), {k} snapshots after EEND {len(after)}')
        return f
    add('6s', 'F2 the surge ends as the desk table says (222 s)', c6('surge'))
    add('6p', 'F2 the psi storm ends as the desk table says (103 s)', c6('psi'))

    def c7(k, tab):
        def f():
            E = [e for e in ev['EPHASE'] if e['kind'] == k]; phs = [e['ph'] for e in E]; n = PH_REQ[k]
            want_a, want_b = list(range(1, n + 1)), list(range(1, n + 2))
            bad = []
            for e in E:
                x = tab[e['ph'] - 1] if 1 <= e['ph'] <= len(tab) else None
                if x is None or not (e['t'] > x - 1 - 0.006 and e['t'] <= max(0.0, x - 1) + PH_LAG):
                    bad.append(f'{e["ph"]}@{e["t"]:.2f}')
            mis = [s['seq'] for s in esn[k] if not phases_ok(tab, s['e'][0]['t'], s['e'][0]['ph'])]
            ok = phs in (want_a, want_b) and not bad and not mis
            return ok, (f'EPHASE {phs} (want 1..{n}, {n + 1} optional), outside (X-1, X-1+{PH_LAG}]: {bad[:4]}, '
                        f'snapshot phase != table in {len(mis)}; t at each: ' + ' '.join(f'{e["ph"]}@{e["t"]:.2f}' for e in E))
        return f
    add('7s', 'F2 surge phases 1..12 (13) once each, in order, each at its threshold', c7('surge', SURGE_X))
    add('7p', 'F2 psi phases 1..5 (6) once each, in order, each at its threshold', c7('psi', PSI_X))

    wave_rep = []
    def c8():
        if not esn['surge']: return False, 'no surge snapshot'
        stray = [s['seq'] for s in snaps if s['w'] and not (s['e'] and s['e'][0]['kind'] == 'surge')]
        badn = [s['seq'] for s in snaps if any(n not in (1, 2) for n, _ in s['w'])]
        both = [s['seq'] for s in snaps if {n for n, _ in s['w']} == {1, 2}]
        ok, nums = not stray and not badn, [f'w lines outside a surge {len(stray)}, bad n {len(badn)}']
        for n in (1, 2):
            P = [(s['e'][0]['t'], z, s['a'][2] if s['a'] else None) for s in esn['surge'] for m, z in s['w'] if m == n]
            if len(P) < 2: ok = False; nums.append(f'wave {n}: {len(P)} lines'); continue
            t0, z0, za = P[0]; x0 = WAVE_T0[n]
            mt = sum(p[0] for p in P) / len(P); mz = sum(p[1] for p in P) / len(P)
            vv = sum((p[0] - mt) * (p[1] - mz) for p in P) / max(1e-9, sum((p[0] - mt) ** 2 for p in P))
            dz = None if za is None else z0 - za
            o = (x0 - 1 - 0.006 < t0 <= x0 + WAVE_LAG and WAVE_V[0] <= vv <= WAVE_V[1] and dz is not None
                 and WAVE_Z0[0] <= dz <= WAVE_Z0[1])
            ok = ok and o
            cross = next((p[0] for p in P if p[2] is not None and p[1] < p[2]), None)
            nums.append(f'wave {n}: first t {t0:.2f} (want ({x0 - 1}, {x0 + WAVE_LAG}]), z - actor z (actor of that snapshot) {dz if dz is None else round(dz, 3)} '
                        f'(want {WAVE_Z0[0]}..{WAVE_Z0[1]}), speed {vv:.2f} m/s (want {WAVE_V[0]}..{WAVE_V[1]}), last t {P[-1][0]:.2f}, {len(P)} lines')
            wave_rep.append(f'wave {n}: first z {z0:.3f}, passes the actor at t {cross}, last z {P[-1][1]:.3f} at t {P[-1][0]:.2f}')
        wave_rep.append(f'snapshots with both waves: {len(both)} {both[:3]}')
        return ok, '; '.join(nums)
    add('8', 'F2 waves 1 and 2: w lines only in the surge, start at 120 / 168, 250 m north of the actor, 15 m/s south', c8)

    vort, vrep = {}, []
    sidx = {id(s): j for j, s in enumerate(snaps)}
    for s in snaps:
        for (n, x, y, z, sv) in s['v']:
            vort.setdefault(n, []).append((s, x, y, z, sv))
    def c9():
        stray = [s['seq'] for s in snaps if s['v'] and not (s['e'] and s['e'][0]['kind'] == 'psi')]
        unsorted = [s['seq'] for s in snaps if [v[0] for v in s['v']] != sorted({v[0] for v in s['v']})]
        if not vort: return False, 'no v line'
        ok, bad, starts, unj = not stray and not unsorted, [], [], []
        for n in sorted(vort):
            V = vort[n]; s0, x, y, z, sf = V[0]; sl = V[-1][4]; t0 = s0['e'][0]['t']
            stt = sf + t0 - 20; S = round(stt); starts.append(S)
            j = sidx[id(s0)]; gap = s0['tx'] - snaps[j - 1]['tx'] if j > 0 else None
            judged = gap is not None and gap <= V_GAP_MS
            a = s0['a']; dy = None if a is None else y - (a[1] + 20); r = None if a is None else ((x - a[0]) ** 2 + (z - a[2]) ** 2) ** 0.5
            mid = [round(sv, 2) for (sn, _x, _y, _z, sv) in V if abs(sv + sn['e'][0]['t'] - 20 - S) > V_INT]
            o = (abs(stt - S) <= V_INT and V_START[0] <= S <= V_START[1] and V_S_FIRST[0] <= sf <= V_S_FIRST[1]
                 and V_S_LAST[0] <= sl <= V_S_LAST[1] and not mid)
            if judged: o = o and dy is not None and abs(dy) <= V_DY and r is not None and r <= V_R
            else: unj.append(n)
            if not o: bad.append(n)
            vrep.append(f'vortex {n}: start {S} ({stt:.2f}), first s {sf:.2f} at t {t0:.2f}, last s {sl:.2f}, y - actor y - 20 '
                        f'{dy if dy is None else round(dy, 3)}, distance {r if r is None else round(r, 2)} m '
                        f'({"judged" if judged else "NOT JUDGED"}: gap before its first snapshot {gap} ms, want <= {V_GAP_MS}), '
                        f'x {x:.3f} z {z:.3f}, {len(V)} lines, samples off the start by > {V_INT}: {mid[:3]}')
        gaps = [b - a for a, b in zip(starts, starts[1:])]
        if any(not (V_GAP[0] <= g <= V_GAP[1]) for g in gaps): ok = False
        nj = len(vort) - len(unj); enough = nj * V_JUDGED[1] >= V_JUDGED[0] * len(vort)
        ok = ok and not bad and enough
        return ok, (f'{len(vort)} vortices, start times {starts} (want {V_START[0]}..{V_START[1]}, gaps {V_GAP[0]}..{V_GAP[1]}: {gaps}), '
                    f'failing vortices {bad} (first s {V_S_FIRST[0]}..{V_S_FIRST[1]:.0f}, last s {V_S_LAST[0]}..{V_S_LAST[1]}, '
                    f'y +20 +/- {V_DY}, distance <= {V_R}), judged for y/distance {nj} of {len(vort)} (want >= {V_JUDGED[0]} of every '
                    f'{V_JUDGED[1]}; not judged {unj}), v lines outside the psi storm {len(stray)}, unsorted {len(unsorted)}')
    add('9', 'F2 vortices: v lines only in the psi storm, each from its start to its strike, height and distance against the moving actor', c9)

    def c10():
        hs = sorted({s['a'][3] for s in snaps if s['a']}); noa = sum(1 for s in snaps if not s['a'])
        vit = other['VITALS'][-1] if other['VITALS'] else None; sm = other['SUMMARY'][-1] if other['SUMMARY'] else None
        V = kv(vit) if vit else {}; Sm = kv(sm) if sm else {}
        okv = V.get('min_health') == '1.000' and V.get('max_radiation') == '0.0000' and V.get('max_bleeding') == '0.0000' and V.get('unread') == '0'
        hc = [Sm.get('hits'), Sm.get('cancelled')]; cnt = all(v is not None and v.isdigit() for v in hc)
        oks = Sm.get('min_health') == '1.000' and Sm.get('refills') == '0' and cnt and int(hc[0]) == int(hc[1])
        ok = hs == ['1.0000'] and noa == 0 and okv and oks
        return ok, (f'actor health values in {len(snaps)} snapshots {hs[:4]}, snapshots without a line {noa}; VITALS {vit}; '
                    f'SUMMARY hits {Sm.get("hits")} cancelled {Sm.get("cancelled")} refills {Sm.get("refills")} min_health {Sm.get("min_health")}')
    add('10', 'F3 the actor is unhurt through both events and the kill: health 1.0000 in every snapshot, VITALS clean, no refill', c10)

    t2sum = other['T2SUM']
    def c11():
        if not t2sum: return False, 'no T2SUM line'
        line = t2sum[-1]; keys = [p.split('=', 1)[0] for p in line.split(',')[1:]]; d = kv(line)
        nz = [f'{k}={d.get(k)}' for k in C11_ZERO if d.get(k) != '0']
        degr = [s['seq'] for s in snaps if s['e'] and s['e'][0]['kind'] == 'none' and s['ns'] and s['ns'][0] == (-1, -1)]
        ok = keys == T2SUM_KEYS and not nz and d.get('paused') == 'false' and not ev['EERR'] and not degr
        return ok, (f'T2SUM keys {"as registered (20, eerr last)" if keys == T2SUM_KEYS else keys}, eerr {d.get("eerr")}, nonzero '
                    f'of {C11_ZERO}: {nz}, paused {d.get("paused")}, EERR lines {len(ev["EERR"])} {[e["line"] for e in ev["EERR"]][:1]}, '
                    f'degraded snapshots (e none + ns -1,-1) {len(degr)} (kill-channel values: k14)')
    add('11', 'F4 eerr = 0 at the end, no EERR line, no degraded snapshot; T2SUM keys as registered, torn/lost/epochs/ktabfail 0', c11)

    add('12', 'F4 no ENOTREADY on a save load (desk prediction)',
        lambda: (not ev['ENOTREADY'] and not ev['EREADY'], f'ENOTREADY {len(ev["ENOTREADY"])}, EREADY {len(ev["EREADY"])}'))

    def c13():
        bad = []
        for s in snaps:
            o = s['order']; ke = s['e'][0]['kind'] if len(s['e']) == 1 else None
            ok_ = len(s['e']) == 1 and len(s['ns']) == 1 and bool(o) and o[0] == 'a' and o.count('a') == 1
            if ok_:
                ie, ins = o.index('e'), o.index('ns')
                pre, mid, post = o[1:ie], o[ie + 1:ins], o[ins + 1:]
                if pre and pre[-1] == 'r': pre = pre[:-1]
                ok_ = (all(x == 'o' for x in pre) and all(x == 'k' for x in post)
                       and all(x == ('w' if ke == 'surge' else 'v' if ke == 'psi' else None) for x in mid))
            if not ok_: bad.append(s['seq'])
        return not bad, f'{len(snaps)} snapshots; not in the form a o* r? e (w|v)* ns k*: {len(bad)} {bad[:5]}'
    add('13', 'snapshot layout: one e and one ns in every v4 snapshot, w only in a surge, v only in a psi storm, k lines last', c13)

    # =========================================================== kill channel: M from the data (kj2's identification)
    kids = sorted({i for p in poses for i in p['k']})
    kinds = {i: sorted({s['o'][i]['kind'] for s in snaps if i in s['o']}) for i in kids}
    monsters = [i for i in kids if kinds[i] == ['m']]
    nv1 = []
    if len(kids) != 1: nv1.append(f'{len(kids)} id(s) with k lines in poses_all.txt {kids} (exactly 1 needed)')
    for i in kids:
        if not kinds[i]: nv1.append(f'id {i} never listed in a snapshot o line (kind unknown)')
        elif kinds[i] != ['m']: nv1.append(f'id {i} listed with kind(s) {kinds[i]} (a stalker or other: em1 has the monster only)')
    M = monsters[0] if len(monsters) == 1 else None   # judged even when nvk1 fires (reported), never with two monsters

    def steps_of(i):
        out_ = []
        for p in poses:
            if i in p['k']:
                sec, ts, t, dead = p['k'][i]; last = out_[-1] if out_ else None
                if t > (last['tot'] if last else 0.0) + 1e-9:
                    out_.append({'tot': t, 'ts': ts, 'dead': dead, 'seq': p['seq'], 'u': p['u'], 'pu': p['pu'], 'rise': True})
                elif dead == '1' and last is not None and last['dead'] == '0':
                    out_.append({'tot': last['tot'], 'ts': last['ts'], 'dead': dead, 'seq': p['seq'], 'u': p['u'], 'pu': p['pu'], 'rise': False})
        return out_
    steps = {i: steps_of(i) for i in kids}
    first_dead = lambda i: next((p for p in poses if i in p['k'] and p['k'][i][3] == '1'), None)
    kap = lambda i: [k for k in ev['KAPPLY'] if k['id'] == i]
    kdead_on = lambda i: [k for k in ev['KDEAD'] if k['id'] == i and k['kind'] == 'online']
    mine = lambda i: kap(i) + [k for k in ev['KDEAD'] if k['id'] == i and k['kind'] in MINE_KDEAD]
    kap_of = lambda i, tot: [k for k in kap(i) if abs(k['ap'] - tot) <= TOL_H]
    def snap_at_or_after(T, i=None):
        for s in snaps[bisect.bisect_left(txs, T):]:
            if i is None or i in s['o']: return s
        return None
    def snap_before(T, i=None):
        for s in reversed(snaps[:bisect.bisect_left(txs, T)]):
            if i is None or i in s['o']: return s
        return None
    first_dead_line = lambda i: next((s for s in snaps if i in s['k'] and s['k'][i][1]), None)
    def need_M():
        if not M: raise RuntimeError(f'M not identified (no single monster with k lines: {kids} {kinds})')
    kconf = lambda i: [e for e in ev['KCONF'] if e['id'] == i]
    def holds(i):   # the dead = 0 totals after M's hit (want exactly one: the hold pose of play 50)
        return [s_ for s_ in steps[i][1:] if s_['dead'] == '0'] if steps.get(i) and steps[i][0]['dead'] == '0' else []
    def hold_kap(i):
        h = holds(i); return (h[0], kap_of(i, h[0]['tot'])) if len(h) == 1 else (None, [])
    def play_of(T):
        W = w_of(T); return None if W is None or play0 is None else (W - play0) / 1000

    def cka():   # B1: every KAPPLY's arithmetic, for every id, in a check no NO VERDICT rule leaves untested
        bad = []; pseq = [p['seq'] for p in poses]; prev = {}
        for k in sorted(ev['KAPPLY'], key=lambda e: e['i']):
            i = k['id']; j = bisect.bisect_right(pseq, k['n']) - 1
            pk = poses[j]['k'].get(i) if j >= 0 else None
            pa = prev.get(i, 0.0); prev[i] = k['ap']
            if pk is None: bad.append(f'{k["line"]}: no kept pose at or before {k["n"]} with a k line for {i}'); continue
            tot, dead = pk[2], pk[3]
            wd = min(tot - pa, 1.0); hn = k['hb'] - k['d']
            if dead != '1': hn = max(hn, min(k['hb'], HOLD_FLOOR))   # kill7:476
            wa = max(hn, 0.0); e = (abs(k['ap'] - tot), abs(k['d'] - wd), abs(k['ha'] - wa))
            if max(e) > TOL_H:
                bad.append(f'{k["line"]}: pose total {tot:.3f} dead {dead}, applied before {pa:.3f}: want d {wd:.3f} after {wa:.4f} '
                           f'(|ap-total| {e[0]:.4f} |d-diff| {e[1]:.4f} |after-want| {e[2]:.4f})')
        return not bad, (f'{len(ev["KAPPLY"])} KAPPLY lines: applied = the pose total, d = total - the id\'s previous applied total (<= 1), '
                         f'after = max(before - d, 0), floored at min(before, {HOLD_FLOOR}) for a dead = 0 total (kill7:476), each +/- {TOL_H}; '
                         f'failing {len(bad)} {bad[:3]}')
    add('ka', 'every KAPPLY\'s arithmetic (hit, hold, death; any id): difference and after as kill7 applies them', cka)

    def ck2():
        need_M(); i = M
        if not steps[i] or steps[i][0]['dead'] != '0': return False, f'M {i}: no part-hit total before its death in the poses'
        a = steps[i][0]; ks = kap_of(i, a['tot']); allk = kap(i)
        if len(ks) != 1: return False, f'M {i}: {len(ks)} KAPPLY with applied {a["ts"]} (first pose seq {a["seq"]})'
        k = ks[0]; dd = abs(k['d'] - a['tot']); dh = abs(k['ha'] - (k['hb'] - k['d']))
        ok = dd <= TOL_H and abs(k['ap'] - a['tot']) <= TOL_H and dh <= TOL_H and k['n'] >= a['seq'] and allk[0] is k
        W = w_of(k['T'])
        return ok, (f'M {i}: pose total {a["ts"]} first in pose seq {a["seq"]}; KAPPLY pose {k["n"]} d {k["d"]:.3f} applied {k["ap"]:.3f} '
                    f'before {k["hb"]:.4f} after {k["ha"]:.4f}: |d-total| {dd:.4f}, |after-(before-d)| {dh:.4f} (each <= {TOL_H}), '
                    f'first KAPPLY of the id {allk[0] is k}; at play {None if W is None or play0 is None else round((W - play0) / 1000, 2)} s '
                    f'(itrzone plan {M_HIT_PLAY}; reported)')
    add('k2', 'M\'s hit: one KAPPLY, difference = the pose total, after = before - difference +/- 0.0002', ck2)

    def ck3():
        bad = []; n = 0; pseq = [p['seq'] for p in poses]
        for i in sorted({k['id'] for k in ev['KAPPLY']}):
            prev = 0.0
            for k in kap(i):
                n += 1; j = bisect.bisect_right(pseq, k['n']) - 1
                tot = poses[j]['k'][i][2] if j >= 0 and i in poses[j]['k'] else None
                if tot is None or abs(k['ap'] - tot) > TOL_H or k['ap'] <= prev + TOL_H:
                    bad.append(f'id {i} pose {k["n"]} applied {k["ap"]:.3f}: pose total then {tot}, applied before {prev:.3f}')
                prev = max(prev, k['ap'])
        return not bad, f'{n} KAPPLY lines; a KAPPLY whose total is not a new pose total: {len(bad)} {bad[:4]}'
    add('k3', 'no second effect: no KAPPLY for an id while its total in poses_all.txt is unchanged', ck3)

    def ck4():
        need_M(); i = M
        a = steps[i][0] if steps[i] else None; ka = kap_of(i, a['tot']) if a else []; h, hk = hold_kap(i); kd = kdead_on(i)
        if not ka or len(hk) != 1: return False, f'M {i}: hit KAPPLY {len(ka)}, hold KAPPLY {len(hk)} (interval not found)'
        ta, th = ka[0]['T'], hk[0]['T']; tb = kd[0]['T'] if len(kd) == 1 else float('inf')
        extra = [k for k in kap(i) if ta < k['T'] < tb and k is not ka[0] and k is not hk[0]]
        ss = [s for s in snaps if ta <= s['tx'] < tb and i in s['o']]; failf, nvf = [], []
        for p, q in zip(ss, ss[1:]):
            fl = p['o'][i]['h'] - q['o'][i]['h']
            if fl > FALL:
                kl = [k for k in mine(i) if p['tx'] < k['T'] <= q['tx']]
                if kl and all(k is hk[0] for k in kl): continue   # the hold's own drop (its arithmetic: ka, kh)
                (failf if kl else nvf).append(f'{p["seq"]}->{q["seq"]} fall {fl:.4f}')
        pre = [s['o'][i]['h'] for s in ss if s['tx'] < th]
        ok = not extra and not failf and len(ss) >= 2
        return ok, (f'M {i}: script time {ta}..hold {th}..kill call {tb if tb != float("inf") else None}, {len(ss)} snapshots, change from the hit to the hold '
                    f'{(pre[-1] - pre[0]) if pre else 0:+.4f} (healing, reported); other KAPPLY {len(extra)} {[k["line"] for k in extra][:2]}; '
                    f'falls > {FALL} with a K line of mine other than the hold {failf[:2]}; without (nvk3) {nvf[:2]}')
    add('k4', 'healing left alone: M from its hit to the hold, and within the hold up to the kill call: no KAPPLY but the hold\'s, no fall > 0.02 with another K line of mine', ck4)

    def ck6():
        need_M(); judged, rep, unj, nev = [], [], [], 0; last_pos = {}
        for e in sorted(ev['KAPPLY'] + ev['KDEAD'], key=lambda e: e['i']):
            b = snap_before(e['T']); a = snap_at_or_after(e['T'])
            if not b or not a: unj.append(e['line']); continue
            nev += 1
            for s_ in (b, a):
                if M in s_['o']: last_pos[M] = (s_['o'][M]['x'], s_['o'][M]['y'], s_['o'][M]['z'])
            for o in sorted(set(b['o']) & set(a['o'])):
                if o in kids or o == '0': continue
                fl = b['o'][o]['h'] - a['o'][o]['h']
                if fl <= FALL: continue
                d = []
                for s_ in (b, a):
                    p = (s_['o'][M]['x'], s_['o'][M]['y'], s_['o'][M]['z']) if M in s_['o'] else last_pos.get(M)
                    if p: d.append(((s_['o'][o]['x'] - p[0]) ** 2 + (s_['o'][o]['y'] - p[1]) ** 2 + (s_['o'][o]['z'] - p[2]) ** 2) ** 0.5)
                dm = min(d) if d else None
                t = f'{o} {b["o"][o]["sec"]} fell {fl:.4f} (snap {b["seq"]}->{a["seq"]}, around {e["line"]}), {dm if dm is None else round(dm, 1)} m from M'
                (judged if dm is not None and dm <= NEAR_M else rep).append(t)
        return not judged and not unj, (f'{nev} KAPPLY/KDEAD lines with a snapshot on each side ({len(unj)} without one, each FAILS: {unj[:2]}); '
                                        f'falls > {FALL} within {NEAR_M} m of M (judged): {judged[:3]}; farther (reported): {rep[:3]}')
    add('k6', 'only M changes: no other creature within 10 m of M loses more than 0.02 across a KAPPLY/KDEAD', ck6)

    pend_rep = []
    def ckh():
        need_M(); i = M; hs = holds(i)
        if len(hs) != 1: return False, f'M {i}: {len(hs)} dead = 0 total(s) after its hit {[x["ts"] for x in hs]} (want 1: the hold)'
        h, hk = hold_kap(i)
        if len(hk) != 1: return False, f'M {i}: {len(hk)} KAPPLY with applied {h["ts"]} (the hold total, first in pose seq {h["seq"]})'
        k = hk[0]; want = h['tot'] - steps[i][0]['tot']; dd = abs(k['d'] - want); wa = max(k['hb'] - k['d'], HOLD_FLOOR); dh = abs(k['ha'] - wa)
        pl = play_of(k['T']); kd = kdead_on(i); Te = kd[0]['T'] if len(kd) == 1 else None
        okk = (dd <= TOL_H and dh <= TOL_H and k['n'] >= h['seq'] and pl is not None and abs(pl - M_HOLD_PLAY) <= KILL_PLAY_TOL_S)
        win = [s for s in snaps if s['tx'] > k['T'] and (Te is None or s['tx'] < Te) and i in s['o']]
        bad = [f'snap {s["seq"]}: alive {s["o"][i]["alive"]} health {s["o"][i]["h"]:.4f} k {s["k"].get(i)}' for s in win
               if not (s['o'][i]['alive'] == '1' and HOLD_LO <= s['o'][i]['h'] <= HOLD_HI and i in s['k'] and not s['k'][i][1] and not s['k'][i][2])]
        ok = okk and Te is not None and len(win) >= HOLD_MIN_SNAPS and not bad
        hh = [s['o'][i]['h'] for s in win]
        return ok, (f'M {i}: hold total {h["ts"]} (pose seq {h["seq"]}); KAPPLY pose {k["n"]} d {k["d"]:.3f} (pose difference {want:.3f}, |.| {dd:.4f}) '
                    f'before {k["hb"]:.4f} after {k["ha"]:.4f} (want max(before - d, {HOLD_FLOOR}) = {wa:.4f}, |.| {dh:.4f}; each <= {TOL_H}); at play '
                    f'{None if pl is None else round(pl, 2)} s (want {M_HOLD_PLAY} +/- {KILL_PLAY_TOL_S}); snapshots after it and before the kill call '
                    f'(X {Te}) listing M: {len(win)} (want >= {HOLD_MIN_SNAPS}, each alive 1, health in [{HOLD_LO}, {HOLD_HI}], plain k line): '
                    f'failing {len(bad)} {bad[:3]}; health {min(hh) if hh else None}..{max(hh) if hh else None}')
    add('kh', 'alive near zero, held (then killed: kd, kc): one hold KAPPLY at play 50 leaves M alive at the 0.0100 floor with a plain k line, >= 5 snapshots, until the kill call', ckh)

    def ckd():
        need_M(); i = M; kd = kdead_on(i); fp = first_dead(i)
        allkd = [k['line'] for k in ev['KDEAD'] if k['id'] == i]
        if len(kd) != 1 or len(allkd) != 1: return False, f'M {i}: KDEAD lines {allkd} (want exactly one, online)'
        h, hk = hold_kap(i); Tk = kd[0]['T']; pl = play_of(Tk); nums = []
        ok = (pl is not None and abs(pl - M_KILL_PLAY) <= KILL_PLAY_TOL_S and len(hk) == 1 and Tk > hk[0]['T']
              and fp is not None and kd[0]['n'] >= fp['seq'])
        nums.append(f'M {i}: {kd[0]["line"]} at play {pl if pl is None else round(pl, 2)} s (want {M_KILL_PLAY} +/- {KILL_PLAY_TOL_S}; X->W by the P2 map), '
                    f'after the hold KAPPLY {len(hk) == 1 and Tk > hk[0]["T"]}, from pose {kd[0]["n"]} >= first dead pose {fp["seq"] if fp else None}')
        lk = [k for k in kap(i) if k['T'] == Tk and k['i'] < kd[0]['i']]   # a KAPPLY in the kill call itself (the death pose's new total)
        if fp is None or h is None: return False, '; '.join(nums + [f'death pose {fp["seq"] if fp else None}, hold total {h["ts"] if h else None}'])
        dt, ht = fp['k'][i][2], h['tot']
        ok = ok and dt >= ht - TOL_H
        if dt > ht + TOL_H:
            if len(lk) != 1: ok = False; nums.append(f'death total {dt:.3f} > hold {ht:.3f}: KAPPLY in the kill call {len(lk)} (want 1)')
            else:
                k = lk[0]; dd = abs(k['d'] - min(dt - ht, 1.0)); wa = max(k['hb'] - k['d'], 0.0); dh = abs(k['ha'] - wa)
                ok = ok and dd <= TOL_H and dh <= TOL_H and abs(k['ap'] - dt) <= TOL_H
                nums.append(f'death KAPPLY d {k["d"]:.3f} (pose difference {dt - ht:.3f}, |.| {dd:.4f}) before {k["hb"]:.4f} after {k["ha"]:.4f} '
                            f'(want max(before - d, 0) = {wa:.4f}, |.| {dh:.4f})')
        else:
            ok = ok and not lk
            nums.append(f'death total {dt:.3f} = hold {ht:.3f}: KAPPLY in the kill call {len(lk)} (want 0)')
        nums.append(f'death pose seq {fp["seq"]} dead 1 total {dt:.3f} (want >= the hold total {ht:.3f})')
        return ok, '; '.join(nums)
    add('kd', 'M\'s death: one KDEAD online, at the kill call of play 52, after the hold, from its first dead pose (dead = 1, total >= the hold\'s)', ckd)

    def ckc():
        need_M(); i = M; kd = kdead_on(i); kc = kconf(i); kn = [e['line'] for e in ev['KNOCONF'] if e['id'] == i]
        if len(kd) != 1 or len(kc) != 1: return False, f'M {i}: KDEAD online {len(kd)}, KCONF {len(kc)} {[e["line"] for e in kc][:2]} (want 1 each); KNOCONF {kn}'
        Tk, Tc = kd[0]['T'], kc[0]['T']; lag = Tc - Tk; aft = snap_at_or_after(Tc, i)
        P = [s for s in snaps if Tk <= s['tx'] < Tc and i in s['o'] and s['o'][i]['alive'] == '1']
        pend_rep.append(f'pending (reported only): KCONF {kc[0]["ms"]} ms after the kill call; kept snapshots between them listing M alive '
                        f'{len(P)}, healths {sorted({s["o"][i]["h"] for s in P})[:6]}')
        ok = 0 <= lag <= KCONF_MAX_MS and 0 <= kc[0]['ms'] <= KCONF_MAX_MS and not kn and aft is not None and aft['o'][i]['alive'] == '0'
        return ok, (f'M {i}: KCONF at X {Tc}, {lag} ms after the kill call (line ms {kc[0]["ms"]}; want 0..{KCONF_MAX_MS}, 0 is a PASS); KNOCONF {kn}; '
                    f'first snapshot listing M at or after KCONF: {"seq " + str(aft["seq"]) + " alive " + aft["o"][i]["alive"] if aft else None} (want alive 0)')
    add('kc', 'X-Ray confirms the kill: exactly one KCONF for M, 0..2000 ms after the kill call, and the next snapshot reads M alive 0', ckc)

    def ck7():
        need_M(); i = M; on = kdead_on(i); fp = first_dead(i); ds = first_dead_line(i)
        lag = ds['u'] - fp['u'] if fp and ds else None
        ok = len(on) == 1 and lag is not None and -EARLY_MS <= lag <= CONFIRM_MS
        return ok, (f'M {i}: first dead pose seq {fp["seq"] if fp else None}; first snapshot with k,{i},...,dead seq {ds["seq"] if ds else None}; '
                    f'lag {lag} ms (W clock both; allowed -{EARLY_MS} .. {CONFIRM_MS})')
    add('k7', 'M\'s dead line (written once M reads dead) read within 2.0 s of the first pose with dead = 1', ck7)

    def ck8():
        need_M(); w = [k for k in ev['KWHO'] if k['id'] == M]
        return len(w) == 1 and w[0]['killer'] == KILLER, f'M {M}: KWHO {[k["line"] for k in w]} (want one, killer {KILLER})'
    add('k8', 'the kill is the actor\'s: one KWHO with killer 0 for M', ck8)

    def ck11():
        need_M(); ds = first_dead_line(M)
        if not ds: return False, f'M {M}: no dead line'
        late = [s for s in snaps if s['u'] >= ds['u'] + DEAD_AFTER_MS]
        bad = [s['seq'] for s in late if M in s['o'] and s['o'][M]['alive'] != '0']
        return bool(late) and not bad, f'M {M}: {len(late)} snapshots from 2.0 s after the first dead line (snap {ds["seq"]}), alive in {len(bad)} {bad[:5]}'
    add('k11', 'dead in X-Ray: from 2.0 s after the dead line M is alive 0 or not listed, to the end', ck11)

    def ck13():
        if not ev['P2x']: return False, 'no P2 line (no pose taken)'
        T0 = ev['P2x'][0]['T']
        pu_by_seq = {p['seq']: p['pu'] for p in poses}; pseqs = sorted(pu_by_seq); pread = [p['u'] for p in poses]
        def pu_of(q):
            if q in pu_by_seq: return pu_by_seq[q]
            j = bisect.bisect_right(pseqs, q) - 1
            return pu_by_seq[pseqs[j]] if j >= 0 else None
        absent, back, slow, ahead, prev, n, minlag, inv = [], [], [], [], None, 0, None, []
        for s in snaps:
            if s['tx'] < T0: continue
            n += 1
            if not s['r']: absent.append(s['seq']); continue
            sess, q = s['r']
            if prev is not None and (sess != prev[0] or q < prev[1]): back.append(f'{prev}->{s["r"]}@{s["seq"]}')
            prev = s['r'] if prev is None or (sess == prev[0] and q >= prev[1]) else prev
            j = bisect.bisect_right(pread, s['u']) - 1
            if j < 0:
                mp = next((p for p in poses if p['sess'] == sess and p['seq'] == q), None)
                if mp is None: slow.append(f'{s["seq"]}:no pose read yet, and no kept pose {sess},{q}')
                elif mp['u'] - s['u'] > EARLY_MS: slow.append(f'{s["seq"]}:no pose read yet; pose {q} read {mp["u"] - s["u"]} ms later')
                else: inv.append(s['seq'])
                continue
            newest = poses[j]
            if newest['sess'] != sess: slow.append(f'{s["seq"]}:session {sess} vs newest pose {newest["sess"]}'); continue
            pq = pu_of(q); lag = newest['pu'] - pq if pq is not None else None
            if lag is None or lag > R_LAG_MS: slow.append(f'{s["seq"]}:{lag}>{R_LAG_MS}')
            if lag is not None:
                minlag = lag if minlag is None else min(minlag, lag)
                if lag < -EARLY_MS: ahead.append(f'{s["seq"]}:{lag}')
        return n > 0 and not absent and not back and not slow and not ahead, (
            f'{n} snapshots written at or after the first pose taken; r absent {len(absent)} {absent[:5]}; back or other session {back[:3]}; '
            f'lag over {R_LAG_MS} ms {len(slow)} {slow[:4]}; ahead by more than {EARLY_MS} ms {len(ahead)} {ahead[:4]}; smallest lag {minlag} ms; '
            f'read before any kept pose (allowed inversion) {len(inv)}')
    add('k13', 'the r line: present after the first pose taken, never back, within 2.0 s of the newest kept pose', ck13)

    rows14 = []
    def ck14():
        ok, rows, summ = counter_table(t2sum, meta, ev, {'khits': khits_expect(poses), 'last_seq': last_seq_expect(poses, snaps)})
        rows14.extend(rows); return ok, summ
    add('k14', 'counters: every key of the last T2SUM judged against the em1 table, plus the KCONF/KNOCONF rows', ck14)

    def ck15():
        bad, stale, nd, dup = [], [], 0, []
        for s in snaps:
            dup += [f'snap {s["seq"]}: {d}' for d in s['dup']]
            for i, (ap, dead, word) in sorted(s['k'].items()):
                if word == 'stale': stale.append(f'snap {s["seq"]} k,{i},{ap},stale')
                if dead:
                    nd += 1
                    if i in s['o'] and s['o'][i]['alive'] == '1': bad.append(f'snap {s["seq"]} k,{i},{ap},dead beside o alive 1 (health {s["o"][i]["h"]:.4f})')
        return bool(snaps) and not bad and not stale and not dup, (f'{len(snaps)} snapshots, {nd} dead lines; dead beside alive 1: {len(bad)} {bad[:4]}; '
                                                                    f'stale lines {len(stale)} {stale[:4]}; a second o or k line for one id: {len(dup)} {dup[:4]}')
    add('k15', 'the dead line only for a creature that reads dead; no stale line; no id with two o or two k lines in one snapshot', ck15)

    # =========================================================== NO VERDICT rules
    nv = []
    first_ef = min((e['now'] for e in ev['EFORCE']), default=None)
    def nve1():
        pre = [s['seq'] for s in snaps if s['e'] and s['e'][0]['kind'] != 'none' and (first_ef is None or s['tx'] < first_ef)]
        foreign = []
        for k in ('surge', 'psi'):
            for e in st[k]:
                if not [f for f in ef[k] if f['res'] == 'started' and f['now'] <= e['now']]: foreign.append(f'{k}@{e["now"]}')
        many = {k: len(st[k]) for k in st if len(st[k]) > 1}
        return bool(pre or foreign or many), f'event snapshots before the first EFORCE {len(pre)}, ESTART without its EFORCE {foreign}, kinds started twice {many}'
    def nveref(k):
        return lambda: (bool(ef[k]) and ef[k][0]['res'] == 'notstarted', f'EFORCE {k} result {ef[k][0]["res"] if ef[k] else None}')
    def nve4():
        if not ef['psi'] or not ef['surge'] or not started['surge']: return False, 'not applicable'
        endn = en['surge'][0]['now'] if en['surge'] else None
        return endn is None or ef['psi'][0]['now'] < endn, f'EFORCE psi at X {ef["psi"][0]["now"]}, EEND surge at X {endn}'
    def nvk2():
        if not M: return False, 'not judged (no M)'
        out_ = []; i = M; a = steps[i][0] if steps[i] else None; ka = kap_of(i, a['tot']) if a else []
        if ka: pre = [s['o'][i]['h'] for s in snaps if i in s['o'] and s['tx'] < ka[0]['T']]; ref = ka[0]['hb']
        elif a: pre = [s['o'][i]['h'] for s in snaps if i in s['o'] and s['u'] < a['u']]; ref = pre[-1] if pre else None
        else: pre, ref = [], None
        if pre and ref is not None and max(pre) - ref > FALL: out_.append(f'M hurt before its hit: max {max(pre):.4f}, at the hit {ref:.4f}')
        obs = [k['line'] for k in ev['KDEAD'] if k['id'] == i and k['kind'] not in MINE_KDEAD]
        if obs: out_.append(f'M death observed, not dealt: {obs[:2]}')
        fd = first_dead(i); s0 = next((s for s in snaps if i in s['o'] and s['o'][i]['alive'] == '0'), None)
        mine0 = [k['line'] for k in kap(i) if k['ha'] <= 0 and s0 is not None and k['T'] <= s0['tx']] + \
                [k['line'] for k in kdead_on(i) if s0 is not None and k['T'] <= s0['tx']]
        early = [s['seq'] for s in snaps if i in s['o'] and s['o'][i]['alive'] == '0' and (fd is None or s['u'] < fd['u'] - EARLY_MS)]
        if early and not mine0: out_.append(f'M dead in X-Ray before its death step: snaps {early[:3]}')
        elif early: out_.append(f'(not fired: M\'s first alive-0 snapshot {s0["seq"]} follows my {mine0[:1]}: a death the bridge caused is judged)')
        return any(not x.startswith('(not fired') for x in out_), '; '.join(out_) or 'none'
    def death_step(i):   # kj2's NV3 exemption, unchanged (one step per id)
        kd = kdead_on(i)
        if not kd: return {'T': None, 'step': None}
        T = kd[0]['T']; ss = [s for s in snaps if i in s['o']]
        for p, q in zip(ss, ss[1:]):
            if p['tx'] < T or p['o'][i]['h'] - q['o'][i]['h'] <= FALL: continue
            if [k for k in mine(i) if p['tx'] < k['T'] <= q['tx']]: continue
            return {'T': T, 'step': (p['seq'], q['seq']) if q['o'][i]['alive'] == '0' and q['o'][i]['h'] <= 0 else None}
        return {'T': T, 'step': None}
    def nvk3():
        if not M: return False, 'not judged (no M)'
        out_ = []; ds = death_step(M); ss = [s for s in snaps if M in s['o']]
        for p, q in zip(ss, ss[1:]):
            fl = p['o'][M]['h'] - q['o'][M]['h']
            if fl > FALL and not [k for k in mine(M) if p['tx'] < k['T'] <= q['tx']] and ds['step'] != (p['seq'], q['seq']):
                out_.append(f'M {M} snap {p["seq"]}->{q["seq"]} fell {fl:.4f}')
        return bool(out_), ('; '.join(out_[:4]) or 'none') + f'; exempt death step: {ds["step"]} (KDEAD online at {ds["T"]})'
    def nvk6():
        if not M: return False, 'not judged (no M)'
        out_ = []
        for st_ in steps[M][1:]:
            sb = [x for x in snaps if x['u'] <= st_['u']]
            if not sb or M not in sb[-1]['o']:
                out_.append(f'M not listed at the first pose of a later step ({st_["ts"]}{" dead" if st_["dead"] == "1" else ""}, pose seq {st_["seq"]})')
        return bool(out_), '; '.join(out_) or 'listed at every later step'
    for k, t, fn in (('nve1', 'an event the run did not force (before the first EFORCE, with no EFORCE started before it, or twice)', nve1),
                     ('nve2', 'the forced surge was refused by the game (EFORCE surge,notstarted)', nveref('surge')),
                     ('nve3', 'the forced psi storm was refused by the game (EFORCE psi,notstarted)', nveref('psi')),
                     ('nve4', 'the psi storm was forced while the surge still ran (PSM:313 cancels it)', nve4),
                     ('nvk1', 'not exactly one id with k lines, a monster (em1: no stalker)', lambda: (bool(nv1), '; '.join(nv1) or f'M {M}')),
                     ('nvk2', 'M hurt by X-Ray\'s own world by more than 0.02 before its hit, or killed before its step', nvk2),
                     ('nvk3', 'M lost more than 0.02 between two snapshots with no K line of mine between them, except its death step', nvk3),
                     ('nvk6', 'M not listed in X-Ray\'s snapshot at the first pose of a later step', nvk6)):
        try: f, d = fn()
        except Exception as e: f, d = False, f'not judged: error {e!r}'
        nv.append((k, t, f, d))

    # =========================================================== report
    out += [f'UNPARSED {u}' for u in unparsed[:20]] + ([f'UNPARSED ... {len(unparsed) - 20} more'] if len(unparsed) > 20 else [])
    for cid in ORDER_ID:
        t, ok, d = res[cid]; out.append(f'{cid} {t}: {"PASS" if ok else "FAIL"} ({d})')
        if cid == 'k14': out += rows14
    out += [f'{k} {t}: {"FIRED" if f else "clear"} ({d}){"; leaves untested: " + ", ".join(sorted(NV_UNTESTED[k])) if f else ""}'
            for k, t, f, d in nv]
    def ans(k):
        if not ef[k]: return 'no EFORCE line'
        r = ef[k][0]['res']
        return {'started': 'started', 'notstarted': 'REFUSED BY THE GAME (notstarted)'}.get(r, f'CALL FAILED ({r})')
    good_read = any(s['e'] and (s['e'][0]['kind'] != 'none' or (s['ns'] and s['ns'][0] != (-1, -1))) for s in snaps)
    out.append(f'F1 ANSWER: managers readable from the bridge {"yes" if good_read and not ev["EERR"] else "NO"}; forced surge: {ans("surge")}; forced psi storm: {ans("psi")}')
    out.append(f'reported: F2 printed tf forms: surge {sorted(forms["surge"])}, psi {sorted(forms["psi"])}, none {sorted(forms["none"])}')
    for k in ('surge', 'psi'):
        out.append(f'reported: {k}: ESTART {[e["line"] for e in st[k]]}; EEND {[e["line"] for e in en[k]]}')
    out += ['reported: ' + x for x in wave_rep + vrep + pend_rep]
    out.append(f'reported: kill channel: M {M}; k-line ids {kids} kinds {kinds}; steps of M: ' +
               (', '.join(f'{s["ts"]}{"(dead)" if s["dead"] == "1" else ""}@seq{s["seq"]}' for s in steps.get(M, [])) if M else '-'))
    out += [f'reported: {e["line"]}' for k in ('KAPPLY', 'KDEAD', 'KWHO', 'KCONF', 'KNOCONF', 'KOTHER') for e in ev[k]]
    hk = {}
    for l in hits:
        f = l.split(',')
        if f[0] == 'h' and len(f) >= 7: hk[(f[2], f[3], f[5])] = hk.get((f[2], f[3], f[5]), 0) + 1
    out.append(f'reported: F3 actor hits seen by the bridge {len(hits)} lines; by (draftsman id, section, type): {sorted(hk.items())[:8]}')
    ap = [s['a'][:3] for s in snaps if s['a'] and s['tx'] >= (snaps[0]['tx'] + 20000)] if snaps else []
    if ap:
        out.append(f'reported: F3 actor x range {min(a[0] for a in ap):.3f}..{max(a[0] for a in ap):.3f}, y {min(a[1] for a in ap):.3f}..'
                   f'{max(a[1] for a in ap):.3f}, z {min(a[2] for a in ap):.3f}..{max(a[2] for a in ap):.3f} after the first 20 s (moving actor)')
    out += f5(snaps, other, esn, st, en)
    out.append(f'reported: F6 export cost this run: {other["SUMMARY"][-1] if other["SUMMARY"] else None}; reference {REF_KJ2}')
    out.append(f'reported: last T2SUM: {t2sum[-1] if t2sum else None}')
    out.append(f'reported: inputs {len(snaps)} snapshots, {len(poses)} poses, {len(meta)} meta lines, {len(ef_rows)} emforce rows')

    fired = [k for k, _, f, _ in nv if f]
    failing = {cid for cid in ORDER_ID if not res[cid][1]}
    if unparsed: verdict, code = 'FAIL', 1; out.append('verdict: unparsable input')
    elif fired:
        U = set().union(*(NV_UNTESTED[k] for k in fired))
        if M and kap(M): U -= JUDGED_IF_KAPPLY; out.append(f'verdict: M has a KAPPLY: {", ".join(sorted(JUDGED_IF_KAPPLY))} judged whatever fired')
        outside = failing - U
        if outside:
            verdict, code = 'FAIL', 1
            out.append(f'verdict: NO VERDICT rule(s) {", ".join(fired)} fired, but failing check(s) {", ".join(c for c in ORDER_ID if c in outside)} '
                       f'are outside what they leave untested: FAIL')
        else:
            verdict, code = 'NO VERDICT', 3
            out.append(f'verdict: NO VERDICT rule(s) {", ".join(fired)} fired; failing checks {", ".join(c for c in ORDER_ID if c in failing) or "none"} all within what they leave untested')
    elif not failing: verdict, code = 'PASS', 0
    else: verdict, code = 'FAIL', 1; out.append(f'verdict: failing checks {", ".join(c for c in ORDER_ID if c in failing)}')
    out.append(f'EM1 X SIDE: {verdict}')
    return out, code


def f5(snaps, other, esn, st, en):
    """kj4p's F5 (counts only)."""
    out = []
    def stalkers(s): return {i for i, o in s['o'].items() if o['kind'] == 's' and o['alive'] == '1' and 'zombied' not in o['sec']}
    for k in ('surge', 'psi'):
        if not esn[k] or len(st[k]) != 1: out.append(f'reported: F5 {k}: no event'); continue
        x0 = st[k][0]['now']; x1 = en[k][0]['now'] + 2000 if len(en[k]) == 1 else None
        before = [s for s in snaps if s['tx'] < x0]; aft = [] if x1 is None else [s for s in snaps if s['tx'] >= x1]
        if not before or not aft:
            out.append(f'reported: F5 {k}: unavailable (EEND lines {len(en[k])}, snapshots before ESTART {len(before)}, at or after EEND + 2 s {len(aft)})'); continue
        b = stalkers(before[-1]); a_all = set(aft[0]['o']); a_alive = stalkers(aft[0])
        out.append(f'reported: F5 {k}: online stalkers before {len(b)}, after (EEND + 2 s) not listed {len(b - a_all)}, listed but not alive {len((b & a_all) - a_alive)}')
    return out


def main():
    a = sys.argv[1:]
    if len(a) != 1 or '=' in a[0]:
        print('usage: em1_check.py <run dir>   (prints only; redirect to a file with set -C)'); print('EM1 X SIDE: REFUSED'); sys.exit(2)
    lines, code = full(a[0])
    sys.stdout.write('\n'.join(lines) + '\n'); sys.exit(code)


if __name__ == '__main__':
    main()
