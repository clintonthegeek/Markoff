# Markoff TODO

## 2026-04-28 — Phase 13 (Foundation library Part 2) acceptance passed

All 25 `tst_foundation_*` targets pass on a fresh build. The `markoff_foundation`
library is feature-complete per spec §12 (foundation-library.md + Part 2 plan).
Baseline captured in `docs/2026-04-28-foundation-tests-baseline.log` (145/147
suite-wide; only `tst_markoff_undo_grouping` and `tst_markoff_table_operations`
fail — both pre-existing master-side failures, not regressions).
