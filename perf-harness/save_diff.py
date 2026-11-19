#!/usr/bin/env python3
"""Per-object diff of two X-Ray save files (.scop), the game's own complete A-Life serialisation.
usage: save_diff.py A.scop B.scop [--online dumpA.txt dumpB.txt] [--tier-a] [--show N]
Save layout (CALifeStorageManager::save): u32 -1, u32 version, u32 raw size, LZO1X data. Raw data = chunks
(u32 id, u32 size): 1 spawns, 2 objects, 5 game time, 9 registries (info portions, relations, news, ...), other = header.
Object chunk: u32 count, then per object  u16 n + M_SPAWN packet (full STATE, incl. script-written state)  u16 n + M_UPDATE packet.
An object record = those two packets, compared byte for byte. NOTHING is masked inside object records.
Whole-chunk compares: 5 (game time) and 9 (registries) and the header chunk are reported separately.
--online: A-Life dumps taken at the same checkpoint give the online flag (column 8) per id; an object counts as OFFLINE
only if it is not online in either run. Without it every object is reported as 'unknown'.
--tier-a: exit 0 only if no offline object differs, none exists in one run only, and chunks 5 and 9 are equal."""
import sys, struct, ctypes, collections
lzo = ctypes.CDLL('liblzo2.so.2')
def raw(path):
    d = open(path, 'rb').read(); m, ver, n = struct.unpack_from('<IIi', d, 0)
    assert m == 0xFFFFFFFF, 'not a save file'
    out = ctypes.create_string_buffer(n); ol = ctypes.c_ulong(n)
    r = lzo.lzo1x_decompress_safe(d[12:], ctypes.c_ulong(len(d) - 12), out, ctypes.byref(ol), None)
    assert r == 0 and ol.value == n, f'decompress failed r={r} got {ol.value} want {n}'
    return out.raw[:n]
def chunks(b):
    o = 0; c = collections.OrderedDict()
    while o + 8 <= len(b):
        i, s = struct.unpack_from('<II', b, o); s &= 0x7FFFFFFF; c[i] = b[o + 8:o + 8 + s]; o += 8 + s
    return c
def zs(b, o):
    e = b.index(b'\0', o); return b[o:e].decode('latin1'), e + 1
def objects(c):
    n, = struct.unpack_from('<I', c, 0); o = 4; res = {}
    for _ in range(n):
        ls, = struct.unpack_from('<H', c, o); sp = c[o + 2:o + 2 + ls]; o += 2 + ls
        lu, = struct.unpack_from('<H', c, o); up = c[o + 2:o + 2 + lu]; o += 2 + lu
        sec, p = zs(sp, 2); name, p = zs(sp, p); p += 2 + 24 + 2      # gameid, rp, pos, angle, respawn
        oid, parent = struct.unpack_from('<HH', sp, p)
        pos = struct.unpack_from('<fff', sp, p - 26)
        assert oid not in res, f'duplicate id {oid}'
        res[oid] = (sec, name, parent, pos, sp, up)
    assert o == len(c), f'object chunk not fully parsed ({o} of {len(c)})'
    return res
def online(path):
    s = {}
    for l in open(path, errors='replace'):
        p = l.rstrip('\r\n').split('\t')
        if p and p[0].isdigit(): s[int(p[0])] = len(p) > 7 and p[7] == 'true'
    return s
def firstdiff(x, y):
    n = min(len(x), len(y)); k = next((i for i in range(n) if x[i] != y[i]), n)
    return k, sum(1 for i in range(n) if x[i] != y[i]) + abs(len(x) - len(y))
if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]; show = 8
    if '--show' in sys.argv: show = int(sys.argv[sys.argv.index('--show') + 1]); a.remove(str(show))
    CA, CB = chunks(raw(a[0])), chunks(raw(a[1])); OA, OB = objects(CA[2]), objects(CB[2])
    on = None
    if '--online' in sys.argv: on = (online(a[2]), online(a[3]))
    def cls(i):
        if on is None: return 'unknown'
        if i not in on[0] and i not in on[1]: return 'not-in-dump'
        return 'online' if (on[0].get(i) or on[1].get(i)) else 'offline'
    print(f"objects in save: {len(OA)} / {len(OB)}; raw size {sum(map(len, CA.values()))} / {sum(map(len, CB.values()))}")
    names = {1: 'spawns', 2: 'objects', 5: 'game time', 9: 'registries'}
    chunk_bad = []
    for i in sorted(set(CA) | set(CB)):
        if i == 2: continue
        same = CA.get(i) == CB.get(i)
        print(f"chunk {i} ({names.get(i, 'header/other')}): {'equal' if same else 'DIFFERS'} ({len(CA.get(i, b''))} / {len(CB.get(i, b''))} bytes)")
        if not same and i in (5, 9): chunk_bad.append(i)
    only = collections.defaultdict(list); diff = collections.defaultdict(list)
    for i in sorted(set(OA) | set(OB)):
        if i not in OA or i not in OB: only[cls(i)].append(i); continue
        x, y = OA[i], OB[i]
        if x[4] == y[4] and x[5] == y[5]: continue
        diff[cls(i)].append(i)
    tot = collections.Counter(cls(i) for i in OA)
    for k in ('offline', 'online', 'not-in-dump', 'unknown'):
        if not tot.get(k) and not diff.get(k) and not only.get(k): continue
        print(f"{k.upper():12s}: {tot.get(k, 0)} objects, {len(diff[k])} differ, {len(only[k])} in one run only")
        sec = collections.Counter((OA.get(i) or OB.get(i))[0] for i in diff[k])
        if sec: print("     differing by section:", dict(sec.most_common(8)))
        for i in diff[k][:show]:
            x, y = OA[i], OB[i]; s1, n1 = firstdiff(x[4], y[4]); s2, n2 = firstdiff(x[5], y[5])
            print(f"     id {i} {x[0]} '{x[1]}': state packet {len(x[4])}/{len(y[4])} bytes, {n1} differ (first at {s1}); update packet {len(x[5])}/{len(y[5])}, {n2} differ")
        for i in only[k][:show]: print(f"     only one run: id {i} {(OA.get(i) or OB.get(i))[0]}")
    fail = bool(diff['offline'] or only['offline'] or chunk_bad or on is None)
    print(f"-> SAVE TIER (a): {'FAIL' if fail else 'PASS'} (offline differ {len(diff['offline'])}, offline one-run-only {len(only['offline'])}, chunks differing {chunk_bad})")
    sys.exit((1 if fail else 0) if '--tier-a' in sys.argv else 0)
