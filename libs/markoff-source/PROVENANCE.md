# qutepart-corbomite Provenance

This library is a fork of **qutepart-cpp**, vendored into Corbomite at `libs/qutepart-corbomite/`.

## Upstream

- **Project:** qutepart-cpp
- **URL:** https://github.com/diegoiast/qutepart-cpp
- **Fork point:** commit `eec2e9ae5b50b591f017296ee743ee2860a280e4` (2026-04-12)
- **Fork date:** 2026-04-15
- **Upstream license:** MIT (see `LICENSE`)
- **Corbomite modifications license:** GPL-3.0-or-later
- **Combined SPDX:** `MIT AND GPL-3.0-or-later` on inherited files, `GPL-3.0-or-later` on net-new files.

## Fork rationale

qutepart-cpp is a mature `QPlainTextEdit`-derived editor widget with bundled Kate-XML syntax highlighting, a folding engine, multi-language indent algorithms, and a themes system. We vendored it (rather than adding it as an external dependency) because Corbomite needs a narrower, markdown-specialised Source-mode widget whose highlighting is driven by `KF6::SyntaxHighlighting` (shared with the rest of the Corbomite app) and whose fold engine is driven by the markdown heading hierarchy produced by `libs/markoff-parser`. We do not need C-style / Python / Ruby / Lisp / Scheme / XML indent algorithms, bundled Kate XML files, or a bundled theme system — Corbomite drives colours from `QPalette` + `KColorScheme`. The full rationale and multi-phase shaping plan live in [`docs/superpowers/specs/2026-04-15-qutepart-corbomite-fork-design.md`](../../docs/superpowers/specs/2026-04-15-qutepart-corbomite-fork-design.md) and [`docs/superpowers/plans/2026-04-15-qutepart-corbomite-fork.md`](../../docs/superpowers/plans/2026-04-15-qutepart-corbomite-fork.md). Vendoring also lets us evolve the widget's public API (visual-line float scroll, fold state serialization, find/replace, wiki-link and tag awareness) without waiting on upstream's cadence.

## Upstream monitoring

Monthly review of upstream commits for the first 6 months post-fork; quarterly thereafter. Bugfixes to `QPlainTextEdit`-derived behaviour, fold-logic edge cases, and cursor/scroll correctness are candidates for cherry-pick. New language support, new themes, and anything under `src/hl/` are not (deleted in Phase 4).

## Divergence from upstream

These sections are populated as phases land. Phase 1 (vendor + CMake + smoke test) introduces only tree-level changes; per-file source diffs start in Phase 2.

### Modified files

- `include/qutepart/qutepart.h` + `src/qutepart.cpp` — Phase 2, 2026-04-15. Added two public methods on `Qutepart::Qutepart`: `scrollPositionVisualLine()` and `setScrollPositionVisualLine(float)`. They expose the otherwise-protected `QPlainTextEdit::firstVisibleBlock` / `blockBoundingGeometry` / `blockBoundingRect` plumbing so the `Corbomite::SourceEditor` shim (in the app tree) can composition-wrap the widget rather than subclass or friend it. No upstream behaviour changed; this is an additive public-API extension.

### Added files

- `PROVENANCE.md` (this file) — Phase 1, 2026-04-15
- `CLAUDE.md` — Phase 1, 2026-04-15
- `README.md` — Phase 1, 2026-04-15 (replaces upstream's)
- `CMakeLists.txt` — Phase 1, 2026-04-15 (replaces upstream's)
- `tests/CMakeLists.txt` — Phase 1, 2026-04-15
- `tests/tst_qutepart_smoke.cpp` — Phase 1, 2026-04-15

### Deleted files

- `example/` — Phase 1, 2026-04-15 (upstream's standalone demo app; not used)
- `README.md` (upstream's) — Phase 1, 2026-04-15 (replaced with our own)
- `qutepart.pro` — Phase 1, 2026-04-15 (qmake build; we use CMake only)
- `cmake/` (upstream's helper dir: CPM, mold-linker, Sphinx) — Phase 1, 2026-04-15 (not used by our CMakeLists)
- Upstream `test/` (13 test files + `indenter/` subdir) — Phase 1, 2026-04-15 (replaced by a single smoke test; we write our own as the library is shaped)
- `.github/`, `.git/` — Phase 1, 2026-04-15 (not vendored)

## SPDX header conventions

- **Inherited files** (cpp/h originally from upstream): dual MIT AND GPL-3.0-or-later header block at top (after any `#pragma once` guard). See the block in any `src/*.cpp` for the canonical form.
- **Net-new files** added by Corbomite: single `GPL-3.0-or-later` SPDX line.

## Binary-size / LOC tracking

Recorded as phases land. Phase 1 post-vendor counts: 66 `.cpp`/`.h` files, 13,451 LOC inherited.
