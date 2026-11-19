#!/usr/bin/env python3
"""Classify the difference between two A-Life state dumps per checkpoint (pre-registered growth check, 2026-11-19).
usage: det_growth.py out/det_A out/det_B
Per object line (tab separated): id section name parent gvid lvid pos online key=value...
  position-derived fields : gvid, lvid, pos
  discrete fields         : everything else (section, name, parent, online flag, alive, hp, comm, rank, money, squad, sq_*, smart counts)
  offline object          : online flag is not "true" in BOTH runs
Header lines (#rel, #actor, #objects ...) other than the #checkpoint line count as discrete.
Exit 0 only if no discrete difference and no offline difference at any checkpoint.
--tier-a (ruling 2026-11-19): exit code follows the TIER (a) verdict instead: every offline object exact, relations and
actor line exact. Ignored for tier (a): online objects, objects present in one run only that are ONLINE there, and the
"#objects" count line (it follows the online prop items)."""
import sys, glob, os, re, math
def load(d):
    out = {}
    for f in glob.glob(os.path.join(d, 'alife_dump_*_*.txt')):
        k = int(re.search(r'_(\d+)\.txt$', f).group(1)); objs = {}; hdr = []; t = None
        for l in open(f, errors='replace'):
            l = l.rstrip('\r\n')
            if l.startswith('#checkpoint'): t = l; continue
            if l.startswith('#'): hdr.append(l); continue
            p = l.split('\t')
            if p and p[0].isdigit(): objs[p[0]] = p
        out[k] = (t, hdr, objs)
    return out
POS = (4, 5, 6)
def xyz(s):
    try: return [float(v) for v in s.split(',')]
    except Exception: return None
TIER_A = '--tier-a' in sys.argv; args = [a for a in sys.argv[1:] if not a.startswith('--')]
A, B = load(args[0]), load(args[1]); bad = False; bad_a = False
for k in sorted(set(A) | set(B)):
    if k not in A or k not in B: print(f"checkpoint {k}: missing in one run"); bad = True; continue
    (ta, ha, oa), (tb, hb, ob) = A[k], B[k]
    only = sorted(set(oa) ^ set(ob), key=int)
    hdr_diff = [(x, y) for x, y in zip(ha, hb) if x != y] + ([("len", "len")] if len(ha) != len(hb) else [])
    disc_off, disc_on, pos_off, pos_on = [], [], [], []
    for i in oa:
        if i not in ob or oa[i] == ob[i]: continue
        a, b = oa[i], ob[i]
        online = (len(a) > 7 and a[7] == 'true') or (len(b) > 7 and b[7] == 'true')
        fields = [j for j in range(max(len(a), len(b))) if (a[j] if j < len(a) else None) != (b[j] if j < len(b) else None)]
        disc = [j for j in fields if j not in POS]
        pa, pb = xyz(a[6]) if len(a) > 6 else None, xyz(b[6]) if len(b) > 6 else None
        dist = math.dist(pa, pb) if pa and pb and len(pa) == 3 and len(pb) == 3 else float('nan')
        rec = (i, a[1], dist, [(a[j] if j < len(a) else '-') + ' / ' + (b[j] if j < len(b) else '-') for j in disc])
        (disc_on if online else disc_off).append(rec) if disc else (pos_on if online else pos_off).append(rec)
    n_on = sum(1 for p in oa.values() if len(p) > 7 and p[7] == 'true')
    print(f"checkpoint {k} [{(ta or '').strip('#')}]: objects {len(oa)}/{len(ob)}, online in A {n_on}")
    print(f"   only in one run: {len(only)} {only[:8]}")
    print(f"   header (relations/actor/count) lines differing: {len(hdr_diff)}")
    print(f"   OFFLINE objects differing: discrete {len(disc_off)}, position-only {len(pos_off)}")
    print(f"   ONLINE  objects differing: discrete {len(disc_on)}, position-only {len(pos_on)}")
    d = sorted(r[2] for r in pos_on + disc_on if r[2] == r[2])
    if d: print(f"   online position spread: n={len(d)} median {d[len(d)//2]:.2f} m, max {d[-1]:.2f} m")
    for name, lst in (("offline discrete", disc_off), ("offline position", pos_off), ("online discrete", disc_on)):
        for r in lst[:6]: print(f"     {name}: id {r[0]} {r[1]} dist {r[2]:.2f} m  {'; '.join(r[3][:3])[:150]}")
    for x, y in hdr_diff[:3]: print(f"     header: {x[:90]} | {y[:90]}")
    verdict = "EXACT on discrete fields and offline objects" if not (only or hdr_diff or disc_off or pos_off or disc_on) else "DISCRETE/OFFLINE DIFFERENCE"
    print(f"   -> {verdict}")
    bad |= verdict != "EXACT on discrete fields and offline objects"
    isoff = lambda p: not (len(p) > 7 and p[7] == 'true')
    only_off = [i for i in only if isoff(oa.get(i) or ob.get(i))]
    hdr_a = [(x, y) for x, y in hdr_diff if not (x.startswith('#objects') or x == 'len')] + ([("len", "len")] if len(ha) != len(hb) else [])
    fail_a = bool(only_off or hdr_a or disc_off or pos_off)
    print(f"   -> TIER (a): {'FAIL' if fail_a else 'PASS'} (offline differing {len(disc_off) + len(pos_off)}, offline only-in-one {len(only_off)} {only_off[:6]}, relation/actor lines {len(hdr_a)})")
    bad_a |= fail_a
sys.exit(1 if (bad_a if TIER_A else bad) else 0)
