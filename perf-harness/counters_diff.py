#!/usr/bin/env python3
"""Compare the exact det_mode counters of two equivalence-harness runs.

Usage: counters_diff.py <dirA> <dirB> [--k 1,2,3]

Each dir is out/det_<tag>/ and holds, per checkpoint k:
  det_alife_counts_<tag>_<k>.txt  (engine: registered/unregistered/upd lines)
  det_counters_<tag>_<k>.txt      (Lua: stream/sq/info lines)

Exact items (counted in C): every stream line, upd ids offline in both
runs, sq ids offline in both runs. Ids online in either run and info lines are informational only.
"""
import argparse
import os
import sys


def tag_of(d):
    name = os.path.basename(os.path.normpath(d))
    return name[4:] if name.startswith("det_") else name


def read_lines(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return [ln.split() for ln in f if ln.strip()]


def load_alife(path):
    totals, upd = {}, {}
    for t in read_lines(path):
        if t[0] in ("registered", "unregistered") and len(t) >= 2:
            totals[t[0]] = t[1]
        elif t[0] == "upd" and len(t) >= 4:
            # upd <id> <count> <name...> <online 0|1|->; the name may be empty
            online = t[-1] == "1"
            name = " ".join(t[3:-1]) or "?"
            upd[int(t[1])] = (int(t[2]), name, online)
    return totals, upd


def load_counters(path):
    streams, sq, info = {}, {}, {}
    for t in read_lines(path):
        if t[0] == "stream" and len(t) >= 2:
            streams[t[1]] = " ".join(t[2:])
        elif t[0] == "sq" and len(t) >= 7:
            sq[int(t[1])] = (" ".join(t[2:6]), t[6] == "1")
        elif t[0] == "info" and len(t) >= 2:
            info[t[1]] = " ".join(t[2:])
    return streams, sq, info


def compare_ids(a, b, values, online, label, exact, informational):
    """a/b: id -> record. An id missing in a run counts as offline there with no value."""
    for i in sorted(set(a) | set(b)):
        ra, rb = a.get(i), b.get(i)
        va = values(ra) if ra else None
        vb = values(rb) if rb else None
        if va == vb:
            continue
        on = (ra is not None and online(ra)) or (rb is not None and online(rb))
        msg = "%s id %d: A=%s B=%s" % (label, i, va if ra else "absent", vb if rb else "absent")
        (informational if on else exact).append((label, msg))


def compare_k(dirA, dirB, k):
    tA, tB = tag_of(dirA), tag_of(dirB)
    paths = {}
    for side, d, t in (("A", dirA, tA), ("B", dirB, tB)):
        for kind in ("det_alife_counts", "det_counters"):
            p = os.path.join(d, "%s_%s_%s.txt" % (kind, t, k))
            if not os.path.isfile(p):
                print("ERROR: missing file %s" % p, file=sys.stderr)
                sys.exit(2)
            paths[(side, kind)] = p

    totA, updA = load_alife(paths[("A", "det_alife_counts")])
    totB, updB = load_alife(paths[("B", "det_alife_counts")])
    stA, sqA, infA = load_counters(paths[("A", "det_counters")])
    stB, sqB, infB = load_counters(paths[("B", "det_counters")])

    exact, informational = [], []
    for key in ("registered", "unregistered"):
        if totA.get(key) != totB.get(key):
            informational.append((key, "%s: A=%s B=%s" % (key, totA.get(key), totB.get(key))))  # includes online-spawned objects (prereg)
    for name in sorted(set(stA) | set(stB)):
        if stA.get(name) != stB.get(name):
            exact.append(("stream", "stream %s: A=%s B=%s" % (name, stA.get(name, "absent"), stB.get(name, "absent"))))
    compare_ids(updA, updB, lambda r: r[0], lambda r: r[2], "upd", exact, informational)
    compare_ids(sqA, sqB, lambda r: r[0], lambda r: r[1], "sq", exact, informational)
    for key in sorted(set(infA) | set(infB)):
        if infA.get(key) != infB.get(key):
            informational.append(("info", "info %s: A=%s B=%s" % (key, infA.get(key, "absent"), infB.get(key, "absent"))))

    print("COUNTERS k=%s C=%d" % (k, len(exact)))
    def breakdown(items):
        counts = {}
        for lab, _ in items:
            counts[lab] = counts.get(lab, 0) + 1
        return " ".join("%s=%d" % kv for kv in sorted(counts.items())) or "none"
    print("  exact breakdown: %s" % breakdown(exact))
    for _, msg in exact[:10]:
        print("    %s" % msg)
    print("  informational (not in C): %d (%s)" % (len(informational), breakdown(informational)))
    for _, msg in informational[:10]:
        print("    %s" % msg)
    return len(exact)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dirA")
    ap.add_argument("dirB")
    ap.add_argument("--k", default="1,2,3", help="comma separated checkpoints (default 1,2,3)")
    a = ap.parse_args()
    for d in (a.dirA, a.dirB):
        if not os.path.isdir(d):
            print("ERROR: not a directory: %s" % d, file=sys.stderr)
            sys.exit(2)
    for k in [x.strip() for x in a.k.split(",") if x.strip()]:
        compare_k(a.dirA, a.dirB, k)


if __name__ == "__main__":
    main()
