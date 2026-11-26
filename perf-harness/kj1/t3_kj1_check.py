#!/usr/bin/env python3
"""Kill channel, joint run kj1, X side (PREREG-killchannel-joint-kj1-DRAFT-2026-11-26.md, sections 1.1, 3, 4, 6).
Run ONCE per run, with `python3 -I`, stdlib only:
  t3_kj1_check.py <X run dir> out=<report file>
  t3_kj1_check.py counters-only <meta_t2.csv> out=<report file> [poses=<poses_all.txt>] [snaps=<snaps_all.txt>]
<X run dir> = the folder the runner (tools/t2_xray_kw.sh) was given: it holds t2_xray.log, restarts.csv, stall.csv and
boot0/. Every file below is REQUIRED; a missing or empty one FAILS by name (never read as empty or 0):
  t2_xray.log  restarts.csv  stall.csv  boot0/meta_t2.csv  boot0/tb/xray.log
  boot0/watch/snaps_all.txt  boot0/watch/poses_all.txt     (tools/t1_watch_keep.py)
CR is stripped from every line of every file (Wine writes CRLF). The report is written to out= in the same call (the
file must not exist: the checker refuses to overwrite, exit 2) and printed.
Prints one line per check `<n> <text>: PASS|FAIL (<numbers>)`, the counter table `14.<key> ...: PASS|FAIL|NOT JUDGED`,
one line per NO VERDICT rule `NV<k> <text>: FIRED|clear (<numbers>)`, `reported:` lines, then
`KJ1 X SIDE: PASS|FAIL|NO VERDICT`. Exit 0 PASS, 1 FAIL, 3 NO VERDICT, 2 usage / refused.
Verdict: a missing, empty, unreadable or unparsable input -> FAIL. Else, if NO VERDICT rules fired: NO VERDICT only if EVERY
failing check is in the union of the fired rules' untested sets (table NV_UNTESTED below); a failing check outside it
-> FAIL, and the output names it. Else PASS only if every check passes. Checks 1, 12 and 13 are in no untested set, so
they are always judged.

S and M are found in the data: the ids that carry `k` lines in poses_all.txt; S = the one whose snapshot `o` kind is
`s`, M = the one whose kind is `m`. Every expected total and difference comes from the kept poses (the first kept pose
that carries each new total); no constant total is named here.

CLOCKS (each comparison uses one clock; the two cross-clock places say so):
  W  = watcher UTC ms: the `#snap,<utc>` / `#pose,<utc>` read times (t1_watch_keep.py, time.time_ns) AND stall.csv's
       STOP/CONT rows (the runner's `date +%s%N`). Both are Linux CLOCK_REALTIME on the same box: one clock.
  X  = X-Ray script time = time_global() ms: KAPPLY/KDEAD/KWHO field 2 (`now`), the snapshot header's t_xray_ms (field
       3), P2 field 4, RESUMED_GAP field 2 and field 6 (time_global of the last frame before the gap), G field 2.
       Within one frame the script reads the pose (K lines) BEFORE it writes the snapshot, so a snapshot with
       t_xray >= T was written after an event at T, one with t_xray < T before it. Exact, no mapping.
  P  = ITR2's pose header utc (field 3 of the pose header). Used only for differences between two poses (check 13).
  The cross-clock places: check 9 (d) maps an event's X time to W through the newest G/RESUMED_GAP line before it in
  the meta file (those lines carry os.time(), whole UTC seconds): W ~ wall*1000 + (T - tg), error [0, +1000) ms from
  the whole seconds; an event counts as inside the stop only if that whole interval lies inside the stop. Check 9 (c)
  pairs the script's RESUMED_GAP line (whole seconds) with stall.csv's rows the same way.
  time_global over the runner's stop: kj1's stop is NOT absorbed, so time_global JUMPS by the wall length of the stop
  (real: out/t2pre2/run/boot0/meta_t2.csv, 61654 -> RESUMED_GAP 361666 after 300 s; f_time_delta is clamped to 100 ms,
  time_global is not). No clause below depends on that: every "before/after the stop" in script time is taken from
  the script's own RESUMED_GAP line (its last frame before the gap = field 6, its first frame after = field 2), which
  is right whether time_global jumps or stands still.

BOUNDS AND MARGINS (name = value: where it is used; reason). Copy-ready for the registration:
  TOL_H          = 0.0002  : 2, 3, 5 ; health/total arithmetic: both healths read in one script step, two 4-decimal
                             roundings, doubled (prereg row 2)
  FALL           = 0.02    : 4, 6, NV2, NV3 ; a fall of health between two snapshots not caused by a hit of mine: far
                             under any hit (0.3 and up), far over rounding and regeneration in one snapshot step (row 4)
  HEAL_MIN       = 0.15    : 5 ; S's rise between its part hits: under half of kw1's 0.40 in 40 s, far over FALL (row 5)
  ALIVE_MIN      = 0.02    : 5 ; S's health after the hit that takes its total above 1.000: above the 0.01 stop (row 5)
  CONFIRM_MS     = 2000    : 7, 10 ; dead line read within 2.0 s: 0.15 s chain plus one frame, plus one 1.5 s stall (row 7)
  EARLY_MS       = 50      : 7 ; the dead line may be read up to 50 ms BEFORE the watcher's first read of the dead pose:
                             the watcher polls the pose file every 2 ms and reads the snapshot first in each poll, so
                             the script can take the pose (and the watcher read its snapshot) before the watcher reads
                             the pose. NOT in the prereg: a build decision, printed in check 7's line
  DEAD_AFTER_MS  = 2000    : 11 ; the death event is handled on a later frame (row 11)
  R_LAG_MS       = 2000    : 13 ; as 7 (row 13); the first snapshot at or after CONT gets R_LAG_MS + the stop length
  NEAR_M         = 10.0 m  : 6 ; other creatures judged within 10 m of S or M, reported beyond (row 6)
  STOP_MARGIN_MS = 100     : 9 (a) snapshots inside the stop, 9 (b) pairing of RESUMED_GAP with the stall rows, 9 (d),
                             13 (which snapshots count as inside the stop), 10 fallback only ; stall.csv's STOP row is
                             stamped just BEFORE `kill -STOP` and its CONT row just AFTER `kill -CONT` (one `date` fork
                             each), and the watcher reads up to ~2 ms after a write; far under the 20 s stop. NOT in the
                             prereg: a build decision, printed in check 9's line. Check 10 and NV4 use the exact stamps
  NV4_MS         = 2000    : NV4 ; M's first dead pose read >= 2 s after STOP and >= 2 s before CONT (section 1.1)
  WALL_ERR_MS    = 1000    : 9 (d) ; os.time() in G/RESUMED_GAP is whole seconds: the W estimate of an event is the
                             interval [wall*1000 + (T - tg), +1000); judged inside only if the whole interval is inside
  KILLER         = '0'     : 8 ; id 0 is the actor
"""
import sys, os, re, bisect, math

# ---- bounds: each from its row in section 4 of the prereg (the derivations are there) -------------------------------
TOL_H = 0.0002        # 2 (and 3, 5): health and total arithmetic, two 4-decimal roundings doubled
FALL = 0.02           # 4, 6, NV2, NV3: largest fall of health between two snapshots not caused by a hit
HEAL_MIN = 0.15       # 5: S's rise between its two part hits
ALIVE_MIN = 0.02      # 5: S's health after the hit that takes its total above 1.000
CONFIRM_MS = 2000     # 7, 10: dead line read within 2.0 s
EARLY_MS = 50         # 7: the dead line may be read up to 50 ms BEFORE the watcher's first read of the dead pose (the
                      #    watcher reads the pose file every 2 ms; the script can read it first). Not in the prereg:
                      #    stated in the build report
DEAD_AFTER_MS = 2000  # 11
R_LAG_MS = 2000       # 13
NEAR_M = 10.0         # 6: judged for other creatures within 10 m of S or M (o-line positions)
STOP_MARGIN_MS = 100  # 9 (a, b, d), 13, 10 fallback: see BOUNDS AND MARGINS in the docstring
WALL_ERR_MS = 1000    # 9 (d): os.time() whole seconds
NV4_MS = 2000         # NV (section 1.1): M's first dead pose read >= 2 s after STOP and >= 2 s before CONT
KILLER = '0'          # 8: id 0 is the actor
LUA_BRIDGE = 'zz_p1_puppet'   # the bridge script's installed name (t2_xray_kw.sh copies kill4 to zz_p1_puppet.script)
# NO VERDICT rules: the checks each rule leaves UNTESTED when it fires (one table). A failing check outside the union of
# the fired rules' sets makes the verdict FAIL. NV1 leaves 13 judged: check 13 uses no S/M (decision, see report).
NV_UNTESTED = {
    'NV1': set(range(2, 12)) | {14},   # identification: everything about S/M
    'NV2': set(range(2, 12)) | {14},   # S or M hurt or killed before its step
    'NV3': set(range(2, 12)) | {14},   # a fall of S or M with no K line of mine
    'NV4': {9, 10},                    # M's first dead pose not 2 s inside the stop at both ends
    'NV6': set(range(3, 12)) | {14},   # S or M not listed in X-Ray's snapshot at a later step's first pose
}
fmt_set = lambda st: ', '.join(str(x) for x in sorted(st))
MINE_KDEAD = ('online', 'offline', 'failed')   # a KDEAD of these kinds is the bridge acting ("a K line of mine");
                                               # `seen` / `released` are the bridge OBSERVING a death it did not deal

FILES = ('t2_xray.log', 'restarts.csv', 'stall.csv', 'boot0/meta_t2.csv', 'boot0/tb/xray.log',
         'boot0/watch/snaps_all.txt', 'boot0/watch/poses_all.txt')


def usage(msg):
    print(msg); print(__doc__.split('\n\n')[0]); sys.exit(2)


def read_lines(path):
    # decoding: errors='replace' for EVERY file (never raises). A replaced byte inside a kept record's number makes that
    # record UNPARSED (FAIL); the engine and runner logs are only searched as text. Unreadable files are caught by
    # read_status() before this is called.
    return open(path, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n')


def read_status(path):
    """'missing' | 'unreadable: <error>' | 'empty' | None (readable, non-empty)"""
    if not os.path.isfile(path): return 'missing'
    try:
        with open(path, 'rb') as f: data = f.read()
    except OSError as e:
        return f'unreadable: {e.strerror or e}'
    return 'empty' if not data.strip() else None


def fnum(s):
    v = float(s)
    if not math.isfinite(v): raise ValueError(f'non-finite number {s!r}')   # nan / inf: the record is UNPARSED
    return v


# ======================================================================================================================
# check 14's table. EVERY key of the last T2SUM line must have an entry here; a key without one FAILS by name, an entry
# whose key is absent from the line FAILS by name. Kinds: ('eq', value) | ('khits',) | ('last_seq',) | ('lines', meta
# line kind(s)) -- the last three are computed from the run's own files and printed with their derivation.
# ======================================================================================================================
COUNTERS = [
    ('last_seq', ('last_seq',), 'seq of the last pose the script took (p_last). Expected: at least the r seq of the last '
                                'kept snapshot (the script had processed that pose) and at most the highest seq in '
                                'poses_all.txt (it cannot take a pose that was never written)'),
    ('torn', ('eq', '0'), 'torn pose reads (prereg row 14)'),
    ('holds', ('lines', ('HOLD',)), 'poses held off-mesh: must equal the HOLD lines in the meta file (the counter and its '
                                    'own log agree; how often the actor is held is not a kill-channel criterion)'),
    ('paused', ('eq', 'false'), 'device():pause in force at the end: false (the runner passes no -t2_pause; the stop is '
                                'the runner\'s SIGSTOP)'),
    ('near', ('lines', ('NEAR', 'P2ring')), 'poses grounded on a neighbouring AI cell: must equal NEAR lines + P2 lines '
                                            'with ring > 0 (the counter and its own log agree; not a kill-channel '
                                            'criterion)'),
    ('guards', ('lines', ('GUARD',)), 'actor position guard fired: must equal the GUARD lines (the counter and its own '
                                      'log agree; not a kill-channel criterion)'),
    ('lost', ('eq', '0'), 'pose LOST events (prereg row 14)'),
    ('epochs', ('eq', '0'), 'new ITR2 sessions / v2 resets (prereg row 14: no restart of either side)'),
    ('khits', ('khits',), 'part hits applied = distinct (id, total) steps with a positive difference in the kept poses'),
    ('kdead_online', ('eq', '2'), 'S and M killed online (prereg row 14)'),
    ('kdead_offline', ('eq', '0'), 'no offline creature (prereg row 14)'),
    ('kowed', ('eq', '0'), 'no owed part hit (prereg row 14)'),
    ('kstale', ('eq', '0'), 'no stale line (prereg row 14)'),
    ('kback', ('eq', '0'), 'no falling total (prereg row 14)'),
    ('krefused', ('eq', '0'), 'nothing refused (prereg row 14)'),
    ('kclamped', ('eq', '0'), 'no single difference above 1.0 (prereg row 14)'),
    ('kver', ('eq', '0'), 'no pose of a wrong version (prereg row 14)'),
    ('kdead_fail', ('eq', '0'), 'no failed kill (prereg row 14)'),
    ('ktabfail', ('eq', '0'), 'no failed table save (prereg row 14; unjudged in kw1, judged here)'),
]
COUNTER_KEYS = [k for k, _, _ in COUNTERS]


def t2sum_pairs(line):
    """T2SUM,k=v,k=v,... -> list of (key, value) in line order, plus fields that are not k=v"""
    pairs, junk = [], []
    for p in line.split(',')[1:]:
        if '=' in p: k, v = p.split('=', 1); pairs.append((k, v))
        else: junk.append(p)
    return pairs, junk


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
    """distinct (id, total) steps with a positive difference, in kept-pose order, per id; returns (n, derivation)"""
    seen = {}
    for p in poses:
        for i, (_, ts, t, _) in p['k'].items():
            st = seen.setdefault(i, [])
            prev = st[-1][1] if st else 0.0
            if t > prev + 1e-9: st.append((ts, t))
    n = sum(len(v) for v in seen.values())
    der = '; '.join(f'id {i}: ' + ', '.join(ts for ts, _ in v) for i, v in sorted(seen.items())) or 'no k lines'
    return n, f'{n} = {der}'


def counter_table(t2_lines, meta, ctx):
    """ctx: 'khits' -> (n, derivation) or None; 'last_seq' -> (lo, hi, derivation) or None; None = not computable.
    Returns (ok, rows, summary). rows: printable lines `14.<key> ...`."""
    if not t2_lines:
        return False, [], 'no T2SUM line in the meta file'
    last = t2_lines[-1]
    pairs, junk = t2sum_pairs(last)
    have = {}
    dup = []
    for k, v in pairs:
        if k in have: dup.append(k)
        have[k] = v
    lc = meta_line_counts(meta)
    rows, bad, notj = [], [], []
    for key, kind, why in COUNTERS:
        if key not in have:
            rows.append(f'14.{key} absent from the last T2SUM: FAIL (the checker expects it: {why})'); bad.append(key); continue
        v = have[key]; st = None; exp = None
        if kind[0] == 'eq':
            exp = kind[1]; st = v == exp
        elif kind[0] == 'lines':
            n = sum(lc[x] for x in kind[1]); exp = f'{n} (' + ' + '.join(f'{lc[x]} {x}' for x in kind[1]) + ' lines in the meta file)'
            st = v == str(n)
        elif kind[0] == 'khits':
            if ctx.get('khits') is None: exp = 'not computable here (needs poses_all.txt)'
            else: n, der = ctx['khits']; exp = f'{n} (derivation: {der})'; st = v == str(n)
        elif kind[0] == 'last_seq':
            if ctx.get('last_seq') is None: exp = 'not computable here (needs poses_all.txt and snaps_all.txt)'
            else:
                lo, hi, der = ctx['last_seq']; exp = f'{lo} .. {hi} ({der})'
                st = v.isdigit() and lo is not None and hi is not None and lo <= int(v) <= hi
        if st is None:
            rows.append(f'14.{key} = {v}, expected {exp}: NOT JUDGED ({why})'); notj.append(key)
        else:
            rows.append(f'14.{key} = {v}, expected {exp}: {"PASS" if st else "FAIL"} ({why})')
            if not st: bad.append(key)
    for k, v in pairs:
        if k not in COUNTER_KEYS and k not in [b for b in bad]:
            rows.append(f'14.{k} = {v}, expected nothing: FAIL (no expectation in the checker for this key)'); bad.append(k)
    for k in sorted(set(dup)):
        rows.append(f'14.{k} appears twice in the last T2SUM: FAIL (one value per key)'); bad.append(k)
    for j in junk:
        rows.append(f'14.<{j}> field without key=value: FAIL'); bad.append(f'<{j}>')
    summ = (f'{len(t2_lines)} T2SUM line(s), last one judged: {len(pairs)} keys; failing: {", ".join(bad) or "none"}'
            + (f'; NOT JUDGED (counters-only mode, input not given): {", ".join(notj)}' if notj else ''))
    # a key NOT JUDGED happens only in counters-only mode (no poses/snaps given); the full checker always computes both
    return not bad, rows, summ


# ======================================================================================================================
# parsing of the kept files
# ======================================================================================================================
def records(lines, tag):
    """'#<tag>,<utc>,<seq>' then the record's lines. Returns (records, stray lines before the first marker)."""
    recs, stray, cur = [], [], None
    for l in lines:
        if l.startswith('#' + tag + ','):
            f = l.split(',')
            cur = {'u': f[1], 'mseq': f[2] if len(f) > 2 else '', 'lines': [], 'mark': l}; recs.append(cur)
        elif l == '':
            continue
        elif cur is None:
            stray.append(l)
        else:
            cur['lines'].append(l)
    return recs, stray


def parse_snap(r):
    L = r['lines']; h = L[0].split(',') if L else []
    if not (len(h) == 7 and h[0] == 'seq' and h[1] == r['mseq'] and L[-1] == f'end,{h[1]}' and h[6] == '4'):
        raise ValueError('header/end/version')
    s = {'u': int(r['u']), 'seq': int(h[1]), 'tx': int(h[2]), 't': h[3], 'level': h[4], 'run': h[5], 'a': None,
         'o': {}, 'r': None, 'k': {}}
    for l in L[1:-1]:
        f = l.split(',')
        if f[0] == 'a' and len(f) == 6: fnum(f[5]); s['a'] = f[5]
        elif f[0] == 'o' and len(f) == 12:
            s['o'][f[1]] = {'sec': f[2], 'kind': f[3], 'x': fnum(f[4]), 'y': fnum(f[5]), 'z': fnum(f[6]), 'h': fnum(f[8]),
                            'alive': f[9]}
        elif f[0] == 'r' and len(f) == 3: s['r'] = (f[1], int(f[2]))
        elif f[0] == 'k' and len(f) == 3: fnum(f[2]); s['k'][f[1]] = (f[2], False)
        elif f[0] == 'k' and len(f) == 4 and f[3] == 'dead': fnum(f[2]); s['k'][f[1]] = (f[2], True)
        else: raise ValueError('line ' + l)
    return s


def parse_pose(r):
    L = r['lines']; h = L[0].split(',') if L else []
    if not (len(h) == 8 and h[0] == 'seq' and h[1] == r['mseq'] and L[-1] == f'end,{h[1]}'):
        raise ValueError('header/end')
    p = {'u': int(r['u']), 'seq': int(h[1]), 'pu': int(h[2]), 'state': h[3], 'level': h[4], 'aw': h[5], 'sess': h[6],
         'ver': h[7], 'k': {}}
    for l in L[1:-1]:
        f = l.split(',')
        if f[0] == 'k':
            if len(f) != 5 or f[4] not in ('0', '1'): raise ValueError('k line ' + l)
            p['k'][f[1]] = (f[2], f[3], fnum(f[3]), f[4])
    return p


# ======================================================================================================================
def main():
    args = sys.argv[1:]
    opt = dict(a.split('=', 1) for a in args if '=' in a)
    pos = [a for a in args if '=' not in a]
    out_path = opt.get('out')
    if not out_path: usage('usage error: out=<report file> is required (score once, output to a file in that call)')
    if os.path.exists(out_path): print(f'REFUSED: {out_path} exists; the checker never overwrites a report'); sys.exit(2)
    if pos and pos[0] == 'counters-only':
        if len(pos) != 2: usage('usage error: counters-only <meta_t2.csv>')
        lines, code = counters_only(pos[1], opt)
    elif len(pos) == 1:
        lines, code = full(pos[0])
    else:
        usage('usage error')
    text = '\n'.join(lines) + '\n'
    print(text, end='')
    with open(out_path, 'x') as f: f.write(text)
    sys.exit(code)


def load_poses(path):
    recs, stray = records(read_lines(path), 'pose'); poses, bad = [], []
    for r in recs:
        try: poses.append(parse_pose(r))
        except (ValueError, IndexError) as e: bad.append(f'{r["mark"]}: {e}')
    return poses, bad + [f'stray line: {s}' for s in stray]


def load_snaps(path):
    recs, stray = records(read_lines(path), 'snap'); snaps, bad = [], []
    for r in recs:
        try: snaps.append(parse_snap(r))
        except (ValueError, IndexError) as e: bad.append(f'{r["mark"]}: {e}')
    return snaps, bad + [f'stray line: {s}' for s in stray]


def counters_only(meta_path, opt):
    out = [f'COUNTERS ONLY (check 14 logic, descriptive): meta {meta_path}']
    for nm, p in (('meta', meta_path), ('poses', opt.get('poses')), ('snaps', opt.get('snaps'))):
        st = read_status(p) if p is not None else None
        if st:
            out.append(f'{st.split(":")[0].upper()} {nm} file {p} ({st})'); out.append('KJ1 COUNTERS ONLY: FAIL'); return out, 1
    meta = [l for l in read_lines(meta_path) if l]
    ctx = {'khits': None, 'last_seq': None}
    poses = snaps = None
    if opt.get('poses'):
        poses, bp = load_poses(opt['poses']); ctx['khits'] = khits_expect(poses)
        if bp: out.append(f'UNPARSED poses file {opt["poses"]}: {len(bp)} record(s), first {bp[:3]}')
    if opt.get('snaps'):   # parsed whenever supplied (a parse error FAILS); last_seq needs poses as well
        snaps, bs = load_snaps(opt['snaps'])
        if bs: out.append(f'UNPARSED snaps file {opt["snaps"]}: {len(bs)} record(s), first {bs[:3]}')
        if poses is not None: ctx['last_seq'] = last_seq_expect(poses, snaps)
    t2 = [l for l in meta if l.startswith('T2SUM,')]
    ok, rows, summ = counter_table(t2, meta, ctx)
    out.append(f'14 counters, every key of the last T2SUM judged: {"PASS" if ok else "FAIL"} ({summ})')
    out += rows
    out.append(f'reported: last T2SUM line: {t2[-1] if t2 else None}')
    bad_input = any(l.startswith('UNPARSED ') for l in out)   # a supplied file that does not parse FAILS, never ignored
    ok = ok and not bad_input
    if bad_input: out.append('verdict: a supplied poses=/snaps= file does not parse')
    out.append(f'KJ1 COUNTERS ONLY: {"PASS" if ok else "FAIL"}')
    return out, 0 if ok else 1


def last_seq_expect(poses, snaps):
    """lower bound = the r seq of the FINAL kept snapshot only (not the last r seen anywhere): if the final snapshot has
    no r line, the bound cannot be formed and last_seq FAILS, named"""
    hi = max((p['seq'] for p in poses), default=None)
    fin = snaps[-1] if snaps else None
    lo = fin['r'][1] if fin and fin['r'] else None
    der = (f'r seq of the final kept snapshot (seq {fin["seq"] if fin else None}): '
           f'{lo if lo is not None else "NONE: the final snapshot has no r line, so the bound FAILS"}; highest seq in poses_all.txt {hi}')
    return lo, hi, der


def full(run_dir):
    paths = {n: os.path.join(run_dir, n) for n in FILES}
    status = {n: read_status(paths[n]) for n in FILES}
    missing = [n for n in FILES if status[n] == 'missing']
    empty = [n for n in FILES if status[n] == 'empty']
    unreadable = [n for n in FILES if status[n] and status[n].startswith('unreadable')]
    gone = set(missing) | set(empty) | set(unreadable)
    rd = lambda n: [] if n in gone else read_lines(paths[n])

    res = []   # (n, text, ok, numbers)
    def add(n, text, fn, needs):
        g = [f for f in needs if f in gone]
        if g: res.append((n, text, False, 'missing, empty or unreadable: ' + ', '.join(g))); return None
        try:
            ok, nums = fn()
        except Exception as e:   # a check that cannot be computed FAILS, with the reason
            ok, nums = False, f'error while judging: {e!r}'
        res.append((n, text, bool(ok), nums))

    # ---- inputs ------------------------------------------------------------------------------------------------------
    meta = [l for l in rd('boot0/meta_t2.csv') if l]
    xlog = rd('boot0/tb/xray.log'); runner_log = [l for l in rd('t2_xray.log') if l.strip()]
    restart_rows = [l for l in rd('restarts.csv')[1:] if l.strip()]
    stall_lines = [l for l in rd('stall.csv') if l.strip()]
    poses, bad_p = ([], []) if 'boot0/watch/poses_all.txt' in gone else load_poses(paths['boot0/watch/poses_all.txt'])
    snaps, bad_s = ([], []) if 'boot0/watch/snaps_all.txt' in gone else load_snaps(paths['boot0/watch/snaps_all.txt'])
    unparsed = []
    if bad_p: unparsed.append(f'UNPARSED boot0/watch/poses_all.txt: {len(bad_p)} record(s), first {bad_p[:2]}')
    if bad_s: unparsed.append(f'UNPARSED boot0/watch/snaps_all.txt: {len(bad_s)} record(s), first {bad_s[:2]}')
    txs = [s['tx'] for s in snaps]
    if any(b < a for a, b in zip(txs, txs[1:])): unparsed.append('UNPARSED boot0/watch/snaps_all.txt: t_xray_ms goes back')
    if any(b['u'] < a['u'] for a, b in zip(snaps, snaps[1:])): unparsed.append('UNPARSED boot0/watch/snaps_all.txt: read times go back')

    # meta events (K lines, G, RESUMED_GAP, P2, T2SUM); index = position in the meta file (execution order)
    ev = {'KAPPLY': [], 'KDEAD': [], 'KWHO': [], 'G': [], 'RG': [], 'P2': [], 'KOTHER': []}
    bad_m = []
    for i, l in enumerate(meta):
        f = l.split(',')
        try:
            if f[0] == 'KAPPLY':
                if len(f) != 9: raise ValueError
                ev['KAPPLY'].append({'i': i, 'T': int(f[1]), 'n': int(f[2]), 'id': f[3], 'd': fnum(f[4]), 'ap': fnum(f[5]),
                                     'hb': fnum(f[6]), 'ha': fnum(f[7]), 'how': f[8], 'line': l})
            elif f[0] == 'KDEAD':
                ev['KDEAD'].append({'i': i, 'T': int(f[1]), 'n': int(f[2]), 'id': f[3], 'kind': f[4], 'line': l})
            elif f[0] == 'KWHO':
                ev['KWHO'].append({'i': i, 'T': int(f[1]), 'id': f[2], 'killer': f[3], 'line': l})
            elif f[0] in ('KOWED', 'KSTALE', 'KBACK', 'KREFUSED', 'KTAB'):
                ev['KOTHER'].append({'i': i, 'line': l})
            elif f[0] == 'G':
                ev['G'].append({'i': i, 'tg': int(f[1]), 'wall': int(f[2])})
            elif f[0] == 'RESUMED_GAP':
                ev['RG'].append({'i': i, 'tg': int(f[1]), 'wall': int(f[2]), 'gap': int(f[3]), 'prev_tg': int(f[5]), 'line': l})
            elif f[0] == 'P2':
                ev['P2'].append({'i': i, 'n': int(f[1]), 'T': int(f[3])})
        except (ValueError, IndexError):
            bad_m.append(l)
    if bad_m: unparsed.append(f'UNPARSED boot0/meta_t2.csv: {len(bad_m)} line(s), first {bad_m[:2]}')
    t2sum = [l for l in meta if l.startswith('T2SUM,')]

    # ---- S and M from the data ---------------------------------------------------------------------------------------
    kids = sorted({i for p in poses for i in p['k']})
    kinds = {i: sorted({s['o'][i]['kind'] for s in snaps if i in s['o']}) for i in kids}
    nv1 = []
    if len(kids) != 2: nv1.append(f'{len(kids)} id(s) with k lines in poses_all.txt {kids} (exactly 2 needed)')
    for i in kids:
        if not kinds[i]: nv1.append(f'id {i} never listed in a snapshot o line (kind unknown)')
        elif len(kinds[i]) > 1: nv1.append(f'id {i} listed with kinds {kinds[i]}')
    S = M = None
    if not nv1:
        ks = {i: kinds[i][0] for i in kids}
        st = [i for i in kids if ks[i] == 's']; mo = [i for i in kids if ks[i] == 'm']
        if len(st) == 1 and len(mo) == 1: S, M = st[0], mo[0]
        else: nv1.append(f'kinds {ks}: not one stalker (s) and one monster (m)')
    roles = {'S': S, 'M': M}

    # per id: steps = first kept pose carrying each new (higher) total; first dead pose
    def steps_of(i):
        out = []
        for p in poses:
            if i in p['k']:
                sec, ts, t, dead = p['k'][i]
                last = out[-1] if out else None
                if t > (last['tot'] if last else 0.0) + 1e-9:
                    out.append({'tot': t, 'ts': ts, 'dead': dead, 'seq': p['seq'], 'u': p['u'], 'pu': p['pu'], 'rise': True})
                elif dead == '1' and last is not None and last['dead'] == '0':
                    # a death at an UNCHANGED total (dead 0 -> 1, no difference): still a step (later-step logic, NV6, the
                    # death step), but no part hit: khits_expect() counts rises only
                    out.append({'tot': last['tot'], 'ts': last['ts'], 'dead': dead, 'seq': p['seq'], 'u': p['u'], 'pu': p['pu'], 'rise': False})
        return out
    def first_dead(i):
        return next((p for p in poses if i in p['k'] and p['k'][i][3] == '1'), None)
    steps = {i: steps_of(i) for i in kids}
    live = {i: [s for s in steps[i] if s['dead'] == '0'] for i in kids}
    kap = lambda i: [k for k in ev['KAPPLY'] if k['id'] == i]
    kdead_on = lambda i: [k for k in ev['KDEAD'] if k['id'] == i and k['kind'] == 'online']
    mine = lambda i: [k for k in ev['KAPPLY'] if k['id'] == i] + [k for k in ev['KDEAD'] if k['id'] == i and k['kind'] in MINE_KDEAD]
    def kap_of(i, tot):
        return [k for k in kap(i) if abs(k['ap'] - tot) <= TOL_H]
    def snap_at_or_after(T, i=None):   # first snapshot written at or after script time T (and listing i)
        j = bisect.bisect_left(txs, T)
        for s in snaps[j:]:
            if i is None or i in s['o']: return s
        return None
    def snap_before(T, i=None):        # last snapshot written before script time T (and listing i)
        j = bisect.bisect_left(txs, T)
        for s in reversed(snaps[:j]):
            if i is None or i in s['o']: return s
        return None
    def first_dead_line(i):
        return next((s for s in snaps if i in s['k'] and s['k'][i][1]), None)
    def need_roles():
        if not (S and M): raise RuntimeError('S and M not identified (NV1)')

    # stall window (W clock)
    stop = cont = None; stall_err = []
    if stall_lines:
        if stall_lines[0] != 'utc_ms,event,seq,track_s': stall_err.append(f'header {stall_lines[0]!r}')
        rows = [l.split(',') for l in stall_lines[1:]]
        sr = [r for r in rows if len(r) == 4 and r[0].isdigit() and r[1] == 'STOP']
        cr = [r for r in rows if len(r) == 4 and r[0].isdigit() and r[1] == 'CONT']
        other = [r for r in rows if r not in sr and r not in cr]
        if len(sr) != 1 or len(cr) != 1 or other: stall_err.append(f'{len(sr)} STOP, {len(cr)} CONT, {len(other)} other rows')
        else:
            stop, cont = int(sr[0][0]), int(cr[0][0])
            if not (cont > stop and rows.index(sr[0]) < rows.index(cr[0])): stall_err.append(f'CONT {cont} not after STOP {stop}'); stop = cont = None
    inside = (lambda u: stop is not None and stop + STOP_MARGIN_MS < u < cont - STOP_MARGIN_MS)
    # the script's own record of the stop: the RESUMED_GAP line whose last frame before the gap (wall - gap, whole s) is
    # at or before STOP and whose first frame after (wall) is at or after CONT, each within STOP_MARGIN_MS + whole seconds
    rg = None
    if stop is not None:
        cands = [g for g in ev['RG'] if (g['wall'] - g['gap']) * 1000 <= stop + STOP_MARGIN_MS and g['wall'] * 1000 + 999 >= cont - STOP_MARGIN_MS]
        rg = cands[0] if cands else None
    # S dead at its second step (prereg C): the first pose that raises S's total after step A already has dead = 1
    s_dead2 = bool(S) and len(steps[S]) > 1 and steps[S][0]['dead'] == '0' and steps[S][1]['dead'] == '1' and steps[S][1]['rise']
    S_NJ = 'not judged for S: S did not heal enough: dead at the second hit (check 5 fails)'

    SN, PO, ME = 'boot0/watch/snaps_all.txt', 'boot0/watch/poses_all.txt', 'boot0/meta_t2.csv'

    # ---- check 1 -----------------------------------------------------------------------------------------------------
    def c1():
        lua_err = [l for l in xlog if re.search(r'LUA error|lua error', l) and LUA_BRIDGE in l] + \
                  [xlog[i] for i in range(len(xlog)) if LUA_BRIDGE + '.script (line' in xlog[i] and
                   not any("doesn't exist" in xlog[j] or 'nil function' in xlog[j] for j in range(max(0, i - 8), i))]
        fatal = [l for l in xlog if 'FATAL' in l]
        rc0 = bool(runner_log) and runner_log[-1].rstrip().endswith('rc=0')
        xl = sum(1 for l in xlog if l.strip())
        boots = sorted(d for d in (os.listdir(run_dir) if os.path.isdir(run_dir) else []) if re.match(r'boot\d+$', d))
        ok = rc0 and not restart_rows and not lua_err and not fatal and xl > 0 and boots == ['boot0']
        return ok, (f'runner rc0 {rc0} (last line {runner_log[-1][-60:] if runner_log else None!r}), restart rows {len(restart_rows)}, '
                    f'Lua errors naming {LUA_BRIDGE} {len(lua_err)} {lua_err[:2]}, FATAL {len(fatal)}, engine log lines {xl}, boots {boots}')
    add(1, 'no crash, no FATAL, no Lua error naming the bridge script, runner rc 0, one boot', c1,
        ('t2_xray.log', 'restarts.csv', 'boot0/tb/xray.log'))

    # ---- check 2: step A ---------------------------------------------------------------------------------------------
    def c2():
        need_roles(); ok = True; out = []
        for r, i in roles.items():
            if not steps[i] or steps[i][0]['dead'] != '0':
                ok = False; out.append(f'{r} {i}: no part-hit total before its death in the poses'); continue
            a = steps[i][0]; ks = kap_of(i, a['tot']); allk = kap(i)
            if len(ks) != 1:
                ok = False; out.append(f'{r} {i}: {len(ks)} KAPPLY with applied {a["ts"]} (first pose seq {a["seq"]})'); continue
            k = ks[0]; dd = abs(k['d'] - a['tot']); dap = abs(k['ap'] - a['tot']); dh = abs(k['ha'] - (k['hb'] - k['d']))
            g = dd <= TOL_H and dap <= TOL_H and dh <= TOL_H and k['n'] >= a['seq'] and allk and allk[0] is k
            ok = ok and g
            out.append(f'{r} {i}: pose total {a["ts"]} first in pose seq {a["seq"]}; KAPPLY pose {k["n"]} d {k["d"]:.3f} '
                       f'applied {k["ap"]:.3f} before {k["hb"]:.4f} after {k["ha"]:.4f}: |d-total| {dd:.4f}, '
                       f'|after-(before-d)| {dh:.4f}, first KAPPLY of the id {bool(allk) and allk[0] is k}, from pose >= first {k["n"] >= a["seq"]}')
        return ok, ' | '.join(out)
    add(2, 'step A: one KAPPLY each for S and M, difference = the pose total, after = before - difference +/- 0.0002', c2, (PO, SN, ME))

    # ---- check 3: no second effect -----------------------------------------------------------------------------------
    def c3():
        bad = []; n = 0
        pseq = [p['seq'] for p in poses]
        for i in sorted({k['id'] for k in ev['KAPPLY']}):
            prev = 0.0
            for k in kap(i):
                n += 1
                # the newest kept pose at or before the KAPPLY's pose seq (= that pose, unless the watcher missed it);
                # it must carry the id
                j = bisect.bisect_right(pseq, k['n']) - 1
                tot = poses[j]['k'][i][2] if j >= 0 and i in poses[j]['k'] else None
                if tot is None or abs(k['ap'] - tot) > TOL_H or k['ap'] <= prev + TOL_H:
                    bad.append(f'id {i} pose {k["n"]} applied {k["ap"]:.3f}: pose total then {tot}, applied before {prev:.3f}')
                prev = max(prev, k['ap'])
        return not bad, f'{n} KAPPLY lines; a KAPPLY whose total is not a new pose total: {len(bad)} {bad[:4]}'
    add(3, 'no second effect: no KAPPLY for an id while its total in poses_all.txt is unchanged', c3, (PO, ME))

    # ---- check 4: healing is left alone --------------------------------------------------------------------------
    def interval(i, r):
        """(T_a, T_b, end KAPPLY/KDEAD) in script time: S from its step-A KAPPLY to its second part-hit KAPPLY; M from its
        step-A KAPPLY to its death step (its KDEAD online)"""
        a = steps[i][0] if steps[i] else None
        ka = kap_of(i, a['tot']) if a else []
        if not ka: return None, f'{r}: no step-A KAPPLY'
        if r == 'S' and not s_dead2:
            if len(live[i]) < 2: return None, f'{r}: no second part-hit total in the poses'
            kb = kap_of(i, live[i][1]['tot'])
            if not kb: return None, f'{r}: no KAPPLY for the second total {live[i][1]["ts"]}'
            return (ka[0]['T'], kb[0]['T'], ka[0], kb[0]), ''
        # M, or S dead at its second step: up to the death step (its KDEAD online)
        kd = kdead_on(i)
        if not kd: return None, f'{r}: no KDEAD online (death step)'
        return (ka[0]['T'], kd[0]['T'], ka[0], kd[0]), ''
    def falls(i, ta, tb):
        """consecutive snapshots with ta <= t_xray < tb listing i: (prev, next, fall, K lines of mine between)"""
        ss = [s for s in snaps if ta <= s['tx'] < tb and i in s['o']]
        out = []
        for p, q in zip(ss, ss[1:]):
            fl = p['o'][i]['h'] - q['o'][i]['h']
            if fl > FALL:
                kl = [k for k in mine(i) if p['tx'] < k['T'] <= q['tx']]
                out.append((p, q, fl, kl))
        return ss, out
    def c4():
        need_roles(); ok = True; out = []
        for r, i in roles.items():
            iv, why = interval(i, r)
            if not iv: ok = False; out.append(why + ' (interval not found)'); continue
            ta, tb, ka, kb = iv
            extra = [k for k in kap(i) if ta < k['T'] < tb and k is not ka and k is not kb]
            ss, fl = falls(i, ta, tb)
            failf = [f'{p["seq"]}->{q["seq"]} fall {x:.4f} with {[k["line"] for k in kl]}' for p, q, x, kl in fl if kl]
            nvf = [f'{p["seq"]}->{q["seq"]} fall {x:.4f}' for p, q, x, kl in fl if not kl]
            g = not extra and not failf and len(ss) >= 2; ok = ok and g
            hs = [s['o'][i]['h'] for s in ss]
            mx = max([0.0] + [a - b for a, b in zip(hs, hs[1:])])   # 0 = it never fell
            out.append(f'{r} {i}: script time {ta}..{tb}, {len(ss)} snapshots, largest fall {mx:.4f}, change first->last '
                       f'{(hs[-1] - hs[0]) if hs else 0:+.4f}{" (healing reported, judged on S only)" if r == "M" else ""}; '
                       f'other KAPPLY {len(extra)} {[k["line"] for k in extra][:2]}; falls > {FALL} with a K line of mine '
                       f'{failf[:2]}; without (NV3, not FAIL) {nvf[:2]}')
        return ok, ' | '.join(out)
    add(4, f'healing left alone: S between its part hits, M between its part hit and death: no KAPPLY, no fall > {FALL} '
           'with a K line of mine', c4, (PO, SN, ME))

    # ---- check 5: S healed, and a total above 1.000 applied -------------------------------------------------------
    def listed_alive_at(i, u):   # newest kept snapshot read at or before W time u: lists i alive?
        sb = [x for x in snaps if x['u'] <= u]
        return bool(sb) and i in sb[-1]['o'] and sb[-1]['o'][i]['alive'] == '1', (sb[-1]['seq'] if sb else None)
    def c5():
        need_roles(); i = S; out = []; ok = True
        if len(steps[i]) < 2: return False, f'S {i} has no step after step A in the poses'
        iv, why = interval(i, 'S')
        if not iv: return False, why
        ta, tb, ka, kb = iv
        s1 = snap_at_or_after(ta, i); s2 = snap_before(tb, i)
        if not s1 or not s2: return False, 'no snapshot after the first or before the second step'
        heal = s2['o'][i]['h'] - s1['o'][i]['h']
        hpart = (f'healing: snap {s1["seq"]} {s1["o"][i]["h"]:.4f} -> snap {s2["seq"]} {s2["o"][i]["h"]:.4f} = '
                 f'{heal:+.4f} (floor {HEAL_MIN})')
        if s_dead2:
            st = steps[i][1]
            return False, (f'S did not heal enough: dead at the second hit (pose seq {st["seq"]} raises the total to '
                           f'{st["ts"]} with dead = 1); {hpart}; checks 7, 8, 11 are not judged for S')
        g = heal >= HEAL_MIN - 1e-9; ok = ok and g; out.append(hpart)
        hi = None
        for j, st in enumerate(steps[i]):
            if st['dead'] == '0' and st['tot'] > 1.0 + 1e-9:
                al, sq = listed_alive_at(i, st['u'])
                if al: hi = j; break
        if hi is None:
            return False, '; '.join(out + ['no S total above 1.000 was sent while S was alive (dead = 0 in the pose, '
                                           'S listed alive in the newest snapshot read at or before that pose)'])
        st = steps[i][hi]; prev = steps[i][hi - 1]['tot'] if hi > 0 else 0.0; want = st['tot'] - prev
        kk = kap_of(i, st['tot'])
        if not kk: return False, '; '.join(out + [f'total above 1.000: pose total {st["ts"]} (seq {st["seq"]}) has no KAPPLY'])
        k = kk[0]; sa = snap_at_or_after(k['T'], i)
        dd = abs(k['d'] - want); dh = abs(k['ha'] - (k['hb'] - k['d']))
        alive = sa is not None and sa['o'][i]['alive'] == '1' and sa['o'][i]['h'] > ALIVE_MIN
        g = dd <= TOL_H and dh <= TOL_H and alive; ok = ok and g
        out.append(f'total above 1.000: pose {st["ts"]} (seq {st["seq"]}), previous total {prev:.3f}, expected difference '
                   f'{want:.3f}; KAPPLY pose {k["n"]} d {k["d"]:.3f} (not cut to 1.000: {dd <= TOL_H}) applied {k["ap"]:.3f} '
                   f'before {k["hb"]:.4f} after {k["ha"]:.4f} (|after-(before-d)| {dh:.4f}); next snapshot '
                   f'{sa["seq"] if sa else None}: alive {sa["o"][i]["alive"] if sa else None} health '
                   f'{sa["o"][i]["h"] if sa else None} (must be alive and > {ALIVE_MIN}); kclamped itself is judged in 14')
        return ok, '; '.join(out)
    add(5, f'S healed (>= {HEAL_MIN} between its part hits) and a total above 1.000 is applied with the pose difference, '
           f'S alive above {ALIVE_MIN} after it', c5, (PO, SN, ME))

    # ---- check 6: only S and M change ------------------------------------------------------------------------------
    def c6():
        need_roles(); judged = []; rep = []; nev = 0; unj = []; last_pos = {}
        def pos(s, i):
            if i in s['o']: return (s['o'][i]['x'], s['o'][i]['y'], s['o'][i]['z'])
            return last_pos.get(i)
        for e in sorted(ev['KAPPLY'] + ev['KDEAD'], key=lambda e: e['i']):
            b = snap_before(e['T']); a = snap_at_or_after(e['T'])
            if not b or not a: unj.append(e['line']); continue
            nev += 1
            for i in (S, M):
                p = pos(b, i) or pos(a, i)
                if p: last_pos[i] = p
            for o in sorted(set(b['o']) & set(a['o'])):
                if o in (S, M, '0'): continue
                fl = b['o'][o]['h'] - a['o'][o]['h']
                if fl <= FALL: continue
                d = []
                for s_ in (b, a):
                    for i in (S, M):
                        p = pos(s_, i)
                        if p: d.append(((s_['o'][o]['x'] - p[0]) ** 2 + (s_['o'][o]['y'] - p[1]) ** 2 + (s_['o'][o]['z'] - p[2]) ** 2) ** 0.5)
                dm = min(d) if d else None
                t = f'{o} {b["o"][o]["sec"]} fell {fl:.4f} (snap {b["seq"]}->{a["seq"]}, around {e["line"]}), {dm if dm is None else round(dm, 1)} m from S/M'
                (judged if dm is not None and dm <= NEAR_M else rep).append(t)
        return not judged, (f'{nev} KAPPLY/KDEAD lines with a snapshot on each side ({len(unj)} without: {unj[:2]}); '
                            f'falls > {FALL} within {NEAR_M} m of S or M (judged): {judged[:3]}; farther (reported): {rep[:3]}')
    add(6, f'only S and M change: no other creature within {NEAR_M} m of S or M loses more than {FALL} across a KAPPLY/KDEAD', c6, (PO, SN, ME))

    # ---- check 7: step C, S dies -----------------------------------------------------------------------------------
    def c7():
        need_roles(); i = S
        if s_dead2: return True, S_NJ
        on = kdead_on(i); fp = first_dead(i); ds = first_dead_line(i)
        lag = ds['u'] - fp['u'] if fp and ds else None
        ok = len(on) == 1 and lag is not None and -EARLY_MS <= lag <= CONFIRM_MS
        return ok, (f'S {i}: KDEAD online {len(on)}; first dead pose seq {fp["seq"] if fp else None} read {fp["u"] if fp else None}; '
                    f'first snapshot with k,{i},...,dead seq {ds["seq"] if ds else None} read {ds["u"] if ds else None}; '
                    f'lag {lag} ms (W clock both; allowed -{EARLY_MS} .. {CONFIRM_MS}; -{EARLY_MS} ms because the watcher polls the pose '
                    f'every 2 ms and may read it after the script took it and the snapshot was read)')
    add(7, 'step C: one KDEAD online for S; its dead line read within 2.0 s of the first pose with dead = 1', c7, (PO, SN, ME))

    # ---- check 8: the killer ------------------------------------------------------------------------------------------
    def c8():
        need_roles(); ok = True; out = []
        for r, i in roles.items():
            if r == 'S' and s_dead2: out.append(f'S {i}: {S_NJ}'); continue
            w = [k for k in ev['KWHO'] if k['id'] == i]
            g = len(w) == 1 and w[0]['killer'] == KILLER; ok = ok and g
            out.append(f'{r} {i}: KWHO {[k["line"] for k in w]}')
        return ok, ' | '.join(out)
    add(8, 'the kill is the actor\'s: one KWHO with killer 0 for S and for M', c8, (PO, SN, ME))

    # ---- check 9: step D, the stop held ------------------------------------------------------------------------------
    def c9():
        if stall_err or stop is None: return False, f'stall.csv: {stall_err or "no window"}'
        m = STOP_MARGIN_MS
        ins = [s['seq'] for s in snaps if inside(s['u'])]
        rg_in = [g['line'] for g in ev['RG'] if g['wall'] * 1000 > stop + m and g['wall'] * 1000 + 999 < cont - m]
        anchors = sorted(ev['G'] + ev['RG'], key=lambda g: g['i'])
        ai = [g['i'] for g in anchors]
        mapped_in, unanch, st_in = [], 0, []
        for e in ev['KAPPLY'] + ev['KDEAD']:
            if rg and rg['prev_tg'] < e['T'] < rg['tg']: st_in.append(e['line'])   # script time: needs no wall anchor
            j = bisect.bisect_left(ai, e['i']) - 1
            if j < 0: unanch += 1; continue                                        # the wall mapping needs one
            g = anchors[j]; lo = g['wall'] * 1000 + (e['T'] - g['tg']); hi = lo + WALL_ERR_MS
            if lo > stop + m and hi < cont - m: mapped_in.append(f'{e["line"]} (W {lo}..{hi})')
        ok = not ins and rg is not None and not rg_in and not mapped_in and not st_in
        return ok, (f'stop W {stop}..{cont} ({cont - stop} ms; edges +/- {m} ms because the STOP row is stamped before '
                    f'kill -STOP, the CONT row after kill -CONT, and the watcher reads up to ~2 ms late); snapshots read inside {len(ins)} {ins[:4]}; '
                    f'the script\'s own record of the stop (RESUMED_GAP whose gap covers it) '
                    f'{rg["line"] if rg else "NONE"}; RESUMED_GAP lines inside the stop {rg_in[:2]}; KAPPLY/KDEAD with script time '
                    f'inside the stop in script time (between RESUMED_GAP\'s last frame before and first frame after) {st_in[:2]}; '
                    f'KAPPLY/KDEAD whose script time maps wholly inside the stop (via the newest G/RESUMED_GAP line, whole UTC '
                    f'seconds, error 0..+1000 ms) {mapped_in[:2]}; events with no anchor line before them {unanch}')
    add(9, 'step D: no KAPPLY or KDEAD inside the stop, no snapshot written inside the stop (stall.csv window)', c9,
        ('stall.csv', SN, ME))

    # ---- check 10: M dies after CONT ---------------------------------------------------------------------------------
    def c10():
        need_roles(); i = M
        if stop is None: return False, f'stall.csv: {stall_err or "no window"}'
        on = kdead_on(i)
        if rg is not None:   # after the stop in script time = at or after the script's first frame after the gap
            after = bool(on) and on[0]['T'] >= rg['tg']
            how = f'script time {on[0]["T"] if on else None} >= {rg["tg"]}, the first frame after the gap (RESUMED_GAP field 2)'
        else:                # no paired RESUMED_GAP (check 9 FAILS then): weaker fallback, stated
            pre = [x for x in snaps if x['u'] <= stop + STOP_MARGIN_MS]; lp = pre[-1] if pre else None
            after = bool(on) and lp is not None and on[0]['T'] > lp['tx']
            how = (f'NO paired RESUMED_GAP (check 9 fails): fallback, script time {on[0]["T"] if on else None} > t_xray '
                   f'{lp["tx"] if lp else None} of the last snapshot read by STOP + {STOP_MARGIN_MS} ms')
        fp = next((x for x in snaps if x['u'] >= cont), None)
        ds = first_dead_line(i)
        early = [x['seq'] for x in snaps if x['u'] < cont and i in x['k'] and x['k'][i][1]]
        later = [x for x in snaps if fp is not None and x['u'] >= fp['u'] and i in x['k'] and x['k'][i][1]]
        lag = later[0]['u'] - fp['u'] if later else None
        ok = len(on) == 1 and after and fp is not None and not early and lag is not None and lag <= CONFIRM_MS
        return ok, (f'M {i}: KDEAD online {len(on)} {[k["line"] for k in on]}; after the stop: {after} ({how}); '
                    f'snapshots read before CONT with the dead line: {len(early)} {early[:3]}; first kept snapshot read at '
                    f'or after CONT: seq {fp["seq"] if fp else None}, {fp["u"] - cont if fp else None} ms after CONT '
                    f'(REPORTED, not judged); first snapshot with the dead line from it: seq {later[0]["seq"] if later else None}, '
                    f'{lag} ms after it (judged <= {CONFIRM_MS}, W clock both)')
    add(10, 'step D: one KDEAD online for M after the stop, its dead line in no snapshot before CONT, and read within '
            '2.0 s of the first snapshot read at or after CONT', c10, ('stall.csv', PO, SN, ME))
    # ---- check 11: dead in X-Ray -------------------------------------------------------------------------------------
    def c11():
        need_roles(); ok = True; out = []
        for r, i in roles.items():
            if r == 'S' and s_dead2: out.append(f'S {i}: {S_NJ}'); continue
            ds = first_dead_line(i)
            if not ds: ok = False; out.append(f'{r} {i}: no dead line'); continue
            t0 = ds['u'] + DEAD_AFTER_MS; late = [s for s in snaps if s['u'] >= t0]
            bad = [s['seq'] for s in late if i in s['o'] and s['o'][i]['alive'] != '0']
            g = bool(late) and not bad; ok = ok and g
            out.append(f'{r} {i}: {len(late)} snapshots from 2.0 s after the first dead line (snap {ds["seq"]}), alive in {len(bad)} {bad[:5]}')
        return ok, ' | '.join(out)
    add(11, 'dead in X-Ray: from 2.0 s after each dead line the id is alive 0 or not listed, to the end', c11, (PO, SN))

    # ---- check 12: the actor ------------------------------------------------------------------------------------------
    def c12():
        a0 = snaps[0]['a'] if snaps else None
        bad = [s['seq'] for s in snaps if s['a'] is None or s['a'] != a0]
        return bool(snaps) and a0 is not None and not bad, f'first actor health {a0}, differing in {len(bad)} {bad[:5]} of {len(snaps)} snapshots'
    add(12, 'the actor is unhurt: actor health in every snapshot = the first', c12, (SN,))

    # ---- check 13: the r line -----------------------------------------------------------------------------------------
    def c13():
        if not ev['P2']: return False, 'no P2 line (no pose taken)'
        T0 = ev['P2'][0]['T']
        pu_by_seq = {p['seq']: p['pu'] for p in poses}; pseqs = sorted(pu_by_seq)
        pread = [p['u'] for p in poses]
        def pu_of(q):   # pose utc of r's seq; if the watcher did not keep that pose, the newest kept one below it (lag overstated)
            if q in pu_by_seq: return pu_by_seq[q]
            j = bisect.bisect_right(pseqs, q) - 1
            return pu_by_seq[pseqs[j]] if j >= 0 else None
        first_post = next((s for s in snaps if stop is not None and s['u'] >= cont), None)
        absent, back, slow, notj, prev, n = [], [], [], 0, None, 0
        for s in snaps:
            if s['tx'] < T0: continue
            n += 1
            if not s['r']: absent.append(s['seq']); continue
            sess, q = s['r']
            if prev is not None and (sess != prev[0] or q < prev[1]): back.append(f'{prev}->{s["r"]}@{s["seq"]}')
            prev = s['r'] if prev is None or (sess == prev[0] and q >= prev[1]) else prev
            if inside(s['u']): notj += 1; continue
            j = bisect.bisect_right(pread, s['u']) - 1
            if j < 0: slow.append(f'{s["seq"]}:no pose read yet'); continue
            newest = poses[j]
            if newest['sess'] != sess: slow.append(f'{s["seq"]}:session {sess} vs newest pose {newest["sess"]}'); continue
            pq = pu_of(q); lag = newest['pu'] - pq if pq is not None else None
            lim = R_LAG_MS + ((cont - stop) if s is first_post else 0)
            if lag is None or lag > lim: slow.append(f'{s["seq"]}:{lag}>{lim}')
        return n > 0 and not absent and not back and not slow, (
            f'{n} snapshots written at or after the first pose taken (P2 script time {T0}); r absent {len(absent)} {absent[:5]}; '
            f'back or other session {back[:3]}; lag over the bound {len(slow)} {slow[:4]} (bound {R_LAG_MS} ms in pose time, the '
            f'first snapshot after CONT {R_LAG_MS} + stop length); inside the stop, not judged here {notj}')
    add(13, 'the r line: present after the first pose taken, never back, within 2.0 s of the newest kept pose', c13,
        ('stall.csv', PO, SN, ME))

    # ---- check 14: counters ------------------------------------------------------------------------------------------
    rows14 = []
    def c14():
        ctx = {'khits': khits_expect(poses), 'last_seq': last_seq_expect(poses, snaps)}
        ok, rows, summ = counter_table(t2sum, meta, ctx); rows14.extend(rows)
        return ok, summ
    add(14, 'counters: every key of the last T2SUM judged against the checker\'s table, none skipped', c14, (PO, SN, ME))

    # ---- NO VERDICT rules ---------------------------------------------------------------------------------------------
    nv = []
    nv.append(('NV1', 'S and M not found: exactly two ids with k lines, one stalker and one monster', bool(nv1),
               '; '.join(nv1) or f'S {S}, M {M}'))
    def nv2():
        if not (S and M): return False, 'not judged (no S/M)'
        out = []
        for r, i in roles.items():
            a = steps[i][0] if steps[i] else None
            ka = kap_of(i, a['tot']) if a else []
            if ka:     # health before step A: snapshots written before its KAPPLY (X clock); reference = KAPPLY's "before"
                pre = [s['o'][i]['h'] for s in snaps if i in s['o'] and s['tx'] < ka[0]['T']]; ref = ka[0]['hb']
            elif a:    # no KAPPLY: snapshots read before the watcher read the step-A pose (W clock); reference = the last
                pre = [s['o'][i]['h'] for s in snaps if i in s['o'] and s['u'] < a['u']]; ref = pre[-1] if pre else None
            else:
                pre, ref = [], None
            if pre and ref is not None and max(pre) - ref > FALL: out.append(f'{r} hurt before step A: max {max(pre):.4f}, at step A {ref:.4f}')
            obs = [k['line'] for k in ev['KDEAD'] if k['id'] == i and k['kind'] not in MINE_KDEAD]
            if obs: out.append(f'{r} death observed, not dealt: {obs[:2]}')
            fd = first_dead(i)
            early = [s['seq'] for s in snaps if i in s['o'] and s['o'][i]['alive'] == '0' and (fd is None or s['u'] < fd['u'] - EARLY_MS)]
            if early: out.append(f'{r} dead in X-Ray before its death step: snaps {early[:3]}')
        return bool(out), '; '.join(out) or 'none'
    def nv3():
        if not (S and M): return False, 'not judged (no S/M)'
        out = []
        for r, i in roles.items():
            ss = [s for s in snaps if i in s['o']]
            for p, q in zip(ss, ss[1:]):
                fl = p['o'][i]['h'] - q['o'][i]['h']
                if fl > FALL and not [k for k in mine(i) if p['tx'] < k['T'] <= q['tx']]:
                    out.append(f'{r} {i} snap {p["seq"]}->{q["seq"]} fell {fl:.4f}')
        return bool(out), '; '.join(out[:4]) or 'none'
    def nv4():
        if not (S and M): return False, 'not judged (no S/M)'
        if stop is None: return False, 'not judged (no stop window: check 9 fails)'
        fd = first_dead(M)
        if not fd: return True, f'no kept pose with dead = 1 for M {M}'
        a, b = fd['u'] - stop, cont - fd['u']
        return not (a >= NV4_MS and b >= NV4_MS), f'M\'s first dead pose seq {fd["seq"]} read {a} ms after STOP and {b} ms before CONT (each must be >= {NV4_MS})'
    def nv6():
        if not (S and M): return False, 'not judged (no S/M)'
        out = []
        for r, i in roles.items():
            for st in steps[i][1:]:   # every step after step A: a new higher total, or dead = 1
                sb = [x for x in snaps if x['u'] <= st['u']]
                if not sb or i not in sb[-1]['o']:
                    out.append(f'{r} is not listed in X-Ray\'s snapshot at the time of the first pose of one of its later steps '
                               f'(step {st["ts"]}{" dead" if st["dead"] == "1" else ""}, pose seq {st["seq"]} read {st["u"]}; '
                               f'newest snapshot then: seq {sb[-1]["seq"] if sb else None})')
        return bool(out), '; '.join(out) or 'listed at every later step'
    for k, t, fn in (('NV2', 'S or M hurt by X-Ray\'s own world by more than 0.02 before step A, or killed before its step', nv2),
                     ('NV3', 'S or M lost more than 0.02 between two snapshots with no K line of mine (KAPPLY, KDEAD online/offline/failed) between them', nv3),
                     ('NV4', 'M\'s first dead pose not read at least 2 s inside the stop at both ends', nv4),
                     ('NV6', 'S or M not listed in X-Ray\'s snapshot at the first pose of a later step', nv6)):
        try: f, d = fn()
        except Exception as e: f, d = False, f'not judged: error {e!r}'
        nv.append((k, t, f, d))

    # ---- report -----------------------------------------------------------------------------------------------------
    out = [f'KJ1 X side checker, run dir {run_dir}']
    out += [f'MISSING {n} ({paths[n]})' for n in missing] + [f'EMPTY {n} ({paths[n]})' for n in empty]
    out += [f'UNREADABLE {n} ({paths[n]}: {status[n][12:]})' for n in unreadable] + unparsed
    for n, t, ok, d in res:
        out.append(f'{n} {t}: {"PASS" if ok else "FAIL"} ({d})')
        if n == 14: out += rows14
    out += [f'{k} {t}: {"FIRED" if f else "clear"} ({d}){"; leaves untested: checks " + fmt_set(NV_UNTESTED[k]) if f else ""}' for k, t, f, d in nv]
    out.append(f'reported: S {S}, M {M}; k-line ids {kids} kinds {kinds}')
    for i in kids:
        out.append(f'reported: steps of {i}: ' + ', '.join(f'{s["ts"]}{"(dead)" if s["dead"] == "1" else ""}@seq{s["seq"]}' for s in steps[i]))
    out.append(f'reported: stall window W {stop}..{cont}; inputs: {len(snaps)} snapshots, {len(poses)} poses, {len(meta)} meta lines')
    out += frame_report(snaps, ev, steps, roles, stop, cont, poses)
    out += [f'reported: {e["line"]}' for k in ('KAPPLY', 'KDEAD', 'KWHO', 'KOTHER') for e in ev[k]]
    out.append(f'reported: last T2SUM: {t2sum[-1] if t2sum else None}')
    fired = [k for k, _, f, _ in nv if f]
    failing = {n for n, _, ok, _ in res if not ok}
    if gone or unparsed: verdict, code = 'FAIL', 1; out.append('verdict: missing, empty, unreadable or unparsable input')
    elif fired:
        U = set().union(*(NV_UNTESTED[k] for k in fired)); outside = failing - U
        if outside:
            verdict, code = 'FAIL', 1
            out.append(f'verdict: NO VERDICT rule(s) {", ".join(fired)} fired, but failing check(s) {fmt_set(outside)} are '
                       f'outside what they leave untested ({fmt_set(U)}): FAIL')
        else:
            verdict, code = 'NO VERDICT', 3
            out.append(f'verdict: NO VERDICT rule(s) {", ".join(fired)} fired; failing checks {fmt_set(failing) or "none"} '
                       f'all within what they leave untested ({fmt_set(U)})')
    elif not failing: verdict, code = 'PASS', 0
    else: verdict, code = 'FAIL', 1; out.append(f'verdict: failing checks {fmt_set(failing)}')
    out.append(f'KJ1 X SIDE: {verdict}')
    return out, code


def frame_report(snaps, ev, steps, roles, stop, cont, poses):
    """REPORTED, not judged (section 4, frame time): gaps between consecutive kept snapshots (W clock)."""
    if len(snaps) < 2: return ['reported: frame time: fewer than 2 snapshots']
    gaps = [(b['u'] - a['u'], a['seq'], b['seq'], a['u']) for a, b in zip(snaps, snaps[1:])]
    marks = [('start', snaps[0]['u'])]
    for r, i in roles.items():
        if not i: continue
        for s in steps.get(i, []): marks.append((f'{r} {s["ts"]}{"(dead)" if s["dead"] == "1" else ""} pose read', s['u']))
    if stop is not None: marks += [('STOP', stop), ('CONT', cont)]
    marks.append(('end', snaps[-1]['u'] + 1)); marks.sort(key=lambda m: m[1])
    out = []
    for (na, ua), (nb, ub) in zip(marks, marks[1:]):
        g = [x for x in gaps if ua <= x[3] < ub]
        if g: mx = max(g); out.append(f'reported: frame time {na} -> {nb}: largest snapshot gap {mx[0]} ms (snap {mx[1]}->{mx[2]}), {len(g)} gaps')
    txs = [s['tx'] for s in snaps]
    for e in sorted(ev['KAPPLY'] + ev['KDEAD'], key=lambda e: e['i']):
        j = bisect.bisect_left(txs, e['T'])
        if 0 < j < len(snaps):
            out.append(f'reported: frame time around {e["line"]}: snap {snaps[j - 1]["seq"]}->{snaps[j]["seq"]} gap {snaps[j]["u"] - snaps[j - 1]["u"]} ms')
    return out


if __name__ == '__main__':
    main()
