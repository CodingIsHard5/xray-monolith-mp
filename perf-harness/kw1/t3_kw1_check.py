#!/usr/bin/env python3
"""Kill channel witness kw1, X side (PREREG-killchannel-xwitness-2026-11-26.md, sections 3 and 4). Run ONCE per run.
  t3_kw1_check.py <t3_kw1.sh out dir> [out=<report file>]
Reads (every one required; a missing file FAILS by name, it is never read as empty or 0):
  pose_written.csv            the writer's poses: utc_ms,seq,phase,session,track_s,w_s,k
  writer_snaps.csv            the writer's per-snapshot log (t2_kill_pose.py docstring)
  run/t2_xray.log, run/restarts.csv, run/boot0/meta_t2.csv, run/boot0/tb/xray.log
Prints one line per check `<n> <text>: PASS|FAIL (<numbers>)`, one line per NO VERDICT rule `NV<k> <text>: FIRED|clear
(<numbers>)`, then `KW1 X SIDE: PASS|FAIL|NO VERDICT`. Exit 0 PASS, 1 FAIL, 3 NO VERDICT.
Verdict: a missing file -> FAIL. Else a NO VERDICT rule fired and check 1 passed -> NO VERDICT (a crash stays FAIL).
Else PASS only if every check passes.
S and M are the ids the writer picked (its PICK row). Times: writer UTC ms (pose_written / writer_snaps); pose seqs
join the two sides (KAPPLY / KDEAD carry the seq of the pose they were applied from)."""
import sys, os, re

# ---- bounds, each from its design row (section 4 / section 3) ------------------------------------------------------
tol_health = 0.0002          # check 2: "health after" = "health before" minus the difference, +/- 0.0002
part_diffs = (0.250, 0.350)  # check 2: the two part hits, in this order
part_totals = (0.250, 0.600) # check 2 / 4 / 5: the totals of the two part hits
drop_floor = {0.250: 0.125, 0.600: 0.175}   # check 4: health drop at least half the difference
confirm_ms = 2000            # check 5 and 6: read by the writer within 2.0 s of the first pose carrying the total / dead
dead_after_ms = 2000         # check 7: from 2.0 s after the dead line (the writer's first read of it)
hold_ms, hold_tol_ms = 10000, 1000   # check 8: last dead line 10.0 s after the last pose carrying the line, +/- 1.0 s
killer_id = '0'              # check 9: id 0 is the actor
r_lag_ms = 2000              # check 11: r within 2.0 s of the writer's newest pose
# check 12: "follows from the steps". The table says khits 4; the script ALSO logs a part hit at the death step (total
# 0.600 -> 1.000 with dead = 1 is a difference of 0.400, applied before the kill), so a correct run counts 2 x 3 = 6.
# See the build report: the table's 4 contradicts zz_p1_puppet_kill4.script; 6 is used here until the design is settled.
t2sum_expect = {'khits': '6', 'kdead_online': '2', 'kdead_offline': '0', 'kdead_fail': '0', 'krefused': '1',
                'kstale': '0', 'kback': '0', 'kclamped': '0', 'kver': '0', 'kowed': '0'}
nv_fight = 0.02              # section 3: a KAPPLY "health before" more than 0.02 below the writer's pick health (less
                             # what was already applied) = hurt by X-Ray's own world

out_dir = sys.argv[1] if len(sys.argv) > 1 else None
opt = dict(a.split('=', 1) for a in sys.argv[2:] if '=' in a)
if not out_dir:
    print('usage: t3_kw1_check.py <out dir> [out=<file>]'); sys.exit(2)
run_dir = os.path.join(out_dir, 'run'); boot_dir = os.path.join(run_dir, 'boot0')
files = {'pose_written.csv': os.path.join(out_dir, 'pose_written.csv'), 'writer_snaps.csv': os.path.join(out_dir, 'writer_snaps.csv'),
         'run/t2_xray.log': os.path.join(run_dir, 't2_xray.log'), 'run/restarts.csv': os.path.join(run_dir, 'restarts.csv'),
         'run/boot0/meta_t2.csv': os.path.join(boot_dir, 'meta_t2.csv'), 'run/boot0/tb/xray.log': os.path.join(boot_dir, 'tb', 'xray.log')}
missing = [n for n, p in files.items() if not os.path.isfile(p)]


def rd(name):
    if name in missing: return []
    return open(files[name], 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n')


res = []   # (n, text, ok, numbers)
def add(n, text, ok, nums, needs=()):
    gone = [f for f in needs if f in missing]
    if gone: ok, nums = False, 'missing ' + ', '.join(gone)
    res.append((n, text, bool(ok), nums))


def safe(f, default=None):
    try: return f()
    except Exception as e: return default if default is not None else ('error', repr(e))


all_files = tuple(files)
# ---- inputs ---------------------------------------------------------------------------------------------------------
meta = [l for l in rd('run/boot0/meta_t2.csv') if l]
xlog = rd('run/boot0/tb/xray.log'); runner_log = [l for l in rd('run/t2_xray.log') if l]
restart_rows = [l for l in rd('run/restarts.csv')[1:] if l]
pose_rows = [l.split(',') for l in rd('pose_written.csv')[1:] if l]
snap_lines = [l for l in rd('writer_snaps.csv') if l]
snap_head = snap_lines[0].split(',') if snap_lines else []
snap_rows = [dict(zip(snap_head, l.split(','))) for l in snap_lines[1:]]
pick_rows = [r for r in snap_rows if 'PICK' in r.get('event', '')]
rows = [r for r in snap_rows if 'PICK' not in r.get('event', '')]   # one per snapshot read
pose_utc = {}; pose_k = []   # pose_k: (utc, seq, {id: (total str, dead str, line)})
for p in pose_rows:
    if len(p) < 7: continue
    try: u, n = int(p[0]), int(p[1])
    except ValueError: continue
    pose_utc[n] = u; kk = {}
    for l in p[6].split('|'):
        f = l.split(':')   # the writer writes a k line's commas as ':' inside the csv field
        if len(f) == 5 and f[0] == 'k': kk[f[1]] = (f[3], f[4], l)
    pose_k.append((u, n, kk))
sess = pose_rows[0][3] if pose_rows and len(pose_rows[0]) > 3 else None
kv = lambda l: dict(p.split('=', 1) for p in l.split(',')[1:] if '=' in p)
mi = lambda k: [i for i, l in enumerate(meta) if l.startswith(k + ',')]
mf = lambda i: meta[i].split(',')
roles = {}
for r in pick_rows:
    for tok in r.get('event', '').split():
        m = re.match(r'PICK_([SM])=(\d+):([^:]+):([\d.]+)$', tok)
        if m: roles[m.group(1)] = {'id': m.group(2), 'sec': m.group(3), 'h': float(m.group(4))}
nopick = [tok for r in pick_rows for tok in r.get('event', '').split() if tok.startswith('NOPICK_')]
kap = [mf(i) + [i] for i in mi('KAPPLY')]          # KAPPLY,now,n,id,d,ap,hb,ha,hit,<meta index>
kde = [mf(i) for i in mi('KDEAD')]
wlines = [(i, int(mf(i)[1])) for i in mi('W') if len(mf(i)) > 1 and mf(i)[1].isdigit()]
col = {'S': 's', 'M': 'm'}


def first_pose(rid, pred):
    for u, n, kk in pose_k:
        if rid in kk and pred(kk[rid]): return u, n
    return None


# ---- check 1 --------------------------------------------------------------------------------------------------------
lua_err = [l for l in xlog if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + \
          [xlog[i] for i in range(len(xlog)) if 'zz_p1_puppet.script (line' in xlog[i] and not any("doesn't exist" in xlog[j] or 'nil function' in xlog[j] for j in range(max(0, i - 8), i))]
fatal = [l for l in xlog if 'FATAL' in l]; rc0 = bool(runner_log) and runner_log[-1].rstrip().endswith('rc=0')
xlines = sum(1 for l in xlog if l.strip()); boots = sorted(d for d in (os.listdir(run_dir) if os.path.isdir(run_dir) else []) if re.match(r'boot\d+$', d))
add(1, 'no crash, no FATAL, no Lua error naming the bridge script, runner rc 0, one boot',
    rc0 and not restart_rows and not lua_err and not fatal and xlines > 0 and boots == ['boot0'],
    f'rc0 {rc0}, restart rows {len(restart_rows)}, lua errors {len(lua_err)}, FATAL {len(fatal)}, engine log lines {xlines}, boots {boots}', all_files)

# ---- check 2: part hits ---------------------------------------------------------------------------------------------
def c2(role):
    g = roles.get(role)
    if not g: return False, f'{role} not picked'
    fd = first_pose(g['id'], lambda k: k[1] == '1'); lim = fd[1] if fd else 10 ** 12
    part = [k for k in kap if k[3] == g['id'] and int(k[2]) < lim]
    ok = len(part) == 2 and all(abs(float(k[4]) - d) <= tol_health and abs(float(k[5]) - t) <= tol_health and
                                abs(float(k[7]) - (float(k[6]) - float(k[4]))) <= tol_health
                                for k, d, t in zip(part, part_diffs, part_totals))
    return ok, f'{role} id {g["id"]}: ' + ('; '.join(','.join(k[2:9]) for k in part) or 'no KAPPLY')
r2 = [safe(lambda r=r: c2(r), (False, 'error')) for r in ('S', 'M')]
add(2, 'part hits: two KAPPLY each, 0.250 then 0.350, after = before - difference +/- 0.0002',
    all(x[0] for x in r2), ' | '.join(x[1] for x in r2), ('pose_written.csv', 'writer_snaps.csv', 'run/boot0/meta_t2.csv'))

# ---- check 3: no second effect --------------------------------------------------------------------------------------
def c3(role):
    g = roles.get(role)
    if not g: return False, f'{role} not picked'
    extra = []; tots = sorted({kk[g['id']][0] for _, _, kk in pose_k if g['id'] in kk})
    for t in tots:
        seqs = [n for _, n, kk in pose_k if g['id'] in kk and kk[g['id']][0] == t]
        a, b = min(seqs), max(seqs)
        inw = [k for k in kap if k[3] == g['id'] and a <= int(k[2]) <= b]
        own = next((k for k in inw if abs(float(k[5]) - float(t)) <= tol_health), None)
        extra += [f'{t}@{k[2]}' for k in inw if k is not own]
    return not extra and bool(tots), f'{role}: totals {tots}, further KAPPLY {extra}'
r3 = [safe(lambda r=r: c3(r), (False, 'error')) for r in ('S', 'M')]
add(3, 'no second effect while a total is repeated', all(x[0] for x in r3), ' | '.join(x[1] for x in r3),
    ('pose_written.csv', 'writer_snaps.csv', 'run/boot0/meta_t2.csv'))

# ---- check 4: the creature really lost health -----------------------------------------------------------------------
def c4(role):
    g = roles.get(role)
    if not g: return False, f'{role} not picked'
    out = []; ok = True
    for t in part_totals:
        k = next((k for k in kap if k[3] == g['id'] and abs(float(k[5]) - t) <= tol_health), None)
        if not k: ok = False; out.append(f'{t:.3f}: no KAPPLY'); continue
        i = k[-1]; sb = max((s for j, s in wlines if j < i), default=None); sa = min((s for j, s in wlines if j > i), default=None)
        hcol = col[role] + '_health'
        bef = [r for r in snap_rows if r.get('snap_seq', '').isdigit() and sb is not None and int(r['snap_seq']) <= sb and r.get(hcol, '')]   # incl. the PICK row: it may be the only row with a health before the first hit
        aft = [r for r in rows if r.get('snap_seq', '').isdigit() and sa is not None and int(r['snap_seq']) >= sa]
        hb = bef[-1].get(hcol, '') if bef else ''; ha = aft[0].get(hcol, '') if aft else ''
        drop = float(hb) - float(ha) if hb and ha else None
        good = drop is not None and drop >= drop_floor[t] - 1e-9; ok = ok and good
        out.append(f'{t:.3f}: snap {bef[-1]["snap_seq"] if bef else None} {hb or None} -> snap {aft[0]["snap_seq"] if aft else None} {ha or None}, drop {None if drop is None else round(drop, 4)} (floor {drop_floor[t]})')
    return ok, f'{role}: ' + '; '.join(out)
r4 = [safe(lambda r=r: c4(r), (False, 'error')) for r in ('S', 'M')]
add(4, 'the creature lost health: next snapshot below the last one before, by at least half the difference',
    all(x[0] for x in r4), ' | '.join(x[1] for x in r4), ('writer_snaps.csv', 'run/boot0/meta_t2.csv'))

# ---- check 5 / 6: confirmation, death -------------------------------------------------------------------------------
def first_read(role, pred):
    c = col[role] + '_k'
    return next((r for r in rows if pred(r.get(c, ''))), None)

def c5(role):
    g = roles.get(role)
    if not g: return False, f'{role} not picked'
    out = []; ok = True; prev = None
    for t in part_totals:
        fp = first_pose(g['id'], lambda k, t=t: k[0] == f'{t:.3f}' and k[1] == '0')
        rr = first_read(role, lambda v, t=t: v == f'{t:.3f}')
        lag = int(rr['utc_ms']) - fp[0] if fp and rr else None
        good = lag is not None and 0 <= lag <= confirm_ms and (prev is None or int(rr['utc_ms']) > prev)
        prev = int(rr['utc_ms']) if rr else prev; ok = ok and good
        out.append(f'{t:.3f} read {lag} ms after its first pose')
    return ok, f'{role}: ' + '; '.join(out)
r5 = [safe(lambda r=r: c5(r), (False, 'error')) for r in ('S', 'M')]
add(5, 'confirmation: k,<id>,0.250 then k,<id>,0.600 read within 2.0 s of the first pose carrying the total',
    all(x[0] for x in r5), ' | '.join(x[1] for x in r5), ('pose_written.csv', 'writer_snaps.csv'))

def dead_read(role):
    return first_read(role, lambda v: v.endswith(';dead'))

def c6(role):
    g = roles.get(role)
    if not g: return False, f'{role} not picked'
    on = [k for k in kde if len(k) > 4 and k[3] == g['id'] and k[4] == 'online']
    fp = first_pose(g['id'], lambda k: k[1] == '1'); rr = dead_read(role)
    lag = int(rr['utc_ms']) - fp[0] if fp and rr else None
    return len(on) == 1 and lag is not None and 0 <= lag <= confirm_ms, f'{role}: KDEAD online {len(on)}, dead read {lag} ms after the first dead pose'
r6 = [safe(lambda r=r: c6(r), (False, 'error')) for r in ('S', 'M')]
add(6, 'death: one KDEAD online each; the writer reads the dead line within 2.0 s of the first pose with dead = 1',
    all(x[0] for x in r6), ' | '.join(x[1] for x in r6), ('pose_written.csv', 'writer_snaps.csv', 'run/boot0/meta_t2.csv'))

# ---- check 7: dead in X-Ray -----------------------------------------------------------------------------------------
def c7(role):
    g = roles.get(role); rr = dead_read(role) if g else None
    if not rr: return False, f'{role}: no dead line read'
    t0 = int(rr['utc_ms']) + dead_after_ms; c = col[role]
    late = [r for r in rows if int(r['utc_ms']) >= t0]
    bad = [r['snap_seq'] for r in late if not (r.get(c + '_alive', '') == '0' or r.get(c + '_health', '') == '')]
    return bool(late) and not bad, f'{role}: {len(late)} snapshots from 2.0 s after the dead line, alive in {len(bad)} {bad[:5]}'
r7 = [safe(lambda r=r: c7(r), (False, 'error')) for r in ('S', 'M')]
add(7, 'dead in X-Ray: alive 0 or not listed, from 2.0 s after the dead line to the end', all(x[0] for x in r7),
    ' | '.join(x[1] for x in r7), ('writer_snaps.csv',))

# ---- check 8: the dead line's hold ----------------------------------------------------------------------------------
def c8(role):
    g = roles.get(role)
    if not g: return False, f'{role} not picked'
    c = col[role] + '_k'; dr = [i for i, r in enumerate(rows) if r.get(c, '').endswith(';dead')]
    lp = [u for u, n, kk in pose_k if g['id'] in kk]
    if not dr or not lp: return False, f'{role}: dead rows {len(dr)}, poses with the line {len(lp)}'
    gap = [rows[i]['snap_seq'] for i in range(dr[0], dr[-1] + 1) if not rows[i].get(c, '').endswith(';dead')]
    after = len(rows) - 1 - dr[-1]; d = int(rows[dr[-1]]['utc_ms']) - max(lp)
    return abs(d - hold_ms) <= hold_tol_ms and not gap and after > 0, f'{role}: last dead line {d} ms after the last pose with the line, gaps {gap[:5]}, snapshots after it {after}'
r8 = [safe(lambda r=r: c8(r), (False, 'error')) for r in ('S', 'M')]
add(8, 'the dead line stays, and goes 10.0 s +/- 1.0 s after the last pose that carried the line', all(x[0] for x in r8),
    ' | '.join(x[1] for x in r8), ('pose_written.csv', 'writer_snaps.csv'))

# ---- check 9: the killer --------------------------------------------------------------------------------------------
kwho = [mf(i) for i in mi('KWHO')]
def c9(role):
    g = roles.get(role)
    if not g: return False, f'{role} not picked'
    w = [k for k in kwho if len(k) > 3 and k[2] == g['id']]
    return len(w) == 1 and w[0][3] == killer_id, f'{role}: KWHO {[",".join(k[2:4]) for k in w]}'
r9 = [safe(lambda r=r: c9(r), (False, 'error')) for r in ('S', 'M')]
add(9, 'the kill is the actor\'s: one KWHO each with killer id 0', all(x[0] for x in r9), ' | '.join(x[1] for x in r9),
    ('writer_snaps.csv', 'run/boot0/meta_t2.csv'))

# ---- check 10: the actor --------------------------------------------------------------------------------------------
ah = [r.get('actor_health', '') for r in rows]
a_bad = [r['snap_seq'] for r in rows if not ah or r.get('actor_health', '') != ah[0]]
ref0 = [mf(i) for i in mi('KREFUSED') if len(mf(i)) > 4 and mf(i)[3] == '0']
a_hit = [k for k in kap if k[3] == '0'] + [k for k in kde if len(k) > 3 and k[3] == '0']
add(10, 'actor unhurt (health = first snapshot\'s in every snapshot); one KREFUSED 0,actor; no KAPPLY/KDEAD for 0',
    bool(rows) and ah[0] != '' and not a_bad and len(ref0) == 1 and ref0[0][4] == 'actor' and not a_hit,
    f'first {ah[0] if ah else None}, differing in {len(a_bad)} {a_bad[:5]}; KREFUSED for 0: {[",".join(k[3:5]) for k in ref0]}; KAPPLY/KDEAD for 0: {len(a_hit)}',
    ('writer_snaps.csv', 'run/boot0/meta_t2.csv'))

# ---- check 11: the r line -------------------------------------------------------------------------------------------
def c11():
    p2 = mi('P2')
    if not p2: return False, 'no P2 line (no pose taken)'
    s0 = min((s for j, s in wlines if j > p2[0]), default=None)
    if s0 is None: return False, 'no snapshot after the first pose taken'
    absent = []; back = []; slow = []; prev = None; seen = False
    for r in rows:
        has = r.get('r_seq', '').isdigit()
        if (int(r['snap_seq']) >= s0 or seen) and not (has and r.get('r_sess') == sess): absent.append(r['snap_seq'])
        if not has: continue
        seen = True; q = int(r['r_seq'])
        if prev is not None and q < prev: back.append(f'{prev}->{q}@{r["snap_seq"]}')
        prev = q if prev is None else max(prev, q)
        pn = int(r['pose_seq']) if r.get('pose_seq', '').isdigit() else None
        lag = pose_utc[pn] - pose_utc[q] if pn in pose_utc and q in pose_utc else None
        if lag is None or lag > r_lag_ms: slow.append(f'{r["snap_seq"]}:{lag}')
    n = sum(1 for r in rows if int(r['snap_seq']) >= s0)
    return n > 0 and not absent and not back and not slow, f'{n} snapshots from seq {s0}: r absent or not session {sess} in {len(absent)} {absent[:5]}, back {back[:5]}, over 2.0 s {slow[:5]}'
ok11, n11 = safe(c11, (False, 'error'))
add(11, 'the r line: present after the first pose taken, never back, within 2.0 s of the newest pose', ok11, n11,
    ('pose_written.csv', 'writer_snaps.csv', 'run/boot0/meta_t2.csv'))

# ---- check 12: counters ---------------------------------------------------------------------------------------------
t2sum = [kv(meta[i]) for i in mi('T2SUM')]; last = t2sum[-1] if t2sum else {}
diff = {k: last.get(k) for k, v in t2sum_expect.items() if last.get(k) != v}
add(12, 'counters in the last T2SUM: ' + ' '.join(f'{k} {v}' for k, v in t2sum_expect.items()), bool(t2sum) and not diff,
    f'differ: {diff}' if t2sum else 'no T2SUM', ('run/boot0/meta_t2.csv',))

# ---- NO VERDICT rules (section 3) -----------------------------------------------------------------------------------
nv = []
nv.append(('NV1', 'no stalker or no monster with health >= 0.9 at the pick', bool(nopick), f'{nopick or "both picked"}'))
fights = []
for role, g in roles.items():
    for k in kap:
        if k[3] != g['id']: continue
        exp = g['h'] - (float(k[5]) - float(k[4]))   # pick health less what was already applied before this hit
        if float(k[6]) < exp - nv_fight: fights.append(f'{role} KAPPLY before {k[6]} < {exp:.4f} - {nv_fight}')
    fights += [f'{role} KDEAD seen' for k in kde if len(k) > 4 and k[3] == g['id'] and k[4] == 'seen']
nv.append(('NV2', 'S or M hurt or killed by X-Ray\'s own world before its step', bool(fights), f'{fights or "none"}'))

out = [f'MISSING {n} ({files[n]})' for n in missing]
out += [f'{n} {t}: {"PASS" if ok else "FAIL"} ({d})' for n, t, ok, d in res]
out += [f'{k} {t}: {"FIRED" if f else "clear"} ({d})' for k, t, f, d in nv]
out += [f'reported: picks {roles}', f'reported: T2SUM {last}']
out += [f'reported: {meta[i]}' for k in ('KAPPLY', 'KDEAD', 'KREFUSED', 'KWHO', 'KTAB') for i in mi(k)]
if missing: verdict, code = 'FAIL', 1
elif any(f for _, _, f, _ in nv) and res[0][2]: verdict, code = 'NO VERDICT', 3
elif all(ok for _, _, ok, _ in res): verdict, code = 'PASS', 0
else: verdict, code = 'FAIL', 1
out.append(f'KW1 X SIDE: {verdict}')
print('\n'.join(out))
if opt.get('out'): open(opt['out'], 'w').write('\n'.join(out) + '\n')
sys.exit(code)
