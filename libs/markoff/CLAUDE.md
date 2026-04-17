# markoff

A QGraphicsView-based Markdown editor library. Self-contained Qt6/C++ library with its own plans, specs, tests, and test app.

## Scope

This library is **encapsulated** — it should not reason about or depend on Corbomite application concerns. Agents working in `libs/markoff/` should stay within this directory. Do not read the parent project's `CLAUDE.md`, `docs/PROJECT-STATE.md`, or cluster plans unless explicitly asked.

External deps (resolved via parent CMake when built as a subdir, or as sibling dirs when standalone):

- Qt6 (Core, Gui, Widgets, Test) ≥ 6.8
- KF6::SyntaxHighlighting
- `MarkoffParser::MarkoffParser` (sibling library `libs/markoff-parser`)
- `jkqtmathtext` (sibling library `libs/jkqtmathtext`)

## Documentation layout

- `docs/plans/` — implementation plans (one per feature, dated)
- `docs/specs/` — design specs the plans reference
- `docs/archive/` — superseded material
- `docs/architecture.md` — current architecture overview
- `docs/TODO.md` — running todo list

## Building

Markoff is typically built as part of the parent Corbomite tree. From the parent:

```bash
cmake -B build -DCORBOMITE_DEV_BUILD=ON
cmake --build build --target markoff
```

Run the test app:

```bash
./build/bin/markoff-testapp [path/to/file.md]
```

## Testing

Tests live in `tests/` and are registered in `tests/CMakeLists.txt`. Run:

```bash
cd build && ctest -R markoff --output-on-failure
```

Or one at a time:

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_<name>
```

## Conventions

- C++20, Qt6
- Use `tr()` for user-visible strings inside markoff (this library is translation-ready on its own, independent of parent i18n).
- Use `QIcon::fromTheme()` for icons.
- SPDX header `GPL-3.0-or-later` on every source file.
- Tests define expected behavior — when a test fails, fix the code, not the test.

## Public API surface

Consumer-facing headers live in `include/markoff/`. Keep internal types in `src/` and forward-declare across headers to keep the public surface minimal.
