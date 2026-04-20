# qutepart-corbomite

Corbomite's permanent Source-mode widget library. A fork of [qutepart-cpp](https://github.com/diegoiast/qutepart-cpp) vendored at commit `eec2e9a` (2026-04-12) and being shaped over 8 phases into a narrower, markdown-specialised, KSyntaxHighlighting-powered editor that Corbomite owns and maintains internally.

See [`PROVENANCE.md`](PROVENANCE.md) for upstream details, license, and a running log of divergences.

## Scope

This library is **encapsulated** — it should not reason about or depend on Corbomite application concerns. Agents working in `libs/qutepart-corbomite/` should stay within this directory. Do not read the parent project's `CLAUDE.md`, `docs/PROJECT-STATE.md`, or cluster plans unless explicitly asked.

## Shaping plan

The 8-phase plan for turning this tree into its target state lives at:
[`../../docs/superpowers/plans/2026-04-15-qutepart-corbomite-fork.md`](../../docs/superpowers/plans/2026-04-15-qutepart-corbomite-fork.md)

The companion design spec:
[`../../docs/superpowers/specs/2026-04-15-qutepart-corbomite-fork-design.md`](../../docs/superpowers/specs/2026-04-15-qutepart-corbomite-fork-design.md)

Phases (summary):

1. Vendor + CMake integration + smoke test.  ← **current**
2. `Corbomite::SourceEditor` shim + visual-line float scroll API.
3. Public find/replace API.
4. Replace bundled Kate-XML engine with `KF6::SyntaxHighlighting` + extract `FoldCalculator`.
5. Trim indent engines to markdown-only.
6. Remove bundled themes; drive from `KColorScheme`.
7. Markdown-specific features (wiki-links, tags, frontmatter, section-fold).
8. Final rename / rebrand to `Corbomite::Source`.

## External dependencies

- Qt6 (Core, Widgets) ≥ 6.8
- Qt6::Test (for tests only)
- (Phase 4 onwards) `KF6::SyntaxHighlighting` — not linked yet in Phase 1.

No dependency on sibling Corbomite libraries yet. The eventual `Corbomite::SourceEditor` shim (added in Phase 2 and living under `src/shim/` within this library) will depend on `MarkoffParser::MarkoffParser` for markdown heading hierarchy and frontmatter detection.

## Public API surface

Phase 1 layout (minimized diff from upstream):

- Public headers at `include/qutepart/*.h` (`qutepart.h`, `theme.h`).
- Target: `qutepart-corbomite` (static lib), alias `Corbomite::QutepartSource`.
- Resources: `qutepart-syntax-files.qrc`, `qutepart-theme-data.qrc` (both deleted in Phases 4 and 6).

Phase 8 will rename public headers to `include/corbomite/source/` and the namespace to `Corbomite::Source`. Until then, the public API is effectively the upstream `Qutepart::` namespace.

## Building

Normally built as part of the parent Corbomite tree via `add_subdirectory(libs/qutepart-corbomite)`:

```bash
cmake -B build -DCORBOMITE_DEV_BUILD=ON
cmake --build build --target qutepart-corbomite
```

Standalone build (useful for isolated iteration — does not need the top-level tree):

```bash
cd libs/qutepart-corbomite
cmake -B build-standalone -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-standalone
QT_QPA_PLATFORM=offscreen ctest --test-dir build-standalone --output-on-failure
```

## Testing

The single smoke test `tests/tst_qutepart_smoke.cpp` proves the widget instantiates and round-trips plain text. It runs headless via `QT_QPA_PLATFORM=offscreen` (set in `tests/CMakeLists.txt`). Additional tests land in later phases, authored fresh — we deleted the upstream test suite because it covers features we're about to remove (Kate-XML highlighting, multi-language indent, bundled themes).

## Conventions

- C++20, Qt6.
- SPDX dual-header (`MIT AND GPL-3.0-or-later`) on inherited files.
- SPDX single-header (`GPL-3.0-or-later`) on net-new files.
- Tests define expected behavior — when a test fails, fix the code, not the test.
