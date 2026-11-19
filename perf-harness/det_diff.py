#!/usr/bin/env python3
"""Compare two equivalence-harness runs checkpoint by checkpoint.  usage: det_diff.py out/det_A out/det_B
Exit 0 only if every checkpoint is identical. Prints, per checkpoint, how many objects differ and in which
fields, and the first few differing lines."""
import sys, glob, os, re
def load(d):
    r = {}
    for f in glob.glob(os.path.join(d, 'alife_dump_*_*.txt')):
        k = int(re.search(r'_(\d+)\.txt$', f).group(1)); objs = {}; meta = []
        for line in open(f, errors='replace'):
            line = line.rstrip('\n')
            if line.startswith('#'): meta.append(line)
            else: p = line.split('\t'); objs[p[0]] = p
        r[k] = (objs, meta)
    return r
A, B = load(sys.argv[1]), load(sys.argv[2]); bad = 0
if not A or sorted(A) != sorted(B): print('checkpoint sets differ or empty:', sorted(A), sorted(B)); sys.exit(2)
names = ['id', 'section', 'name', 'parent', 'gvid', 'lvid', 'pos', 'online']
for k in sorted(A):
    oa, ma = A[k]; ob, mb = B[k]; only_a = set(oa) - set(ob); only_b = set(ob) - set(oa); fields = {}; ex = []
    for i in set(oa) & set(ob):
        if oa[i] != ob[i]:
            for j, (x, y) in enumerate(zip(oa[i], ob[i])):
                if x != y:
                    n = names[j] if j < len(names) else x.split('=')[0]; fields[n] = fields.get(n, 0) + 1
            if len(oa[i]) != len(ob[i]): fields['<field count>'] = fields.get('<field count>', 0) + 1
            if len(ex) < 3: ex.append((oa[i], ob[i]))
    nd = sum(1 for i in set(oa) & set(ob) if oa[i] != ob[i]); md = [(x, y) for x, y in zip(ma, mb) if x != y]
    same = not (nd or only_a or only_b or md or len(ma) != len(mb))
    print('checkpoint %d: %d objects; %s' % (k, len(oa), 'IDENTICAL' if same else
          '%d differ, %d only in A, %d only in B, %d header lines differ' % (nd, len(only_a), len(only_b), len(md))))
    if not same:
        bad += 1; print('   fields:', dict(sorted(fields.items(), key=lambda kv: -kv[1])))
        for a, b in ex: print('   A:', ' '.join(a)[:200]); print('   B:', ' '.join(b)[:200])
        for a, b in md[:3]: print('   A:', a[:200]); print('   B:', b[:200])
# generator states logged by the engine (det_rng_state) and the Lua probe: must match line for line
def rng(d):
    f = os.path.join(d, 'xray.log')
    return [l.strip() for l in open(f, errors='replace') if 'det_rng_state' in l or 'det_lua_state' in l] if os.path.exists(f) else []
ra, rb = rng(sys.argv[1]), rng(sys.argv[2])
if ra or rb:
    same = ra == rb
    print('generator states: %d lines, %s' % (len(ra), 'IDENTICAL' if same else 'DIFFER'))
    if not same:
        bad += 1
        for x, y in zip(ra, rb):
            if x != y: print('   A:', x[:170]); print('   B:', y[:170])
print('RESULT:', 'EMPTY DIFF' if not bad else '%d of %d checkpoints differ' % (bad, len(A)))
sys.exit(1 if bad else 0)
