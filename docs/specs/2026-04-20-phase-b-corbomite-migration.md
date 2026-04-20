# Phase B — Corbomite Migration Spec

**Status:** Draft
**Depends on:** Phase A (`2026-04-20-tri-view-unified-api-design.md`, `2026-04-20-tri-view-phase-a.md`), currently on Markoff branch `feature/tri-view-phase-a`.
**Audience:** Agents and humans who will bump Corbomite's Markoff submodule pin and migrate CorbomiteApp onto the new tri-view API.

---

## Goal

Flip CorbomiteApp to consume the three Markoff leaf libraries (`Markoff::Live`, `Markoff::Source`, `Markoff::Reading`) instead of its own in-tree copies of qutepart-corbomite and readingview. Delete the now-orphaned Corbomite sibling libraries. No behavior change visible to the user; this is a target-name and header-path migration.

## Non-goals

- Rewriting `NoteEditorWidget` or `MarkdownView.cpp` around `MarkoffDocument`. Phase A's `setDocument()` is forwarding-only; Corbomite stays content-authoritative on the leaf widgets. Shared-document adoption is Phase C.
- Consolidating `Theme` / `ResourceProvider` / `LinkResolver`. Phase C.
- Activating the stubbed Mermaid / embed-subpath / `MarkdownRenderChild` paths. See "Stub repayment" below.

---

## Current state of Corbomite (survey)

From `git grep` over `/home/clinton/dev/Corbomite` at SHA `924e9b53` (2026-04-20), excluding `.worktrees/`:

**Submodule pin**
- `.gitmodules` → `libs/markoff-family` pins Markoff at `d2cf27013029ffb6da1da7cf37e242a509d0347f` (pre-Phase-A).

**CMake consumers**
- `src/CMakeLists.txt:73–75` — `target_link_libraries(CorbomiteApp ... Markoff::Markoff Corbomite::ReadingView Corbomite::QutepartSource ...)`.
- `libs/readingview/CMakeLists.txt:74` — readingview links `Corbomite::Storage` (unused by its sources, see below).

**Source consumers**
- `src/editor/MarkdownView.cpp:10–11` — includes `<corbomite/readingview/ReadingView.h>` and `<markoff/Editor.h>`.
- `src/editor/NoteEditorWidget.{h,cpp}` — constructs `Markoff::Editor` + `Corbomite::ReadingView::ReadingView` (lazy, line 125 in .cpp), wires signals.
- `src/editor/SourceEditor.{h,cpp}` — includes `<qutepart.h>`, owns a `Qutepart::Qutepart *m_qutepart` directly; wraps it as the Corbomite source-mode widget.

**No plugins** consume `Markoff::`, `Corbomite::ReadingView`, or `Corbomite::QutepartSource`. The migration is contained within `src/editor/` and `src/CMakeLists.txt`.

---

## Breaking-change manifest (what Corbomite sees after the pin bump)

Once Corbomite's submodule pin is advanced to the merged Phase A SHA, every item below becomes a compile break until fixed. This is the manifest the Corbomite agent can work from.

### 1. Target renames (CMake)

| Old                          | New                          |
| ---------------------------- | ---------------------------- |
| `Markoff::Markoff`           | `Markoff::Live`              |
| library `markoff`            | library `markoff_live`       |
| directory `libs/markoff/`    | `libs/markoff-live/`         |

Action: update `src/CMakeLists.txt:73`.

### 2. New Markoff targets now available

- `Markoff::Core` (shared primitives — `MarkdownView`, `MarkoffDocument`, search/replace). Corbomite doesn't need to link this directly; `Markoff::Live/Source/Reading` each PUBLIC-link it.
- `Markoff::Source` (replaces `Corbomite::QutepartSource`)
- `Markoff::Reading` (replaces `Corbomite::ReadingView`)

### 3. Class and header-path renames

| Old                                              | New                                                   |
| ------------------------------------------------ | ----------------------------------------------------- |
| `#include <qutepart/qutepart.h>` (direct)        | `#include <qutepart/qutepart.h>` (unchanged — `Markoff::Source` re-exposes the same public include)  |
| `Qutepart::Qutepart`                             | `Qutepart::Qutepart` (unchanged class + namespace)    |
| `#include <corbomite/readingview/ReadingView.h>` | `#include <markoff/reading/ReadingView.h>`            |
| `Corbomite::ReadingView::ReadingView`            | `Markoff::Reading::ReadingView`                       |
| `Corbomite::ReadingView::*` (namespace)          | `Markoff::Reading::*`                                 |

Markoff preserves `<qutepart/qutepart.h>` and the `Qutepart::` namespace intentionally — the raw Qutepart library is vendored unchanged. Only its packaging (target name, sibling-library alias) moved. So `src/editor/SourceEditor.cpp` keeps compiling with just a CMake link change.

### 4. `Markoff::Editor` inheritance change

`Markoff::Editor` now inherits `Markoff::MarkdownView` (a `QWidget`) rather than `QGraphicsView`. Internally it composes a private `QGraphicsView *m_view` child via a zero-margin `QVBoxLayout`; mouse/wheel/context events route through an `eventFilter`, and `setFocusProxy(m_view)` preserves focus semantics.

**Effects visible to Corbomite:**
- `qobject_cast<QGraphicsView *>(editor)` no longer succeeds. If Corbomite does this anywhere, use `editor->viewport()` or the new forwarders below.
- `Editor` now exposes these QGraphicsView-shaped forwarders so existing call sites keep working: `scene()`, `viewport()`, `verticalScrollBar()`, `horizontalScrollBar()`, `mapToScene(QPoint)`, `mapToScene(QPointF)`, `mapFromScene(QPointF)`. Corbomite already uses none of these directly in `src/editor/`, but plugins that do will still work.
- `Markoff::Reading::ReadingView` made the same change (previously `QGraphicsView`, now `MarkdownView` composing a private `QGraphicsView`, with analogous forwarders).

### 5. Renamed member on `Markoff::Editor`

The old `Editor::document()` returned a `const MarkoffParser::Document *` (the parser AST). That clashed with `MarkdownView::document()` which must return `MarkoffDocument *`. Resolution: the parser-AST getter was renamed **`parsedDocument()`**. The new `document()` returns the attached `MarkoffDocument *` (nullptr in Phase B unless Corbomite opts in).

**Corbomite impact:** none in the current tree — no caller invokes the old `document()` getter. If a future caller needs the AST, use `parsedDocument()`.

### 6. Renamed member on `Markoff::Reading::ReadingView`

`ReadingView::foldedHeadings()` / `setFoldedHeadings()` used to take `QVector<int>` (source-line indices). Those conflicted with the `MarkdownView::foldedHeadings()` override returning `QVector<FoldSpec>`. Resolution: the line-vector API was renamed to `foldedHeadingLines()` / `setFoldedHeadingLines()`; the `MarkdownView` overrides now sit on top of them.

**Corbomite impact:** none in the current tree. Plugins or internal callers that used the old names must rename.

### 7. Return type change: `setReadOnly`

Both `Markoff::Editor::setReadOnly(bool)` and `Markoff::Source::SourceEditor::setReadOnly(bool)` now return `bool` instead of `void` (capability feedback per the `MarkdownView` contract). Return-value-ignoring call sites keep compiling; nothing to change in Corbomite.

### 8. New `MarkoffDocument::parseUpdated(const Document *)` signal

(Was `parsed(...)` in the plan; renamed at implementation time to avoid a moc name-collision with the `parsed()` getter.) Not consumed by Corbomite in Phase B.

---

## Stub repayment

Phase A imported readingview's sources with three Corbomite-side dependencies stubbed so markoff-reading links standalone:

| Stub package                      | What is stubbed                                                                              | Phase A consequence                       |
| --------------------------------- | -------------------------------------------------------------------------------------------- | ----------------------------------------- |
| `Corbomite::Core` (~7 symbols)    | `CodeBlockProcessorRegistry`, `EmbedRegistry`, `EmbedRequest`/`EmbedFactory`, `EmbedDepthGuard`, `MarkdownRenderChild`, `VaultResourceProvider`, `CodeBlockContext` | Mermaid returns empty; embed subpath slicing falls through; `MarkdownRenderChild::mountInto()` is a no-op |
| `Corbomite::Storage` (~4 symbols) | `CachedMetadata`, `LinkResolver`, `MetadataCache`, `MetadataParser`                          | Never actually called by the imported sources — stubs are inert. Keep them until Phase B proves they're dead. |
| `mmdr_ffi.h`                      | `mmdr_render_svg`, `mmdr_free`                                                               | Mermaid rendering disabled                |

Also: four reading tests marked `# TODO Phase B:` in `libs/markoff-reading/tests/CMakeLists.txt` — `tst_sectionlayout_mermaid`, `tst_readingview_mermaid_registered`, `tst_readingview_embedrenderer`, `tst_readingview_embed_builtins`.

**Phase B decides** per package:

- **Corbomite::Core**: Corbomite must keep providing the real types until they migrate into Markoff. The CorbomiteApp link line continues to include `Corbomite::Core`; the `Markoff::Reading` build falls back to the stubs *only* when Corbomite::Core is absent. See "Stub override mechanism" below.
- **Corbomite::Storage**: likely dead. If a clean Phase B build with `Markoff::Reading` linked into CorbomiteApp runs all tests green without a `Storage` link, drop the link + the stub.
- **mmdr**: Rust FFI crate lives in `/home/clinton/dev/Corbomite/libs/mmdr/`. Two options in Phase B: (a) vendor it into Markoff as `libs/mmdr/` (Rust toolchain requirement bleeds into Markoff), or (b) keep mmdr in Corbomite and have markoff-reading pick it up the same way it picks up `Corbomite::Core` — via the override mechanism.

### Stub override mechanism — CMake option in Phase B, DI seam in Phase C

**Phase B ships a CMake option.** A new option `MARKOFF_READING_USE_REAL_COREDEPS` (default OFF) flips Markoff's `libs/markoff-reading/` between stubs and Corbomite-provided real types at configure time. When ON: `target_include_directories` drops the stubs dir and `target_link_libraries(markoff_reading PRIVATE Corbomite::Core mmdr)` is added (Storage omitted — see below). CorbomiteApp turns this ON; standalone Markoff builds leave it OFF.

This is deliberately the smaller of the two options available. It reuses the stub headers Phase A already shipped, has near-zero refactor cost on the Markoff side, and gets Corbomite off its own readingview/qutepart-corbomite copies on a predictable timeline. The cost is a two-configuration build matrix (stubs-on, stubs-off) that only exercises fully across both codebases.

**Phase C replaces it with a dependency-injection seam.** Markoff will own abstract interfaces (`IEmbedRegistry`, `ICodeBlockProcessorRegistry`, `IVaultResourceProvider`, etc.); Corbomite writes adapters that implement them; the stub becomes the default no-op impl — one exercised code path instead of a build-mode switch. This is the correct long-term shape, and Phase C is already scoped to consolidate `Theme` / `ResourceProvider` / `LinkResolver` — the DI refactor touches the same files as that consolidation, so bundling them is one pass instead of two. Phase B's CMake option retires cleanly when Phase C lands.

Corbomite devs should treat the Phase B `MARKOFF_READING_USE_REAL_COREDEPS=ON` build as a temporary bridge, not a long-term API. Don't grow new Markoff-side code that hardcodes `Corbomite::Core::X` in response to this option — Phase C will reshape those call sites into interface dispatch. Adding new consumers should wait for Phase C or use the existing seven symbols only.

---

## Migration map — file by file

1. **`src/CMakeLists.txt:73–75`**
   ```diff
   - target_link_libraries(CorbomiteApp PRIVATE
   -     ...
   -     Markoff::Markoff
   -     Corbomite::ReadingView
   -     Corbomite::QutepartSource
   -     ...
   - )
   + target_link_libraries(CorbomiteApp PRIVATE
   +     ...
   +     Markoff::Live
   +     Markoff::Reading
   +     Markoff::Source
   +     ...
   + )
   ```
   Also enable `MARKOFF_READING_USE_REAL_COREDEPS` (or whatever the Phase B switch is named) above this target.

2. **`src/editor/MarkdownView.cpp:10–11`**
   ```diff
   - #include <corbomite/readingview/ReadingView.h>
   - #include <markoff/Editor.h>
   + #include <markoff/reading/ReadingView.h>
   + #include <markoff/Editor.h>
   ```
   References to `Corbomite::ReadingView::ReadingView` become `Markoff::Reading::ReadingView`.

3. **`src/editor/NoteEditorWidget.{h,cpp}`**
   - Header include renames as above.
   - `Corbomite::ReadingView::ReadingView` → `Markoff::Reading::ReadingView` in both member type and ctor.
   - Signal/slot wiring on `Markoff::Editor` is unchanged — all the signals Corbomite connects (`textChanged`, `cursorPositionChanged`, `wordCountChanged`, `linkClicked`, `linkHovered`, `completionDismissHint`) are untouched by Phase A.

4. **`src/editor/SourceEditor.{h,cpp}`** — no source change required. `#include <qutepart.h>` still resolves through `Markoff::Source`'s re-exposed include path. `Qutepart::Qutepart` is the same class. The only nudge is the CMake link in #1.

5. **Delete `libs/qutepart-corbomite/` and `libs/readingview/`** (entire directories) plus their entries in `Corbomite/libs/CMakeLists.txt` / `Corbomite/CMakeLists.txt`. Keep `libs/mmdr/` until the Phase B mmdr decision lands.

6. **`.gitmodules` + submodule pin** — bump `libs/markoff-family` to the merged Phase A SHA as the first commit of the Phase B branch, then iterate on (1)–(5) with the fresh headers in scope.

---

## Recommended ordering for the Phase B task list

1. **Markoff side, one commit on `master`**: add the `MARKOFF_READING_USE_REAL_COREDEPS` CMake option. Default OFF preserves the current standalone build (stubs in use). When ON, the stubs dir is dropped from `target_include_directories`, and `target_link_libraries(markoff_reading PRIVATE Corbomite::Core mmdr)` is added. No change to library sources. Tag the resulting SHA `v0.2.0`.
2. **Corbomite side**, one branch, commits in this order:
   a. Bump the `libs/markoff-family` submodule pin to Markoff `v0.2.0`.
   b. Set `MARKOFF_READING_USE_REAL_COREDEPS=ON` (in Corbomite's top-level CMakeLists, above the submodule's `add_subdirectory`).
   c. Update `src/CMakeLists.txt:73–75`: `Markoff::Markoff` → `Markoff::Live`; add `Markoff::Source` + `Markoff::Reading`; remove `Corbomite::QutepartSource` + `Corbomite::ReadingView`.
   d. Rename `Corbomite::ReadingView::` → `Markoff::Reading::` and `#include <corbomite/readingview/...>` → `#include <markoff/reading/...>` in `src/editor/MarkdownView.cpp` and `src/editor/NoteEditorWidget.{h,cpp}`.
   e. Build CorbomiteApp. Fix any test breakage — expect very little; the survey shows no other consumers.
   f. Delete `libs/qutepart-corbomite/` and `libs/readingview/` (entire directories) plus their CMake registration.
   g. Validate the `Corbomite::Storage` drop by doing one clean build without the old readingview CMakeLists' storage link. If green, no further action; if something breaks, keep Storage linked but document why.
   h. Commit.

---

## Coordination

**No freeze required on Corbomite.** The in-flight agent is in `src/plugins/bookmarks/`; the migration target is `src/editor/` + `src/CMakeLists.txt`. Zero overlap.

**Soft freeze on two directories:** once Phase A merges to Markoff `master`, the Corbomite-side copies `libs/qutepart-corbomite/` and `libs/readingview/` should be treated as read-only until deleted in Phase B. Any post-Phase-A fix to those directories is divergence that will have to be re-applied on the Markoff side before deletion. If an urgent fix is needed, land it in Markoff (`libs/markoff-source/` or `libs/markoff-reading/`) and cherry-pick back to Corbomite's copy; do not patch Corbomite's copy in isolation.

**`markoff-fold-v2` worktree** flagged by the survey is inside the Markoff submodule, not the Corbomite tree — it's a parallel Markoff branch. If its fixes touch files Phase A also touched (`libs/markoff/` → `libs/markoff-live/`), there will be a merge conflict when it rebases onto post-Phase-A `master`. That's a Markoff-side concern, not Corbomite's; raise it separately.

---

## Decisions recorded

1. **Switch mechanism**: CMake option `MARKOFF_READING_USE_REAL_COREDEPS` for Phase B. DI seam in Phase C (see "Stub override mechanism" above).
2. **mmdr ownership**: stays in Corbomite. Reason: dragging the Rust toolchain into Markoff is heavier than the override mechanism we're already using for `Corbomite::Core`. Corbomite's `libs/mmdr/` continues to provide the `mmdr_ffi.h` surface that markoff-reading's `MermaidRenderer` consumes; when `MARKOFF_READING_USE_REAL_COREDEPS=ON`, markoff-reading's CMakeLists adds an mmdr link and drops the `stubs/mmdr_ffi.h` shim from the include path. Phase C revisits if/when Mermaid rendering gets abstracted.
3. **`Corbomite::Storage` linkage**: drop it. Phase A survey confirmed readingview sources have zero direct `<corbomite/storage/...>` includes; the link is dead weight inherited from an older state. Phase B Task 1 validates this by building CorbomiteApp with `Corbomite::Storage` omitted; if that passes, delete `stubs/corbomite/storage/` and the corresponding target_link entry. Revert if anything breaks.
4. **Markoff version tag**: `v0.2.0`. Cut on the Markoff-side Phase A merge commit (after the `MARKOFF_READING_USE_REAL_COREDEPS` option lands). Corbomite's Phase B branch bumps the submodule pin to this tag in its first commit.

---

## Acceptance criteria for Phase B

- Corbomite submodule pin is on a Markoff SHA that includes Phase A + the stub override mechanism.
- `CorbomiteApp` builds and runs with zero references to `Corbomite::ReadingView::` or `Corbomite::QutepartSource`.
- `libs/qutepart-corbomite/` and `libs/readingview/` are removed from the Corbomite tree.
- Every Corbomite test passes at parity with pre-migration behavior (source editor, reading view, live editor all functional end-to-end including embeds and mermaid).
- `libs/markoff-reading/stubs/` is no longer compiled into CorbomiteApp's path (the real deps win).
- The four Phase-A-skipped reading tests re-enable cleanly under the real-deps build flag.
