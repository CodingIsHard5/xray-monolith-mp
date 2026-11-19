#!/usr/bin/env python3
"""Tier-A difference count between two determinism run dirs (pre-registered rules, no extra masks).
usage: tier_a.py <dirA> <dirB> [--k 1,2,3] [--show N] [--json]

A run dir out/det_<tag> holds det_<tag>_<k>.scop, det_<tag>_<k>.scoc, alife_dump_<tag>_<k>.txt.
Per k, an object id is ONLINE if online (dump column 8 == 'true') in either run's dump, OFFLINE if it is
in at least one dump and online in neither, UNKNOWN if in neither dump.
Item sources:
 1 .scop object records (state + update packet bytes): one item per OFFLINE/UNKNOWN id that differs or is in
   one save only (scop-offline / scop-unknown). ONLINE ids: informational.
 2 .scop chunk 5 (game time): one item if not byte-equal.
 3 registries (chunk 9): entries are the flattened (key, element) tuples of save_registries.flat().
   info_portions, character_relations, game_tasks, actor_statistics: one item per entry present in only one
   side (multiset), plus one item per key whose common entries are in a different order.
   specific_characters, map_locations: as sets, one item per entry present in only one side.
   game_news: informational only.
 4 .scoc decoded as maps: every differing leaf path is a candidate. Dropped (informational): top-level key in
   game_object / xr_sound / disguise_npcs_memory, or second path element an integer-valued id that is ONLINE.
   Paths that reach the same leaf (shared tables, e.g. pawdata.pins == pawsys.pins) are one candidate; it is
   dropped only if ALL its access paths are dropped, else it is one item, attributed to the top-level key of
   its first non-dropped path.
Exit 0 always, unless a file is missing or unparsable (exit 2)."""
import sys, os, re, json, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

def die(msg):
    print(f'tier_a: ERROR: {msg}', file=sys.stderr); sys.exit(2)

try:
    import save_diff as sd, save_registries as sr, scoc_diff as sc
except Exception as e:  # e.g. liblzo2 missing
    die(f'cannot import helper tools: {e}')

DROP_KEYS = ('game_object', 'xr_sound', 'disguise_npcs_memory')
ORDERED_REGS = ('info_portions', 'character_relations', 'game_tasks', 'actor_statistics')
SET_REGS = ('specific_characters', 'map_locations')
NUMSTR = re.compile(r'^\s*-?\d+(\.0*)?\s*$')

def files(d, k):
    tag = os.path.basename(os.path.normpath(d))
    if tag.startswith('det_'): tag = tag[4:]
    return (os.path.join(d, f'det_{tag}_{k}.scop'), os.path.join(d, f'det_{tag}_{k}.scoc'),
            os.path.join(d, f'alife_dump_{tag}_{k}.txt'))

def load(path, fn, what):
    if not os.path.isfile(path): die(f'missing {what}: {path}')
    try: return fn(path)
    except SystemExit: raise
    except Exception as e: die(f'cannot parse {what} {path}: {type(e).__name__}: {e}')

def load_scop(path):
    c = sd.chunks(sd.raw(path))
    for i in (2, 5, 9):
        if i not in c: raise ValueError(f'chunk {i} missing')
    regs, _ = sr.parse(c[9])
    return c, sd.objects(c[2]), regs

def load_dump(path):
    s = sd.online(path)
    if not s: raise ValueError('no object rows')
    return s

def scoc_leaves(path):
    """Same traversal and values as scoc_diff.leaves(), plus a leaf identity per path so that aliases
    (one table reachable by several paths) can be merged: identity = (id(owning table), key)."""
    v, dc = sc.decode(path)
    out = {}; ident = {}
    def walk(v, path, stack):
        if isinstance(v, sc.Ref):
            t = dc.seen.get(v.n)
            if t is None: out[path] = f'<unresolved ref {v.n}>'; ident[path] = ('path', path); return
            v = t
        if isinstance(v, dict):
            if id(v) in stack: out[path] = '<cycle>'; ident[path] = ('path', path); return
            keys = [k for k in v if k != '\0order']
            if not keys: out[path] = '{}'; ident[path] = ('empty', id(v))
            for k in keys:
                x = v[k]
                if not isinstance(x, (dict, sc.Ref)): out[path + (k,)] = x; ident[path + (k,)] = ('leaf', id(v), k)
                else: walk(x, path + (k,), stack + (id(v),))
        else: out[path] = v; ident[path] = ('path', path)
    walk(v, (), ())
    return out, ident, (v, dc)   # keep decoded objects alive so id()s stay unique

def as_id(x):
    if isinstance(x, bool): return None
    if isinstance(x, int): return x
    if isinstance(x, float): return int(x) if x == x and x not in (float('inf'), float('-inf')) and x.is_integer() else None
    if isinstance(x, str) and NUMSTR.match(x): return int(float(x))
    return None

def pstr(p): return sc.pstr(p)
def short(x, w=60): return sc.short(x, w)

def compare_k(dA, dB, k):
    (pa, ca_, ta), (pb, cb_, tb) = files(dA, k), files(dB, k)
    onA, onB = load(ta, load_dump, 'A-Life dump'), load(tb, load_dump, 'A-Life dump')
    CA, OA, RA = load(pa, load_scop, '.scop save')
    CB, OB, RB = load(pb, load_scop, '.scop save')
    LA = load(ca_, scoc_leaves, '.scoc script save')
    LB = load(cb_, scoc_leaves, '.scoc script save')

    def cls(i):
        if i not in onA and i not in onB: return 'unknown'
        return 'online' if (onA.get(i) or onB.get(i)) else 'offline'

    items = collections.OrderedDict()   # source -> list of example strings (all items listed)
    info = collections.OrderedDict()
    def add(src, ex): items.setdefault(src, []).append(ex)

    # 1 object records
    online_n = 0
    for i in sorted(set(OA) | set(OB)):
        x, y = OA.get(i), OB.get(i)
        if x is not None and y is not None and x[4] == y[4] and x[5] == y[5]: continue
        c = cls(i)
        if c == 'online': online_n += 1; continue
        o = x or y
        if x is None or y is None: ex = f"id {i} {o[0]} '{o[1]}': only in {'A' if y is None else 'B'}"
        else:
            s1, n1 = sd.firstdiff(x[4], y[4]); s2, n2 = sd.firstdiff(x[5], y[5])
            ex = f"id {i} {o[0]} '{o[1]}': state {n1} bytes differ (first at {s1}), update {n2} differ"
        add('scop-' + c, ex)
    info['scop-online differing/one-run objects'] = online_n

    # 2 game time
    if CA[5] != CB[5]: add('game-time', f'chunk 5 differs ({CA[5].hex()} / {CB[5].hex()})')
    other = [i for i in sorted(set(CA) | set(CB)) if i not in (2, 5, 9) and CA.get(i) != CB.get(i)]
    if other: info['other .scop chunks differing (not items)'] = other

    # 3 registries
    for name in sr.NAMES:
        ma, mb = RA[name], RB[name]
        if name == 'game_news':
            fa, fb = collections.Counter(sr.flat(name, ma)), collections.Counter(sr.flat(name, mb))
            info['game_news differing entries'] = sum(((fa - fb) + (fb - fa)).values()); continue
        src = 'reg:' + name
        if name in SET_REGS:
            fa, fb = collections.Counter(sr.flat(name, ma)), collections.Counter(sr.flat(name, mb))
            for e in (fa - fb).elements(): add(src, f'only A: {short(e, 120)}')
            for e in (fb - fa).elements(): add(src, f'only B: {short(e, 120)}')
            continue
        # ordered registries: per key, entry-level multiset difference + order difference of the common entries
        ga, gb = collections.OrderedDict(), collections.OrderedDict()
        for kk, v in ma: ga.setdefault(kk, []).append(v)
        for kk, v in mb: gb.setdefault(kk, []).append(v)
        for kk in list(ga) + [x for x in gb if x not in ga]:
            ea = sr.flat(name, [(kk, v) for v in ga.get(kk, [])]); eb = sr.flat(name, [(kk, v) for v in gb.get(kk, [])])
            if ea == eb: continue
            xa, xb = collections.Counter(ea), collections.Counter(eb)
            for e in (xa - xb).elements(): add(src, f'only A: {short(e, 120)}')
            for e in (xb - xa).elements(): add(src, f'only B: {short(e, 120)}')
            com = xa & xb
            def strip(f, c):
                c = collections.Counter(c); r = []
                for e in f:
                    if c[e] > 0: c[e] -= 1; r.append(e)
                return r
            if strip(ea, com) != strip(eb, com): add(src, f'key {kk!r}: common entries in a different order')

    # 4 .scoc
    (la, ia, _ka), (lb, ib, _kb) = LA, LB
    diffp = []
    for p in set(la) | set(lb):
        x = la.get(p, '<absent>'); y = lb.get(p, '<absent>')
        if x == y or (isinstance(x, float) and isinstance(y, float) and x != x and y != y): continue   # as scoc_diff
        diffp.append(p)
    # union-find over differing paths that share a leaf identity on either side
    par = {p: p for p in diffp}
    def find(p):
        while par[p] != p: par[p] = par[par[p]]; p = par[p]
        return p
    for ident in (ia, ib):
        first = {}
        for p in diffp:
            if p in ident:
                key = ident[p]
                if key in first: par[find(p)] = find(first[key])
                else: first[key] = p
    groups = collections.defaultdict(list)
    for p in diffp: groups[find(p)].append(p)
    # also pull in non-differing aliases? A shared leaf has the same value under all its paths on one side, so if
    # one alias differs, all do (on the side(s) where they are aliases); the group already holds every access path.
    def drop_reason(p):
        if not p: return None
        if p[0] in DROP_KEYS: return p[0]
        if len(p) > 1:
            i = as_id(p[1])
            if i is not None and cls(i) == 'online': return 'online-id'
        return None
    dropped = collections.Counter(); shared_items = 0
    for g in sorted(groups.values(), key=lambda g: tuple(map(str, min(g, key=lambda p: tuple(map(str, p)))))):
        g.sort(key=lambda p: tuple(map(str, p)))
        reasons = [drop_reason(p) for p in g]
        keep = [p for p, r in zip(g, reasons) if r is None]
        if not keep: dropped[reasons[0]] += 1; continue
        if len(g) > 1: shared_items += 1
        p = keep[0]
        alias = f' (aliases: {", ".join(pstr(q) for q in g if q != p)})' if len(g) > 1 else ''
        add('scoc:' + str(p[0] if p else '<root>'),
            f'{pstr(p)}: A={short(la.get(p, "<absent>"))} B={short(lb.get(p, "<absent>"))}{alias}')
    for r in DROP_KEYS + ('online-id',): info[f'scoc dropped [{r}]'] = dropped.get(r, 0)
    info['scoc items reachable via several paths (counted once)'] = shared_items
    return items, info

def main():
    argv = sys.argv[1:]; ks = [1, 2, 3]; show = 5; as_json = False; pos = []
    i = 0
    try:
        while i < len(argv):
            a = argv[i]
            if a == '--k': ks = [int(x) for x in argv[i + 1].split(',') if x]; i += 2
            elif a == '--show': show = int(argv[i + 1]); i += 2
            elif a == '--json': as_json = True; i += 1
            elif a.startswith('--'): raise ValueError(f'unknown option {a}')
            else: pos.append(a); i += 1
        if len(pos) != 2: raise ValueError('need exactly two run dirs')
    except (ValueError, IndexError) as e:
        print(f'tier_a: {e}\nusage: tier_a.py <dirA> <dirB> [--k 1,2,3] [--show N] [--json]', file=sys.stderr); sys.exit(2)
    dA, dB = pos
    for d in pos:
        if not os.path.isdir(d): die(f'missing run dir: {d}')
    res = {}
    for k in ks:
        items, info = compare_k(dA, dB, k)
        D = sum(len(v) for v in items.values())
        res[k] = {'D': D, 'by_source': {s: len(v) for s, v in items.items()}}
        if as_json: continue
        print(f'=== {dA} vs {dB}, k={k}')
        for s, v in items.items():
            print(f'  {s:32s} {len(v)}')
            for e in v[:show]: print(f'      {e}')
        print('  informational:')
        for n, v in info.items(): print(f'    {n}: {v}')
        print(f'TIER_A k={k} D={D}')
    if as_json: print(json.dumps({str(k): v for k, v in res.items()}))
    sys.exit(0)

if __name__ == '__main__':
    main()
