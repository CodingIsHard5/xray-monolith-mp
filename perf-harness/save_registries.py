#!/usr/bin/env python3
"""Parse the registries chunk (chunk 9) of an X-Ray .scop save and compare two saves per sub-registry.
usage: save_registries.py A.scop B.scop [--show N]      (compare)
       save_registries.py A.scop --dump                  (print parsed summary)

Layout (CALifeRegistryContainer::save, alife_registry_container.cpp; every sub-registry is an
xr_map<key, value> written by save_data(): u32 count, then key,value pairs in ascending key order):
 1 info portions      map<u16 id, vector<shared_str>>         : u16 id, u32 n, n * stringZ
 2 char. relations    map<u16 id, RELATION_DATA>              : u16 id, map<u16,int> personal, map<int,int> communities
 3 game news          map<u16 id, vector<GAME_NEWS_DATA>>     : u16 id, u32 n, n*(u32 type, sZ caption, sZ text, u64 time, sZ texture)
 4 specific chars     map<shared_str, int>                    : sZ name, s32
 5 map locations      map<u16 id, Locations> (custom save)    : u16 id, u32 n_serializable, n*(u16 obj, sZ spot, u8 0, sZ hint, u32 flags, sZ task)
 6 game tasks         map<u16 id, vector<SGameTaskKey>>       : u16 id, u32 n, n*(sZ id, u32 state, u32 type, 4*u64 times,
                                                                sZ title, sZ descr, 4*vector<sZ> script helper, sZ icon, sZ hint, sZ loc, u16 obj, u32 prio)
                                                                then sZ g_active_task_id
 7 actor statistics   map<u16 id, vector<SStatSectionData>>   : u16 id, u32 n, n*(u32 m, m*(sZ key, s32, s32, sZ str), sZ key)
"""
import sys, os, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import save_diff as sd

NAMES = ['info_portions', 'character_relations', 'game_news', 'specific_characters', 'map_locations', 'game_tasks', 'actor_statistics']

class R:
    def __init__(s, b): s.b = b; s.o = 0
    def u8(s): v = s.b[s.o]; s.o += 1; return v
    def u16(s): v, = struct.unpack_from('<H', s.b, s.o); s.o += 2; return v
    def u32(s): v, = struct.unpack_from('<I', s.b, s.o); s.o += 4; return v
    def s32(s): v, = struct.unpack_from('<i', s.b, s.o); s.o += 4; return v
    def u64(s): v, = struct.unpack_from('<Q', s.b, s.o); s.o += 8; return v
    def sz(s): e = s.b.index(b'\0', s.o); v = s.b[s.o:e].decode('latin1'); s.o = e + 1; return v
    def vec(s, f): return [f() for _ in range(s.u32())]

def parse(b):
    r = R(b); out = collections.OrderedDict(); spans = {}
    def reg(name, key, val, extra=None):
        st = r.o; n = r.u32(); m = []
        for _ in range(n): k = key(); m.append((k, val()))
        if extra: m.append(('<extra>', extra()))
        out[name] = m; spans[name] = (st, r.o)
    reg('info_portions', r.u16, lambda: r.vec(r.sz))
    reg('character_relations', r.u16, lambda: (tuple((r.u16(), r.s32()) for _ in range(r.u32())),
                                                tuple((r.s32(), r.s32()) for _ in range(r.u32()))))
    reg('game_news', r.u16, lambda: r.vec(lambda: (r.u32(), r.sz(), r.sz(), r.u64(), r.sz())))
    reg('specific_characters', r.sz, r.s32)
    reg('map_locations', r.u16, lambda: r.vec(lambda: (r.u16(), r.sz(), r.u8(), r.sz(), r.u32(), r.sz())))
    reg('game_tasks', r.u16, lambda: r.vec(lambda: (r.sz(), r.u32(), r.u32(), r.u64(), r.u64(), r.u64(), r.u64(), r.sz(), r.sz(),
                                                     tuple(tuple(r.vec(r.sz)) for _ in range(4)), r.sz(), r.sz(), r.sz(), r.u16(), r.u32())),
        extra=r.sz)
    reg('actor_statistics', r.u16, lambda: r.vec(lambda: (tuple(r.vec(lambda: (r.sz(), r.s32(), r.s32(), r.sz()))), r.sz())))
    assert r.o == len(b), f'registries chunk not fully parsed ({r.o} of {len(b)})'
    return out, spans

def load(path): return parse(sd.chunks(sd.raw(path))[9])

def flat(name, m):
    """List of hashable leaf entries for one sub-registry: (key, element) for vector-valued maps, (key, value) otherwise."""
    res = []
    for k, v in m:
        if isinstance(v, list): res += [(k, x if not isinstance(x, list) else tuple(x)) for x in v]
        else: res.append((k, v))
    return res

def fmt(e, w=150):
    s = repr(e); return s if len(s) <= w else s[:w] + '...'

def compare(A, B, sa, sb, show=10):
    verdicts = {}
    for name in NAMES:
        ba = sa[name]; bb = sb[name]
        ma, mb = A[name], B[name]
        if ma == mb:
            verdicts[name] = 'equal'; print(f'{name:20s}: EQUAL ({len(ma)} keys, {len(flat(name, ma))} entries)'); continue
        fa, fb = flat(name, ma), flat(name, mb)
        ca, cb = collections.Counter(fa), collections.Counter(fb)
        if ca == cb:
            # same multiset; locate which keys have reordered vectors
            da, db = dict(ma), dict(mb)
            keys = [k for k in da if da[k] != db.get(k)]
            verdicts[name] = 'same set, different order'
            print(f'{name:20s}: SAME SET, DIFFERENT ORDER ({len(fa)} entries; reordered inside keys {keys[:10]})')
            for k in keys[:3]:
                va, vb = da[k], db[k]; i = next(i for i in range(len(va)) if va[i] != vb[i])
                print(f'     key {k}: first order difference at position {i}/{len(va)}: A={fmt(va[i], 80)} B={fmt(vb[i], 80)}')
            continue
        verdicts[name] = 'different content'
        oa, ob = list((ca - cb).elements()), list((cb - ca).elements())
        print(f'{name:20s}: DIFFERENT CONTENT ({len(fa)} / {len(fb)} entries; {len(oa)} only in A, {len(ob)} only in B)')
        for e in oa[:show]: print(f'     only A: {fmt(e)}')
        for e in ob[:show]: print(f'     only B: {fmt(e)}')
        # also report if, apart from the differing entries, the common part is reordered
        common = ca & cb
        def strip(f, c):
            c = collections.Counter(c); res = []
            for e in f:
                if c[e] > 0: c[e] -= 1; res.append(e)
            return res
        if strip(fa, common) != strip(fb, common): print('     (the common entries are also in a different order)')
    return verdicts

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]; show = 10
    if '--show' in sys.argv: show = int(sys.argv[sys.argv.index('--show') + 1]); args.remove(str(show))
    A, sa = load(args[0])
    if '--dump' in sys.argv or len(args) == 1:
        for n in NAMES: print(f'{n:20s}: bytes {sa[n][0]}..{sa[n][1]}, {len(A[n])} keys, {len(flat(n, A[n]))} entries')
        sys.exit(0)
    B, sb = load(args[1])
    compare(A, B, sa, sb, show)
