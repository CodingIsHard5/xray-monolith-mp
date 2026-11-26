# wu1 CodeRabbit triage (2026-11-26, review of perf/render-split -> perf/equivalence-harness, wu1/ findings only)
Source: scratchpad cr_wu1_full.txt (14 wu1 findings; the 5 kj2/ findings are not wu1's and are not triaged here).
Each point was checked against the code before acting. Test names are self-test cases (wu1_selftest.sh) or mutants.

1. PREREG:232 warm-up duration is an estimate, not a lower bound: FIXED (text, P1). The low ends of the hitch (6.5 s)
   and train end (20.8 s) are now labelled ESTIMATE; the upper ends (7.8 s, 22.1 s) stay bounds (mcm_log stamps).
   No registered bound changes: only the upper ends decide whether 30 s holds them. No test (text).
2. selftest:480 find error counted as "unchanged": FIXED (T1). newer_count() fails on a find error or a missing
   folder; test "untouched check fails on a find error" (a path that does not exist must fail the check).
3. run.sh:173 exit code of a completed CONFOUNDED capture: DECLINED as a code change, ANSWERED. tools/s2_bench.sh sets
   RC only in its capture loop (lines 57-64: 0 when the capture finished) and writes the VERDICT after (72-74), so a
   completed CONFOUNDED capture exits 0 and reaches the driver's counter (stop at the third), as registered. Comment
   added in wu1_run.sh and prereg 8; the scorer's check= does not stop on not_clean (case check_not_clean).
4. run.sh:102-105 capture markers not checked by the driver: FIXED (D3). The driver's check is now the scorer's
   check= mode, which stops on VOID capture_lines; cases check_no_cap, check_cap_order.
5. run.sh:103 command line not matched to the registered arm: FIXED (D3). check= stops on arm_mismatch (exe, save,
   -perf_capture, lever flag, -perf_fixed_dt / -det_mode / -sim_tick); cases check_cordon2, check_det_mode,
   check_sim_tick, check_lever_absent, check_lever_present.
6. score.py:179 malformed VERDICT line dropped as not_clean: FIXED (S1). Exactly one VERDICT line, one of the bench's
   three forms, else UNPARSABLE; cases f_verdictx, f_verdict2; mutant verdict_form.
7. score.py:185/190 `1..2` matches the pattern, float() raises, scorer exits without a report: FIXED (S2). Named
   UNPARSABLE line, plus a per-load safety net (scorer_error) so the report is always written; cases f_full_ms,
   f_slow_ms; mutant log_num (the mutant shows the net catching it as scorer_error).
8. selftest:236 slow-cap fixture (8414 rows) is not what the profiler can write: FIXED (T2). Fixtures at 8386 (VALID),
   8387 (VOID) and 8400 (VOID, the most the profiler writes); cases slow_8386_ok, v_slow_8387, v_slow_cap; constant
   mutants slow_cap_rows_up (cutoff 8400) and slow_cap_rows_down (8385).
9. run.sh:146 cleanup installed after the WANT queue: FIXED (D4). Trap installed before the first queue operation;
   WANT always withdrawn; release only when CLAIMED=1. bash -n only (driver not run, by brief).
10. run.sh:74 state folder failure not checked; done.txt append not checked: FIXED (D5). mkdir (new) + done.txt +
    series.log must succeed before the queue, else REFUSED exit 2; a failed append stops the series by name.
    bash -n only.
11. score.py:115/159 inf / nan in timing values: FIXED (S2). num() rejects non-finite values in frame times, site
    times, the slow threshold and slow rows; cases f_inf_frame, f_nan_frame, f_nan_thr, f_inf_site, f_inf_slow_ms;
    mutant finite.
12. score.py:304-313 held precache frames understate the load time: FIXED (S4). From device.cpp:131-170 a line at
    P > 3 gives F0 = F + P + K, at P <= 2 F0 = F + P, a line at P = 3 is not used when K > 0; no line left = load
    time unmeasurable (counts toward NO VERDICT loadtime). Case l_held_ok (K = 12, realistic countdown: 33.00 s; the
    old rule gives 32.94 s); mutant held_k.
13. score.py:412 capture finish marker before start: FIXED (S3). Required textures-handled < started < finished in log
    order; case v_cap_order (finished inside loading, started after); mutant capture_order.
14. NOTES:8-9 an unrecognised line format is not a parse failure (OFF becomes OFF-OTHER): FIXED (S5). FAIL
    no_measure_lines when a load has no alife_pass or no alife_opu line (both guaranteed by the code, prereg 0);
    cases f_no_opu, f_no_pass, f_opu_renamed; mutant measure_lines. Also guarded before any scored load by the
    prime probe (D1); cases probe_ok, probe_no_opu, probe_no_pass, probe_no_save, probe_no_cap, probe_lever,
    probe_bad_row, probe_refused; mutant probe_save.

Count: 14 wu1 findings (19 in the review minus 5 on kj2/), 13 FIXED, 1 answered without a code change (item 3).
The D1 probe was added by the brief on top of item 14.

## Second review (CodeRabbit --uncommitted --dir perf-harness/wu1, on the Revision 3 changes)
15. run.sh:117 a TERM during a successful claim could run the trap before CLAIMED=1 (claim left unreleased): FIXED.
    TERM/INT are trapped to a flag during the claim call; CLAIMED is recorded, then a pending cancel runs finish.
    bash -n only (driver not run, by brief).
16. run.sh:154 a failed write of check_<tag>.txt did not stop the series: FIXED. Named stop "state write failed".
    bash -n only.
