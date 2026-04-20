# Markoff

Qt6/C++ markdown editor family. Four peer libraries + one shared core:

- `libs/markoff-core` (`Markoff::Core`) — shared primitives: `MarkdownView` abstract base, `MarkoffDocument`, `SearchController`/`ReplaceController`, `SearchBar`.
- `libs/markoff-live` (`Markoff::Live`) — QGraphicsView-based live-preview editor (class `Markoff::Editor`).
- `libs/markoff-source` (`Markoff::Source`) — Qutepart-backed plaintext editor (class `Markoff::Source::SourceEditor`).
- `libs/markoff-reading` (`Markoff::Reading`) — Obsidian-compatible reading view (class `Markoff::Reading::ReadingView`).
- `libs/markoff-parser` (`MarkoffParser::MarkoffParser`) — tree-sitter-based Markdown AST + YAML frontmatter.

All three leaf widgets inherit `Markoff::MarkdownView` and expose the same polymorphic contract. Host applications (CorbomiteApp, test app, any future consumer) hold a `MarkdownView *` and dispatch through the contract.

## Ownership and phase

As of 2026-04-20, this repo is jointly owned with `/home/clinton/dev/Corbomite/`: the Corbomite agent holds development responsibility for both sides of the submodule boundary through Phase C. See `docs/handoff/2026-04-20-phase-c-ownership-handoff.md` for scope of authority and invariants.

Current state: `v0.2.0` = Phase A complete (tri-view API landed) + Phase B bridge (CMake option `MARKOFF_READING_USE_REAL_COREDEPS` for host-side `Corbomite::Core` / mmdr). Phase C retires the bridge and delivers the final API shape.

## Invariants (must hold on every commit to master)

1. **Standalone build is green.** Fresh checkout, `cmake -S . -B build-dev && cmake --build build-dev -j && cd build-dev && ctest` must pass with no host project present. `MARKOFF_READING_USE_REAL_COREDEPS=OFF` is the standalone default.
2. **No `Corbomite`-named types in Markoff public API.** The Phase B stubs under `libs/markoff-reading/stubs/corbomite/` are the one exception and retire in Phase C.
3. **Public includes are `<markoff/...>`**, `<markoff/source/...>`, `<markoff/reading/...>`, never `<corbomite/...>`.
4. **Every phase milestone tags a Markoff version.** Never force-push a tag.
5. **`master` is append-only.** Revert commits, no force-push.

## Building

Standalone:

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j
```

Within a host project (e.g. CorbomiteApp): `add_subdirectory(libs/markoff-family)` with `MARKOFF_READING_USE_REAL_COREDEPS=ON` to wire host-provided `Corbomite::Core` + `mmdr` targets.

Test app:

```bash
./build-dev/bin/markoff-testapp [path/to/file.md]
```

## Testing

```bash
cd build-dev && ctest --output-on-failure -j
```

At `v0.2.0`: 70 tests across `markoff-core`, `markoff-live`, `markoff-source`, `markoff-reading`, `markoff-parser`, plus the cross-leaf smoke test at `tests/markoff/tst_tri_view_smoke`.

Four markoff-reading tests are gated on `MARKOFF_READING_USE_REAL_COREDEPS=ON` (mermaid + embed features that need host-provided concretes); see `libs/markoff-reading/tests/CMakeLists.txt` `# TODO Phase B` comments.

## Conventions

- C++20, Qt6.8+, CMake 3.19+.
- `tr()` for user-visible strings (translation-ready independent of any host).
- `QIcon::fromTheme()` for icons.
- SPDX header `GPL-3.0-or-later` on every source file.
- Tests define expected behavior — when a test fails, fix the code, not the test. Exception: tests that probed pre-Phase-A internal representation (e.g. `qobject_cast<QGraphicsView*>(editor)` assuming Editor IS-A QGraphicsView) may be rewritten to match the composed-widget shape; that's a shape change, not a behavior change.

## Docs layout

- `docs/specs/` — design specs (dated, kebab-case).
- `docs/plans/` — implementation plans (one per feature/phase, dated).
- `docs/handoff/` — ownership-transfer notes.
- `docs/phase-c-status.md` — living status board for the Phase C work-units.
- `docs/archive/` — superseded material.
- `docs/architecture.md` — current architecture overview.
- `docs/TODO.md` — running todo list.

## Per-library guides

Each library has its own CLAUDE.md with library-specific conventions:

- `libs/markoff-live/CLAUDE.md` (still named `libs/markoff/CLAUDE.md` internally at `v0.2.0` — Phase C or an opportunistic touch renames it)
- `libs/markoff-core/CLAUDE.md`
- `libs/markoff-source/CLAUDE.md` (Qutepart conventions — imported intact from the original vendored library)
- `libs/markoff-reading/CLAUDE.md`

The old "stay within this directory; do not reason about Corbomite concerns" rule in `libs/markoff-live/CLAUDE.md` is overridden by the handoff doc for agents designing cross-boundary interfaces — but it still applies to library-internal work.

## External dependencies

Resolved via parent CMake when built as a subdirectory, or as sibling dirs when standalone:

- Qt6 (Core, Gui, Widgets, Test, Svg, SvgWidgets) ≥ 6.8
- KF6::SyntaxHighlighting
- `jkqtmathtext` (sibling library `libs/jkqtmathtext`)
- `MarkoffParser::MarkoffParser` (sibling library `libs/markoff-parser`)

When `MARKOFF_READING_USE_REAL_COREDEPS=ON`:
- `Corbomite::Core` (host-provided)
- `mmdr` (host-provided; Rust FFI crate staying in Corbomite through Phase C)
