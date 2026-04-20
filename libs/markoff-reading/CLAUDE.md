# readingview

Corbomite's Reading-mode widget library. A tree-sitter-AST-driven, section-recycling,
async, virtualized, frame-budgeted Obsidian-compatible Reading-mode renderer. Sibling
library to `libs/markoff/` — the two are **peer widgets**, `readingview` does NOT
depend on `markoff`.

## Scope

This library is **encapsulated** — it should not reason about or depend on Corbomite
application concerns. Agents working in `libs/readingview/` should stay within this
directory. Do not read the parent project's `CLAUDE.md`, `docs/PROJECT-STATE.md`, or
cluster plans unless explicitly asked.

External deps (resolved via parent CMake when built as a subdir, or as sibling dirs
when standalone):

- Qt6 (Core, Gui, Widgets, Test) ≥ 6.8
- KF6::SyntaxHighlighting
- `MarkoffParser::MarkoffParser` (sibling library `libs/markoff-parser`)
- `JKQTMathText` (sibling library `libs/jkqtmathtext`)
- `mmdr` (sibling library `libs/mmdr`, Rust Mermaid renderer bridge)
- `Corbomite::Core` (sibling library `libs/core`, domain types)

**Peer, not dependency:** `readingview` and `markoff` are both consumed by the parent
app as alternative editing modes. Neither depends on the other. Shared concepts
(theme, AST schema) are either vendored or promoted to a common parent library if
needed — they are not inherited from `markoff`.

## Provenance

Parts of this library are transplanted from
[Penelope](https://github.com/... — see SPDX headers on individual files), GPL-3.0-or-later,
adapted for Corbomite's encapsulation and dependency contract. See the cluster plan
(`../../docs/superpowers/plans/2026-04-14-cluster-e-markoff-three-mode-pivot.md`) for
the Penelope transplant manifest and phase sequencing.

## Documentation layout

- `docs/plans/` — implementation plans (one per feature, dated)

## Building

Built as part of the parent Corbomite tree via `add_subdirectory(libs/readingview)`.
Can also be built standalone from within this directory:

```bash
cmake -B build-standalone -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-standalone
QT_QPA_PLATFORM=offscreen ctest --test-dir build-standalone --output-on-failure
```

## Testing

Tests live in `tests/` and are registered in `tests/CMakeLists.txt`. All tests run
with `QT_QPA_PLATFORM=offscreen`. Tests define expected behavior — when a test
fails, fix the code, not the test.

## Conventions

- C++20, Qt6
- Use `tr()` for user-visible strings inside readingview (this library is
  translation-ready on its own, independent of parent i18n).
- Use `QIcon::fromTheme()` for icons.
- SPDX header `GPL-3.0-or-later` on every source file.
- Public API in `include/corbomite/readingview/`, internal types in `src/`.
- Namespace: `Corbomite::ReadingView`.
