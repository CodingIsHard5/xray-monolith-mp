#!/usr/bin/env python3
"""Kill channel witness kw1 (PREREG-killchannel-xwitness-2026-11-26.md, section 2): a stand-in for ITR2's pose writer,
pose VERSION 4, X-Ray only. NOT ITR2's code. Made from t2_lost_pose2.py (75f0941b).
  t2_kill_pose.py <shm_dir> <out_csv> <track csv> [key=value ...]
Writes pose v4 at 20 Hz:  seq,<n>,<utc_ms>,play,<zone>,<keep|freeze>,<session>,4  /  p,x,yfloor,z,yaw,hp,stance  /
k,<id>,<section>,<total>,<dead> (0..3 lines)  /  end,<n>.   seq starts at 1.
Every cycle it also reads <shm_dir>/snap.txt (absent, torn or CRLF tolerated; a bad read is counted, never fatal) and
logs ONE ROW PER NEW SNAPSHOT to <snaplog> (CSV with a header row).
Steps on the writer's own clock w (seconds since its first pose; defaults are the design's):
  w < pick                 no k line (control)
  at pick                  S = the nearest alive stalker, M = the nearest alive monster (`o` lines of the newest snapshot
                           read, alive 1, health >= minh), each picked on its own; a pick with no candidate logs a NOPICK
                           row and that creature gets no k line, ever
  pick .. pick+step        k,S,<sec>,0.250,0  k,M,<sec>,0.250,0  and  k,0,actor,0.500,0 (X-Ray must refuse it)
  pick+step .. pick+2step  totals 0.600
  pick+2step on            totals 1.000, dead 1, until a snapshot with X-Ray's `k,<id>,...,dead` for the id was read,
                           then `hold` s more, then that line is dropped for good
Keys:
  aw=keep|freeze     away policy (default keep)
  sess=<n>           session number (default: this process's start time in UTC ms); must be digits (version 4)
  lvl=<level>        level name instead of waiting for a snapshot with track time >= 0
  t0=<s>             track time at the first pose (default 0)
  pick=<s>           pick time (default 30; the registered retry uses 45)
  step=<s>           length of the 0.250 and of the 0.600 step (default 5)
  hold=<s>           how long the dead line is still sent after X-Ray's dead line was read (default 5)
  minh=<h>           least health of a pick (default 0.9)
  snaplog=<path>     per-snapshot log (default: writer_snaps.csv beside <out_csv>)
<out_csv> rows: utc_ms,seq,phase,session,track_s,w_s,k   (k = the k lines of that pose, '|'-joined, commas as ':').
Snapshot log columns: utc_ms,w_s,snap_seq,r_sess,r_seq,actor_health,s_id,s_health,s_alive,s_k,m_id,m_health,m_alive,
m_k,pose_seq,k_sent,event. *_health / *_alive empty = the id is not listed; *_k = X-Ray's k line for the id without
`k,<id>,` (e.g. 0.250 or 0.250;dead), empty = none; k_sent = the k lines of the newest pose, written as in <out_csv>; event: PICK /
NOPICK row written at the pick itself (PICK_S=id:section:health / NOPICK_S, same for M; its snapshot fields are the
newest snapshot's, so its snap_seq repeats one), DEADREAD_S / DEADREAD_M, DROP_S / DROP_M on the row where they happened.
The driver kills this process; it never stops by itself.
"""
import sys, time, os

def kcol(lines):
    """k lines as ONE csv field: lines joined by '|', their commas written as ':' (k:201:stalker_bandit:0.250:0)."""
    return '|'.join(l.replace(',', ':') for l in lines)


SNAP_COLS = ('utc_ms,w_s,snap_seq,r_sess,r_seq,actor_health,s_id,s_health,s_alive,s_k,m_id,m_health,m_alive,m_k,'
             'pose_seq,k_sent,event')
POSE_COLS = 'utc_ms,seq,phase,session,track_s,w_s,k'


def parse_snapshot(raw):
    """A whole snapshot or None. Never raises. Returns dict: seq, t_track, level, r (sess, seq) or None,
    actor (health str), o {id: (section, kind, health float, alive int, dist float, health str)}, k {id: str}."""
    try:
        txt = raw.decode('utf-8', 'replace') if isinstance(raw, bytes) else str(raw)
        L = [l for l in txt.replace('\r', '').split('\n') if l]
        if len(L) < 3 or not L[0].startswith('seq,'):
            return None
        h = L[0].split(',')
        if len(h) < 4 or L[-1] != 'end,' + h[1]:
            return None
        s = {'seq': int(h[1]), 't_track': float(h[3]), 'level': h[4] if len(h) > 4 else '', 'r': None,
             'actor': '', 'o': {}, 'k': {}}
        for l in L[1:-1]:
            f = l.split(',')
            try:
                if f[0] == 'a' and len(f) >= 6:
                    s['actor'] = f[5]
                elif f[0] == 'o' and len(f) >= 12:
                    s['o'][int(f[1])] = (f[2], f[3], float(f[8]), int(f[9]), float(f[11]), f[8])
                elif f[0] == 'r' and len(f) == 3:
                    s['r'] = (f[1], int(f[2]))
                elif f[0] == 'k' and len(f) >= 3:
                    s['k'][int(f[1])] = ';'.join(f[2:])
            except (ValueError, IndexError):
                continue   # one bad line does not spoil the snapshot
        return s
    except Exception:
        return None


class Writer:
    """The writer's logic without clocks or files (the driver below and the checker's selftest give it the time)."""

    def __init__(self, K, sess, lvl, track):
        self.aw = K.get('aw', 'keep'); self.sess = sess; self.lvl = lvl; self.trk = track
        self.t0 = float(K.get('t0', 0)); self.pick_t = float(K.get('pick', 30)); self.step = float(K.get('step', 5))
        self.hold = float(K.get('hold', 5)); self.minh = float(K.get('minh', 0.9))
        self.n = 0; self.newest = None; self.last_seq = None; self.picked = False
        self.tgt = {}            # 'S' / 'M' -> {'id', 'sec', 'h'}
        self.dead_read = {}      # role -> writer time the dead line was first read
        self.dropped = set(); self.events = []; self.klines = []; self.bad_reads = 0; self.pick_row = None

    def at(self, t):
        T = self.trk
        i = max(j for j in range(len(T)) if T[j][0] <= t) if t >= T[0][0] else 0
        a = T[i]; b = T[min(i + 1, len(T) - 1)]; f = 0 if b[0] == a[0] else min(1.0, (t - a[0]) / (b[0] - a[0]))
        return [a[k] + f * (b[k] - a[k]) for k in (1, 2, 3)]

    def do_pick(self):
        s = self.newest; ev = []
        for role, kind in (('S', 's'), ('M', 'm')):
            best = None
            if s:
                for i, (sec, kd, h, al, d, hs) in s['o'].items():
                    if kd == kind and al == 1 and h >= self.minh and (best is None or (d, i) < (best[0], best[1])):
                        best = (d, i, sec, hs)
            if best:
                self.tgt[role] = {'id': best[1], 'sec': best[2], 'h': best[3]}
                ev.append(f'PICK_{role}={best[1]}:{best[2]}:{best[3]}')
            else:
                ev.append(f'NOPICK_{role}')
        self.picked = True; self.events += ev
        # the PICK / NOPICK row is written at once, with the newest snapshot's fields (snap_seq repeats that snapshot's)
        self.pick_row = self.row(s, self.pick_w, self.pick_u) if s else ','.join(
            [str(self.pick_u), f'{self.pick_w:.3f}'] + [''] * 14 + [' '.join(self.events)])
        if not s: self.events = []

    def lines(self, w):
        if not self.picked:
            return []
        L = []; dt = w - self.pick_t
        for role in ('S', 'M'):
            g = self.tgt.get(role)
            if not g or role in self.dropped:
                continue
            if dt < self.step: tot, dead = 0.25, 0
            elif dt < 2 * self.step: tot, dead = 0.6, 0
            else: tot, dead = 1.0, 1
            L.append(f"k,{g['id']},{g['sec']},{tot:.3f},{dead}")
        if dt < self.step:
            L.append('k,0,actor,0.500,0')
        return L

    def pose(self, w, u):
        """The pose for writer time w (s since the first pose) at utc u (ms): (seq, body bytes, k lines)."""
        self.pick_row = None
        if not self.picked and w >= self.pick_t:
            self.pick_w, self.pick_u = w, u; self.do_pick()
        for role in ('S', 'M'):   # the drop: hold s after the dead line was read
            if role in self.dead_read and role not in self.dropped and w - self.dead_read[role] >= self.hold:
                self.dropped.add(role); self.events.append(f'DROP_{role}')
        t = self.t0 + w; x, y, z = self.at(t); self.n += 1; self.klines = self.lines(w)
        body = (f"seq,{self.n},{u},play,{self.lvl},{self.aw},{self.sess},4\n"
                f"p,{x:.3f},{y + 0.3:.3f},{z:.3f},0.0000,1.0,{int(t // 10) % 3}\n"
                + ''.join(l + '\n' for l in self.klines) + f"end,{self.n}\n")
        return self.n, body.encode(), self.klines

    def read(self, raw, w, u):
        """One snapshot read. Returns the log row (str, no newline) for a NEW snapshot, else None."""
        s = parse_snapshot(raw) if raw is not None else None
        if s is None:
            if raw is not None: self.bad_reads += 1
            return None
        if s['seq'] == self.last_seq:
            return None
        self.last_seq = s['seq']; self.newest = s
        for role in ('S', 'M'):
            g = self.tgt.get(role)
            if g and role not in self.dead_read and s['k'].get(g['id'], '').endswith(';dead'):
                self.dead_read[role] = w; self.events.append(f'DEADREAD_{role}')
        return self.row(s, w, u)

    def row(self, s, w, u):
        def part(role):
            g = self.tgt.get(role)
            if not g: return ['', '', '', '']
            o = s['o'].get(g['id'])
            return [str(g['id']), o[5] if o else '', str(o[3]) if o else '', s['k'].get(g['id'], '')]
        r = s['r'] or ('', '')
        ev = ' '.join(self.events); self.events = []
        return ','.join([str(u), f'{w:.3f}', str(s['seq']), str(r[0]), str(r[1]), s['actor']] + part('S') + part('M')
                        + [str(self.n), kcol(self.klines), ev])


def load_track(path):
    return [[float(v) for v in l.split(',')[:4]] for l in open(path, 'rb').read().decode().replace('\r', '').split('\n')[1:] if l]


def main():
    shm, out, trk = sys.argv[1:4]
    K = dict(a.split('=', 1) for a in sys.argv[4:])
    utc = lambda: time.time_ns() // 1_000_000
    SESS = K.get('sess') or str(utc())
    if not SESS.isdigit(): sys.exit('sess must be digits for pose version 4')
    snaplog = K.get('snaplog') or os.path.join(os.path.dirname(os.path.abspath(out)), 'writer_snaps.csv')
    sp = os.path.join(shm, 'snap.txt')

    def read_raw():
        try: return open(sp, 'rb').read()
        except OSError: return None
    lvl = K.get('lvl')
    while lvl is None:
        s = parse_snapshot(read_raw() or b'')
        if s and s['t_track'] >= 0 and s['level']: lvl = s['level']
        else: time.sleep(0.2)
    if 'lvl' not in K: open(out + '.lvl', 'w').write(lvl + '\n')
    wr = Writer(K, SESS, lvl, load_track(trk))
    new = not os.path.exists(out); log = open(out, 'a')
    if new: log.write(POSE_COLS + '\n')
    newl = not os.path.exists(snaplog); slog = open(snaplog, 'a')
    if newl: slog.write(SNAP_COLS + '\n')
    w0 = None
    while True:
        u = utc()
        if w0 is None: w0 = u
        w = (u - w0) / 1000
        n, body, kl = wr.pose(w, u)
        with open(os.path.join(shm, 'pose.txt'), 'wb') as f: f.write(body)
        log.write(f"{u},{n},T,{SESS},{wr.t0 + w:.2f},{w:.3f},{kcol(kl)}\n"); log.flush()
        if wr.pick_row is not None: slog.write(wr.pick_row + '\n'); slog.flush()
        try:
            row = wr.read(read_raw(), w, utc())
        except Exception as e:   # never crash on a snapshot
            row = None; wr.bad_reads += 1; print(f'snapshot read error: {e!r}', file=sys.stderr)
        if row is not None: slog.write(row + '\n'); slog.flush()
        time.sleep(0.05)


if __name__ == '__main__':
    main()
