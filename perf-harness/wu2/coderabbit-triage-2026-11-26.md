# wu2 CodeRabbit triage, pass 1 (2026-11-26, base e6e1e9bc6, head 671ad4731; 11 findings: 9 wu2, 2 kj3)

Full output: plan-facts/cr_wu2_kj3_2026-11-26.txt (main project dir). Status column is filled after the fix round.

| # | where | finding | decision |
|---|---|---|---|
| 1 | wu2_score.py:736 | prime probe passes with a window shorter than 30 s (note only) | FIX: probe fails by name on window_short |
| 2 | PREREG 5.4 text | clock-fit residual is not an error bound at E | FIX (text): load time called an estimate; the bound stated as what the fit supports |
| 3 | wu2_run.sh:89 | order file reread after hash preflight | FIX: verified copy in the state dir used for dispatch. Scorer side: the order hash is pinned in the text and checked by hand at scoring, stated in the result (declined in code) |
| 4 | wu2_score.py:882 | PASS possible for a series the driver stopped (3rd non-clean load is the last) | FIX: non-clean loads >= driver's stop count = no PASS, by name |
| 5 | wu2_selftest.sh:591 | held-frame fixture keeps E=71 | FIX: hold extends the precache block, E=83, control 71 |
| 6 | wu2_selftest.sh:836 | find-error test never reaches find | FIX |
| 7 | wu2_score.py:548 | -perf_nopresent / -perf_noinput not required | FIX: arm_mismatch |
| 8 | wu2_score.py:459 | clock_order 1 ms rule may stop valid ON data | VERIFY in the engine source; keep with derivation or loosen with derivation |
| 9 | wu2_run.sh:142 | slice deadline not rechecked after the display wait | FIX |
| 10 | kill6 :862 | forced-start line appended during a crossing is skipped | FIX in kill6 stage 3 |
| 11 | kill6 :241 | eerr counted before the managers exist on a healthy boot | VERIFY in GAMMA scripts; not-ready state without eerr, in kill6 stage 3 |
