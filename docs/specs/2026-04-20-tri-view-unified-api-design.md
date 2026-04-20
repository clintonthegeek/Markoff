# Tri-View Unified API — Design

**Spec written:** 2026-04-20. **Scope:** the whole `~/dev/Markoff/`
monorepo — a cross-cutting redesign, not a `libs/markoff/` feature.

## Goal

Make Markoff the canonical home for an Obsidian-style three-view markdown
widget family — **Live Preview**, **Source**, **Reading** — sharing a
single parser, theme, document, undo stack, and search stack. Corbomite
consumes the three widgets through a polymorphic `Markoff::MarkdownView`
base and deletes its downstream hacks (`libs/qutepart-corbomite/`,
`libs/readingview/`, `src/editor/SourceEditor`). Non-Corbomite Qt
consumers can pick widgets à la carte.

## Why this exists

Corbomite currently hosts two downstream libraries that were supposed to
be Markoff's responsibility but were built outside it because Markoff
wasn't ready:

- `libs/qutepart-corbomite/` — a fork of `qutepart-cpp` driving
  Corbomite's Source mode. Chosen over KTextEditor / QScintilla /
  stripped-Markoff after explicit evaluation (see archived plan
  `Corbomite/docs/superpowers/plans/archive/2026-04-14-cluster-e-markoff-three-mode-pivot.md`).
- `libs/readingview/` — a greenfield Reading-mode widget with
  virtualization, section recycling, async parse, and 5 ms / 10-section
  frame budget. Built because "LivePreview with `setReadOnly(true)`" —
  the only other option at the time — cannot scale to the 100 k-line
  notes Obsidian must handle.

Both libraries are well-chosen for their jobs and neither is disposable.
The problem is their *location*: they are parallel to Markoff rather
than part of it, so the three views ship as three unrelated codebases
that duplicate the parser, theme, link resolution, code highlighting,
math rendering, and search. That duplication has already caused drift
(three different `Theme` types, three different `CodeBlockHighlighter`s
being planned or shipped), and each consumer that wants the full family
has to do its own orchestration. Moving them into Markoff and adding a
thin polymorphic contract solves both problems at once.

## What is Not in Scope

- Re-evaluating the Qutepart decision. It stands.
- Rewriting ReadingView's pipeline. The virtualization / recycling /
  frame-budget architecture stays.
- Obsidian wire-format concerns (`workspace.json` `{mode, source}`
  encoding). That stays in Corbomite's `ViewModeSerializer`.
- Plugin parity with Obsidian. Out of scope entirely.

## Architecture

### Library layout

Under `~/dev/Markoff/libs/` after absorption:

| Library                   | CMake target            | Namespace                | Depends on                                                                       |
| ------------------------- | ----------------------- | ------------------------ | -------------------------------------------------------------------------------- |
| `libs/markoff-parser/`    | `MarkoffParser::MarkoffParser` | `Markoff::` (AST types) | Qt6::Core, tree-sitter                                                           |
| `libs/markoff-core/`      | `Markoff::Core`         | `Markoff::`              | Qt6::Widgets, Qt6::Gui, KF6::SyntaxHighlighting, JKQTMathText, MarkoffParser     |
| `libs/markoff-live/`      | `Markoff::Live`         | `Markoff::Live::`        | Core, Parser                                                                     |
| `libs/markoff-source/`    | `Markoff::Source`       | `Markoff::Source::`, internal `Qutepart::` | Core, Parser                                                   |
| `libs/markoff-reading/`   | `Markoff::Reading`      | `Markoff::Reading::`     | Core, Parser                                                                     |

`libs/markoff-live/` is today's `libs/markoff/` (renamed).
`libs/markoff-source/` is today's `Corbomite/libs/qutepart-corbomite/`.
`libs/markoff-reading/` is today's `Corbomite/libs/readingview/`.
`libs/markoff-core/` is new.

Consumer include forms:

```cpp
#include <markoff/MarkdownView.h>                    // abstract base
#include <markoff/MarkoffDocument.h>                 // shared document
#include <markoff/LivePreviewEditor.h>               // Markoff::Live
#include <markoff/source/SourceEditor.h>             // Markoff::Source
#include <markoff/reading/ReadingView.h>             // Markoff::Reading
```

Each leaf library is independently buildable (preserves Markoff's
encapsulation convention).

### `MarkdownView` — polymorphic contract

Lives in `markoff-core`. All three leaf widgets inherit it.

```cpp
namespace Markoff {

class MarkdownView : public QWidget {
    Q_OBJECT
public:
    // Content — canonical source lives on MarkoffDocument, not on the view.
    virtual void setDocument(MarkoffDocument *doc) = 0;
    virtual MarkoffDocument *document() const = 0;

    // Appearance and resources — shared types from markoff-core.
    virtual void setTheme(const Theme &theme) = 0;
    virtual void setResourceProvider(ResourceProvider *rp) = 0;
    virtual void setLinkResolver(LinkResolver *lr) = 0;

    // Scroll — visual-line float, every mode.
    virtual float scrollPosition() const = 0;
    virtual void setScrollPosition(float visualLine) = 0;

    // Zoom — every mode.
    virtual void zoomIn() = 0;
    virtual void zoomOut() = 0;
    virtual void resetZoom() = 0;

    // Ephemeral state — opaque per-view JSON blob.
    virtual QJsonObject ephemeralState() const = 0;
    virtual void setEphemeralState(const QJsonObject &) = 0;

    // Search adapter — see the Search section. Every mode provides one.
    virtual SearchAdapter *searchAdapter() = 0;

    // Capability probes — callers dispatch through these instead of RTTI.
    virtual bool hasCursor() const { return false; }
    virtual bool hasEditing() const { return false; }
    virtual bool hasFold() const { return false; }

    // Optional — default no-ops / capability-denied returns.
    virtual CursorPos cursorPosition() const { return {}; }
    virtual bool setCursorPosition(CursorPos) { return false; }
    virtual bool setReadOnly(bool) { return false; }
    virtual bool isReadOnly() const { return !hasEditing(); }
    virtual QVector<int> foldedHeadings() const { return {}; }
    virtual void setFoldedHeadings(const QVector<int> &) {}

Q_SIGNALS:
    void scrollPositionChanged(float visualLine);
    void cursorPositionChanged(CursorPos pos);   // only when hasCursor()
    void linkActivated(const QUrl &url);
};

}  // namespace Markoff
```

Design choices worth reading twice:

- **No `setPlainText` / `toPlainText` on the view.** Content lives on
  `MarkoffDocument`. This is what makes shared undo work and lets mode
  switches happen without reloading content.
- **Capability probes, not throws.** `hasCursor()`, `hasEditing()`,
  `hasFold()`. Reading returns false for all three. Callers dispatch
  cleanly without RTTI.
- **Unsupported-operation feedback.** `setReadOnly(false)` on a
  ReadingView returns `false` *and* logs a diagnostic. Silent no-ops
  hide consumer bugs; the log tells the Corbomite (or any consumer) dev
  they're calling something senseless.
- **Ephemeral state is opaque JSON.** Each view serialises whatever it
  needs (Source's fold line list, LivePreview's per-block scroll,
  Reading's section+offset). The `EphemeralState` transport struct in
  core carries the common fields (scroll, cursor, viewMode,
  foldedHeadings); the opaque blob lets each view round-trip extras
  without polluting the base class.

### `MarkoffDocument` — shared content + undo + parse

Owned by the consumer; views attach to it via `setDocument()`.

```cpp
namespace Markoff {

class MarkoffDocument : public QObject {
    Q_OBJECT
public:
    explicit MarkoffDocument(QObject *parent = nullptr);

    QString plainText() const;
    void setPlainText(const QString &text);            // resets undo

    // Native Qt access for widgets that bind directly.
    // Source: QPlainTextEdit::setDocument(doc->textDocument()).
    QTextDocument *textDocument() const;

    // Source-coordinate mutations — LivePreview commits per-block
    // edits as atomic ranges; ReplaceController writes here directly.
    // All routed through textDocument()'s native undo.
    void replace(int sourceOffset, int removeLen, const QString &insert);
    void insert(int sourceOffset, const QString &text);
    void remove(int sourceOffset, int len);

    // Transaction grouping — LivePreview calls begin/end around a
    // burst of per-block-document edits so they coalesce into one
    // undo entry.
    void beginTransaction();
    void endTransaction();

    // Keystroke-coalescing idle threshold for LivePreview auto-flush.
    void setCoalescingIdleMs(int ms);                  // default 500

    // Cached parse. Invalidated on contentsChanged, rebuilt on demand.
    // Sync if size < 10240 bytes, on a worker thread otherwise.
    const Document *parsed() const;                    // nullptr while pending
    bool parseIsPending() const;

Q_SIGNALS:
    void contentsChanged();
    void parsed(const Document *);
};

}  // namespace Markoff
```

**What this buys:**

1. **Single undo stack.** Source's `QPlainTextEdit` adopts
   `doc->textDocument()` via `setDocument()`; its Ctrl+Z *is* the
   central stack. LivePreview commits per-block deltas through
   `doc->replace(...)` inside `beginTransaction()/endTransaction()`.
   Reading doesn't edit. Mode switch preserves history because the
   stack lives on the document, not the widget. This also retires the
   TODO on cross-item undo coordination in LivePreview.
2. **Single parse.** `contentsChanged` triggers one parse; views consume
   the cached AST. Replaces the current reality where
   `SceneCoordinator::reparse()`, `ensureHeadingMap()`, and
   `Document::fromMarkdown()` each re-parse the whole doc independently.
   The 10240-byte async threshold moves from per-view to
   document-level.
3. **Views are stateless about content.** Attaching a view is
   `view->setDocument(doc)`; swapping views in a `QStackedWidget` costs
   no text reload.

**Transaction-boundary heuristic for LivePreview:** auto-flush its
per-block `editBlock` on `focusOut`, on mode-switch request, on any
non-typing action (toggle format, paste, cut, replace), and on 500 ms
idle after the last keystroke. Tunable via
`MarkoffDocument::setCoalescingIdleMs()`.

**Cursor restoration across mode switches.** `QTextDocument`'s undo
restores cursor to a source-offset. Source maps trivially. LivePreview
translates source-offset → (block-item, cursor-in-block) using the
block-item source-range bookkeeping that already exists. Reading ignores
cursor restore.

### `markoff-core` scope

Absorbed into core — types that today exist in triplicate:

| Type                            | Current home(s)                                                                            | Role in core                                                          |
| ------------------------------- | ------------------------------------------------------------------------------------------ | --------------------------------------------------------------------- |
| `MarkdownView`                  | n/a (new)                                                                                  | Abstract base                                                         |
| `MarkoffDocument`               | n/a (new)                                                                                  | Canonical text + shared undo + cached parse                           |
| `EphemeralState`                | `Corbomite::EphemeralState`                                                                | Transport struct for workspace.json round-trip                        |
| `Theme`                         | `Markoff::Theme`; readingview `StyleManager`/`ParagraphStyle`/`CharacterStyle`             | Unified theme                                                         |
| `ResourceProvider`              | `Markoff::ResourceProvider` + `readingview::VaultResourceProvider`                         | One abstract interface                                                |
| `LinkResolver`                  | `Markoff::LinkRenderer` + `readingview::LinkRenderer`                                      | Wiki/markdown link resolution strategy                                |
| `CodeHighlighter`               | Markoff inline KF6 wrapper + readingview `CodeBlockHighlighter` (Penelope) + Qutepart Kate-XML | Façade over KF6::SyntaxHighlighting                                 |
| `MathRenderer`                  | `Markoff::MathTextObject` + `readingview::ReadingMathObject`                               | Wraps JKQTMathText                                                    |
| `MermaidRenderer`               | `readingview::MermaidRenderer`                                                             | Wraps mmdr                                                            |
| `SearchController`              | n/a (new; walks `MarkoffDocument::plainText()`)                                            | Search engine, independent of view                                    |
| `ReplaceController`             | n/a (new; writes through `MarkoffDocument::replace()`)                                     | Replace engine; atomic undo via shared stack                          |
| `SearchBar`                     | `Markoff::SearchBar` (live lib today)                                                      | Default UI; replaceable by consumers                                  |
| `SearchAdapter`                 | n/a (new; interface)                                                                       | View-specific hook (highlight, scroll, cursor-origin)                 |
| `CursorPos`, `TextSpan`, `FoldSpec` | scattered                                                                               | Value types in the contract                                           |

Stays in the leaf libs:

- **Live:** `SceneCoordinator`, `MarkdownTextItem`, `ImageBlockItem`,
  `SelectableItem`, `SelectionManager`, `TextControl` (Qt fork), fold
  gutter visuals.
- **Source:** the whole `Qutepart::` namespace (vendored), fold
  calculator, indent engines.
- **Reading:** `ReadingSection`, `ReadingPipeline`, `SectionRecyclePool`,
  `VirtualScrollController`, `SectionLayout`, `EmbedRenderer`. The
  async parse worker *moves* to `MarkoffDocument`; the reading pipeline
  consumes the cached AST.

**Consolidation is opportunistic, not blocking.** The absorption step
moves code wholesale. Duplication collapses into shared primitives in
later tranches once each view's internals have settled in their new
home. This is what keeps the first move from turning into a rewrite.

**Non-goal: widget-lifecycle policy.** `markoff-core` does not own
mode-switch orchestration, `QStackedWidget` plumbing, or ephemeral-state
persistence. Those are consumer concerns (they live in Corbomite and
are documented for other consumers in the integration guide). This is
the bright line that keeps Markoff reusable.

### Search + replace

Search engine lives in core; UI is provided (replaceable) in core; each
view implements a small adapter.

```cpp
namespace Markoff {

class SearchAdapter {
public:
    virtual ~SearchAdapter() = default;
    virtual int cursorSourceOffset() const = 0;            // find-from-cursor origin
    virtual void highlightMatches(QVector<TextSpan>) = 0;
    virtual void clearMatchHighlight() = 0;
    virtual void scrollMatchIntoView(TextSpan) = 0;
    virtual bool supportsReplace() const { return true; }  // Reading returns false
};

class SearchController : public QObject {
    Q_OBJECT
public:
    SearchController(MarkoffDocument *doc, SearchAdapter *adapter,
                     QObject *parent = nullptr);

    struct Flags { bool caseSensitive = false; bool wholeWord = false;
                   bool regex = false; bool wrap = true; };
    void setFlags(Flags);
    void setQuery(const QString &);

    int  matchCount() const;
    int  currentIndex() const;
    void next();
    void prev();

Q_SIGNALS:
    void matchesChanged();
    void currentMatchChanged(int index);
};

class ReplaceController : public SearchController {
    Q_OBJECT
public:
    void replaceCurrent(const QString &with);
    int  replaceAll(const QString &with);            // returns count
};

}  // namespace Markoff
```

`ReplaceController::replaceAll` writes through
`MarkoffDocument::replace()` inside one
`beginTransaction()/endTransaction()` pair — so it is one undo step,
and it naturally retires the current `replaceAll` TODO about
cross-item undo coordination.

`Markoff::SearchBar` in core is the default UI, driven by
`SearchController`. Consumers who want a different chrome (ribbon,
modal, VS-Code overlay) instantiate `SearchController` directly and
build their own UI. The three leaf libraries do not ship search UI.

## Migration path

Phased so Corbomite keeps building at every step.

### Phase A — Absorb into `~/dev/Markoff/` (no Corbomite changes)

1. Copy `Corbomite/libs/qutepart-corbomite/` → `Markoff/libs/markoff-source/`
   with history preserved (`git subtree add` or `git format-patch` replay,
   whichever keeps the audit trail cleaner). Rename CMake target to
   `Markoff::Source`; namespace the public API as `Markoff::Source::*`.
2. Copy `Corbomite/libs/readingview/` → `Markoff/libs/markoff-reading/`,
   same treatment; namespace becomes `Markoff::Reading::*`.
3. Create `Markoff/libs/markoff-core/` with `MarkdownView.h`,
   `MarkoffDocument.{h,cpp}`, `EphemeralState.h`, and stubs for
   `Theme`, `ResourceProvider`, `LinkResolver`, `CodeHighlighter`,
   `MathRenderer`, `SearchController`, `SearchAdapter`, `SearchBar`.
   No logic migrated yet; just the header surface.
4. Rename `Markoff/libs/markoff/` → `Markoff/libs/markoff-live/`.
   `markoff-live` now depends on `markoff-core`.
5. Each leaf widget inherits `MarkdownView` — mostly forwarding to
   existing APIs, no internal refactor.
6. Each leaf lib's existing tests keep passing.

### Phase B — Flip Corbomite to external consumption

7. Delete `Corbomite/libs/markoff-family/` (the submodule). Corbomite's
   top CMake does `find_package(Markoff CONFIG REQUIRED)` or
   `add_subdirectory(../Markoff)` depending on how dev builds are
   shaped.
8. Delete `Corbomite/libs/qutepart-corbomite/` and
   `Corbomite/libs/readingview/`. Update includes:
   `<corbomite/readingview/ReadingView.h>` → `<markoff/reading/ReadingView.h>`;
   `"qutepart/qutepart.h"` becomes internal to `Markoff::Source`.
9. `Corbomite/src/editor/SourceEditor.{h,cpp}` collapses — Corbomite
   uses `Markoff::Source::SourceEditor` directly.
10. `NoteEditorWidget` becomes a polymorphic `Markoff::MarkdownView *`
    stacked widget. The lazy-construct + flush/restore logic stays
    (consumer policy) but is expressed through the shared base.
11. Corbomite's `EphemeralState` is replaced by the one in
    `markoff-core`. `ViewModeSerializer` stays in Corbomite (wire-format
    policy is not Markoff's concern).

### Phase C — Consolidate duplication

Done in tranches. No strict ordering.

12. Unify the three `Theme`-ish types into `Markoff::Theme`. Each view
    reads from it instead of its own copy.
13. Unify `CodeBlockHighlighter` + Markoff inline KF6 wrapper into
    `Markoff::CodeHighlighter`.
14. Unify `LinkRenderer` across live + reading.
15. Unify `MathTextObject` + `ReadingMathObject` behind
    `Markoff::MathRenderer`.
16. Move async-parse worker from `ReadingParseWorker` onto
    `MarkoffDocument`. Reading pipeline consumes the cached AST.

Phase C is months-long and opportunistic. Nothing in A/B blocks on it.

### Phase D — Consumer documentation

17. Write `Markoff/docs/integration-guide.md` covering: constructing a
    `MarkoffDocument`, attaching views, switching modes, persistence
    conventions, `ResourceProvider` / `LinkResolver` contracts, search
    integration, theme installation. Aimed at Corbomite but written
    for any Qt consumer.
18. Corbomite adopts it; internal Corbomite docs link out rather than
    duplicate.

## Testing

- **Each leaf lib keeps its suites.** markoff-live today has ~40
  tests; markoff-source and markoff-reading bring theirs. These are
  the primary regression safety net during Phase A absorption.
- **`markoff-core/tests/`:** `tst_markoff_document` (plain-text ↔ undo
  ↔ transaction boundaries ↔ parse-worker threshold),
  `tst_search_controller` (match walk, next/prev/wrap, case/regex/
  whole-word flags, replace-all via document),
  `tst_ephemeral_state` (JSON round-trip against Obsidian fixtures),
  `tst_theme` (moved from live).
- **Cross-mode integration tests** live at `Markoff/tests/` (new top
  level): undo survives mode switch (type in Source, switch to
  LivePreview, Ctrl+Z undoes the Source edit), visual-line scroll
  round-trips across all three views within ±0.5 line after reflow,
  ephemeral state serialise → switch → deserialise is byte-stable.
- **Corbomite Phase B tests** verify polymorphic dispatch through
  `MarkdownView*` and the Obsidian `{mode, source}` wire-format
  round-trip. No Markoff internals tested from Corbomite.
- **Tests define behavior** — when a test fails, fix the code. Markoff
  convention.

## Definition of Done

- Four peer libraries + `markoff-core` build standalone in
  `~/dev/Markoff/`. Each has its own tests.
- `Markoff::MarkdownView` is the only type Corbomite holds by pointer
  when orchestrating modes.
- `Ctrl+Z` undoes an edit made in the previous mode after a switch.
- `MarkoffDocument` serves the AST; each view consumes the cached
  parse. The three-parses-per-reparse reality is gone.
- Corbomite's `libs/qutepart-corbomite/` and `libs/readingview/` are
  deleted.
- `docs/integration-guide.md` is written and Corbomite cites it.
- Obsidian `workspace.json` `{mode, source, eState}` round-trip still
  passes byte-stably.

## Risks and Unknowns

- **Qutepart's `setDocument()` compatibility.** Source-mode undo
  unification assumes the Qutepart QPlainTextEdit subclass tolerates an
  externally-owned `QTextDocument`. If Qutepart's internal state
  assumes ownership, we may need a shim that mirrors edits both ways.
  Verified during Phase A before committing to the model.
- **LivePreview source-offset ↔ block-item translation fidelity.**
  Already used by cursor placement and reveal logic, but undo restore
  stresses it differently (the cursor needs to land exactly where the
  user expects after an undo across a block boundary). Needs dedicated
  tests.
- **History preservation across absorption.** `git subtree add` vs.
  `git format-patch` replay have different audit-trail shapes. Pick
  during Phase A task 1; document the choice in the commit trail.
- **Parser cache invalidation timing.** If a view reads `parsed()`
  while the worker is mid-parse, it gets `nullptr`. Views either check
  `parseIsPending()` and defer their work, or connect to
  `parsed(const Document*)` and react when the AST lands. Which
  pattern each view picks is a view-local choice, documented per view.
