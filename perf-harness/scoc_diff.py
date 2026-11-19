#!/usr/bin/env python3
"""Decode / diff X-Ray script save state (.scoc), written by alife_storage_manager.script via marshal.encode(m_data).
usage: scoc_diff.py A.scoc B.scoc [--show N] [--depth D]   (compare; D = how many path levels form a group, default 1)
       scoc_diff.py A.scoc --dump [--depth D]               (print key counts per group)

Format (engine/src/3rd party/lua-extensions/lmarshal.c, Richard Hundt's lua-marshal):
  u8 0x8e magic, then one value.  value = u8 lua_type, then
    1 boolean : u8
    3 number  : f64 (lua_Number)
    4 string  : u32 len, bytes
    5 table   : u8 tag; 1 TREF: i32 ref | 2 TVAL: u32 len, len bytes of (key value)* pairs | 3 TUSR: u32 len, table {1=function}
    6 function: u8 tag; 1 TREF: i32 ref | else u32 len, lua_dump bytecode, u32 len, upvalue table body
    7 userdata: u8 tag; 1 TREF: i32 ref | 3 TUSR: u32 len, table body {1=function} (__persist)
    0 nil / 8 thread: nothing
  Key/value pairs are written in lua_next() order, i.e. Lua hash-table iteration order, so tables are compared as maps.
  Ref numbers follow the encoder: tables (TVAL), functions and userdata get the next index when first seen, in
  encounter order; a ref is resolved to that object (cycles are shown as '<cycle>').
"""
import sys, struct, hashlib, collections

class Ref:
    __slots__ = ('n',)
    def __init__(s, n): s.n = n

class Dec:
    def __init__(s, b):
        assert b[0] == 0x8e, 'bad magic'
        s.b = b; s.seen = {}; s.idx = 1; s.nrefs = 0
    def value(s, o, end):
        t = s.b[o]; o += 1
        if t == 1: return bool(s.b[o]), o + 1
        if t == 3: return struct.unpack_from('<d', s.b, o)[0], o + 8
        if t == 4:
            n, = struct.unpack_from('<I', s.b, o); return s.b[o + 4:o + 4 + n].decode('latin1'), o + 4 + n
        if t in (0, 8): return None, o
        if t in (5, 6, 7):
            tag = s.b[o]; o += 1
            if tag == 1:
                r, = struct.unpack_from('<i', s.b, o); s.nrefs += 1; return Ref(r), o + 4
            n, = struct.unpack_from('<I', s.b, o); o += 4
            if t == 6:
                code = s.b[o:o + n]; o += n; my = s.idx; s.idx += 1
                fn = {'<function>': hashlib.md5(code).hexdigest()[:12]}; s.seen[my] = fn
                n2, = struct.unpack_from('<I', s.b, o); o += 4
                fn['<upvalues>'] = s.table(o, o + n2, {}); return fn, o + n2
            if t == 5 and tag == 2:
                d = {}; s.seen[s.idx] = d; s.idx += 1; s.table(o, o + n, d); return d, o + n
            if tag == 3:
                if t == 7: my = s.idx; s.idx += 1
                d = {'<persist>': s.table(o, o + n, {})}
                if t == 7: s.seen[my] = d
                return d, o + n
            raise ValueError(f'bad tag {tag} at {o}')
        raise ValueError(f'bad type {t} at {o - 1}')
    def table(s, o, end, d):
        order = []
        while o < end:
            k, o = s.value(o, end); v, o = s.value(o, end)
            if isinstance(k, dict): k = '<table-key>' + str(id(k))
            if isinstance(k, float) and k.is_integer(): k = int(k)
            if isinstance(k, Ref): k = f'<ref-key {k.n}>'
            d[k] = v; order.append(k)
        d['\0order'] = order
        return d

def decode(path):
    b = open(path, 'rb').read(); dc = Dec(b); v, o = dc.value(1, len(b))
    assert o == len(b), f'trailing data {o} of {len(b)}'
    return v, dc

def leaves(v, dc, path=(), stack=(), out=None, orders=None):
    """path -> scalar; orders: path -> key order of that table."""
    if out is None: out = {}; orders = {}
    if isinstance(v, Ref):
        t = dc.seen.get(v.n)
        if t is None: out[path] = f'<unresolved ref {v.n}>'; return out, orders
        v = t
    if isinstance(v, dict):
        if id(v) in stack: out[path] = '<cycle>'; return out, orders
        orders[path] = v.get('\0order', [])
        keys = [k for k in v if k != '\0order']
        if not keys: out[path] = '{}'
        for k in keys: leaves(v[k], dc, path + (k,), stack + (id(v),), out, orders)
    else: out[path] = v
    return out, orders

def pstr(p): return '.'.join(str(x) for x in p) if p else '<root>'
def short(x, w=70):
    s = repr(x); return s if len(s) <= w else s[:w] + '...'

def group(p, depth): return pstr(p[:depth])

def compare(fa, fb, show=15, depth=1, quiet=False):
    va, da = decode(fa); vb, db = decode(fb)
    la, oa = leaves(va, da); lb, ob = leaves(vb, db)
    raw_equal = open(fa, 'rb').read() == open(fb, 'rb').read()
    groups = collections.OrderedDict(); counts = collections.Counter()
    for p in sorted(set(la) | set(lb), key=lambda p: tuple(map(str, p))):
        x = la.get(p, '<absent>'); y = lb.get(p, '<absent>')
        if x == y or (isinstance(x, float) and isinstance(y, float) and x != x and y != y): continue
        g = group(p, depth); counts[g] += 1; groups.setdefault(g, []).append((p, x, y))
    # order-only differences: tables whose key set is equal but iteration order differs
    ordr = collections.Counter(); ordex = {}
    for p in set(oa) & set(ob):
        if oa[p] != ob[p] and sorted(map(str, oa[p])) == sorted(map(str, ob[p])):
            g = group(p, depth) if p else '<root>'; ordr[g] += 1; ordex.setdefault(g, p)
    if not quiet:
        print(f'{fa} vs {fb}: bytes {"EQUAL" if raw_equal else "differ"}; leaves {len(la)} / {len(lb)}; refs {da.nrefs}/{db.nrefs}')
        if not counts: print('  content: EQUAL as maps' + (' (byte-identical)' if raw_equal else ' (bytes differ -> order / encoding only)'))
        for g, c in counts.most_common():
            print(f'  [{g}] {c} differing paths')
            for p, x, y in groups[g][:show]: print(f'      {pstr(p)}: A={short(x)} B={short(y)}')
        if ordr:
            print('  tables with same keys in a different order (per group): ' +
                  ', '.join(f'{g}:{c} (e.g. {pstr(ordex[g])})' for g, c in ordr.most_common()))
    return counts, groups, ordr, la, lb

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]; show = 15; depth = 1
    for f in ('--show', '--depth'):
        if f in sys.argv:
            val = int(sys.argv[sys.argv.index(f) + 1]); args.remove(str(val))
            if f == '--show': show = val
            else: depth = val
    if len(args) == 1 or '--dump' in sys.argv:
        v, dc = decode(args[0]); l, o = leaves(v, dc)
        c = collections.Counter(group(p, depth) for p in l)
        print(f'{len(l)} leaves, {len(o)} tables, {dc.nrefs} refs, {dc.idx - 1} indexed objects')
        for g, n in c.most_common(): print(f'  {g}: {n}')
        sys.exit(0)
    compare(args[0], args[1], show, depth)
