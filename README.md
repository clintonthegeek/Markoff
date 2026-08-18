# Markoff

A Qt6/C++20 Markdown **editor widget family**: one CRDT-backed document
model, three interchangeable view widgets. Live-preview (Obsidian-style
inline rendering), inline-styled rich editing, and raw source — all
editing the same document object, swappable at runtime through a common
`Markoff::MarkdownView` base.

Extracted from, and primarily consumed by, the
[Corbomite](https://github.com/clintonthegeek/Corbomite) note-taking
app, but designed as a reusable widget library.

## What's novel here

- **Per-block CRDT document model.** Every top-level Markdown block
  (paragraph, heading, list item, table, …) is an independent
  collaborative text buffer; block order, kinds, and attributes are
  CRDT maps. Typing mutates the per-block buffer directly — no
  whole-document reparse on the hot path.
- **Three real views over one model**, not three serializations:
  - `Markoff::Live::EditorWidget` (`libs/markoff-live`) — QML
    live-preview leaf: inline-format rendering with cursor-aware
    delimiter visibility, wikilinks, editable tables with smart column
    widths, LaTeX math, theming + zoom.
  - `Markoff::Styled::Editor` (`libs/markoff-styled`) — QWidget/
    QTextEdit inline-styled editor; word-processor feel, native
    QTextTable rendering for tables; plus a headless
    `DocumentRenderer` for previews/popovers. No QML, no KF6.
  - `Markoff::Source::Editor` (`libs/markoff-source`) — QPlainTextEdit
    source view with syntax highlighting (KSyntaxHighlighting), line
    gutter, and format-toggle operations.
- **Round-trip fidelity.** Untouched blocks save byte-identical to
  what was loaded; serializers reconstruct only what the user touched.
- **Parser at the edges, not in the loop.** Tree-sitter Markdown
  (vendored grammars) parses at load time and per-block on demand for
  inline spans; editing never blocks on a parse.

## Layout

| Path | What |
|---|---|
| `libs/markoff-core` | `Markoff::MarkoffDocument` (the CRDT document), command set + undo, sessions, find, themes, the shared flat-view text binding, `MarkdownView` base |
| `libs/markoff-parser` | Tree-sitter Markdown AST + YAML frontmatter (`Markoff::Document` value snapshot) |
| `libs/markoff-live` | QML live-preview view leaf |
| `libs/markoff-styled` | QTextEdit inline-styled view leaf |
| `libs/markoff-source` | QPlainTextEdit source view leaf |
| `libs/collabtext` | CRDT text engine (git submodule) |
| `libs/rapidyaml`, `libs/jkqtmathtext` | vendored third-party (YAML, LaTeX rendering) |
| `apps/` | demo / dogfood apps (`markoff-live-app`, `markoff-styled-app`, …) — skip with `-DMARKOFF_BUILD_APPS=OFF` |

## Building

Requires Qt ≥ 6.8, CMake ≥ 3.19, a C++20 compiler. The source widget
additionally uses KF6SyntaxHighlighting.

```bash
git clone --recurse-submodules <this-repo>
cmake -S . -B build-dev
cmake --build build-dev -j 8
```

Consume as a submodule (the supported mode today — no install/export
story yet):

```cmake
add_subdirectory(libs/markoff-family)   # your submodule path
target_link_libraries(myapp PRIVATE Markoff::Core Markoff::Styled
                                    Markoff::Source Markoff::Live)
```

## Testing

```bash
scripts/run-tests.sh                                   # full suite, offscreen
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'  # fast inner loop
```

~260 test binaries run headless under `QT_QPA_PLATFORM=offscreen`.
Current baseline is documented in `docs/STATUS.md`.

## Status

Pre-1.0, under active development (v0.7.x series). The live and source
leaves are mature; the styled leaf is newest (tables render read-only;
in-grid editing is on the roadmap). API is not yet frozen — see
`docs/STATUS.md` for the live status board and `docs/queue.md` for
open work.

## For contributors / AI agents

Start with `CLAUDE.md` (orientation + engineering discipline), then
`docs/VIEW-IMPLEMENTORS-GUIDE.md` and `docs/INVARIANTS.md`. Design
history is under `docs/specs/` and `docs/plans/`.

## License

GPL-3.0-or-later (see `LICENSE`). Vendored components keep their own
licenses: rapidyaml (MIT), jkqtmathtext (LGPL-2.1+), tree-sitter
grammars (MIT).

## Provenance

Extracted from Corbomite on 2026-04-16; rebuilt around the CRDT
foundation 2026-04..05. Pre-extraction history lives in the Corbomite
git log.
