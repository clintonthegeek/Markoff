# Markoff

A Qt6/C++20 Markdown editor library with a shared provider-backed document
model and three view widgets: Canvas, inline-styled rich editing, and raw
source. Canvas is the primary live-preview editor.

Extracted from, and primarily consumed by, the
[Corbomite](https://github.com/clintonthegeek/Corbomite) note-taking
app, but designed as a reusable widget library.

## What's novel here

- **Per-block CRDT document model.** Every top-level Markdown block
  (paragraph, heading, list item, table, …) is an independent
  collaborative text buffer; block order, kinds, and attributes are
  CRDT maps. Typing mutates the per-block buffer directly — no
  whole-document reparse on the hot path.
- **Three views over one document model:**
  - `Markoff::Canvas::EditorWidget` (`libs/markoff-canvas`) — the primary
    block-projection editor, with inline formatting, tables, accessibility,
    folding, media seams, and LaTeX math.
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
| `libs/markoff-core` | `Markoff::MarkoffDocument`, command set and undo, sessions, find, themes, text bindings, and `MarkdownView` base |
| `libs/markoff-parser` | Tree-sitter Markdown AST + YAML frontmatter (`Markoff::Document` value snapshot) |
| `libs/markoff-canvas` | Primary block-projection editor and live-preview view |
| `libs/markoff-styled` | QTextEdit inline-styled view leaf |
| `libs/markoff-source` | QPlainTextEdit source view leaf |
| `libs/markoff-collabtext-vnext` | Adapter to the installed provider-backed CollabText Core package |
| `libs/markoff-embed` | Embedded-renderer plugin runtime (contract in core: `CodeBlockProcessor`/`EmbedRenderService`) plus backends; `markoff_embed_math` is the first real engine |
| `libs/markoff-text-units` | UTF-8↔UTF-16 boundary helpers (header ships as `<markoff/core/TextUnits.h>`) |
| `libs/rapidyaml`, `libs/jkqtmathtext` | vendored third-party (YAML, LaTeX rendering) |
| `apps/` | Provider-backed collaboration app |

## Building

Requires Qt ≥ 6.8, CMake ≥ 3.19, a C++20 compiler. The source widget
additionally uses KF6SyntaxHighlighting.

```bash
git clone <this-repo>
cmake -S . -B build-dev -DCMAKE_PREFIX_PATH=/path/to/collabtext-core
cmake --build build-dev -j 8
```

Every Markoff build requires the selected installed CollabText Core package.
Markoff has no pinned-engine fallback, and the retired Live leaf is not part
of the supported build.

Consume as a subdirectory:

```cmake
add_subdirectory(path/to/Markoff)
target_link_libraries(myapp PRIVATE Markoff::Core Markoff::Canvas
                                    Markoff::Styled Markoff::Source)
```

The libraries provide install rules for a parent project's target export; a
standalone `find_package(Markoff)` config is not provided yet.

## Testing

```bash
scripts/run-tests.sh                                   # full suite, offscreen
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'  # fast inner loop
```

Tests run headless under `QT_QPA_PLATFORM=offscreen`.
The latest recorded result is in `docs/STATUS.md`.

## Status

Pre-1.0, under active development (v0.7.x series; current release **v0.7.1**).
Canvas is the primary editing view; Source and Styled remain available. The API
is not frozen. See `docs/STATUS.md` and
`docs/plans/2026-10-19-corbomite-open-asks-triage.md` for current status and
open work.

## For contributors / AI agents

Start with `AGENTS.md` and `docs/STATUS.md`. The task list is
`docs/plans/2026-10-19-corbomite-open-asks-triage.md`. `CLAUDE.md`
holds build rules and leaf layout. Design history is under `docs/specs/`
and `docs/plans/`.

## License

GPL-3.0-or-later (see `LICENSE`). Vendored components keep their own
licenses: rapidyaml (MIT), jkqtmathtext (LGPL-2.1+), tree-sitter
grammars (MIT).

## Provenance

Extracted from Corbomite on 2026-04-16; rebuilt around the CRDT
foundation 2026-04..05. Pre-extraction history lives in the Corbomite
git log.
