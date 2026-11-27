#!/usr/bin/env python3
"""kj3p: X-ONLY emission preflight checker (kj3/PREREG-kj3p-emission-preflight-DRAFT.md). Stdlib only, run with python3 -I.
  kj3p_check.py <run dir>
<run dir> = the folder the runner tools/t2_xray_kj3.sh was given (kj3p_run.sh passes <out>/run). Required, by name:
  t2_xray.log  restarts.csv  emforce.csv  boot0/meta_t2.csv  boot0/hits_t2.csv (may be empty: no hit)
  boot0/tb/xray.log  boot0/watch/snaps_all.txt  boot0/watch/poses_all.txt   (tools/t1_watch_keep.py)
A missing, unreadable or (except hits_t2.csv) empty required file: `REFUSED: <name> ...`, exit 2. Nothing is ever written
by this program: it prints only; the single scoring run redirects stdout to a file (set -C, see the registration).
CR is stripped from every line of every file (the engine runs under Wine; its bytes may be CRLF).
Output: one line per check `<id> <text>: PASS|FAIL (<values>)`, one line per NO VERDICT rule `NV<k> <text>: FIRED|clear
(<values>)`, `F1 ANSWER:` / `reported:` lines, then `KJ3P: PASS|FAIL|NO VERDICT|REFUSED`. Exit 0 PASS, 1 FAIL, 3 NO VERDICT,
2 REFUSED (missing input or usage).
Verdict: unparsable input -> FAIL. Else if NO VERDICT rules fired: NO VERDICT only if EVERY failing check lies in the union
of the fired rules' untested sets (NV_UNTESTED); a failing check outside it -> FAIL (named). Else PASS only if all pass.
Checks 1, 2, 4n, 10, 11, 12, 13 are in no untested set: always judged.

CLOCKS. X = time_global() ms of the engine (snapshot header field 3, field 2 of the E* meta lines, P2 field 4).
W = Linux realtime ms (the watcher's #snap/#pose stamps, emforce.csv utc_ms, the runner's play0 = header utc of the first
play pose it read). The one cross-clock place (check 2's EFORCE delay) maps X to W through the newest P2 line at or before
the event: W ~ P2 pose utc + (T - P2 now); the pose was written before the script read it, so the estimate is EARLY by
the pose's age at the read (<= 50 ms writer period + one frame + file read), never late.
"""
import math, os, re, sys

# ---------------------------------------------------------------- registered plan and bounds (derivations: registration
# section 6 margin table; each one is repeated here in a word)
SURGE_PLAY_S, PSI_PLAY_S, END_PLAY_S = 70, 310, 440   # the partner's plan (T2_SURGE_PLAY, T2_PSI_PLAY; run end)
RUNNER_LATE_MS = 2500        # runner loop = sleep 1 + body (kj2 measured 757 ms late on its 105 s trigger)
EF_EARLY_MS, EF_LATE_MS = 250, 2500   # EFORCE after the runner's append: true delay 0..1000 ms + frame (one read per s);
                                      # estimate early by <= pose age (~50-90 ms)
START_MS = 1000              # ESTART after EFORCE (X): start() sets started in the call; next snapshot <= 100 ms + frame
START_T = 1.0                # t at ESTART: inited_time = now at the call
TF_EVENT, TF_NONE = 10.0, 6.0  # SM:33,245 PSM:26,117 set 10; alife.ltx:7 time_factor 6, restored SM:388 / main_loop
CLOCK_SPAN = 0.5             # max - min of (t - X/1000) over one event: 0 by the clock (inline.h:25), 0.1 s game-second
                             # quantum at factor 10 + 0.01 print rounding
END_T = {'surge': (220.5, 223.0), 'psi': (101.5, 104.0)}   # last t seen: (X-1, X] at the 1 s update (X = 222 / 103)
SURGE_X = [0, 30, 47, 50, 80, 108, 120, 156, 168, 200, 212, 220, 222]   # kj3/emission-phases.md (independent copy)
PSI_X = [0, 10, 12, 30, 78, 103]
PH_REQ = {'surge': 12, 'psi': 5}   # the last phase (end due) may be cut by the script's own end update: optional
PH_LAG = 1.0                 # phase p first logged at t in (X_p - 1, X_p - 1 + PH_LAG]
WAVE_T0 = {1: 120, 2: 168}   # start_wave at the update with ceil(t) >= 120 / 168 (SM:870, SM:894)
WAVE_LAG = 1.0
WAVE_V = (-16.5, -13.5)      # 3 m per 2 game s at factor 10 = -15 m/s (SM:688, SM:1065)
WAVE_Z0 = (244.0, 250.5)     # inited_pos.z = actor z + 250 (SM:496); <= 2 steps of 3 m before the first snapshot
V_START = (33, 77)           # vortex start_time: first at 30 + 3..6, last while d <= 77 (PSM:357-365, PVF:137)
V_GAP = (3, 16)              # PVF 3..6, or stock 8..16 if PVF wrapped initialize after the instance was made
V_S_FIRST = (19.0, 21.005)   # s = start + 20 - t at the first sighting
V_S_LAST = (-1.0, 1.5)       # s at the last sighting before the strike (the strike at t > start + 19, 1 s updates)
V_INT = 0.02                 # |s + t - 20 - start| (two %.2f roundings)
V_DY, V_R = 0.05, 150.5      # particle_pos y = actor y + 20 (PSM:410); distance <= vortex_distance_max 150 (PSM:59)
T2SUM_KEYS = ['last_seq', 'torn', 'holds', 'paused', 'near', 'guards', 'lost', 'epochs', 'khits', 'kdead_online',
              'kdead_offline', 'kowed', 'kstale', 'kback', 'krefused', 'kclamped', 'kver', 'kdead_fail', 'ktabfail', 'eerr']
T2SUM_ZERO = ['torn', 'lost', 'epochs', 'khits', 'kdead_online', 'kdead_offline', 'kowed', 'kstale', 'kback', 'krefused',
              'kclamped', 'kver', 'kdead_fail', 'ktabfail', 'eerr']
LEVEL = 'l01_escape'
LUA_BRIDGE = 'zz_p1_puppet'
EV_SCRIPTS = ('surge_manager', 'psi_storm_manager')
REF_KJ2 = ('kj2 X run /home/caden/Projects/ITRZone/out/runs/20261126-172548-t2kj2-xray (kill5, no e lines, 240 s): '
           'export_mean_us=795.8 export_p99_us=1558.1 export_max_us=15920.6, 1949 ticks')

NV_UNTESTED = {
    'NV1': {'3s', '3p', '4s', '4p', '5s', '5p', '6s', '6p', '7s', '7p', '8', '9'},   # an event the run did not force
    'NV2': {'3s', '4s', '5s', '6s', '7s', '8'},                                     # surge refused by the game
    'NV3': {'3p', '4p', '5p', '6p', '7p', '9'},                                     # psi storm refused by the game
    'NV4': {'3p', '4p', '5p', '6p', '7p', '9'},                                     # psi forced while the surge ran
    'NV5': {'9'},                                                                   # no vortex at all
}
REQ = ('t2_xray.log', 'restarts.csv', 'emforce.csv', 'boot0/meta_t2.csv', 'boot0/hits_t2.csv', 'boot0/tb/xray.log',
       'boot0/watch/snaps_all.txt', 'boot0/watch/poses_all.txt')
EMPTY_OK = {'boot0/hits_t2.csv'}
ORDER_ID = ['1', '2', '3s', '3p', '4s', '4p', '4n', '5s', '5p', '6s', '6p', '7s', '7p', '8', '9', '10', '11', '12', '13']

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
    L = r['lines']; h = L[0].split(',') if L else []
    if not (len(h) == 7 and h[0] == 'seq' and h[1] == r['mseq'] and L[-1] == f'end,{h[1]}' and h[6] == '4'):
        raise ValueError('header/end/version 4')
    s = {'u': pint(r['u']), 'seq': pint(h[1]), 'tx': pint(h[2]), 't': fnum(h[3]), 'level': h[4], 'run': h[5], 'a': None,
         'o': {}, 'r': None, 'e': [], 'w': [], 'v': [], 'ns': [], 'k': [], 'order': []}
    for l in L[1:-1]:
        f = l.split(','); k = f[0]; s['order'].append(k)
        if k == 'a' and len(f) == 6: s['a'] = (fnum(f[1]), fnum(f[2]), fnum(f[3]), f[5]); fnum(f[5])
        elif k == 'o' and len(f) == 12:
            s['o'][f[1]] = {'sec': f[2], 'kind': f[3], 'x': fnum(f[4]), 'z': fnum(f[6]), 'h': fnum(f[8]), 'alive': f[9]}
        elif k == 'r' and len(f) == 3: s['r'] = (f[1], pint(f[2]))
        elif k == 'e' and len(f) == 5 and f[1] in ('none', 'surge', 'psi'):
            s['e'].append({'kind': f[1], 't': fnum(f[2]), 'ph': pint(f[3]), 'tf': f[4]})
        elif k == 'w' and len(f) == 3: s['w'].append((pint(f[1]), fnum(f[2])))
        elif k == 'v' and len(f) == 6: s['v'].append((pint(f[1]), fnum(f[2]), fnum(f[3]), fnum(f[4]), fnum(f[5])))
        elif k == 'ns' and len(f) == 3: s['ns'].append((pint(f[1]), pint(f[2])))
        elif k == 'k' and len(f) in (3, 4): s['k'].append(l)
        else: raise ValueError('line ' + l)
    return s


def parse_pose(r):
    L = r['lines']; h = L[0].split(',') if L else []
    if not (len(h) == 8 and h[0] == 'seq' and h[1] == r['mseq'] and L[-1] == f'end,{h[1]}' and h[7] == '4'):
        raise ValueError('pose header/end/version 4')
    p = {'u': pint(r['u']), 'seq': pint(h[1]), 'pu': pint(h[2]), 'state': h[3], 'p': None, 'k': []}
    for l in L[1:-1]:
        f = l.split(',')
        if f[0] == 'p' and len(f) == 7: p['p'] = (f[1], f[2], f[3])
        elif f[0] == 'k': p['k'].append(l)
    return p


META_SHAPE = {'EFORCE': 4, 'ESTART': 4, 'EPHASE': 5, 'EEND': 4, 'EERR': 3, 'ENOTREADY': 2, 'EREADY': 2}


def parse_meta(lines):
    ev, bad = {k: [] for k in META_SHAPE}, []
    other = {'P2': [], 'T2SUM': [], 'VITALS': [], 'SUMMARY': [], 'TICK': [], 'ON': [], 'OFF': [], 'S': []}
    for i, l in enumerate(lines):
        if not l: continue
        f = l.split(','); k = f[0]
        try:
            if k in META_SHAPE:
                if len(f) != META_SHAPE[k]: raise ValueError(f'{k} has {len(f)} fields, want {META_SHAPE[k]}')
                e = {'line': l, 'i': i, 'now': pint(f[1])}
                if k in ('EFORCE',): e.update(what=f[2], res=f[3])
                elif k == 'ESTART': e.update(kind=f[2], t=fnum(f[3]))
                elif k == 'EPHASE': e.update(kind=f[2], ph=pint(f[3]), t=fnum(f[4]))
                elif k == 'EEND': e.update(kind=f[2], t=fnum(f[3]))
                elif k == 'EERR': e.update(msg=f[2])
                if k in ('ESTART', 'EPHASE', 'EEND') and e['kind'] not in ('surge', 'psi'): raise ValueError('kind ' + e['kind'])
                ev[k].append(e)
            elif k == 'P2' and len(f) >= 4: other['P2'].append((pint(f[3]), pint(f[2])))
            elif k in other: other[k].append(l)
        except ValueError as x:
            bad.append(f'meta line {i + 1}: {x} ({l[:80]!r})')
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
    # the reader takes ceil of the UNROUNDED t; the line prints t to 2 decimals, so allow the printed value +/- 0.005
    return ph in (phase_of(tab, t - 0.005), phase_of(tab, t + 0.005))


def script_errs(xlog, name):
    # engine log forms (real logs under out/): '<path>\<name>.script:16: attempt ...', '<name>.script(325) : fn',
    # '... <name>.script (line: 267) in function'. Callback-registration noise (a 'doesn't exist' / 'nil function' line
    # within the 8 lines before) is not counted, as the kj2 checker does.
    pat = re.compile(re.escape(name) + r'\.script(\(\d| \(line|:\d)')
    return [xlog[i] for i in range(len(xlog)) if pat.search(xlog[i]) and
            not any("doesn't exist" in xlog[j] or 'nil function' in xlog[j] for j in range(max(0, i - 8), i))]


def full(run_dir):
    out, unparsed = [f'KJ3P X-only emission preflight checker, run dir {run_dir}'], []
    data, refused = {}, []
    for n in REQ:
        L, st = read(os.path.join(run_dir, n))
        if st == 'empty' and n in EMPTY_OK: st = None
        if st: refused.append(f'REFUSED: {n} is {st} ({os.path.join(run_dir, n)})')
        data[n] = L
    if refused:
        return out + refused + ['KJ3P: REFUSED'], 2

    rlog = [l for l in data['t2_xray.log'] if l.strip()]
    restarts = [l for l in data['restarts.csv'][1:] if l.strip()]
    xlog = data['boot0/tb/xray.log']
    hits = [l for l in data['boot0/hits_t2.csv'] if l.strip()]
    # emforce.csv
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
    meta_lines = data['boot0/meta_t2.csv']
    ev, other, bad = parse_meta(meta_lines); unparsed += bad
    srecs, sstray = records(data['boot0/watch/snaps_all.txt'], 'snap'); snaps = []
    for r in srecs:
        try: snaps.append(parse_snap(r))
        except (ValueError, IndexError) as e: unparsed.append(f'{r["mark"]}: {e}')
    unparsed += [f'stray snapshot line: {s[:60]}' for s in sstray[:3]]
    precs, pstray = records(data['boot0/watch/poses_all.txt'], 'pose'); poses = []
    for r in precs:
        try: poses.append(parse_pose(r))
        except (ValueError, IndexError) as e: unparsed.append(f'{r["mark"]}: {e}')
    if not snaps: unparsed.append('no kept snapshot')
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

    # ---- 1 the run --------------------------------------------------------------------------------------------------
    def c1():
        lua_b = script_errs(xlog, LUA_BRIDGE)
        fatal = [l for l in xlog if 'FATAL' in l]
        rc0 = bool(rlog) and rlog[-1].rstrip().endswith('rc=0')
        boots = sorted(d for d in os.listdir(run_dir) if re.match(r'boot\d+$', d))
        lv = sorted({s['level'] for s in snaps}); runs = sorted({s['run'] for s in snaps})
        kpose = sum(1 for p in poses if p['k'])
        span = (snaps[-1]['u'] - play0) / 1000 if snaps and play0 else None
        ok = (rc0 and not restarts and not lua_b and not fatal and boots == ['boot0'] and lv == [LEVEL] and len(runs) == 1
              and kpose == 0 and play0 is not None and span is not None and span >= END_PLAY_S)
        return ok, (f'runner rc0 {rc0}, restart rows {len(restarts)}, boots {boots}, FATAL {len(fatal)}, Lua errors naming '
                    f'{LUA_BRIDGE} {len(lua_b)} {lua_b[:1]}, levels {lv}, run ids {len(runs)}, kept poses with k lines {kpose}, '
                    f'play0 {play0}, last snapshot at play {span if span is None else round(span, 1)} s (want >= {END_PLAY_S})')
    add('1', 'one boot ran to the end on Cordon: rc 0, no restart, no FATAL, no bridge Lua error, X-only (no k line), play >= end', c1)

    # ---- 2 F1: forced starts through emtest.txt ----------------------------------------------------------------------
    p2 = other['P2']
    def w_of(T):   # X -> W through the newest P2 at or before T
        best = None
        for now, pu in p2:
            if now <= T and (best is None or now > best[0]): best = (now, pu)
        return None if best is None else best[1] + (T - best[0])
    def c2():
        nums, ok = [], True
        kinds = [r['kind'] for r in ef_rows]
        if kinds != ['surge', 'psi']: ok = False; nums.append(f'emforce rows {kinds} (want [surge, psi])')
        lua_e = [l for x in EV_SCRIPTS for l in script_errs(xlog, x)]
        if lua_e: ok = False
        nums.append(f'Lua errors naming {"/".join(EV_SCRIPTS)} {len(lua_e)} {lua_e[:1]}')
        for k, plan in (('surge', SURGE_PLAY_S), ('psi', PSI_PLAY_S)):
            rows = [r for r in ef_rows if r['kind'] == k]
            if len(ef[k]) != 1: ok = False; nums.append(f'{k}: EFORCE lines {len(ef[k])} (want 1)'); continue
            e = ef[k][0]
            if e['res'] not in ('started', 'notstarted'): ok = False   # error:<text> or unknown = the call failed
            d = None
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

    # ---- 3 start -----------------------------------------------------------------------------------------------------
    def c3(k):
        def f():
            if len(st[k]) != 1 or not ef[k]: return False, f'ESTART {k} lines {len(st[k])} (want 1), EFORCE {len(ef[k])}'
            dt = st[k][0]['now'] - ef[k][0]['now']; t = st[k][0]['t']
            return 0 <= dt <= START_MS and 0 <= t <= START_T, f'ESTART {dt} ms after EFORCE (want 0..{START_MS}), t {t:.2f} (want 0..{START_T})'
        return f
    add('3s', 'surge started by the forced call: one ESTART soon after EFORCE, t near 0', c3('surge'))
    add('3p', 'psi storm started by the forced call: one ESTART soon after EFORCE, t near 0', c3('psi'))

    # ---- 4 F2 time factor ------------------------------------------------------------------------------------------
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

    # ---- 5 F2 clock --------------------------------------------------------------------------------------------------
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

    # ---- 6 F2 end ----------------------------------------------------------------------------------------------------
    def c6(k):
        def f():
            lo, hi = END_T[k]
            if len(en[k]) != 1 or not st[k]: return False, f'EEND {k} lines {len(en[k])} (want 1)'
            e = en[k][0]; after = [s['seq'] for s in snaps if s['tx'] > e['now'] and s['e'] and s['e'][0]['kind'] == k]
            last = esn[k][-1]['e'][0]['t'] if esn[k] else None
            ok = lo <= e['t'] <= hi and not after and e['now'] > st[k][0]['now']
            return ok, (f'EEND t {e["t"]:.2f} (want {lo}..{hi}), last {k} snapshot t {last}, {(e["now"] - st[k][0]["now"]) / 1000:.1f} s '
                        f'after ESTART, {k} snapshots after EEND {len(after)}')
        return f
    add('6s', 'F2 the surge ends as the desk table says (222 s)', c6('surge'))
    add('6p', 'F2 the psi storm ends as the desk table says (103 s)', c6('psi'))

    # ---- 7 F2 phases -------------------------------------------------------------------------------------------------
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

    # ---- 8 F2 waves --------------------------------------------------------------------------------------------------
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
            nums.append(f'wave {n}: first t {t0:.2f} (want ({x0 - 1}, {x0 + WAVE_LAG}]), z - actor z {dz if dz is None else round(dz, 3)} '
                        f'(want {WAVE_Z0[0]}..{WAVE_Z0[1]}), speed {vv:.2f} m/s (want {WAVE_V[0]}..{WAVE_V[1]}), last t {P[-1][0]:.2f}, '
                        f'{len(P)} lines')
            wave_rep.append(f'wave {n}: first z {z0:.3f}, passes the actor at t {cross}, last z {P[-1][1]:.3f} at t {P[-1][0]:.2f}')
        wave_rep.append(f'snapshots with both waves: {len(both)} {both[:3]}')
        return ok, '; '.join(nums)
    add('8', 'F2 waves 1 and 2: w lines only in the surge, start at 120 / 168, 250 m north of the actor, 15 m/s south', c8)

    # ---- 9 F2 vortices -----------------------------------------------------------------------------------------------
    vort, vrep = {}, []
    for s in snaps:
        for (n, x, y, z, sv) in s['v']:
            vort.setdefault(n, []).append((s, x, y, z, sv))
    def c9():
        stray = [s['seq'] for s in snaps if s['v'] and not (s['e'] and s['e'][0]['kind'] == 'psi')]
        unsorted = [s['seq'] for s in snaps if [v[0] for v in s['v']] != sorted({v[0] for v in s['v']})]
        if not vort: return False, 'no v line'
        ok, bad, starts = not stray and not unsorted, [], []
        for n in sorted(vort):
            V = vort[n]; s0, x, y, z, sf = V[0]; sl = V[-1][4]; t0 = s0['e'][0]['t']
            stt = sf + t0 - 20; S = round(stt); starts.append(S)
            a = s0['a']; dy = None if a is None else y - (a[1] + 20); r = None if a is None else ((x - a[0]) ** 2 + (z - a[2]) ** 2) ** 0.5
            o = (abs(stt - S) <= V_INT and V_START[0] <= S <= V_START[1] and V_S_FIRST[0] <= sf <= V_S_FIRST[1]
                 and V_S_LAST[0] <= sl <= V_S_LAST[1] and dy is not None and abs(dy) <= V_DY and r <= V_R)
            if not o: bad.append(n)
            vrep.append(f'vortex {n}: start {S} ({stt:.2f}), first s {sf:.2f} at t {t0:.2f}, last s {sl:.2f}, y - actor y - 20 '
                        f'{dy if dy is None else round(dy, 3)}, distance {r if r is None else round(r, 1)} m, x {x:.3f} z {z:.3f}, {len(V)} lines')
        gaps = [b - a for a, b in zip(starts, starts[1:])]
        if any(not (V_GAP[0] <= g <= V_GAP[1]) for g in gaps): ok = False
        ok = ok and not bad
        return ok, (f'{len(vort)} vortices, start times {starts} (want {V_START[0]}..{V_START[1]}, gaps {V_GAP[0]}..{V_GAP[1]}: {gaps}), '
                    f'failing vortices {bad} (first s {V_S_FIRST[0]}..{V_S_FIRST[1]:.0f}, last s {V_S_LAST[0]}..{V_S_LAST[1]}, '
                    f'y +20 +/- {V_DY}, distance <= {V_R}), v lines outside the psi storm {len(stray)}, unsorted {len(unsorted)}')
    add('9', 'F2 vortices: v lines only in the psi storm, each from its start to its strike, s in range at the strike', c9)

    # ---- 10 F3 actor -------------------------------------------------------------------------------------------------
    def c10():
        hs = sorted({s['a'][3] for s in snaps if s['a']}); noa = sum(1 for s in snaps if not s['a'])
        vit = other['VITALS'][-1] if other['VITALS'] else None; sm = other['SUMMARY'][-1] if other['SUMMARY'] else None
        V = kv(vit) if vit else {}; Sm = kv(sm) if sm else {}
        okv = V.get('min_health') == '1.000' and V.get('max_radiation') == '0.0000' and V.get('max_bleeding') == '0.0000' and V.get('unread') == '0'
        oks = Sm.get('min_health') == '1.000' and Sm.get('refills') == '0' and Sm.get('hits') == Sm.get('cancelled')
        ok = hs == ['1.0000'] and noa == 0 and okv and oks
        return ok, (f'actor health values in {len(snaps)} snapshots {hs[:4]}, snapshots without a line {noa}; VITALS {vit}; '
                    f'SUMMARY hits {Sm.get("hits")} cancelled {Sm.get("cancelled")} refills {Sm.get("refills")} min_health {Sm.get("min_health")}')
    add('10', 'F3 the actor is unhurt through both events: health 1.0000 in every snapshot, VITALS clean, no refill', c10)

    # ---- 11 F4 eerr --------------------------------------------------------------------------------------------------
    def c11():
        if not other['T2SUM']: return False, 'no T2SUM line'
        line = other['T2SUM'][-1]; keys = [p.split('=', 1)[0] for p in line.split(',')[1:]]; d = kv(line)
        nz = [f'{k}={d.get(k)}' for k in T2SUM_ZERO if d.get(k) != '0']
        degr = [s['seq'] for s in snaps if s['e'] and s['e'][0]['kind'] == 'none' and s['ns'] and s['ns'][0] == (-1, -1)]
        ok = keys == T2SUM_KEYS and not nz and d.get('paused') == 'false' and not ev['EERR'] and not degr
        return ok, (f'T2SUM keys {"as registered (20, eerr last)" if keys == T2SUM_KEYS else keys}, eerr {d.get("eerr")}, nonzero '
                    f'{nz}, paused {d.get("paused")}, EERR lines {len(ev["EERR"])} {[e["line"] for e in ev["EERR"]][:1]}, '
                    f'degraded snapshots (e none + ns -1,-1) {len(degr)}')
    add('11', 'F4 eerr = 0 at the end, no EERR line, no degraded snapshot; every other T2SUM counter as an X-only run', c11)

    def c12():
        return not ev['ENOTREADY'] and not ev['EREADY'], f'ENOTREADY {len(ev["ENOTREADY"])}, EREADY {len(ev["EREADY"])}'
    add('12', 'F4 no ENOTREADY on a save load (desk prediction)', c12)

    # ---- 13 layout ---------------------------------------------------------------------------------------------------
    def c13():
        bad = []
        for s in snaps:
            o = s['order']; ke = s['e'][0]['kind'] if len(s['e']) == 1 else None
            seq_ok = len(s['e']) == 1 and len(s['ns']) == 1 and not s['k'] and bool(o) and o[0] == 'a'
            if seq_ok:
                ie, ins = o.index('e'), o.index('ns')
                pre, mid = o[1:ie], o[ie + 1:ins]
                if pre and pre[-1] == 'r': pre = pre[:-1]
                seq_ok = (all(x == 'o' for x in pre) and ins == len(o) - 1
                          and all(x == ('w' if ke == 'surge' else 'v' if ke == 'psi' else None) for x in mid))
            if not seq_ok: bad.append(s['seq'])
        return not bad, f'{len(snaps)} snapshots; not in the form a o* r? e (w|v)* ns: {len(bad)} {bad[:5]}'
    add('13', 'snapshot layout: one e and one ns in every v4 snapshot, w only in a surge, v only in a psi storm, no k line', c13)

    # ---- NO VERDICT rules ------------------------------------------------------------------------------------------
    nv = []
    first_ef = min((e['now'] for e in ev['EFORCE']), default=None)
    def nv1():
        pre = [s['seq'] for s in snaps if s['e'] and s['e'][0]['kind'] != 'none' and (first_ef is None or s['tx'] < first_ef)]
        foreign = []   # an ESTART with no EFORCE <kind>,started at or before it (a late one is check 3's, not this rule's)
        for k in ('surge', 'psi'):
            for e in st[k]:
                if not [f for f in ef[k] if f['res'] == 'started' and f['now'] <= e['now']]: foreign.append(f'{k}@{e["now"]}')
        many = {k: len(st[k]) for k in st if len(st[k]) > 1}
        return bool(pre or foreign or many), f'event snapshots before the first EFORCE {len(pre)}, ESTART without its EFORCE {foreign}, kinds started twice {many}'
    def nvref(k):
        return lambda: (bool(ef[k]) and ef[k][0]['res'] == 'notstarted', f'EFORCE {k} result {ef[k][0]["res"] if ef[k] else None}')
    def nv4():
        if not ef['psi'] or not ef['surge'] or not started['surge']: return False, 'not applicable'
        endn = en['surge'][0]['now'] if en['surge'] else None
        f = endn is None or ef['psi'][0]['now'] < endn
        return f, f'EFORCE psi at X {ef["psi"][0]["now"]}, EEND surge at X {endn}'
    def nv5():
        return started['psi'] and bool(esn['psi']) and not vort, f'v lines {sum(len(s["v"]) for s in snaps)} in {len(esn["psi"])} psi snapshots'
    for k, t, fn in (('NV1', 'an event the run did not force (before the first EFORCE, with no EFORCE started before it, or twice)', nv1),
                     ('NV2', 'the forced surge was refused by the game (EFORCE surge,notstarted: level, option or veto)', nvref('surge')),
                     ('NV3', 'the forced psi storm was refused by the game (EFORCE psi,notstarted)', nvref('psi')),
                     ('NV4', 'the psi storm was forced while the surge still ran (PSM:313 cancels it)', nv4),
                     ('NV5', 'the psi storm made no vortex at all (PSM:361 `==` can end the chain before the first)', nv5)):
        try: f, d = fn()
        except Exception as e: f, d = False, f'not judged: error {e!r}'
        nv.append((k, t, f, d))

    # ---- report ------------------------------------------------------------------------------------------------------
    out += [f'UNPARSED {u}' for u in unparsed[:20]] + ([f'UNPARSED ... {len(unparsed) - 20} more'] if len(unparsed) > 20 else [])
    for cid in ORDER_ID:
        t, ok, d = res[cid]; out.append(f'{cid} {t}: {"PASS" if ok else "FAIL"} ({d})')
    out += [f'{k} {t}: {"FIRED" if f else "clear"} ({d}){"; leaves untested: " + ", ".join(sorted(NV_UNTESTED[k])) if f else ""}'
            for k, t, f, d in nv]
    def ans(k):
        if not ef[k]: return 'no EFORCE line'
        r = ef[k][0]['res']
        return {'started': 'started', 'notstarted': 'REFUSED BY THE GAME (notstarted)'}.get(r, f'CALL FAILED ({r})')
    good_read = any(s['e'] and (s['e'][0]['kind'] != 'none' or (s['ns'] and s['ns'][0] != (-1, -1))) for s in snaps)
    out.append(f'F1 ANSWER: managers readable from the bridge {"yes" if good_read and not ev["EERR"] else "NO"} '
               f'(a non-degraded e/ns line seen: {good_read}; EERR {len(ev["EERR"])}); forced surge: {ans("surge")}; forced psi storm: {ans("psi")}')
    out.append(f'reported: F2 printed tf forms: surge {sorted(forms["surge"])}, psi {sorted(forms["psi"])}, none {sorted(forms["none"])}')
    for k in ('surge', 'psi'):
        out.append(f'reported: {k}: ESTART {[e["line"] for e in st[k]]}; EEND {[e["line"] for e in en[k]]}')
    out += ['reported: ' + x for x in wave_rep + vrep]
    ns0 = next((s['ns'][0] for s in snaps if s['ns']), None)
    if ns0: out.append(f'reported: ns at the first snapshot (game s to the next scheduled start): surge {ns0[0]}, psi {ns0[1]} '
                       f'(real s at factor 6: {ns0[0] / 6:.0f}, {ns0[1] / 6:.0f})')
    hk = {}
    for l in hits:
        f = l.split(',')
        if f[0] == 'h' and len(f) >= 7: hk[(f[2], f[3], f[5])] = hk.get((f[2], f[3], f[5]), 0) + 1
    out.append(f'reported: F3 actor hits seen by the bridge {len(hits)} lines; by (draftsman id, section, type): {sorted(hk.items())[:8]} '
               f'(under g_god none is expected: t3pfE3_prot had 0 in the same fields that gave 1230 with god off)')
    pts = sorted({p['p'] for p in poses if p['p']}); ap = [s['a'][:3] for s in snaps if s['a'] and s['tx'] >= (snaps[0]['tx'] + 20000)]
    if ap:
        dx = max(abs(a[0] - ap[0][0]) + abs(a[2] - ap[0][2]) for a in ap)
        out.append(f'reported: F3 pose points {pts[:2]}{" ..." if len(pts) > 2 else ""} ({len(pts)} distinct); actor x,z drift after the first 20 s '
                   f'{dx:.3f} m (|dx|+|dz|, max); first actor position {ap[0]}')
    out += f5(snaps, other, esn)
    out += f6(other, esn, snaps)
    out.append(f'reported: inputs {len(snaps)} snapshots, {len(poses)} poses, {len(meta_lines)} meta lines, {len(ef_rows)} emforce rows')

    fired = [k for k, _, f, _ in nv if f]
    failing = {cid for cid in ORDER_ID if not res[cid][1]}
    if unparsed: verdict, code = 'FAIL', 1; out.append('verdict: unparsable input')
    elif fired:
        U = set().union(*(NV_UNTESTED[k] for k in fired)); outside = failing - U
        if outside:
            verdict, code = 'FAIL', 1
            out.append(f'verdict: NO VERDICT rule(s) {", ".join(fired)} fired, but failing check(s) {", ".join(c for c in ORDER_ID if c in outside)} '
                       f'are outside what they leave untested: FAIL')
        else:
            verdict, code = 'NO VERDICT', 3
            out.append(f'verdict: NO VERDICT rule(s) {", ".join(fired)} fired; failing checks {", ".join(c for c in ORDER_ID if c in failing) or "none"} all within what they leave untested')
    elif not failing: verdict, code = 'PASS', 0
    else: verdict, code = 'FAIL', 1; out.append(f'verdict: failing checks {", ".join(c for c in ORDER_ID if c in failing)}')
    out.append(f'KJ3P: {verdict}')
    return out, code


def _tf_is(s, want):
    try: return abs(float(s) - want) < 1e-6
    except ValueError: return False


def f5(snaps, other, esn):
    """F5: counts only. Stalkers = o lines of kind s, alive 1, section without 'zombied'."""
    out = []
    def stalkers(s): return {i for i, o in s['o'].items() if o['kind'] == 's' and o['alive'] == '1' and 'zombied' not in o['sec']}
    for k in ('surge', 'psi'):
        S = esn[k]
        if not S: out.append(f'reported: F5 {k}: no event'); continue
        before = [s for s in snaps if s['tx'] < S[0]['tx']]; aft = [s for s in snaps if s['tx'] >= S[-1]['tx'] + 2000]
        b = stalkers(before[-1]) if before else set(); a_all = set(aft[0]['o']) if aft else set()
        a_alive = stalkers(aft[0]) if aft else set()
        gone = b - a_all; dead = (b & a_all) - a_alive
        x0, x1 = S[0]['tx'], S[-1]['tx'] + 2000
        offs = [l.split(',') for l in other['OFF']]; ons = [l.split(',') for l in other['ON']]
        off_s = sum(1 for f in offs if len(f) >= 4 and f[2].lstrip('-').isdigit() and x0 <= int(f[2]) <= x1 and f[3] in b)
        newz = [f[3] + ':' + f[4] for f in ons if len(f) >= 5 and f[2].lstrip('-').isdigit() and x0 <= int(f[2]) <= x1 and 'zombied' in f[4]]
        out.append(f'reported: F5 {k}: online stalkers before {len(b)}, after (2 s past the end) not listed {len(gone)}, listed but not alive '
                   f'{len(dead)}; OFF lines for them in the event {off_s}; new zombied ids ON in the event {len(newz)} {newz[:4]}')
    return out


def f6(other, esn, snaps):
    out = []
    sm = other['SUMMARY'][-1] if other['SUMMARY'] else None
    out.append(f'reported: F6 export cost this run: {sm}; reference {REF_KJ2}')
    win = {}
    for k in ('surge', 'psi'):
        if esn[k]: win[k] = (esn[k][0]['t'], esn[k][-1]['t'])
    prev = None
    for l in other['TICK']:
        f = l.split(','); d = kv(l)
        try: t, n, mu = float(f[1]), int(d['export_ticks']), float(d['export_mean_us'])
        except (ValueError, KeyError, IndexError): continue
        if prev:
            dn = n - prev[1]
            m = (n * mu - prev[1] * prev[2]) / dn if dn > 0 else None
            tag = [k for k, (a, b) in win.items() if a < t and b > prev[0]]
            out.append(f'reported: F6 track {prev[0]:.0f}..{t:.0f} s: {dn} exports, mean {m if m is None else round(m, 1)} us'
                       f'{" (overlaps " + "/".join(tag) + ")" if tag else ""}')
        prev = (t, n, mu)
    return out


def main():
    a = sys.argv[1:]
    if len(a) != 1 or '=' in a[0]:
        print('usage: kj3p_check.py <run dir>   (prints only; redirect to a file with set -C)'); print('KJ3P: REFUSED'); sys.exit(2)
    lines, code = full(a[0])
    sys.stdout.write('\n'.join(lines) + '\n'); sys.exit(code)


if __name__ == '__main__':
    main()
