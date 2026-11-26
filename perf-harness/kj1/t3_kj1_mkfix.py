#!/usr/bin/env python3
"""Fixture run folders for t3_kj1_check.py (kill channel joint run kj1, X side). Desk only: no engine, no display.
  python3 -I t3_kj1_mkfix.py <dest dir (must not exist)> [<kw1 runner dir, default .../out/kw1/run>]
Writes one folder per fixture under <dest> (each laid out as the runner's out dir: t2_xray.log, restarts.csv, stall.csv,
freeze.csv, lost.csv, boot0/meta_t2.csv, boot0/tb/xray.log, boot0/watch/snaps_all.txt, boot0/watch/poses_all.txt,
README.txt) and <dest>/fixtures.tsv, the expectations the selftest compares against:
  name  kind  verdict  rc  fails  nvs  must  note
  fails = the exact set of failing check numbers ('*' = not compared); nvs = the exact set of fired NO VERDICT rules;
  must  = '|'-separated strings that must appear in the report.
Never deletes or overwrites anything: files that are identical to the good folder's are HARD LINKS to it (every file is
written once, with open(..., 'x')).

WHAT IS REAL AND WHAT IS NOT (also in every folder's README.txt):
  REAL bytes, copied verbatim: boot0/tb/xray.log (kw1's engine log, CRLF); in boot0/meta_t2.csv the CHANGER, ABSORB_ON,
    S, VITALS and MOVESUM lines; restarts.csv, freeze.csv, lost.csv (kw1's, header only).
  REAL-DERIVED (a real kw1 line with only its numbers replaced, format and key order kept): meta P2 lines (kw1's last
    P2 line's 16 trailing fields kept, seq/utc/time replaced), T2SUM (kw1's line, values replaced), SUMMARY, TICK.
  t2_xray.log is SYNTHETIC: 8 lines written by this script, not read from kw1's file. Six copy the text of kw1's six
    lines (boot 0 run_id / wall limit / level / first snapshot / boot 0 end / end) with the timestamps changed and,
    in the run_id line, the run id; the last line says `stall 1` where kw1's says `stall no`. Two lines (STALL start,
    STALL end) are in kw1/t2_xray_kw.sh's log format; kw1 had no stall, so no real STALL line exists.
  SYNTHETIC (made by the simulation below from the script's format strings): every K line (KAPPLY, KDEAD, KWHO), G,
    RESUMED_GAP, F, W, ON, STANCE, STALE lines of the meta file; snaps_all.txt; poses_all.txt; stall.csv. No real
    kept snapshot or pose file exists yet (the keeping watcher has never run on the engine).
The simulation: X-Ray frames every 25 ms, a snapshot every 100 ms (10 a second: the script exports when now - last_exp
>= 100), ITR2 poses every 50 ms. time_global advances by the wall time, also over the runner's unabsorbed 20 s stop (it
JUMPS by the stop's length, as in out/t2pre2/run/boot0/meta_t2.csv: 61654 -> RESUMED_GAP 361666 after 300 s);
f_time_delta, game time, regeneration and the F lines use the frame delta clamped to 100 ms. RESUMED_GAP has kill4's 6
fields (line 666). The kill-channel arithmetic is zz_p1_puppet_kill4.script's (k_line), the snapshot k lines its
export_b1.
"""
import sys, os, copy, hashlib, re
from datetime import datetime, timezone

KW1 = '/home/caden/Projects/StalkerGameEngine/out/kw1/run'
U0 = 1795800000000            # ITR2's first play pose (play clock 0)
SESS = '1795799990000'        # ITR2 session number
RUN_ID = U0 - 60000           # X-Ray run id (launch)
UF0 = U0 - 20000 + 7          # first X-Ray frame (script t = -15 s there; poses are read from t >= 0)
TG0 = 16944                   # time_global at the first frame (= kw1's first meta lines, so ABSORB_ON/S stay verbatim)
FRAME, SNAP_MS = 25, 100   # the script exports when now - last_exp >= 100: 10 snapshots a second
STOP_U, CONT_U = U0 + 105640, U0 + 125644
UEND = U0 + 170000
POSE_END = 160000
LEVEL = 'l01_escape'
ACT = (-146.312, -15.197, -152.190)
S_ID, M_ID = '19428', '23183'
NEAR_S, NEAR_M, FAR_BOAR = '23212', '19486', '23185'


def creatures_base():
    ax, ay, az = ACT
    sx, sz = ax + 20, az; mx, mz = ax + 20, az + 15
    return {   # id: section, kind, position, health, heal per second (X-Ray's own regeneration)
        S_ID: ['sim_default_stalker_0', 's', (sx, ay, sz), 1.0, 0.01],
        M_ID: ['flesh_03a_normal', 'm', (mx, ay, mz), 1.0, 0.0004],
        NEAR_S: ['flesh_01a_strong', 'm', (sx + 6, ay, sz), 1.0, 0.0004],                 # 6 m from S: judged in 6
        NEAR_M: ['esc_smart_terrain_5_7_loner_mechanic_stalker', 's', (mx, ay, mz + 8), 0.9, 0.01],   # 8 m from M
        FAR_BOAR: ['boar_01a_hard', 'm', (ax - 40, ay, az), 1.0, 0.0004],                # far: reported in 6
        '23196': ['sim_default_zombied_1', 's', (ax, ay, az - 60), 1.0, 0.01],
        '23175': ['cat_strong_c', 'm', (ax + 25, ay, az + 30), 1.0, 0.0004],
    }


GOOD = {
    'hits': [(30.0, S_ID, 600), (30.0, M_ID, 300), (70.0, S_ID, 500)],   # play-clock s, id, share in thousandths
    'kills': [(90.0, S_ID), (115.0, M_ID)],
    'world': [(30.0, FAR_BOAR, 0.1)],    # X-Ray's own world: a far creature loses 0.1 at S's step A (reported in 6)
    'leak': [],                           # frames that run inside the stop (the stop did not hold)
    'cont_delay': 35,                     # ms from CONT to X-Ray's first frame after the stop
    'heal': {},                           # id -> heal per second override
    'kind': {},                           # id -> kind override
}


def utcstr(u):
    return datetime.fromtimestamp(u / 1000, timezone.utc).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3] + 'Z'


class Real:
    def __init__(self, d):
        self.d = d
        need = ['boot0/meta_t2.csv', 'boot0/tb/xray.log', 't2_xray.log', 'restarts.csv', 'freeze.csv', 'lost.csv']
        for n in need:
            if not os.path.isfile(os.path.join(d, n)): sys.exit(f'mkfix: real kw1 file missing: {os.path.join(d, n)}')
        rl = lambda n: open(os.path.join(d, n), 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n')
        m = rl('boot0/meta_t2.csv')
        self.changer = [l for l in m if l.startswith('CHANGER,')]
        self.absorb = next(l for l in m if l.startswith('ABSORB_ON,'))
        self.sline = next(l for l in m if l.startswith('S,'))
        self.vitals = next(l for l in m if l.startswith('VITALS,'))
        self.movesum = next(l for l in m if l.startswith('MOVESUM,'))
        self.summary = next(l for l in m if l.startswith('SUMMARY,'))
        self.tick = next(l for l in m if l.startswith('TICK,'))
        self.t2sum = [l for l in m if l.startswith('T2SUM,')][-1]
        self.p2tail = [l for l in m if l.startswith('P2,')][-1].split(',')[4:]
        self.p2tail[8] = '0'   # ring 0 (field 12 of the line): kw1's last pose was on a neighbouring cell; here none is
        self.runner = [l for l in rl('t2_xray.log') if l]
        self.xray = open(os.path.join(d, 'boot0/tb/xray.log'), 'rb').read()
        self.hdr = {n: open(os.path.join(d, n), 'rb').read() for n in ('restarts.csv', 'freeze.csv', 'lost.csv')}
        assert self.absorb.split(',')[1] == str(TG0) and self.sline.split(',')[1] == str(TG0), 'kw1 first tg changed'


def kv_replace(line, vals):
    """real line 'NAME,k=v,...' -> same keys and order, values from vals (missing keys keep the real value)"""
    out = []
    for p in line.split(','):
        if '=' in p: k, v = p.split('=', 1); out.append(f'{k}={vals.get(k, v)}')
        else: out.append(p)
    return ','.join(out)


# ======================================================================================================================
def simulate(R, kn):
    """returns a fixture dict: meta (list of lines, no CR), snaps (list of {'u','lines'}), poses (list of {'u','lines'}),
    files (rel -> bytes), info (times used by the mutants)"""
    cr = creatures_base()
    for i, k in kn['kind'].items(): cr[i][1] = k
    for i, h in kn['heal'].items(): cr[i][4] = h
    C = {i: {'sec': v[0], 'kind': v[1], 'pos': v[2], 'h': v[3], 'heal': v[4], 'alive': True} for i, v in cr.items()}
    order = list(C)
    meta = list(R.changer) + [R.absorb, R.sline]
    snaps, poses = [], []
    # ITR2 state
    itot, idead, idrop = {}, {}, set()
    hits = sorted(kn['hits']); kills = sorted(kn['kills']); world = sorted(kn['world'], key=lambda w: w[0])
    done_h, done_k, done_w = set(), set(), set()
    pose_seq = 0
    pose_list = []      # (pu, seq, lines) ITR2 writes
    # X-Ray state
    K = {}; kc = dict(khits=0, kdead_online=0)
    p_last = 0; p_last_t = None; k_r = None; stale = False; stance_done = False
    seq = 0; last_exp = -10 ** 9; last_g = -10 ** 9; last_wall = None; prev_tg = None; f_until = None
    tg = TG0; gms = 3165006250; t0 = TG0 + 15000; frame_no = 32; seen = set(); exp_n = 0; last_tick = TG0
    info = {'kapply': [], 'rg': None}

    def itr2_snapshot(at):   # newest snapshot ITR2 has read at utc `at` (it reads 20 a second; 10 ms after the write)
        j = len(snaps) - 1
        while j >= 0 and snaps[j]['wu'] > at - 10: j -= 1
        return snaps[j] if j >= 0 else None

    def itr2_pose(pu):
        nonlocal pose_seq
        p = (pu - U0) / 1000
        sn = itr2_snapshot(pu)
        if sn:
            for i in list(itot):
                if i in sn['kd'] and sn['kd'][i][1]: idrop.add(i)      # X-Ray's dead line read: the line is dropped
        for j, (ph, i, sh) in enumerate(hits):
            if p >= ph and j not in done_h and i not in idrop and not idead.get(i):
                done_h.add(j); itot[i] = itot.get(i, 0) + sh
        for j, kl in enumerate(kills):
            pk, i = kl[0], kl[1]
            if p >= pk and j not in done_k and i not in idrop and not idead.get(i):
                done_k.add(j)
                if len(kl) > 2 and kl[2] == 'zero': idead[i] = True; continue   # death at an UNCHANGED total
                sh_h = sn['h'].get(i, 1.0) if sn else 1.0; ap = sn['kd'].get(i, (0.0, False))[0] if sn else 0.0
                shown = sh_h - (itot.get(i, 0) / 1000 - ap)
                itot[i] = itot.get(i, 0) + max(0, round(shown * 1000)); idead[i] = True
        pose_seq += 1
        state = 'end' if pu - U0 >= POSE_END else 'play'
        L = [f'seq,{pose_seq},{pu},{state},{LEVEL},keep,{SESS},4', f'p,{ACT[0]:.3f},{ACT[1] + 0.3:.3f},{ACT[2]:.3f},0.0000,1.000,0']
        for i in sorted(itot):
            if i in idrop: continue
            L.append(f'k,{i},{C[i]["sec"]},{itot[i] / 1000:.3f},{1 if idead.get(i) else 0}')
        L.append(f'end,{pose_seq}')
        pose_list.append((pu, pose_seq, L))
        poses.append({'u': pu + 1 + pose_seq % 2, 'lines': L})

    def k_line(now, n, line, deaths):
        _, i, sec, ts, dead = line.split(',')
        c = C[i]; r = K.setdefault(i, {'ap': 0.0, 'dd': None})
        tot = float(ts)
        if r['dd'] is not None:
            if not c['alive']: r['dd'] = now
            return
        if not c['alive']:
            r['dd'] = now; meta.append(f'KDEAD,{now},{n},{i},seen'); return
        diff = max(0.0, tot - r['ap'])
        if diff > 1e-9:
            d = min(diff, 1.0); hb = c['h']; hn = hb - d
            if dead != '1': hn = max(hn, min(hb, 0.01))
            c['h'] = max(hn, 0.0); r['ap'] = tot; kc['khits'] += 1
            meta.append(f'KAPPLY,{now},{n},{i},{d:.3f},{r["ap"]:.3f},{hb:.4f},{c["h"]:.4f},hit')
            info['kapply'].append((i, tot, now, n))
        if dead == '1':
            c['alive'] = False; c['h'] = 0.0; r['dd'] = now; kc['kdead_online'] += 1
            meta.append(f'KDEAD,{now},{n},{i},online'); deaths.append(i)

    # frame times: every 25 ms, none inside the stop except the leak frames
    ft = [u for u in range(UF0, STOP_U, FRAME)] + sorted(kn['leak'])
    u = CONT_U + kn['cont_delay']
    while u <= UEND: ft.append(u); u += FRAME
    ft.sort()
    next_pose = U0; prev_u = None
    for u in ft:
        while next_pose <= min(u, U0 + POSE_END): itr2_pose(next_pose); next_pose += 50
        dt = (u - prev_u) if prev_u is not None else 0
        # time_global advances by the WALL time, also over an unabsorbed SIGSTOP (real: out/t2pre2 61654 -> 361666 after
        # a 300 s stop); f_time_delta (regeneration, game time, F lines) is clamped to 100 ms
        dtg = min(dt, 100); prev_gms = gms; gms += 6 * dtg
        tg += dt; prev_u = u; frame_no += 1; now = tg
        t = (now - t0) / 1000
        wall = u // 1000
        if last_wall is not None and wall - last_wall >= 2:
            meta.append(f'RESUMED_GAP,{now},{wall},{wall - last_wall},{gms:.0f},{prev_tg},{prev_gms:.0f}')   # kill4 line 666
            info.setdefault('rgs', []).append(len(meta) - 1); f_until = wall + 30
        last_wall = wall; prev_tg = now
        if f_until and wall <= f_until: meta.append(f'F,{now},{float(dtg):.1f}')
        if now - last_g >= 1000:
            last_g = now; meta.append(f'G,{now},{wall},{frame_no},{gms:.0f},26250')
        # X-Ray's own world and regeneration
        for j, (pw, i, dh) in enumerate(world):
            if j not in done_w and u >= U0 + pw * 1000:
                done_w.add(j)
                if dh == 'kill': C[i]['h'] = 0.0; C[i]['alive'] = False
                else: C[i]['h'] = max(0.0, C[i]['h'] - dh)
        for c in C.values():
            if c['alive']: c['h'] = min(1.0, c['h'] + c['heal'] * dtg / 1000)
        # the pose (script t >= 0)
        if t >= 0:
            cur = None
            for pl in reversed(pose_list):
                if pl[0] <= u: cur = pl; break
            if cur and cur[1] > p_last:
                pu, n, L = cur
                if stale: stale = False; meta.append(f'RESUME,{t:.2f},{now},{n}')
                p_last = n; p_last_t = now
                state = L[0].split(',')[3]
                deaths = []
                for l in L:
                    if l.startswith('k,'): k_line(now, n, l, deaths)
                k_r = f'{SESS},{n}'
                if state == 'play':
                    if not stance_done: stance_done = True; meta.append(f'STANCE,{now},{n},0')
                    meta.append(','.join(['P2', str(n), str(pu), str(now)] + R.p2tail))
                for i in deaths: meta.append(f'KWHO,{now},{i},0')
            elif p_last_t is not None and not stale and now - p_last_t >= 1000:
                stale = True; meta.append(f'STALE,{t:.2f},{now},{p_last}')
        if now - last_tick >= 60000:
            last_tick = now
            meta.append(kv_replace(R.tick.replace(R.tick.split(',')[1], f'{t:.2f}', 1), {'export_ticks': exp_n}))
        if now - last_exp >= SNAP_MS:
            last_exp = now; seq += 1; exp_n += 1
            L = [f'seq,{seq},{now},{t:.2f},{LEVEL},{RUN_ID},4', f'a,{ACT[0]:.3f},{ACT[1]:.3f},{ACT[2]:.3f},0.0000,1.0000']
            kd = {}; hh = {}
            for i in order:
                c = C[i]; x, y, z = c['pos']
                dist = ((x - ACT[0]) ** 2 + (z - ACT[2]) ** 2) ** 0.5
                L.append(f'o,{i},{c["sec"]},{c["kind"]},{x:.3f},{y:.3f},{z:.3f},0.0000,{c["h"]:.4f},{1 if c["alive"] else 0},-1,{dist:.2f}')
                hh[i] = round(c['h'], 4)
                if i not in seen: seen.add(i); meta.append(f'ON,{t:.2f},{now},{i},{c["sec"]},{dist:.1f}')
            if k_r: L.append(f'r,{k_r}')
            for i in sorted(K):
                r = K[i]
                if r['dd'] is not None and now - r['dd'] <= 10000: L.append(f'k,{i},{r["ap"]:.3f},dead'); kd[i] = (r['ap'], True)
                elif r['ap'] > 0 and r['dd'] is None: L.append(f'k,{i},{r["ap"]:.3f}'); kd[i] = (r['ap'], False)
            L.append(f'end,{seq}')
            snaps.append({'u': u + 1 + seq % 2, 'wu': u, 'lines': L, 'kd': kd, 'h': hh, 'tx': now, 't': t})
            meta.append(f'W,{seq},{520 + seq % 97}')
    meta.append(kv_replace(R.summary, {'export_ticks': exp_n}))
    t2 = dict(last_seq=p_last, torn=0, holds=0, paused='false', near=0, guards=0, lost=0, epochs=0, khits=kc['khits'],
              kdead_online=kc['kdead_online'], kdead_offline=0, kowed=0, kstale=0, kback=0, krefused=0, kclamped=0, kver=0,
              kdead_fail=0, ktabfail=0)
    meta.append(kv_replace(R.t2sum, t2))
    meta += [R.vitals, R.movesum]
    # runner files
    last_pre = [s for s in snaps if s['wu'] < STOP_U][-1]
    stall = f'utc_ms,event,seq,track_s\n{STOP_U},STOP,{last_pre["lines"][0].split(",")[1]},{last_pre["t"]:.2f}\n{CONT_U},CONT,{last_pre["lines"][0].split(",")[1]},{last_pre["t"]:.2f}\n'
    rl = [f'{utcstr(UF0 - 26000)} boot 0: run_id {RUN_ID} engine_s 125 load 3.49 5.86 6.33',
          f'{utcstr(UF0 - 25999)} boot 0: wall limit 525 s from launch; over-run limit off',
          f'{utcstr(UF0 + 40)} boot 0: level {LEVEL} (run length counted from here)',
          f'{utcstr(UF0 + 81)} boot 0: first snapshot seq 1 after 26 s; engine 1805655; candidates: 1805478 /usr/lib/wine/wine 18336;1805651 /usr/lib/wine/wine 29280;1805655 /usr/lib/wine/wine64 16883124;',
          f'{utcstr(STOP_U)} STALL start: seq {last_pre["lines"][0].split(",")[1]} track {last_pre["t"]:.2f}s engine 1805655',
          f'{utcstr(CONT_U)} STALL end: SIGCONT sent',
          f'{utcstr(UEND + 3000)} boot 0 end: tb_run rc=0 reason=engine run ended',
          f'{utcstr(UEND + 3001)} end: ran to the end; boots 1; unplanned restarts 0; stall 1; planned kill no; rc=0']
    files = {'t2_xray.log': ('\n'.join(rl) + '\n').encode(), 'restarts.csv': R.hdr['restarts.csv'],
             'freeze.csv': R.hdr['freeze.csv'], 'lost.csv': R.hdr['lost.csv'], 'stall.csv': stall.encode(),
             'boot0/tb/xray.log': R.xray}
    return {'meta': meta, 'snaps': snaps, 'poses': poses, 'files': files, 'info': info, 'kn': kn, 'readme': []}


# ---- edit helpers -----------------------------------------------------------------------------------------------------
def snap_tx(s): return int(s['lines'][0].split(',')[2])
def snap_seq(s): return int(s['lines'][0].split(',')[1])
def o_idx(s, i): return next(j for j, l in enumerate(s['lines']) if l.startswith(f'o,{i},'))
def o_set(s, i, field, val):
    j = o_idx(s, i); f = s['lines'][j].split(','); f[field] = val; s['lines'][j] = ','.join(f)
def o_get(s, i, field): return s['lines'][o_idx(s, i)].split(',')[field]
def meta_idx(F, pred): return next(j for j, l in enumerate(F['meta']) if pred(l.split(',')))
def kap_idx(F, i, nth=0): return [j for j, l in enumerate(F['meta']) if l.startswith(f'KAPPLY,') and l.split(',')[3] == i][nth]
def has_dead(s, i): return any(l == f'k,{i},' + l.split(',')[2] + ',dead' for l in s['lines'] if l.startswith(f'k,{i},'))
def first_dead_pose_u(F, i): return next(p['u'] for p in F['poses'] if any(l.startswith(f'k,{i},') and l.endswith(',1') for l in p['lines']))


def m01_fatal(F): F['files']['boot0/tb/xray.log'] = F['files']['boot0/tb/xray.log'] + b'FATAL ERROR (fixture mutant)\r\n'
def m01_lua(F): F['files']['boot0/tb/xray.log'] = F['files']['boot0/tb/xray.log'] + b'! LUA error: z:\\run\\gamedata\\scripts\\zz_p1_puppet.script:327: attempt to index a nil value (fixture mutant)\r\n'
def m01_rc(F): F['files']['t2_xray.log'] = F['files']['t2_xray.log'].replace(b'rc=0\n', b'rc=6\n')
def m01_restart(F): F['files']['restarts.csv'] = F['files']['restarts.csv'] + f'{U0 + 50000},0,engine exited (tb_run rc 1),0\n'.encode()
def m01_boot1(F): F['extra_dirs'] = ['boot1']

def m02_after(F):
    j = kap_idx(F, S_ID); f = F['meta'][j].split(','); f[7] = f'{float(f[7]) + 0.001:.4f}'; F['meta'][j] = ','.join(f)
def m02_diff(F):
    j = kap_idx(F, M_ID); f = F['meta'][j].split(','); d = float(f[4]) - 0.010
    f[4] = f'{d:.3f}'; f[7] = f'{float(f[6]) - d:.4f}'; F['meta'][j] = ','.join(f)
def m03_second(F):   # S: a further KAPPLY ~10 s after its 1.100 one, while the pose total is still 1.100
    j = kap_idx(F, S_ID, 1); T2 = int(F['meta'][j].split(',')[1])
    k = meta_idx(F, lambda f: f[0] == 'P2' and int(f[3]) >= T2 + 10000); f = F['meta'][k].split(',')
    F['meta'].insert(k, f'KAPPLY,{f[3]},{f[1]},{S_ID},0.500,1.100,0.4000,0.0100,hit')
def m04_fall_with_k(F):   # S: one snapshot between its part hits 0.05 lower, and a KDEAD,failed line of mine before it
    a = int(F['meta'][kap_idx(F, S_ID, 0)].split(',')[1]); b = int(F['meta'][kap_idx(F, S_ID, 1)].split(',')[1])
    ss = [s for s in F['snaps'] if a <= snap_tx(s) < b]; s = ss[len(ss) // 2]
    o_set(s, S_ID, 8, f'{float(o_get(s, S_ID, 8)) - 0.05:.4f}')
    w = meta_idx(F, lambda f: f[0] == 'W' and f[1] == str(snap_seq(s)))
    n = [l.split(',')[1] for l in F['meta'][:w] if l.startswith('P2,')][-1]
    F['meta'].insert(w, f'KDEAD,{snap_tx(s)},{n},{S_ID},failed,alive')
def m05_clamped(F):
    j = kap_idx(F, S_ID, 1); f = F['meta'][j].split(','); f[4] = '0.400'; f[7] = f'{float(f[6]) - 0.4:.4f}'; F['meta'][j] = ','.join(f)
def m07_late(F):
    u0 = first_dead_pose_u(F, S_ID)
    for s in F['snaps']:
        if s['u'] < u0 + 2500: s['lines'] = [l for l in s['lines'] if not (l.startswith(f'k,{S_ID},') and l.endswith(',dead'))]
def dup_kdead(i):
    def f(F):
        j = meta_idx(F, lambda f: f[0] == 'KDEAD' and f[3] == i and f[4] == 'online'); F['meta'].insert(j + 1, F['meta'][j])
    return f
def m08_killer(F):
    j = meta_idx(F, lambda f: f[0] == 'KWHO' and f[2] == S_ID); F['meta'][j] = re.sub(r',[-\d]+$', ',5', F['meta'][j])
def m09_norg(F):
    F['meta'] = [l for l in F['meta'] if not l.startswith('RESUMED_GAP,')]
def m09_snapinside(F):   # the watcher kept one snapshot 10 s into the stop (a copy of the last one before, next seq);
    # later snapshots renumbered; the meta file is left as it is (clause "no snapshot written inside the stop" alone)
    j = max(j for j, s in enumerate(F['snaps']) if s['u'] < STOP_U); s0 = F['snaps'][j]; q = snap_seq(s0) + 1
    new = {'u': STOP_U + 10000, 'lines': [f'seq,{q},' + s0['lines'][0].split(',', 2)[2]] + s0['lines'][1:-1] + [f'end,{q}']}
    for s in F['snaps'][j + 1:]:
        n = snap_seq(s) + 1; s['lines'][0] = f'seq,{n},' + s['lines'][0].split(',', 2)[2]; s['lines'][-1] = f'end,{n}'
    F['snaps'].insert(j + 1, new)
def m09_eventinside(F):   # a frame's lines inside the stop, no snapshot: a G line (8 s into the stop) and a KDEAD,failed
    # of a creature that is neither S nor M, at the script time of the last snapshot before the stop (so the script-time
    # clause cannot see it): clause "no KAPPLY or KDEAD whose script time maps inside the stop" alone
    w = max(j for j, l in enumerate(F['meta']) if l.startswith('W,') and int(l.split(',')[1]) <= max(snap_seq(s) for s in F['snaps'] if s['u'] < STOP_U))
    tg = max(snap_tx(s) for s in F['snaps'] if s['u'] < STOP_U); n = [l.split(',')[1] for l in F['meta'][:w] if l.startswith('P2,')][-1]
    F['meta'][w + 1:w + 1] = [f'G,{tg},{(STOP_U + 8000) // 1000},99999,{3165006250 + 6 * (tg - TG0)},26250', f'KDEAD,{tg},{n},23196,failed,alive']
def m09_scripttime(F):   # a KDEAD,failed (another creature) whose script time lies strictly between RESUMED_GAP's last frame
    # before the gap and its first frame after, written in the meta file before the stop (no G line, so the W mapping puts
    # it before STOP): clause "no KAPPLY or KDEAD with script time inside the stop" alone
    rg = next(l.split(',') for l in F['meta'] if l.startswith('RESUMED_GAP,')); T = int(rg[5]) + 1   # 1 ms after the last frame before the gap
    w = max(j for j, l in enumerate(F['meta']) if l.startswith('W,') and int(l.split(',')[1]) <= max(snap_seq(s) for s in F['snaps'] if s['u'] < STOP_U))
    n = [l.split(',')[1] for l in F['meta'][:w] if l.startswith('P2,')][-1]
    F['meta'].insert(w + 1, f'KDEAD,{T},{n},23196,failed,alive')
def m10_late(F):
    for s in F['snaps']:
        if s['u'] <= CONT_U + 2500: s['lines'] = [l for l in s['lines'] if not (l.startswith(f'k,{M_ID},') and l.endswith(',dead'))]
def m11_alive(F):
    u0 = next(s['u'] for s in F['snaps'] if has_dead(s, M_ID)); s = next(s for s in F['snaps'] if s['u'] >= u0 + 5000)
    o_set(s, M_ID, 9, '1')
def mid_snap(F, p): return next(s for s in F['snaps'] if s['u'] >= U0 + p * 1000)
def m12_actor(F):
    s = mid_snap(F, 60); j = next(j for j, l in enumerate(s['lines']) if l.startswith('a,')); f = s['lines'][j].split(',')
    f[5] = '0.9990'; s['lines'][j] = ','.join(f)
def r_idx(s): return next(j for j, l in enumerate(s['lines']) if l.startswith('r,'))
def m13_back(F):
    s = mid_snap(F, 60); j = r_idx(s); f = s['lines'][j].split(','); f[2] = str(int(f[2]) - 5); s['lines'][j] = ','.join(f)
def m13_absent(F):
    s = mid_snap(F, 60); del s['lines'][r_idx(s)]
def m13_lag(F):
    ss = [s for s in F['snaps'] if U0 + 50000 <= s['u'] < U0 + 53000]; r0 = ss[0]['lines'][r_idx(ss[0])]
    for s in ss: s['lines'][r_idx(s)] = r0
def t2_edit(key, how):
    def f(F):
        j = max(j for j, l in enumerate(F['meta']) if l.startswith('T2SUM,')); parts = F['meta'][j].split(',')
        if how == 'drop': parts = [p for p in parts if not p.startswith(key + '=')]
        elif how == 'add': parts.append(f'{key}=0')
        else:
            for q, p in enumerate(parts):
                if p.startswith(key + '='):
                    v = p.split('=', 1)[1]; parts[q] = f'{key}=' + ('true' if v == 'false' else str(int(v) + how))
        F['meta'][j] = ','.join(parts)
    return f
def m_badrec(F):
    s = mid_snap(F, 40); s['lines'][-1] = 'end,1'
def nv1_kindunknown(F):
    for s in F['snaps']: s['lines'] = [l for l in s['lines'] if not l.startswith(f'o,{M_ID},')]
def m_crlf(F): F['crlf'] = True


def m10_late24(F):   # M's dead line first appears 2.4 s after the first snapshot read at or after CONT
    fp = next(s['u'] for s in F['snaps'] if s['u'] >= CONT_U)
    for s in F['snaps']:
        if s['u'] < fp + 2400: s['lines'] = [l for l in s['lines'] if not (l.startswith(f'k,{M_ID},') and l.endswith(',dead'))]
def m10_kdead_before(F):   # M's KDEAD,online line moved before the stop (script time of the last snapshot before it);
    # snapshots unchanged: clause "the KDEAD is after the stop" alone
    j = meta_idx(F, lambda f: f[0] == 'KDEAD' and f[3] == M_ID and f[4] == 'online'); f = F['meta'].pop(j).split(',')
    tx = max(snap_tx(s) for s in F['snaps'] if s['u'] < STOP_U); f[1] = str(tx)
    w = meta_idx(F, lambda g: g[0] == 'RESUMED_GAP'); F['meta'].insert(w, ','.join(f))
def nv6_unlisted(F):   # S not listed in the snapshots read from 69.5 s to 70.5 s (around the first pose of its 1.100 step)
    for s in F['snaps']:
        if U0 + 69500 <= s['u'] < U0 + 70500: s['lines'] = [l for l in s['lines'] if not l.startswith(f'o,{S_ID},')]
def m09_noanchor(F):   # m09_scripttime's event, and every G line before it removed: no wall anchor precedes the event,
    # so only the script-time clause (which needs none) can catch it
    m09_scripttime(F)
    j = meta_idx(F, lambda f: f[0] == 'KDEAD' and f[4] == 'failed')
    F['meta'] = [l for k, l in enumerate(F['meta']) if not (k < j and l.startswith('G,'))]
def m14_final_nor(F):   # the final kept snapshot has no r line (all earlier ones keep theirs)
    s = F['snaps'][-1]; del s['lines'][r_idx(s)]
def nv6_mdeath_unlisted(F):   # M not listed in the snapshots read in the last 1.5 s before STOP: the newest snapshot read at
    # M's death pose (115 s, inside the stop) is the last one before the stop
    for s in F['snaps']:
        if STOP_U - 1500 <= s['u'] < STOP_U: s['lines'] = [l for l in s['lines'] if not l.startswith(f'o,{M_ID},')]
def nan_health(F):   # one snapshot's o line for S carries health nan
    s = mid_snap(F, 60); o_set(s, S_ID, 8, 'nan')
def v5_pose(F):   # one kept pose with 8 header fields and version 5
    p = F['poses'][1000]; h = p['lines'][0].split(','); h[7] = '5'; p['lines'][0] = ','.join(h)
def m09_longgap(F):   # RESUMED_GAP's gap 20 s longer: its last frame before lies 20 s before STOP (a 40 s gap around the stop)
    j = meta_idx(F, lambda f: f[0] == 'RESUMED_GAP'); f = F['meta'][j].split(','); f[3] = str(int(f[3]) + 20); F['meta'][j] = ','.join(f)
def m13_ahead(F):   # the snapshots read from 60 s to 61 s carry the r line of the snapshot read at 61 s: they name a pose
    # up to 1 s later than the newest kept pose read by then (r never goes back, so only the 'ahead' clause fails)
    r1 = mid_snap(F, 61); r1 = r1['lines'][r_idx(r1)]
    for s in F['snaps']:
        if U0 + 60000 <= s['u'] < U0 + 61000: s['lines'][r_idx(s)] = r1
def m06_noafter(F):   # a KDEAD,failed (another creature) 50 ms after the last snapshot was written: no snapshot after it
    T = snap_tx(F['snaps'][-1]) + 50
    n = [l.split(',')[1] for l in F['meta'] if l.startswith('P2,')][-1]
    j = max(k for k, l in enumerate(F['meta']) if l.startswith(('W,', 'P2,', 'TICK,', 'ON,')))
    F['meta'].insert(j + 1, f'KDEAD,{T},{n},23196,failed,alive')
def bad_pose(F):
    p = F['poses'][1000]; p['lines'][-1] = 'end,1'
def bad_kline(F):
    j = kap_idx(F, S_ID); F['meta'][j] = ','.join(F['meta'][j].split(',')[:7])
def v3_pose(F):
    p = F['poses'][1000]; p['lines'][0] = p['lines'][0].rsplit(',', 1)[0]
def chain(*fs):
    def f(F):
        for g in fs: g(F)
    f.__name__ = '+'.join(g.__name__ for g in fs); return f


def kn(**over):
    k = copy.deepcopy(GOOD)
    for a, b in over.items(): k[a] = b
    return k


# ======================================================================================================================
REQ = ('t2_xray.log', 'restarts.csv', 'stall.csv', 'boot0/meta_t2.csv', 'boot0/tb/xray.log',
       'boot0/watch/snaps_all.txt', 'boot0/watch/poses_all.txt')
COUNTER_KEYS = ['last_seq', 'torn', 'holds', 'paused', 'near', 'guards', 'lost', 'epochs', 'khits', 'kdead_online',
                'kdead_offline', 'kowed', 'kstale', 'kback', 'krefused', 'kclamped', 'kver', 'kdead_fail', 'ktabfail']


def render(F):
    crlf = F.get('crlf'); nl = '\r\n' if crlf else '\n'
    out = dict(F['files'])
    if crlf:
        for n in ('t2_xray.log', 'restarts.csv', 'stall.csv', 'freeze.csv', 'lost.csv'):
            out[n] = out[n].replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
    out['boot0/meta_t2.csv'] = ('\r\n'.join(F['meta']) + '\r\n').encode()      # the real meta file is CRLF
    out['boot0/watch/snaps_all.txt'] = ''.join(f'#snap,{s["u"]},{s["lines"][0].split(",")[1]}{nl}' + nl.join(s['lines']) + nl for s in F['snaps']).encode()
    out['boot0/watch/poses_all.txt'] = ''.join(f'#pose,{p["u"]},{p["lines"][0].split(",")[1]}{nl}' + nl.join(p['lines']) + nl for p in F['poses']).encode()
    for n, old, new in F.get('patch', ()):
        assert out[n].count(old) == 1, (n, old); out[n] = out[n].replace(old, new)
    for n in F.get('drop', ()): del out[n]
    for n in F.get('empty', ()): out[n] = b''
    return out


SYN = ('SYNTHETIC: boot0/watch/snaps_all.txt, boot0/watch/poses_all.txt, stall.csv, and in boot0/meta_t2.csv every K, G, '
       'RESUMED_GAP, F, W, ON, STANCE, STALE line; t2_xray.log (8 lines written by t3_kj1_mkfix.py, not read from kw1: six '
       'copy the text of kw1\'s six runner lines with the timestamps and the run id changed and "stall no" -> "stall 1"; '
       'the two STALL lines are in kw1/t2_xray_kw.sh\'s log format, kw1 itself had no stall)')
REALD = ('REAL-DERIVED (kw1 line, numbers replaced): meta P2, T2SUM, SUMMARY, TICK lines')
REALV = ('REAL, verbatim from out/kw1/run: boot0/tb/xray.log, restarts.csv, freeze.csv, lost.csv; meta CHANGER, '
         'ABSORB_ON, S, VITALS, MOVESUM lines')


class Writer:
    def __init__(self, dest):
        self.dest = dest; self.seen = {}; self.rows = []

    def write(self, name, F, kind, verdict, rc, fails, nvs, must, note):
        O = os.path.join(self.dest, name); os.makedirs(O)
        files = render(F)
        for rel, data in files.items():
            p = os.path.join(O, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
            key = (rel, hashlib.sha256(data).hexdigest())
            if rel in F.get('unreadable', ()):   # its own inode (never a hard link), then mode 000: open() -> PermissionError
                with open(p, 'xb') as f: f.write(data)
                os.chmod(p, 0)
            elif key in self.seen: os.link(self.seen[key], p)
            else:
                with open(p, 'xb') as f: f.write(data)
                self.seen[key] = p
        for d in F.get('extra_dirs', ()): os.makedirs(os.path.join(O, d))
        readme = [f'kj1 checker fixture {name} ({kind}): {note}', REALV, REALD, SYN,
                  f'simulation knobs: {F["kn"]}', *F['readme'],
                  f'expected: verdict {verdict}, rc {rc}, failing checks {fails}, NO VERDICT rules fired {nvs}, report contains {must}',
                  'Files identical to another fixture\'s are hard links (made by t3_kj1_mkfix.py; nothing was deleted).']
        with open(os.path.join(O, 'README.txt'), 'x') as f: f.write('\n'.join(readme) + '\n')
        self.rows.append('\t'.join([name, kind, verdict, str(rc), fails, nvs, must, note]))

    def close(self):
        with open(os.path.join(self.dest, 'fixtures.tsv'), 'x') as f:
            f.write('name\tkind\tverdict\trc\tfails\tnvs\tmust\tnote\n' + '\n'.join(self.rows) + '\n')


def main():
    if len(sys.argv) < 2: sys.exit('usage: t3_kj1_mkfix.py <dest dir (must not exist)> [<kw1 runner dir>]')
    dest = sys.argv[1]; R = Real(sys.argv[2] if len(sys.argv) > 2 else KW1)
    if os.path.exists(dest): sys.exit(f'mkfix: {dest} exists')
    os.makedirs(dest); W = Writer(dest)
    good = simulate(R, kn())
    W.write('good', good, 'good', 'PASS', 0, '', '', '', 'the design run: S 0.600 at 30 s, healing, 1.100 at 70 s, dead at 90 s; '
            'M 0.300 at 30 s, dead at 115 s inside the stop, confirmed after CONT; a far creature hurt by X-Ray at 30 s (reported)')

    def mut(name, n, f, note, base=None, fails=None):
        F = copy.deepcopy(base or good); f(F); F['readme'].append(f'MUTANT of good by {f.__name__}: {note}')
        W.write(name, F, 'mutant', 'FAIL', 1, fails or str(n), '', '', note)

    def sim(name, knobs, note, fails, kind='mutant', verdict='FAIL', rc=1, nvs='', must='', post=None):
        F = simulate(R, knobs)
        if post: post(F)
        F['readme'].append(f'SIMULATED with changed knobs: {note}')
        W.write(name, F, kind, verdict, rc, fails, nvs, must, note)

    sim('good_slowframe', kn(cont_delay=1800), 'X-Ray\'s first frame after CONT comes 1.8 s late; that first snapshot already '
        'carries M\'s dead line (PASS: the CONT -> first snapshot time is reported, not judged)', '', 'good', 'PASS', 0)
    W.write('good_crlf', dict(copy.deepcopy(good), crlf=True), 'good', 'PASS', 0, '', '', '',
            'good with every kept file and runner file in CRLF (the checker strips CR everywhere)')
    # ---- one or more mutants per check ---------------------------------------------------------------------------
    mut('m01_fatal', 1, m01_fatal, 'a FATAL line appended to the engine log')
    mut('m01_lua', 1, m01_lua, 'a Lua error naming zz_p1_puppet.script appended to the engine log')
    mut('m01_rc', 1, m01_rc, 'runner log ends rc=6')
    mut('m01_restart', 1, m01_restart, 'one unplanned restart row')
    mut('m01_boot1', 1, m01_boot1, 'a second boot folder boot1')
    mut('m02_after', 2, m02_after, 'S step A: KAPPLY health after off by +0.001')
    mut('m02_diff', 2, m02_diff, 'M step A: KAPPLY difference 0.010 below the pose total (after = before - d kept)')
    mut('m03_second', 3, m03_second, 'S: a further KAPPLY 10 s after its 1.100 one while the pose total stays 1.100')
    mut('m04_fall_with_k', 4, m04_fall_with_k, 'S between its part hits: one snapshot 0.05 lower with a KDEAD,failed line of mine before it')
    sim('m05_nototal', kn(hits=[(30.0, S_ID, 600), (30.0, M_ID, 300), (70.0, S_ID, 300)]),
        'S\'s second part hit 0.300: no S total above 1.000 while alive (check 5 fails by name)', '5',
        must='no S total above 1.000 was sent while S was alive')
    sim('m05_deadsecond', kn(heal={S_ID: 0.0}, hits=[(30.0, S_ID, 600), (30.0, M_ID, 300)], kills=[(70.0, S_ID), (115.0, M_ID)]),
        'S does not regenerate and the rig\'s second hit kills it (total 1.000, dead = 1 at 70 s)', '5',
        must='S did not heal enough: dead at the second hit|not judged for S')
    sim('m05_noheal', kn(heal={S_ID: 0.0}), 'S does not regenerate: no healing, and S stops at 0.01 after the 1.100 hit', '5')
    sim('m05_slowheal', kn(heal={S_ID: 0.0035}), 'S regenerates 0.0035/s: +0.14 in 40 s, under the 0.15 floor; S still alive at 0.04 after the 1.100 hit (healing clause alone)', '5')
    sim('m05_lowafter', kn(hits=[(30.0, S_ID, 600), (30.0, M_ID, 300), (70.0, S_ID, 790)]), 'S\'s second hit 0.790 (total 1.390): healed, the pose difference applied, but S left at 0.01 (alive-above-0.02 clause alone)', '5')
    mut('m05_clamped', 5, m05_clamped, 'S: the 1.100 KAPPLY difference is 0.400, not the pose difference 0.500')
    sim('m06_near_drop', kn(world=GOOD['world'] + [(30.0, NEAR_S, 0.05)]), 'a creature 6 m from S loses 0.05 across S\'s step A', '6')
    mut('m07_late', 7, m07_late, 'S: its dead line first read 2.5 s after the first dead pose')
    mut('m07_two', 7, dup_kdead(S_ID), 'S: two KDEAD online lines')
    mut('m08_killer', 8, m08_killer, 'S: KWHO names killer 5')
    sim('m09_leak', kn(leak=[U0 + 110000 + 3]), 'one X-Ray frame runs at 110 s inside the stop (a snapshot written inside)', '9')
    mut('m09_norg', 9, m09_norg, 'no RESUMED_GAP line: the script recorded no stop')
    mut('m09_snapinside', 9, m09_snapinside, 'one kept snapshot read 10 s into the stop (meta unchanged)')
    mut('m09_scripttime', 9, m09_scripttime, 'a KDEAD,failed (another creature) with a script time between the last frame before the stop and the first after')
    mut('m09_eventinside', 9, m09_eventinside, 'a G line and a KDEAD,failed (another creature) whose script time maps 8 s into the stop')
    sim('m09_10_kill_in_stop', kn(leak=list(range(U0 + 116003, U0 + 117003, 25))), 'frames run 116-117 s inside the stop and '
        'deal M\'s death there (MULTI: a death inside the stop is also not after CONT)', '9,10')
    mut('m09_noanchor', 9, m09_noanchor, 'as m09_scripttime, and every G line before the event removed: no wall anchor precedes it '
        '(the script-time clause alone, which needs no anchor)')
    mut('m09_longgap', 9, m09_longgap, 'RESUMED_GAP\'s gap 20 s longer (40 s, spanning the 20 s stop): its edges are not near STOP/CONT, it does not pair')
    sim('m09_slowresume', kn(cont_delay=6500), 'X-Ray\'s first frame after CONT comes 6.5 s late: RESUMED_GAP\'s end edge is '
        'later than CONT + RESUME_MAX_MS (5 s, the bridge\'s T2_LOST_MS) + WALL_ERR_MS (1 s), so X-Ray stalled on its own after '
        'the stop and the gap is no longer "the stop": it does not pair, check 9 FAILS', '9')
    F = copy.deepcopy(good); m13_ahead(F); F['readme'].append('MUTANT of good by m13_ahead: r lines 60-61 s name the pose read at 61 s')
    W.write('m13_ahead', F, 'mutant', 'FAIL', 1, '13', '', 'more than 50 ms 10 |smallest lag -1000 ms', 'the snapshots read 60-61 s carry '
            'the r line of the snapshot read at 61 s (up to 1 s ahead of the newest kept pose read by then; r never goes back)')
    mut('m06_noafter', 6, m06_noafter, 'a KDEAD,failed (another creature) 50 ms after the last snapshot: no snapshot after it')
    mut('m10_late', 10, m10_late, 'M: its dead line first read 2.5 s after CONT')
    mut('m10_kdead_before', 10, m10_kdead_before, 'M: its KDEAD online line has a script time before the stop (snapshots unchanged)')
    mut('m10_late24', 10, m10_late24, 'M: its dead line first read 2.4 s after the first snapshot read at or after CONT')
    mut('m10_two', 10, dup_kdead(M_ID), 'M: two KDEAD online lines')
    mut('m11_alive', 11, m11_alive, 'M: alive 1 in one snapshot 5 s after its dead line')
    mut('m12_actor', 12, m12_actor, 'actor health 0.9990 in one snapshot')
    mut('m13_back', 13, m13_back, 'r seq 5 back in one snapshot')
    mut('m13_absent', 13, m13_absent, 'r line missing in one snapshot')
    mut('m13_lag', 13, m13_lag, 'r stands still for 3 s while poses go on')
    F = copy.deepcopy(good); m14_final_nor(F); F['readme'].append('MUTANT of good: the final kept snapshot has no r line')
    W.write('m14_final_nor', F, 'mutant', 'FAIL', 1, '13,14', '', 'NONE: the final snapshot has no r line, so the bound FAILS|14.last_seq = ',
            'the final kept snapshot has no r line (check 13 for the missing line; 14.last_seq has no lower bound and FAILS)')
    # ---- a death at an unchanged total ----------------------------------------------------------------------------
    MZ = [(90.0, S_ID), (115.0, M_ID, 'zero')]
    sim('good_mdeath_sametotal', kn(kills=MZ), 'M\'s death pose (115 s) keeps its total 0.300 (dead 0 -> 1, no difference): no '
        'KAPPLY, one KDEAD online after CONT; PASS with khits one lower than good', '', 'good', 'PASS', 0,
        must='14.khits = 4, expected 4|0.300(dead)@seq')
    sim('nv6_mdeath_unlisted', kn(kills=MZ), 'as good_mdeath_sametotal, and M not listed in the snapshots read in the last 1.5 s '
        'before STOP: at its death step (same total, dead = 1) X-Ray does not list M', '', 'nv', 'NO VERDICT', 3, 'NV6',
        'M is not listed in X-Ray\'s snapshot at the time of the first pose of one of its later steps (step 0.300 dead', post=nv6_mdeath_unlisted)
    # ---- check 14: every counter one off, an unknown key, a missing key ---------------------------------------------
    for key in COUNTER_KEYS:
        for d in ((1,) if key != 'khits' else (1, -1)):
            F = copy.deepcopy(good); f = t2_edit(key, d); f(F); F['readme'].append(f'MUTANT of good: T2SUM {key} one off ({d:+d})')
            W.write(f'c14_{key}{"_minus" if d < 0 else ""}', F, 'counter', 'FAIL', 1, '14', '', f'14.{key} = ',
                    f'T2SUM {key} one off ({d:+d}; paused -> true)')
    F = copy.deepcopy(good); t2_edit('kfoo', 'add')(F); F['readme'].append('MUTANT of good: T2SUM gains kfoo=0')
    W.write('c14_unknown_kfoo', F, 'counter', 'FAIL', 1, '14', '', '14.kfoo = 0, expected nothing: FAIL', 'unknown counter kfoo=0 in T2SUM')
    F = copy.deepcopy(good); t2_edit('ktabfail', 'drop')(F); F['readme'].append('MUTANT of good: T2SUM without ktabfail')
    W.write('c14_missing_ktabfail', F, 'counter', 'FAIL', 1, '14', '', '14.ktabfail absent from the last T2SUM: FAIL', 'ktabfail removed from T2SUM')
    # ---- NO VERDICT -------------------------------------------------------------------------------------------------
    sim('nv1_ids0', kn(hits=[], kills=[], world=[]), 'the rig hits nobody: no k lines', '*', 'nv', 'NO VERDICT', 3, 'NV1')
    sim('nv1_ids1', kn(hits=[h for h in GOOD['hits'] if h[1] == S_ID], kills=[(90.0, S_ID)]), 'only S gets k lines', '*', 'nv', 'NO VERDICT', 3, 'NV1')
    sim('nv1_ids3', kn(hits=GOOD['hits'] + [(40.0, '23175', 200)]), 'a third creature gets a k line', '*', 'nv', 'NO VERDICT', 3, 'NV1')
    sim('nv1_twostalkers', kn(kind={M_ID: 's'}), 'the second target is a stalker too', '*', 'nv', 'NO VERDICT', 3, 'NV1')
    F = copy.deepcopy(good); nv1_kindunknown(F); F['readme'].append('MUTANT of good: M never listed in an o line')
    W.write('nv1_kindunknown', F, 'nv', 'NO VERDICT', 3, '*', 'NV1', '', 'M never listed in a snapshot o line (kind unknown)')
    sim('nv2_hurt_before_A', kn(world=GOOD['world'] + [(20.0, M_ID, 0.05)]), 'M hurt 0.05 by X-Ray\'s world at 20 s, before step A',
        '*', 'nv', 'NO VERDICT', 3, 'NV2,NV3')
    sim('nv2_killed_before_step', kn(world=GOOD['world'] + [(100.0, M_ID, 'kill')]), 'M killed by X-Ray\'s world at 100 s, before its step',
        '*', 'nv', 'NO VERDICT', 3, 'NV2,NV3,NV4')
    sim('nv3_fight', kn(world=GOOD['world'] + [(50.0, S_ID, 0.1)]), 'S loses 0.1 at 50 s with no K line (X-Ray\'s own fight)',
        '*', 'nv', 'NO VERDICT', 3, 'NV3')
    sim('nv4_early', kn(kills=[(90.0, S_ID), (106.6, M_ID)]), 'M killed 1.0 s after STOP', '*', 'nv', 'NO VERDICT', 3, 'NV4')
    sim('nv4_late', kn(kills=[(90.0, S_ID), (124.0, M_ID)]), 'M killed 1.6 s before CONT', '*', 'nv', 'NO VERDICT', 3, 'NV4')
    sim('nv6_unlisted', kn(), 'S not listed in X-Ray\'s snapshots read 69.5-70.5 s (its 1.100 pose at 70 s); check 5 then cannot see a total above 1.000 while S is listed alive (5 is in NV6\'s set)', '5', 'nv',
        'NO VERDICT', 3, 'NV6', 'S is not listed in X-Ray\'s snapshot at the time of the first pose of one of its later steps', post=nv6_unlisted)
    sim('nv6_with7', kn(), 'as nv6_unlisted, and S\'s dead line read 2.5 s late (checks 5 and 7 fail, both inside NV6\'s untested set)', '5,7',
        'nv', 'NO VERDICT', 3, 'NV6', '', post=chain(nv6_unlisted, m07_late))
    # ---- E: a NO VERDICT must not hide a failure it does not explain ---------------------------------------------------
    sim('nvf_nv4_12', kn(kills=[(90.0, S_ID), (106.6, M_ID)]), 'NV4 fires (M killed 1.0 s after STOP) and the actor health '
        'differs in one snapshot (check 12, outside NV4\'s set)', '12', 'nvfail', 'FAIL', 1, 'NV4', 'are outside what they leave untested', post=m12_actor)
    sim('nvf_nv3_13', kn(world=GOOD['world'] + [(50.0, S_ID, 0.1)]), 'NV3 fires (S loses 0.1 with no K line) and the r line goes '
        'back once (check 13, outside NV3\'s set)', '13', 'nvfail', 'FAIL', 1, 'NV3', 'are outside what they leave untested', post=m13_back)
    # ---- missing / empty / unparsable input -------------------------------------------------------------------------
    for rel in REQ:
        tag = rel.replace('/', '_')
        F = copy.deepcopy(good); F['drop'] = [rel]; F['readme'].append(f'good without {rel}')
        W.write(f'miss_{tag}', F, 'missing', 'FAIL', 1, '*', '', f'MISSING {rel} (|missing, empty or unreadable: {rel})', f'{rel} removed')
        F = copy.deepcopy(good); F['empty'] = [rel]; F['readme'].append(f'good with {rel} empty (0 bytes)')
        W.write(f'empty_{tag}', F, 'empty', 'FAIL', 1, '*', '', f'EMPTY {rel} (|missing, empty or unreadable: {rel})', f'{rel} empty')
        F = copy.deepcopy(good); F['unreadable'] = [rel]; F['readme'].append(f'good with {rel} unreadable (mode 000)')
        W.write(f'unread_{tag}', F, 'unreadable', 'FAIL', 1, '*', '', f'UNREADABLE {rel} (|missing, empty or unreadable: {rel})',
                f'{rel} present but mode 000 (open fails with PermissionError)')
    F = copy.deepcopy(good); m_badrec(F); F['readme'].append('MUTANT of good: one snapshot record whose end line does not match')
    W.write('bad_snaprecord', F, 'unparsed', 'FAIL', 1, '*', '', 'UNPARSED boot0/watch/snaps_all.txt', 'one kept snapshot record cannot be parsed')
    for nm, f, note in (('bad_poserecord', bad_pose, 'one kept pose record whose end line does not match'),
                        ('bad_kline', bad_kline, 'S\'s first KAPPLY line cut to 7 fields'),
                        ('bad_v3pose', v3_pose, 'one kept pose with a version-3 header (7 fields)'),
                        ('bad_v5pose', v5_pose, 'one kept pose with 8 header fields and version 5')):
        F = copy.deepcopy(good); f(F); F['readme'].append(f'MUTANT of good: {note}')
        tgt = 'boot0/meta_t2.csv' if nm == 'bad_kline' else 'boot0/watch/poses_all.txt'
        W.write(nm, F, 'unparsed', 'FAIL', 1, '*', '', f'UNPARSED {tgt}', note)
    F = copy.deepcopy(good); F['patch'] = [('stall.csv', b'utc_ms,event,seq,track_s', b'utc,event,seq,track_s')]
    F['readme'].append('MUTANT of good: stall.csv header utc,event,seq,track_s (rows unchanged and valid)')
    W.write('bad_stallheader', F, 'unparsed', 'FAIL', 1, '*', '', 'UNPARSED stall.csv: header', 'stall.csv with a wrong header, rows valid')
    F = copy.deepcopy(good); nan_health(F); F['readme'].append('MUTANT of good: one snapshot o line with health nan')
    W.write('bad_nan_health', F, 'unparsed', 'FAIL', 1, '*', '', 'UNPARSED boot0/watch/snaps_all.txt', 'one snapshot o line for S with health nan')
    F = copy.deepcopy(good); o_set(mid_snap(F, 60), S_ID, 8, 'XBADX')
    F['patch'] = [('boot0/watch/snaps_all.txt', b'XBADX', b'0.5\xff1')]
    F['readme'].append('MUTANT of good: one snapshot o line health carries a byte that is not UTF-8 (0.5<ff>1)')
    W.write('bad_utf8_health', F, 'unparsed', 'FAIL', 1, '*', '', 'UNPARSED boot0/watch/snaps_all.txt',
            'one snapshot o line health with an undecodable byte (decoded with errors=replace, the number then does not parse)')
    W.close()
    print(f'mkfix: {len(W.rows)} fixtures in {dest}')


if __name__ == '__main__':
    main()
