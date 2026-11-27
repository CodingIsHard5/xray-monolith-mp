# wu3 review triage, 2026-11-27 (fix round 1, ordered by the coordinator)

Fixed (each with a self-test check and a mutant caught by name; PREREG sections 3, 4, 7):
- A: a re-run folder next to a first load that fails check= is FAIL stop_contradiction.
- B: a box stop during a live bench writes `class=box interrupted=<tag> reason=...`; the scorer treats that folder as not run (it must be the last folder in run order).
- C: finish() exits non-zero and says "stop record NOT written" in series.log and the notify when the stop record cannot be written; it still reaps the bench and releases.
- D: a slot counts as failed when either attempt failed; its loads leave the group metrics.
- E: stop.txt must be exactly one physical line ended by LF or CRLF; an extra blank line or no line end is FAIL stop_record.
- F: a parsed VERDICT stays in the not-clean counts when another file of the load fails to parse.
- G: the self-test makes a relative output parent absolute.

Skipped on purpose:
- Quoting state paths for spaces: the paths are fixed strings without spaces (out/wu3_state), and the self-test parent is ours.
- State init failing after mkdir: the scorer already gives FAIL stop_record (no stop record), the same outcome class.
- Scoring from the state copy of the order: EnginePerf checks the sha256 of BOTH the tree copy and the state copy against the registered hash by hand before the single scoring run (sentence added to PREREG section 4).

Pass 2 (2026-11-27, on d9ddb4e9e; plan-facts/cr_wu3_pass2_2026-11-27.txt): 1 finding, minor.
- STOP_RE allows only [a-z0-9_] in an interrupted= tag while the order rules allow any lowercase tag: SKIPPED. The
  order file is hash-pinned and every registered tag is wu3_NN_on / wu3_NN_off / wu3_prime, so it cannot occur in this
  registration. No byte of driver, scorer or self-test changed after pass 2 except the three pins set at registration.
