#!/usr/bin/env python3
"""R1 step 0 (PREREG-R1-step0-2026-11-22.md): split the main thread's render feed of one or more bench runs.

Per run (out/<tag>/perf_sites_<tag>.csv and perf_frames_<tag>.csv), main thread, ms per frame:
  frame  mean frame time (perf_frames, column frame_ms or the second column)
  feed   incl of "Process seqRender" (device.cpp)                     = whole render feed on the main thread
  render incl of "IGame_Level::OnRender: Render"
  (a)    lists: SUM OF SELF of every site whose name starts with r_dsgraph_capture, or is CDSGraphManager::traverse,
         CSector::traverse or "Render HOM" (self, so nested sites are never counted twice)
  (b)    submission = render - (a)   (an upper bound: other small work under OnRender stays in it)
  (c)    rest = feed - render
  sun    incl of "Render Cascade" (R_sun.cpp, gammavr's file), reported only
  probes main-thread probe calls per frame (all sites)
Usage: r1_split.py <run dir> [...]   prints one line per run, then the mean per group of runs given.
"""
import csv, os, sys

A_PREFIX = ("r_dsgraph_capture",)
A_NAMES = {"CDSGraphManager::traverse", "CSector::traverse", "Render HOM"}


def run(d):
    tag = os.path.basename(os.path.normpath(d))
    rows = open(os.path.join(d, "perf_sites_%s.csv" % tag)).read().splitlines()
    fr = int(rows[0].split(",")[1])
    sites = list(csv.DictReader(rows[2:]))
    def incl(name):
        v = [float(r["incl_main_ms"]) for r in sites if r["name"] == name]
        if len(v) != 1:
            raise SystemExit("%s: site %r found %d times" % (d, name, len(v)))
        return v[0] / fr
    a = sum(float(r["self_main_ms"]) for r in sites
            if r["name"].startswith(A_PREFIX) or r["name"] in A_NAMES) / fr
    feed, render = incl("Process seqRender"), incl("IGame_Level::OnRender: Render")
    sun = sum(float(r["incl_main_ms"]) for r in sites if r["name"] == "Render Cascade") / fr
    probes = sum(int(r["calls_main"]) for r in sites) / fr
    fl = list(csv.reader(open(os.path.join(d, "perf_frames_%s.csv" % tag))))
    hdr = fl[0]; col = hdr.index("frame_ms") if "frame_ms" in hdr else 1
    vals = [float(x[col]) for x in fl[1:] if len(x) > col and x[col]]
    frame = sum(vals) / len(vals)
    return dict(tag=tag, frame=frame, feed=feed, render=render, a=a, b=render - a, c=feed - render, sun=sun, probes=probes)


KEYS = ("frame", "feed", "render", "a", "b", "c", "sun", "probes")
if __name__ == "__main__":
    rs = [run(d) for d in sys.argv[1:]]
    for r in rs:
        print("%-14s " % r["tag"] + "  ".join("%s=%.3f" % (k, r[k]) if k != "probes" else "probes=%.0f" % r[k] for k in KEYS))
    if len(rs) > 1:
        print("%-14s " % "MEAN" + "  ".join("%s=%.3f" % (k, sum(r[k] for r in rs) / len(rs)) if k != "probes"
                                           else "probes=%.0f" % (sum(r[k] for r in rs) / len(rs)) for k in KEYS))
