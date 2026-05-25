# Markoff foundation — design

**Date:** 2026-04-28
**Status:** Draft, pending user review
**Branch:** `exploration/new-foundation` (worktree at `.worktrees/foundation-exploration`)
**Predecessor input:** [`docs/2026-04-28-codebase-audit.md`](../2026-04-28-codebase-audit.md)
**Tracked work-units:** none yet — implementation plan to follow
**Master is unaffected.** This branch is exploratory; the existing Markoff family on master continues to be maintained for CorbomiteApp.

---

## 1. TL;DR

The existing Markoff family has accumulated significant architectural debt concentrated in `markoff-live` (per the audit: bandage-saturated `Editor`, leaky `MarkdownView` base contract, three Themes, two `LinkRenderer`s with one orphaned, code-block highlighting fragmented across leaves, hot paths logging on every keystroke). Rather than refactoring each pain point in place, this spec proposes a **hard reset on the upper part of the stack** while salvaging and refining the bottom.

The new foundation is a single library — `markoff-foundation` — built directly on top of `~/dev/collabtext`'s CRDT engine. It owns canonical text, AST, theme, links, selections, sessions, edit commands, search, completion detection, and code-block services. It owns *no rendering*. Views are subscribers — anything that connects to its signals and dispatches edits via `Markoff::Cmd::*` commands. Multiple views can attach to one document; the foundation tracks each view's session-level state (cursor, selection, scroll, fold) so views are hot-swappable and CRDT presence is a natural extension of the same model.

The design is validated by **one running proof-of-concept view** (a QML simple editor, no live preview, ~1,200–1,700 LOC) plus **three paper sketches** (Qt Widget, TUI, WebEngine) showing how other view backends would consume the foundation without building them.

The foundation is Heavy-mode CRDT from day one: no `InMemoryCanonicalBuffer` layer to throw away, no abstract `CanonicalBuffer` interface. CollabText's `Buffer` is the storage; CollabText's `Anchor` is the cursor primitive. Single-user use is just the trivial-replication case of multi-user use.

---

## 2. Goals and non-goals

### Goals

1. **Replace the leaky `MarkdownView` contract** with one that doesn't reference leaf-defined types (today's `Theme` / `ResourceProvider` / `LinkResolver` are forward-declared in `markoff-core` but defined in `markoff-live`).
2. **Eliminate cross-leaf duplication** of theming, search adapters, link rendering, inline span detection, fold representation, math rendering, and code-block dispatch.
3. **Move off the QGraphicsView + Qt-private-`TextControl` architecture** that has produced six consecutive typing-triggered SEGVs in soak (per audit §2.5). The new POC uses standard `TextArea` (Qt Quick Controls); future views can pick their own toolkit.
4. **Make views pluggable across toolkits** — Qt Widget, QML, TUI, and (if forced) WebEngine should be feasible without forcing the foundation to grow a toolkit-specific surface.
5. **Make CRDT-backed editing the default**, not a future bolt-on. The collabtext engine is small, well-tested, and the affordances it requires (anchors, replica-aware undo, version vectors) are useful even single-user.
6. **Validate the foundation with running code** — a QML POC view that proves the API binds to a non-current-Live toolkit.

### Non-goals

1. **Replacing the existing Markoff family on master.** Master continues to ship for CorbomiteApp under the current Phase C plan. This work is on a worktree branch, exploratory.
2. **Implementing CRDT replication / network transport.** Heavy mode adopts the CRDT primitives, but transport (Syncthing, network sync, file watching) is a host-level concern out of scope for this spec.
3. **Building a full live-preview editor.** The POC is a *simple* editor; live-preview-formatted rendering is a future view's problem and not designed here. Doing it right requires a different rendering model than the current scene-graph-of-`MarkdownTextItem` approach, but the design of that view is deferred.
4. **Migrating CorbomiteApp to the new foundation.** Migration sequencing is a follow-up phase predicated on this exploration succeeding.
5. **Deciding whether to retire the existing Markoff family.** That's a future decision based on what this exploration produces.
6. **Building Reading-, Source-, and TUI-equivalent views in this phase.** Those exist as paper sketches only.

---

## 3. Motivation

The full audit is at [`docs/2026-04-28-codebase-audit.md`](../2026-04-28-codebase-audit.md). For the purposes of this spec, the relevant points are:

- **`markoff-live`'s `Editor` is bandage-saturated.** Six consecutive soak-week fix tags (`v0.6.0-alpha.3` through `.8`); two unresolved architectural flaws (key-dispatch recursion guarded by an `m_inKeyPressEvent` bandage; cursor-drift defended by a `rectForPosition` clamp); shipped `qCWarning` instrumentation on every keystroke. The architecture spec at `docs/specs/2026-04-21-editor-key-dispatch-architecture.md` documents the path forward but the file is large and the fix touches every key-driven feature.
- **The `MarkdownView` base contract leaks types** — `Theme`, `ResourceProvider`, `LinkResolver` are forward-declared in `markoff-core` but defined in `markoff-live`. Any consumer using the polymorphic base must include a leaf header, which inverts the abstraction.
- **The `TextControl` Qt-private fork is 2,596 lines** with no upstream. Cursor-drift, IME composition, and CJK autocorrect bugs all live inside it; the audit calls it the single largest "we don't fully understand what's happening" risk in the codebase.
- **Cross-leaf duplication is structural, not incidental.** Three Theme types (`Markoff::Theme` in live, `Qutepart::Theme` in source, ad-hoc enum in reading), two `LinkRenderer` classes (one orphaned), three different inline-span walkers, three different fold representations, two of three views not consulting the `CodeBlockProcessorRegistry`. Every consolidation work-unit on the Phase C roadmap addresses one or two of these in isolation; in aggregate they reflect that the *contract* is wrong, not just that individual pieces are messy.

Fixing the existing stack in place would require executing C2 (theme consolidation), C4 (renderer unification), the editor key-dispatch architecture spec, and a `TextControl` migration — sequenced over weeks, with each step risking regression in CorbomiteApp. The hard-reset path puts the architectural decisions in one place and lets master continue shipping with its known issues until or unless the new foundation supersedes it.

---

## 4. Architecture overview

```
  HOSTS (CorbomiteApp, test app, future apps)
    Compose: instantiate document + services + view(s); manage lifecycle.
    │
    ▼
  VIEWS (any number, any toolkit, in any process)
    ┌─────────────────┐  ┌───────────────────┐  ┌──────────────────┐
    │  QML editor     │  │  Qt Widget editor │  │  TUI viewer      │
    │  (POC)          │  │  (sketch)         │  │  (sketch)        │
    └────────┬────────┘  └────────┬──────────┘  └────────┬─────────┘
             │                    │                      │
             │  subscribe to MarkoffDocument signals     │
             │  call Markoff::Cmd::* functions           │
             │  hold their own Selection                 │
             │  bind a LinkService implementation        │
             ▼                    ▼                      ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  FOUNDATION  (markoff-foundation)                               │
  │                                                                 │
  │   MarkoffDocument         Markoff::Cmd::          Theme         │
  │   - canonical text         - toggleBold            (Q_GADGET    │
  │   - parsed AST             - toggleItalic          value type)  │
  │   - applyLocalEdit         - setHeading                         │
  │   - applyRemoteOps         - insertTable           Selection,   │
  │   - undo / redo            - insertLink            Anchor,      │
  │   - sessions               - ...                   FoldRef      │
  │   - signals                                       (value types) │
  │                                                                 │
  │   LinkService            SearchEngine,           SyntaxHighlight│
  │   (interface)            ReplaceController       Service        │
  │                                                                 │
  │   CompletionDetector     CompletionRegistry      CodeBlock-     │
  │   CompletionProvider     (host-extensible)       ProcessorRegistry│
  └────────────────────┬────────────────────────────────────────────┘
                       │ uses
                       ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  collabtext  (`~/dev/collabtext`, sibling library)              │
  │  CollabText::Crdt::Buffer   - canonical bytes (UTF-8)           │
  │  CollabText::Crdt::Anchor   - stable position handle            │
  │  CollabText::Crdt::Operation - CRDT op (broadcast unit)         │
  │  CollabText::Crdt::TextEdit - post-apply edit description       │
  │                                                                 │
  │  markoff-parser  (existing, salvaged unchanged)                 │
  │  TreeSitterParser, Document, YamlValue, MarkdownSplitter        │
  └─────────────────────────────────────────────────────────────────┘
```

### Layer responsibilities

**Hosts** (CorbomiteApp, test apps): instantiate `MarkoffDocument`s, services (`SyntaxHighlightService`, `CodeBlockProcessorRegistry`, `LinkService`, `CompletionRegistry`), and themes; choose which views are active; manage layout; serialize sessions; in CRDT-extended futures, marshal remote ops.

**Views**: render the canonical text + AST + theme; handle user input; dispatch edits via `Markoff::Cmd::*` and update their session state. Views own no canonical state. Views are **subscribers** — anything that connects to foundation signals counts; views are not a base class but a usage pattern.

**Foundation**: the canonical truth and the operations on it. Owns text, AST, theme, links, selections, sessions, edits, search, completion, code-block services. Knows nothing about toolkits.

**collabtext**: CRDT engine. The foundation uses its `Buffer` as the canonical text storage; its `Anchor` as the cursor primitive; its `Operation` as the CRDT-broadcast unit. The engine is Qt-free and could be replaced if needed, but it's the deliberate choice: well-tested, offline-first, designed for exactly this use case.

**markoff-parser**: tree-sitter wrapper, salvaged from the existing family unchanged.

### Key shape decisions (full rationale in §14)

- View ≠ subclass. Views subscribe to signals; they are not required to inherit any base class.
- Sessions live on the document. Each view's per-document state (cursor, selections, scroll, folds) is a `Session` owned by `MarkoffDocument`.
- Heavy CRDT mode from day one. `CollabText::Crdt::Buffer` is the storage; no in-memory abstraction layer.
- Foundation owns commands, not views. `Markoff::Cmd::toggleBold(doc, selection)` is a free function that builds a `MarkoffEdit` and applies it. Views don't reimplement editing logic.
- Code-block highlighting and special-block processing are foundation services. Three views = same code-block treatment by default.
- Completion detection is foundation; candidate sources are host-provided.

---

## 5. Repository organization

### New libraries (added on this branch)

```
libs/
  markoff-foundation/      (NEW)  — the foundation
  markoff-view-qml/        (NEW)  — POC view
```

### Existing libraries (unchanged on this branch)

```
libs/
  markoff-parser/          existing — depended on by markoff-foundation
  markoff-core/            existing — master-maintained, untouched
  markoff-live/            existing — master-maintained, untouched
  markoff-source/          existing — master-maintained, untouched
  markoff-reading/         existing — master-maintained, untouched
```

### External dependency

```
libs/
  collabtext/              symlink → /home/clinton/dev/collabtext/
```

Symlinked for the worktree (mirrors the existing `libs/jkqtmathtext` precedent on master). A standalone build documents the expected sibling-directory layout.

### Top-level CMakeLists changes

```cmake
if(NOT TARGET collabtext)
    add_subdirectory(libs/collabtext)
endif()
add_subdirectory(libs/markoff-parser)        # existing
add_subdirectory(libs/markoff-core)    # NEW
add_subdirectory(libs/markoff-view-qml)      # NEW
# Existing markoff-core / markoff-live / markoff-source / markoff-reading
# add_subdirectory() lines remain so the existing test suite continues to build.
```

### Naming rationale

- **`markoff-foundation`** rather than reusing `markoff-core`: the existing `markoff-core` is master-maintained and lives in the same tree; using a distinct name avoids any conflict and makes it explicit that the new library is not a refactor of the old one but a replacement.
- **`markoff-view-<toolkit>`** for view backends: future siblings would be `markoff-view-widget`, `markoff-view-tui`, `markoff-view-webengine`. The `view-` prefix signals "pluggable view backend, not foundational."

### Spec output

This document lives at `docs/specs/2026-04-28-foundation-design.md` (the existing repo spec convention). Plan files for implementation will land at `docs/plans/`.

---

## 6. Salvage and migration plan

The audit identified what's worth keeping; this section is the file-by-file accounting.

### 6.1 Salvaged from existing `markoff-core` (transplanted with refinement)

```
From libs/markoff-core/  →  libs/markoff-core/

include/markoff/
  MarkdownDelta.h           →  refactored as MarkoffEdit + helpers; no longer
                                a QUndoCommand (undo lives in Buffer).
  MarkoffDocument.h         →  heavy refactor — wraps CollabText::Crdt::Buffer.
  ParsePool.h               →  same shape, takes UTF-8 input.
  TextSpan.h                →  same shape, byte offsets are UTF-8.
  SearchController.h        →  same shape, operates on Session.
  ReplaceController.h       →  same shape, operates on Session.

src/
  MarkoffDocument.cpp       →  heavy refactor.
  ParsePool.cpp             →  minor (UTF-8 in).
  ParsePoolWorker.{h,cpp}   →  same.
  SearchController.cpp      →  reshape to session-driven.
  ReplaceController.cpp     →  reshape to session-driven.
```

### 6.2 Dropped from existing `markoff-core`

These are replaced by collabtext primitives or by new design:

```
include/markoff/
  CanonicalBuffer.h         dropped — Buffer is the only impl.
  CursorPos.h               dropped — replaced by CollabText::Crdt::Anchor.
  CursorPosition.h          dropped — replaced by Anchor.
  MarkdownView.h            dropped — no view base class in the new design.
  EphemeralState.h          dropped — replaced by Session.
  FoldSpec.h                dropped — replaced by FoldRef (anchor-based).
  SearchAdapter.h           dropped — replaced by session-driven search.
  EmbedRegistry.h           not foundation; Reading-specific concern, future view
  CodeBlockProcessorRegistry.h  refactored (different shape; toolkit-agnostic output)
  EmbedDepthGuard.h         not foundation
  MarkdownRenderChild.h     not foundation
  MermaidRenderer.h         not foundation; processor lives in registry
  DefaultMermaidRenderer.h  not foundation
  vault/                    not foundation; host concern

src/
  InMemoryCanonicalBuffer.{h,cpp}  dropped — Buffer is the impl.
  CursorPosition.cpp        dropped.
```

### 6.3 New foundation files

```
libs/markoff-core/
  CMakeLists.txt
  include/markoff-foundation/
    MarkoffDocument.h
    Session.h
    Selection.h           — kinded selection value type
    FoldRef.h             — anchor-based fold reference
    MarkoffEdit.h         — wrapper over CollabText::Crdt::TextEdit
    Origin.h              — reset origin enum
    Theme.h               — Q_GADGET value type, slot enum
    LinkService.h         — abstract interface + DefaultLinkService
    LinkActivation.h      — value type
    Cmd.h                 — aggregate include
    Cmd/InlineFormat.h
    Cmd/Block.h
    Cmd/Insert.h
    Cmd/Edit.h
    CommandFacade.h       — Q_OBJECT facade for QML
    SearchEngine.h
    ReplaceController.h
    SyntaxHighlightService.h  — interface
    Kf6SyntaxHighlightService.h  — default impl
    CodeBlockProcessor.h      — interface
    CodeBlockProcessorRegistry.h
    RenderedBlock.h           — value type
    CompletionTrigger.h       — enum + CompletionContext
    CompletionDetector.h      — static API
    CompletionCandidate.h     — value type
    CompletionProvider.h      — interface
    CompletionRegistry.h
    EmojiCompletionProvider.h — default provider
    MarkoffServices.h         — host-owned bundle struct
  src/
    [implementations]
  tests/
    [unit tests, see §11]
```

### 6.4 Salvaged from existing `markoff-parser` (unchanged)

`markoff-parser` is left alone. The foundation depends on it as today.

### 6.5 Migration sequence (commit ordering)

These are the commit milestones for implementing this spec. Implementation plan (separate doc) numbers the tasks; this is the structural ordering only:

1. Scaffold `libs/markoff-core/` (CMakeLists, headers, no impl).
2. Wire the `libs/collabtext/` symlink and dependency.
3. Drop in `MarkoffEdit`, `Anchor` aliasing, `Origin` enum.
4. Implement `MarkoffDocument` over `Buffer` (sync construction, signals, applyLocalEdit, applyRemoteOps, undo/redo, resetContent).
5. Add Session lifecycle on `MarkoffDocument`.
6. Add `Selection`, `FoldRef` value types.
7. Add `Theme` value type with default light/dark presets.
8. Add `LinkService` + `DefaultLinkService`.
9. Add `Markoff::Cmd::*` namespace functions (start with inline format; add block, insert, edit incrementally).
10. Add `CommandFacade` for QML.
11. Add `SearchEngine` + `ReplaceController` (session-driven).
12. Add `SyntaxHighlightService` (interface) + `Kf6SyntaxHighlightService` (default impl).
13. Add `CodeBlockProcessorRegistry` and `RenderedBlock`.
14. Add `CompletionDetector`, `CompletionRegistry`, `CompletionProvider`, `EmojiCompletionProvider`.
15. Foundation unit tests (per §11).
16. Scaffold `libs/markoff-view-qml/`.
17. Implement `EditorBackend`, `EditorHighlighter`, `SearchBackend`.
18. Implement `MarkoffEditor.qml` + `SearchBar.qml`.
19. POC test app at `libs/markoff-view-qml/app/`.
20. POC tests (per §11).

Each commit on master-style "spec:" / "feat(foundation):" / "feat(view-qml):" prefix. The branch is append-only.

---

## 7. Foundation API

This is the largest section. Sub-sections describe each public type or service.

### 7.1 Data model — `MarkoffDocument`

The single source of truth for canonical text + AST + sessions.

```cpp
namespace Markoff {

class MarkoffDocument : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarkoffDocument)
public:
    /// Construct a fresh document. The replicaId is the CRDT identity
    /// for this MarkoffDocument instance; for single-user use, a random
    /// quint16 is fine. For sync scenarios the host provides a stable
    /// per-machine or per-identity value.
    explicit MarkoffDocument(quint16 replicaId, QObject *parent = nullptr);

    ~MarkoffDocument() override;

    // ===== Reads =====
    QByteArray toMarkdownUtf8() const;            // Buffer::text()
    QString    toMarkdown() const;                // convenience: UTF-8 → QString
    quint32    visibleLength() const;             // UTF-8 byte length
    const Markoff::Document *parsedDocument() const;
    bool       parseIsPending() const;

    // ===== CRDT identity =====
    quint16                       replicaId() const;
    CollabText::Crdt::Global      version() const;

    // ===== Local writes =====
    /// Apply a local edit. The edits list (all in OLD-text byte coordinates)
    /// is consolidated into a single Buffer::apply_local_edit call, producing
    /// one Operation. Pushed onto Buffer's replica-aware undo stack. Emits
    /// contentsChanged. Returns the Operation for broadcast (CRDT future).
    CollabText::Crdt::Operation
        applyLocalEdit(const QList<MarkoffEdit> &edits);

    // ===== Undo / redo =====
    std::optional<CollabText::Crdt::Operation> undo();
    std::optional<CollabText::Crdt::Operation> redo();
    int  undoDepth() const;
    bool coalesceLastUndo();   // editor calls when keystrokes should group

    // ===== Remote ops =====
    /// Apply remote operations. Causal ordering and dedup handled by Buffer.
    /// Emits contentsChanged with the resulting MarkoffEdits in old-text coords.
    void applyRemoteOps(const std::vector<CollabText::Crdt::Operation> &ops);

    // ===== Wholesale reload =====
    /// External-reload paths (file changed on disk, test fixture, revert).
    /// Resets the Buffer to a new replica with given content. Emits
    /// documentReloaded.
    void resetContent(const QByteArray &newContent, Origin origin);

    // ===== Anchors =====
    /// Pass-through to Buffer's anchor APIs.
    CollabText::Crdt::Anchor
        anchorAt(quint32 byteOffset, CollabText::Crdt::Bias bias) const;
    quint32 resolveAnchor(const CollabText::Crdt::Anchor &) const;

    // ===== Sessions =====
    Session *createSession(const SessionParams &params = {});
    void     destroySession(Session *);
    QList<Session *> sessions() const;
    Session *sessionForParticipant(const QString &participantId) const;

    // ===== Garbage collection =====
    qsizetype collectGarbage();
    qsizetype compact(const CollabText::Crdt::Global &watermark);

    // ===== Coalescing tuning =====
    void setCoalescingIdleMs(int ms);
    int  coalescingIdleMs() const;

Q_SIGNALS:
    /// Emitted whenever Buffer content changes (local or remote).
    /// edits is in OLD-text UTF-8 byte coordinate space; size>=1, may be
    /// non-adjacent for batched remote ops.
    void contentsChanged(QList<Markoff::MarkoffEdit> edits);

    /// Emitted after the parse pool has refreshed the AST. Debounced.
    void parseUpdated(const Markoff::Document *parsed);

    /// Emitted after resetContent.
    void documentReloaded();

    void sessionCreated(Markoff::Session *);
    void sessionDestroyed(Markoff::Session *);
};

}  // namespace Markoff
```

#### `MarkoffEdit`

Surgical edit in OLD-text byte coordinates. Wraps `CollabText::Crdt::TextEdit` with QObject-side-friendly types.

```cpp
struct MarkoffEdit {
    quint32    oldStart = 0;     // UTF-8 byte offset in OLD visible text
    quint32    oldEnd = 0;       // UTF-8 byte offset; oldEnd >= oldStart
    QByteArray newText;          // UTF-8 bytes; empty for pure deletion

    bool isInsertion() const { return oldStart == oldEnd; }
    bool isDeletion()  const { return newText.isEmpty(); }
    bool isReplacement() const { return !isInsertion() && !isDeletion(); }

    QJsonObject toJson() const;
    static MarkoffEdit fromJson(const QJsonObject &);
};
```

#### `Origin` enum (carried over from C3)

```cpp
enum class Origin {
    FirstOpen,              // empty undo stack, no command pushed
    ExternalReloadClean,    // file changed on disk; no local pending edits
    ExternalReloadResolved, // post-merge-modal resolution
    UserRevertToSaved,      // pushes one mega edit so Ctrl+Z reverses it
    TestFixture,
};
```

### 7.2 `Session`

A view's per-document state. Each view that attaches to a document creates a session. Two simultaneous views = two sessions on the same document. Hot-swap = `copyStateFrom`.

```cpp
struct SessionParams {
    QString participantId;     // for CRDT presence — empty = local
    QString participantLabel;
    QColor  presenceColor;
};

class Session : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Session)
public:
    QString id() const;
    QString participantId() const;
    QString participantLabel() const;
    QColor  presenceColor() const;

    // Primary edit selection
    Selection primarySelection() const;
    void      setPrimarySelection(const Selection &);

    // Secondary selections (kinded; multi-cursor + search hits + presence)
    const QList<Selection> &secondarySelections() const;
    void setSecondarySelections(QList<Selection>);
    void addSecondarySelection(Selection);
    void clearSecondarySelectionsOfKind(Selection::Kind);

    // Scroll — anchor-bound, cross-toolkit-portable
    CollabText::Crdt::Anchor topVisibleAnchor() const;
    qreal                    topVisibleFraction() const;
    void setTopVisible(CollabText::Crdt::Anchor, qreal fraction);

    // Folds — anchor-based, survive concurrent edits
    const QList<FoldRef> &foldedRegions() const;
    void                  setFoldedRegions(QList<FoldRef>);
    void                  toggleFold(const FoldRef &);

    // Hot-swap helper: copy ephemeral state from another session
    // (primary sel, secondaries, scroll, folds). View-toolkit-specific
    // opaque state does not transfer.
    void copyStateFrom(const Session &other);

    // Persistence
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &);

Q_SIGNALS:
    void primarySelectionChanged(const Markoff::Selection &);
    void secondarySelectionsChanged();
    void scrollChanged(CollabText::Crdt::Anchor, qreal fraction);
    void foldedRegionsChanged();
};
```

### 7.3 `Selection`

```cpp
struct Selection {
    enum class Kind {
        Primary,        // editable, the typing one (one per session)
        Secondary,      // editable, multi-cursor (commands apply to all)
        SearchMatch,    // not editable, search controller manages
        Presence,       // not editable, remote-session indicator
    };

    CollabText::Crdt::Anchor anchor;       // typically left-bias
    CollabText::Crdt::Anchor active;       // typically right-bias; cursor head
    Kind                     kind = Kind::Primary;

    // Only meaningful for Kind::Presence
    QString participantId;
    QString participantLabel;
    QColor  presenceColor;
    quint64 cursorVersion = 0;             // for view dedup of remote cursor renders

    bool isEmpty() const;
    bool isReversed() const;

    QJsonObject toJson() const;
    static Selection fromJson(const QJsonObject &);
};
```

`CollabText::Crdt::Anchor` is used directly (not aliased). It already has `Bias::Left` / `Bias::Right` semantics matching what we want. For serialization the foundation provides JSON helpers (added per §10's WebEngine sketch finding).

### 7.4 `FoldRef`

```cpp
struct FoldRef {
    enum class Kind {
        Heading,    // fold heading + everything until next heading at <= level
        Block,      // fold a specific block range
    };

    Kind                      kind = Kind::Heading;
    CollabText::Crdt::Anchor  start;       // survives concurrent edits
    QString                   headingPath; // for Heading: ["Intro", "Ch 1", ...]
    int                       headingLevel = 0;

    QJsonObject toJson() const;
    static FoldRef fromJson(const QJsonObject &);
};
```

The anchor + heading path together survive concurrent CRDT edits and arbitrary parse drift. Audit §3.6's "FoldSpec is too lossy" is resolved here.

### 7.5 `Theme`

Toolkit-independent semantic slots. Each view translates to its native styling primitive.

```cpp
class Theme {
    Q_GADGET
public:
    enum class Slot {
        // Text foreground
        TextDefault,
        Heading1, Heading2, Heading3, Heading4, Heading5, Heading6,
        InlineCode, CodeBlock,
        Link, WikiLink, Tag, Math,
        Quote,
        BoldEmphasis, ItalicEmphasis, StrikeEmphasis,
        Highlight,
        // Decorations
        SelectionBackground,
        CursorPrimary, CursorSecondary, CursorPresence,
        SearchMatchBackground, SearchActiveMatchBackground,
        // Surfaces
        EditorBackground,
        GutterBackground,
        CodeBlockBackground,
        QuoteBackground,
        CalloutNote, CalloutWarning, CalloutTip,
        CalloutImportant, CalloutCaution,
        // Code-token colors (applied to CodeSpans returned by SyntaxHighlightService)
        CodeKeyword, CodeControlFlow, CodeBuiltin,
        CodeType, CodeFunction, CodeVariable, CodeConstant,
        CodeOperator, CodePunctuation,
        CodeString, CodeNumber, CodeBoolean,
        CodeComment, CodeDocumentation,
        CodePreprocessor, CodeAnnotation,
        // Chrome
        FoldArrow,
        ScrollbarThumb,
    };
    Q_ENUM(Slot)

    enum class FontRole { Body, Monospace, Heading };
    Q_ENUM(FontRole)

    QColor color(Slot) const;
    void   setColor(Slot, QColor);

    QFont  font(FontRole) const;
    void   setFont(FontRole, QFont);

    bool   isBold(Slot) const;
    bool   isItalic(Slot) const;
    qreal  fontSizeMultiplier(Slot) const;

    // Convenience: map CodeTokenKind to Slot (Code* family)
    QColor colorForCodeToken(CodeTokenKind) const;

    static Theme defaultLight();
    static Theme defaultDark();

    QJsonObject toJson() const;
    static Theme fromJson(const QJsonObject &);
};
```

`Q_GADGET` not `Q_OBJECT` — the theme is a value type, copyable, equality-comparable, naturally bindable in QML via property assignment. Theme changes propagate by the host calling `setTheme(theme)` on each view.

### 7.6 `LinkService`

Abstract interface; the host or a view provides an implementation. Replaces the audit's two competing `LinkRenderer` classes.

```cpp
enum class LinkKind {
    Unknown,
    External,    // http://, https://, mailto:, etc.
    File,        // local file path or file://
    WikiLink,    // [[note title]] or [[note#anchor]]
    Tag,         // #tag
    Anchor,      // in-document #heading-id
};

struct LinkActivation {
    QString  rawText;
    QUrl     resolvedTarget;
    LinkKind kind = LinkKind::Unknown;
    QString  anchorHint;      // for WikiLink: part after #
    QString  fromContext;     // host fills in (note path / identity)
};

class LinkService : public QObject {
    Q_OBJECT
public:
    /// Cheap classification (used for syntax highlighting).
    virtual LinkKind classify(const QString &linkText) const = 0;

    /// Resolve a link to a target. May return invalid QUrl.
    virtual QUrl resolve(const QString &linkText,
                         const QString &fromContext = {}) const = 0;

    /// Default: emits linkActivated. Override for custom routing.
    virtual void activate(const LinkActivation &);

    virtual void notifyHover(const LinkActivation &, const QPoint &globalPos);
    virtual void notifyHoverLeft(const QString &linkText);

Q_SIGNALS:
    void linkActivated(const Markoff::LinkActivation &);
    void linkHovered(const Markoff::LinkActivation &, const QPoint &globalPos);
    void linkHoverLeft(const QString &linkText);
};

class DefaultLinkService : public LinkService {
    // Classifies http/https/mailto as External; everything else as Unknown.
    // resolve() returns the literal QUrl. Hosts override for richer behavior.
};
```

Views call into `LinkService` directly when the user clicks/hovers a link. Sessions don't carry link state — links are stateless dispatch.

### 7.7 Commands — `Markoff::Cmd::`

Two-layer namespace. Pure-function "compute the edits" (testable; used in QML facade and in tests). Convenience "compute and apply" wrappers (used 99% of the time in C++).

```cpp
namespace Markoff::Cmd {

// === Pure functions (testable) ===
QList<MarkoffEdit> editsForToggleBold(const MarkoffDocument *, const Selection &);
QList<MarkoffEdit> editsForToggleItalic(const MarkoffDocument *, const Selection &);
QList<MarkoffEdit> editsForToggleStrikethrough(const MarkoffDocument *, const Selection &);
QList<MarkoffEdit> editsForToggleInlineCode(const MarkoffDocument *, const Selection &);
QList<MarkoffEdit> editsForSetHeading(const MarkoffDocument *, const Selection &, int level);
QList<MarkoffEdit> editsForToggleCheckbox(const MarkoffDocument *, const Anchor &);
QList<MarkoffEdit> editsForBlockQuote(const MarkoffDocument *, const Selection &);
QList<MarkoffEdit> editsForInsertTable(const MarkoffDocument *, const Anchor &,
                                       int rows, int cols, bool hasHeader);
QList<MarkoffEdit> editsForInsertLink(const MarkoffDocument *, const Anchor &,
                                       const QString &linkText, const QString &target);
QList<MarkoffEdit> editsForInsertImage(const MarkoffDocument *, const Anchor &,
                                        const QString &alt, const QString &target);
QList<MarkoffEdit> editsForInsertHorizontalRule(const MarkoffDocument *, const Anchor &);

// === Convenience: compute + apply ===
CollabText::Crdt::Operation toggleBold(MarkoffDocument *, const Selection &);
CollabText::Crdt::Operation toggleItalic(MarkoffDocument *, const Selection &);
// ... matching pair for each editsFor*

// === Multi-cursor helper ===
void applyToAllPrimaryAndSecondaries(
    MarkoffDocument *doc,
    Session *session,
    std::function<QList<MarkoffEdit>(const MarkoffDocument *, const Selection &)> editsFn);

}  // namespace Markoff::Cmd
```

Headers split by category: `Cmd/InlineFormat.h`, `Cmd/Block.h`, `Cmd/Insert.h`, `Cmd/Edit.h` (undo/redo wrappers). `Cmd.h` aggregates.

#### `CommandFacade` for QML

```cpp
class CommandFacade : public QObject {
    Q_OBJECT
    Q_PROPERTY(MarkoffDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Session *session READ session WRITE setSession NOTIFY sessionChanged)
public:
    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleStrikethrough();
    Q_INVOKABLE void toggleInlineCode();
    Q_INVOKABLE void setHeading(int level);
    Q_INVOKABLE void toggleCheckbox();
    Q_INVOKABLE void blockQuote();
    Q_INVOKABLE void insertTable(int rows, int cols, bool hasHeader = true);
    Q_INVOKABLE void insertLink(const QString &linkText, const QString &target);
    Q_INVOKABLE void insertImage(const QString &alt, const QString &target);
    Q_INVOKABLE void insertHorizontalRule();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

Q_SIGNALS:
    void documentChanged();
    void sessionChanged();
};
```

QML usage: `CommandFacade { id: cmd; document: theDoc; session: thisView.session }` and `Button { onClicked: cmd.toggleBold() }`.

### 7.8 Search

Session-driven, no `SearchAdapter` interface.

```cpp
class SearchEngine : public QObject {
    Q_OBJECT
public:
    enum class FindFlag {
        CaseSensitive  = 0x01,
        WholeWords     = 0x02,
        Regex          = 0x04,
        Backwards      = 0x08,
    };
    Q_DECLARE_FLAGS(FindFlags, FindFlag)

    /// Populates session->secondarySelections with Kind::SearchMatch entries
    /// (anchor-bound; survive concurrent edits). Returns count.
    int  findAll(MarkoffDocument *, Session *, const QString &needle, FindFlags = {});

    /// Cycle. Sets session->primarySelection to the next/prev match.
    bool findNext(MarkoffDocument *, Session *);
    bool findPrevious(MarkoffDocument *, Session *);

    void clearMatches(Session *);
};

class ReplaceController : public QObject {
    Q_OBJECT
public:
    std::optional<CollabText::Crdt::Operation>
        replaceCurrent(MarkoffDocument *, Session *, const QString &replacement);

    struct ReplaceAllResult {
        int count = 0;
        std::optional<CollabText::Crdt::Operation> op;
    };
    ReplaceAllResult
        replaceAll(MarkoffDocument *, Session *, const QString &replacement);
};
```

Views render `secondarySelections()` of `Kind::SearchMatch` however they like (highlighted backgrounds, scrollbar markers, etc.). The "current" match is whichever one matches `primarySelection`.

### 7.9 Code-block services

#### `SyntaxHighlightService`

```cpp
enum class CodeTokenKind {
    Default, Keyword, ControlFlow, Builtin,
    Type, Function, Variable, Constant,
    Operator, Punctuation,
    String, Number, Boolean,
    Comment, Documentation,
    Preprocessor, Annotation,
};

struct CodeSpan {
    quint32       offset;     // UTF-8 byte offset within the code block content
    quint32       length;     // UTF-8 byte length
    CodeTokenKind kind = CodeTokenKind::Default;
};

class SyntaxHighlightService : public QObject {
    Q_OBJECT
public:
    virtual QList<CodeSpan> highlight(const QString &language,
                                      const QByteArray &contentUtf8) const = 0;
    virtual QStringList     availableLanguages() const = 0;
    virtual bool            supportsLanguage(const QString &lang) const = 0;
};

/// Default impl. Wraps KF6::SyntaxHighlighting. The foundation hard-depends
/// on KF6::SyntaxHighlighting; this matches existing reality across the
/// Markoff family.
class Kf6SyntaxHighlightService : public SyntaxHighlightService { ... };
```

#### `CodeBlockProcessor` and `CodeBlockProcessorRegistry`

```cpp
struct RenderedBlock {
    enum class Kind {
        Image,         // QImage; works in Widgets, QML, WebEngine
        Svg,           // QString of SVG markup
        Highlighted,   // QList<CodeSpan> for processors that re-tokenize
        Empty,         // signal "I can't render this"; view falls back
    };

    Kind             kind = Kind::Empty;
    QImage           image;
    QString          svg;
    QList<CodeSpan>  spans;
    QSize            preferredSize;
    QString          fallbackText;
};

class CodeBlockProcessor {
public:
    virtual ~CodeBlockProcessor() = default;
    virtual QString       language() const = 0;
    virtual RenderedBlock render(const QByteArray &contentUtf8,
                                 const Theme &theme) = 0;
};

class CodeBlockProcessorRegistry : public QObject {
    Q_OBJECT
public:
    void registerProcessor(std::shared_ptr<CodeBlockProcessor>);
    void unregisterProcessor(const QString &language);
    std::shared_ptr<CodeBlockProcessor> processorFor(const QString &language) const;
    QStringList registeredLanguages() const;

Q_SIGNALS:
    void processorRegistered(const QString &);
    void processorUnregistered(const QString &);
};
```

#### Resolution order (every view)

When a view encounters a code block with language `lang`:

1. **Special processor first.** Ask the registry. If found, render via it; the view displays whatever `RenderedBlock::kind` says.
2. **Falls through to syntax highlighting.** No processor → `SyntaxHighlightService::highlight(lang, content)`. Apply colors per `Theme::colorForCodeToken`.
3. **Falls through to monospace plain.** Empty / unsupported lang → render as monospace text in `Theme::Slot::CodeBlock` color.

### 7.10 Completion

Foundation owns trigger detection (AST-aware); host owns candidate sources.

```cpp
enum class CompletionTrigger {
    None,
    WikiLink,           // [[…
    WikiLinkAnchor,     // [[name#…
    Tag,                // #… (body context, not heading marker)
    Footnote,           // [^…
    Emoji,              // :…
    Mention,            // @…
    SlashCommand,       // /… at line start
    LinkPath,           // ](… inside a link
    ImagePath,          // ![]( inside an image
};

struct CompletionContext {
    CompletionTrigger        trigger = CompletionTrigger::None;
    QString                  prefix;
    CollabText::Crdt::Anchor triggerStart;
    CollabText::Crdt::Anchor cursorAnchor;
    QString                  anchorContext;   // for WikiLinkAnchor
    bool isActive() const { return trigger != CompletionTrigger::None; }
};

class CompletionDetector {
public:
    /// Cheap; called on every cursor move. AST-aware.
    static CompletionContext
        detect(const MarkoffDocument *, const CollabText::Crdt::Anchor &cursor);
};

struct CompletionCandidate {
    QString  display;
    QString  insertion;
    QString  detail;
    QString  iconName;
    int      priority = 0;
};

class CompletionProvider : public QObject {
    Q_OBJECT
public:
    virtual QSet<CompletionTrigger> handledTriggers() const = 0;

    /// Sync path: return immediately. Async path: return empty + emit
    /// candidatesReady(requestId, ...) later.
    virtual QList<CompletionCandidate>
        candidatesFor(const CompletionContext &, quint64 requestId) = 0;

Q_SIGNALS:
    void candidatesReady(quint64 requestId, QList<CompletionCandidate>);
};

class CompletionRegistry : public QObject {
    Q_OBJECT
public:
    void registerProvider(std::shared_ptr<CompletionProvider>);
    void unregisterProvider(CompletionProvider *);
    QList<CompletionCandidate>
        gather(const CompletionContext &, quint64 requestId);

Q_SIGNALS:
    void candidatesReady(quint64 requestId, QList<CompletionCandidate>);
};

/// Default emoji provider; ~600 emojis baked in. Optional; hosts can replace.
class EmojiCompletionProvider : public CompletionProvider { ... };
```

### 7.11 `MarkoffServices` bundle

A simple struct the host populates and passes to views, so views don't reach for global singletons:

```cpp
struct MarkoffServices {
    SyntaxHighlightService     *syntax = nullptr;
    CodeBlockProcessorRegistry *codeProcessors = nullptr;
    LinkService                *links = nullptr;
    CompletionRegistry         *completion = nullptr;
};
```

Pointers are non-owning; host owns lifetimes.

---

## 8. View integration contract

A view is anything that subscribes to foundation signals and dispatches edits via commands. There is no required base class.

### 8.1 Lifecycle

```
1. Host constructs document and services.
2. Host constructs view, hands it doc + services + theme.
3. View creates a session via doc->createSession().
4. View subscribes to doc + session signals.
5. View renders initial state from doc->toMarkdownUtf8() and doc->parsedDocument().
6. Steady state: input → Cmd::* → doc->applyLocalEdit → signals → re-render.
7. Teardown: unbind session; doc->destroySession() (or retain for hot-swap).
```

### 8.2 Threading model

- **Main thread** owns all views, `MarkoffDocument`, `Buffer`, all signals. Single-threaded by convention; foundation is not lock-protected.
- **Parse worker thread** runs `ParsePoolWorker`; emits `parseUpdated` back to main thread via `QMetaObject::invokeMethod`.
- Multi-threaded use (e.g., remote ops from a network thread) requires posting to the main thread first.

### 8.3 Host responsibilities

1. Construct `MarkoffDocument` with a replica ID. Random `quint16` for in-process; persistent identity for sharing.
2. Construct services (`SyntaxHighlightService`, `CodeBlockProcessorRegistry`, `LinkService`, `CompletionRegistry`). Register host-specific code processors and completion providers.
3. Construct theme; propagate to views.
4. Instantiate views; manage layout (mode-swap is QStackedWidget-equivalent).
5. Handle hot-swap (§8.5).
6. Persist sessions to disk via `Session::toJson` / `fromJson` if the app wants per-document UI state.
7. (CRDT future) Marshal remote ops between transport and `applyRemoteOps`.

The foundation does NOT do any of these.

### 8.4 What a view MUST, MAY, CAN do

**MUST:**
- Subscribe to `MarkoffDocument::contentsChanged` and update display.
- Subscribe to `MarkoffDocument::parseUpdated` if it consumes AST-derived structure.
- Bind to a `Session`.
- Update `session->primarySelection()` on cursor moves.
- Update `session->setTopVisible(...)` when scrolled.
- Use UTF-8 byte offsets when communicating with the foundation; convert to/from the toolkit's native unit at the view's boundary.

**MAY:**
- Dispatch edits via `Markoff::Cmd::*` (only if editable).
- Render `Kind::SearchMatch` selections (search support).
- Render `Kind::Presence` selections (CRDT/multi-user awareness).
- Handle `Kind::Secondary` (multi-cursor).
- Maintain `Session::foldedRegions()` (only if fold-capable).
- Render code blocks via the registry + highlight service.
- Show completion popups via `CompletionDetector` + registry.

**MUST NOT:**
- Modify canonical text bypassing `applyLocalEdit` / commands.
- Hold references to other views or assume their existence.
- Spin up its own parser instance.

### 8.5 Hot-swap protocol

```cpp
void Host::switchToView(View *newView) {
    Session *outgoing = m_activeView ? m_activeView->session() : nullptr;
    Session *incoming = doc->createSession();
    if (outgoing) incoming->copyStateFrom(*outgoing);

    newView->bindSession(incoming);
    if (m_activeView) m_activeView->unbindSession();
    if (outgoing) doc->destroySession(outgoing);

    m_layout->setCurrentWidget(newView);
    m_activeView = newView;
}
```

Survives the swap: cursor + selections + scroll + folds (all canonical-anchored).
Does not survive: per-view ephemeral state (e.g., open find-bar text) — view-internal concerns, not session state.

### 8.6 Read-only vs editable

There's no read-only flag in the foundation. A read-only view simply doesn't dispatch edit commands and ignores text-input events. Capability is by composition, not by flag.

---

## 9. POC: QML simple editor

The running-code component of the exploration. Validates the foundation API binds cleanly to a non-current-Live toolkit.

### 9.1 Scope

In scope:
- Document binding (QML reads canonical text via signals).
- Edit dispatch (Cmd::* through QML facade).
- Session-based cursor and selection.
- Theme application via Q_GADGET property bindings.
- AST consumption (markdown source highlighting).
- Undo/redo via Buffer's replica-aware stack.
- Search via `SearchEngine`.
- Code-block highlighting via `SyntaxHighlightService` (minimal).
- Link activation via `LinkService` (basic).
- Emoji completion popup.

Out of scope:
- Live-preview formatting (no in-place rendering of `**bold**` as bold).
- Inline image / table / math rendering.
- Multi-cursor / presence rendering (foundation supports them; POC doesn't exercise).
- Mermaid / special-block processors (registry wired; nothing registered in POC).
- Fold UI.
- CRDT transport.

### 9.2 File layout

```
libs/markoff-view-qml/
  CMakeLists.txt
  include/markoff/view/qml/
    EditorBackend.h        — Q_OBJECT bridge between QML TextArea + MarkoffDocument
    EditorHighlighter.h    — QSyntaxHighlighter for code-block content
    SearchBackend.h        — Q_OBJECT wrapper around SearchEngine for QML
    CompletionPopupModel.h — Q_OBJECT wrapper around CompletionRegistry for QML
    QmlTypes.h             — qmlRegisterType declarations
  src/
    EditorBackend.cpp
    EditorHighlighter.cpp
    SearchBackend.cpp
    CompletionPopupModel.cpp
    QmlTypes.cpp
  qml/
    MarkoffEditor.qml      — main editor: TextArea + bindings + key handling
    SearchBar.qml          — embedded find/replace
    CompletionPopup.qml    — emoji popup
  tests/
    tst_editor_backend.cpp
    tst_editor_highlighter.cpp
    tst_qml_editor_integration.cpp
  app/
    CMakeLists.txt
    main.cpp
    Main.qml
```

### 9.3 Architecture

```
                    ┌─────────────── QML ──────────────────┐
                    │  MarkoffEditor.qml                   │
                    │  TextArea + Theme bindings           │
                    │  CommandFacade (Q_INVOKABLE)         │
                    │  SearchBar.qml + CompletionPopup.qml │
                    └───────────────┬──────────────────────┘
                                    │ property + Q_INVOKABLE
                                    ▼
                    ┌────────── EditorBackend (C++) ──────┐
                    │  - holds *MarkoffDocument           │
                    │  - holds *Session                   │
                    │  - exposes QQuickTextDocument *     │
                    │  - UTF-8 ↔ UTF-16 bridge            │
                    │  - cycle guards                     │
                    │  - cursor sync to session           │
                    └───────────────┬─────────────────────┘
                                    ▼
                          markoff-foundation
```

### 9.4 `EditorBackend`

The load-bearing class. Mirrors collabtext's `CollabPlainTextEdit` and marknote's `DocumentHandler` patterns; adapted for QML's `TextArea` (which exposes `QQuickTextDocument *textDocument`).

```cpp
class EditorBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(MarkoffDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Session *session READ session NOTIFY sessionChanged)
    Q_PROPERTY(QQuickTextDocument *qtQuickDocument READ qtQuickDocument
               WRITE setQtQuickDocument NOTIFY qtQuickDocumentChanged)
    Q_PROPERTY(Theme theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition
               NOTIFY cursorPositionChanged)
    Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart
               NOTIFY selectionStartChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd
               NOTIFY selectionEndChanged)
public:
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

private slots:
    void onContentsChanged(const QList<MarkoffEdit> &);
    void onParseUpdated(const Markoff::Document *);
    void onQtContentsChange(int qtPos, int charsRemoved, int charsAdded);

private:
    int     byteOffsetToQtPos(quint32 byteOff) const;
    quint32 qtPosToByteOffset(int qtPos) const;

    MarkoffDocument   *m_doc = nullptr;
    Session           *m_session = nullptr;
    QQuickTextDocument *m_qtQuickDoc = nullptr;
    EditorHighlighter *m_highlighter = nullptr;
    bool               m_applyingRemoteEdit = false;
    bool               m_applyingLocalEdit = false;
    Theme              m_theme;
};
```

**Cycle guards** are essential: when applying a foundation-emitted edit to QTextDocument, set `m_applyingRemoteEdit = true` so the resulting `onQtContentsChange` doesn't loop back. Same pattern collabtext's `CollabPlainTextEdit` uses.

**UTF-8 ↔ UTF-16 conversion** at the boundary: foundation deals in UTF-8 byte offsets; QTextDocument deals in UTF-16 code units. Conversion functions cribbed from collabtext's `CollabPlainTextEdit`.

### 9.5 `EditorHighlighter`

`QSyntaxHighlighter` subclass attached to the QTextDocument inside `QQuickTextDocument`. Handles **code-block content** highlighting (the language inside ```` ```python ```` etc.). For **markdown source** highlighting (heading colors, bold runs, code spans), we use the existing `org.kde.syntaxhighlighting`'s QML `SyntaxHighlighter` component:

```qml
SyntaxHighlighter {
    textEdit: editor
    definition: "Markdown"
}
```

KF6's QML component handles markdown source colorization (proven by marknote's `RawEditPage.qml`). `EditorHighlighter` only handles code-block content via `SyntaxHighlightService`. Saves substantial custom highlighting code.

### 9.6 `MarkoffEditor.qml`

```qml
Rectangle {
    property MarkoffDocument document
    property Theme theme
    color: theme.color(Theme.EditorBackground)

    TextArea {
        id: textArea
        anchors.fill: parent
        font: theme.font(Theme.Body)

        Keys.onPressed: (event) => {
            if (event.modifiers & Qt.ControlModifier) {
                if (event.key === Qt.Key_B)      cmd.toggleBold();
                else if (event.key === Qt.Key_I) cmd.toggleItalic();
                else if (event.key === Qt.Key_F) searchBar.show();
                else if (event.key === Qt.Key_Z) editorBackend.undo();
                else if (event.key === Qt.Key_Y) editorBackend.redo();
                else return;
                event.accepted = true;
            }
        }
    }

    EditorBackend {
        id: editorBackend
        document: parent.document
        qtQuickDocument: textArea.textDocument
        theme: parent.theme
        cursorPosition: textArea.cursorPosition
        selectionStart: textArea.selectionStart
        selectionEnd: textArea.selectionEnd
    }

    SyntaxHighlighter {
        textEdit: textArea
        definition: "Markdown"
    }

    CommandFacade {
        id: cmd
        document: parent.document
        session: editorBackend.session
    }

    SearchBar { id: searchBar; ... }
    CompletionPopup { id: completionPopup; ... }
}
```

### 9.7 Test app

```cpp
// libs/markoff-view-qml/app/main.cpp
QGuiApplication app(argc, argv);

auto syntax = std::make_shared<Kf6SyntaxHighlightService>();
auto codeProcessors = std::make_shared<CodeBlockProcessorRegistry>();
auto links = std::make_shared<DefaultLinkService>();
auto completion = std::make_shared<CompletionRegistry>();
completion->registerProvider(std::make_shared<EmojiCompletionProvider>());
MarkoffServices services { syntax.get(), codeProcessors.get(),
                           links.get(), completion.get() };

quint16 replicaId = QRandomGenerator::global()->generate() & 0xFFFF;
auto doc = std::make_unique<MarkoffDocument>(replicaId);
doc->resetContent(loadFile(argv[1]), Origin::FirstOpen);

QQmlApplicationEngine engine;
engine.rootContext()->setContextProperty("doc", doc.get());
engine.rootContext()->setContextProperty("services", QVariant::fromValue(services));
engine.rootContext()->setContextProperty("theme", QVariant::fromValue(Theme::defaultLight()));
engine.load("qrc:/Main.qml");
return app.exec();
```

### 9.8 LOC estimate

| File group | LOC |
|---|---|
| `EditorBackend.{h,cpp}` | 450–550 |
| `EditorHighlighter.{h,cpp}` | 100–150 (smaller — KF6 QML covers markdown source) |
| `SearchBackend.{h,cpp}` | 100–150 |
| `CompletionPopupModel.{h,cpp}` | 80–120 |
| `QmlTypes.cpp` | 30 |
| QML files | 250–350 |
| `app/main.cpp` + `Main.qml` | 100–150 |
| Tests | 300–400 |
| **Total** | **1,400–1,900** |

---

## 10. Playbook sketches for other view backends

Paper sketches only — these are validation artifacts, not implementations. If a sketch hits a foundation-API smell, that's a design issue to fix. None did.

### 10.1 Qt Widget view (`markoff-view-widget`)

Source-mode equivalent in classical Qt Widgets without QGraphicsView (the audit's pain).

- Subclass `QPlainTextEdit`. Mirror collabtext's `CollabPlainTextEdit`.
- Hold `Session *`. Bridge via the same UTF-8/UTF-16 conversions.
- `QSyntaxHighlighter` subclass for source highlighting; consult `SyntaxHighlightService` for code blocks.
- Multi-cursor + presence: reuse `CollabText::Ui::MultiCursorController` near-verbatim; foundation's `Session` secondary selections feed it.
- Estimated LOC for working build: 1,000–1,500.
- **Foundation API issues exposed: none.**

### 10.2 TUI view (`markoff-view-tui`)

Validates that foundation works without `QTextDocument` infrastructure.

- Built on Tui Widgets (`https://tuiwidgets.namepad.de`) — Qt6-based terminal UI library.
- Subclass `Tui::ZWidget`. Subscribe to foundation signals.
- Maintain an in-memory row buffer of styled cells. On `contentsChanged`, recompute affected rows.
- Theme: `Theme::color(Slot)` returns `QColor`; map to `Tui::ZColor` (24-bit RGB or 256-color fallback). Bold/italic from `Theme::isBold/isItalic` map to terminal escape attributes.
- Cursor: convert byte offset → grapheme cluster → terminal cell column (wcwidth via Tui).
- Code blocks: `RenderedBlock::Image` falls back to `fallbackText`; `Highlighted` spans applied with ANSI colors.
- Estimated LOC: 800–1,200 read-only; ~2,000 with editing.
- **Foundation API issues exposed: none.** Verified `SourceSpan` ranges are byte-based (TUI translates to cells); foundation never assumes `1 byte = 1 column`.

### 10.3 WebEngine view (`markoff-view-webengine`)

The "ugh" option. If foundation API survives this, it'll survive anything.

- `QWebEngineView` hosts a page; page has a JS layer subscribing to a C++ bridge via `QWebChannel`.
- Foundation types serialize through QWebChannel as JSON. `MarkoffEdit` becomes `{offset, removed, inserted}`; `Anchor` becomes `{replicaId, charValue, bias}`.
- Theme: `Theme::toJson()` → CSS variables injected into page via `style` element. `:root { --color-heading-1: ... }`.
- Edits dispatched from DOM `beforeinput` events; mapped to byte offsets via DOM walk; routed to `applyLocalEdit` through bridge.
- Selection model mismatch (HTML `Selection` vs `Anchor + Anchor`) handled by JS-side translation layer.
- Estimated LOC: 1,500–2,500 (highest of the three sketches).
- **One foundation API change surfaced:** `Anchor::toJson() / fromJson()` for serialization. Trivial; included in §7.3.

### 10.4 What the sketches prove

| Concern | Verdict |
|---|---|
| Foundation requires `QTextDocument`? | **No** — TUI sketch builds without it. |
| Foundation requires Qt Widgets? | **No** — TUI uses Tui Widgets; WebEngine is JS. |
| Foundation API serializes? | **Yes**, with the small `Anchor::toJson` addition. |
| Foundation forecloses async dispatch? | **No** — all signals async-safe. |
| Foundation enforces a single editing paradigm? | **No** — Widget / TUI / WebEngine all use input → Cmd::* shape. |

No deal-breakers. The foundation API as designed composes with all three sketched toolkits.

---

## 11. Testing strategy

### 11.1 Foundation unit tests

Each piece of the foundation gets its own test target. Sized to run in single-digit seconds total.

```
tst_markoff_document          — applyLocalEdit, applyRemoteOps, undo/redo,
                                signals, coalesceLastUndo, setCoalescingIdleMs
tst_session                   — primary + secondaries, scroll, folds, copyStateFrom
tst_selection                 — kinds, isEmpty, isReversed, equality, JSON roundtrip
tst_fold_ref                  — value type, anchor binding, JSON roundtrip
tst_anchor_serialization      — Anchor toJson/fromJson roundtrip
tst_markoff_edit              — value type validation, JSON roundtrip
tst_parse_pool                — async, debounce, cancel-on-destroy
tst_theme                     — slot lookup, fonts, defaults, JSON roundtrip
tst_link_service              — classify, resolve, activate (DefaultLinkService)
tst_cmd_inline_format         — toggleBold/Italic/Strike/InlineCode (pure)
tst_cmd_block                 — setHeading, toggleCheckbox, blockQuote (pure)
tst_cmd_insert                — insertTable/Link/Image/HR (pure)
tst_search_engine             — findAll, findNext/Previous, clearMatches
tst_replace_controller        — replaceCurrent, replaceAll
tst_syntax_highlight_service  — language list, basic Kf6 backing
tst_code_block_processor_registry  — register/unregister/lookup
tst_completion_detector       — wikilink/tag/emoji/footnote/mention edge cases
tst_completion_registry       — multi-provider gather, async candidatesReady
```

Fixture markdown files at `libs/markoff-core/tests/fixtures/`. Mocks for `CompletionRegistry` (fake provider). Real `CollabText::Crdt::Buffer` with seeded ops where determinism is needed.

### 11.2 POC tests

```
tst_editor_backend            — UTF-8↔UTF-16 across multibyte chars,
                                edit dispatch QML→foundation,
                                edit application foundation→QML,
                                cycle guards, cursor sync
tst_editor_highlighter        — code-block highlighting on fixture, theme respect
tst_qml_editor_integration    — Qt Test for QML; loads Main.qml offscreen,
                                types into TextArea, asserts foundation state
```

### 11.3 Property-based tests

Lightweight, one per library:

- `tst_markoff_document_property` — random sequence of `applyLocalEdit`; assert `doc->toMarkdown()` always equals reference text computed independently. ~100 sequences per CI run.
- `tst_editor_backend_property` — random QML-side text edits; assert MarkoffDocument state always matches QTextDocument state at each step.

### 11.4 Out of scope

- CRDT replication / multi-replica convergence (tested upstream by collabtext).
- Live-preview rendering correctness (POC has no live preview).
- Cross-toolkit hot-swap (only one real view exists).
- Performance benchmarks (rely on collabtext's reported numbers; smoke test in QML integration).
- Network transport / file sync (host concern).
- GC under heavy load (tested upstream).

### 11.5 Test environment

- `ctest --output-on-failure` from build directory.
- `QT_QPA_PLATFORM=offscreen` on CI.
- All foundation tests are pure C++ + Qt Test.
- POC integration test loads QML via `QQmlApplicationEngine` offscreen.

---

## 12. Acceptance criteria

The exploration is accepted as **viable** when:

1. ✅ All foundation unit tests pass.
2. ✅ POC backend tests pass.
3. ✅ POC QML integration test passes (TextArea round-trips edits, theme changes propagate, cursor stays in sync).
4. ✅ POC test app (`./build-dev/bin/markoff-qml-testapp some-file.md`) launches, displays the file, accepts typed edits, persists changes back to disk on save.
5. ✅ Property tests pass with no desync events across 100+ random sequences per library.
6. ✅ The three sketches in §10 still hold up (no foundation API issue surfaces during the build that contradicts the sketches).

**At acceptance:**

- All ✅: viability proven; user can decide whether to proceed with follow-up phases (real Live equivalent, CorbomiteApp migration, etc.).
- Any ❌: foundation API needs revision; we patch and re-run.

The exploration is NOT trying to prove "the new foundation is better than the existing one" or "production-ready for CorbomiteApp." Those claims need data we won't have at the end of this phase.

---

## 13. Out of scope (explicit)

These are intentionally NOT addressed in this spec or its implementation plan:

- **Live-preview formatting in any view.** POC is a simple editor. A future view might do per-block live preview; its design comes later.
- **CRDT transport layer.** Foundation is CRDT-internal-ready; transports (file sync, network, IPC) are host concerns.
- **CorbomiteApp migration.** Master continues; migration sequencing is a future decision.
- **Retiring the existing Markoff family.** Decision depends on this exploration's results.
- **Reading-, Source-, Live-equivalent views.** Sketches only.
- **Plugin API for code-block processors.** The registry exists; defining a stable public ABI for third-party plugins is out of scope.
- **Performance tuning beyond what collabtext provides.** No custom optimization passes.
- **Full live-preview-style block-by-block rendering.** Future view design.
- **Multi-cursor UX in the POC.** Foundation supports it; POC doesn't exercise.
- **Presence rendering UX in the POC.** Same.

---

## 14. Decisions recorded

The substantive design choices and their rationales.

### D1. Hard reset over in-place fix

**Decision:** Build a new foundation rather than refactor the existing one in place.

**Why:** The audit identified that the contract — not just individual pieces — is wrong. C2 (theme consolidation), C4 (renderer unification), the editor key-dispatch architecture spec, and a `TextControl` migration would each have to ship with master under maintenance. Hard reset puts all the architectural decisions in one branch.

**How to apply:** Master remains the source of truth for CorbomiteApp; this branch is exploratory; future migration is a separate decision.

### D2. View concept = C+D

**Decision:** Foundation owns commands and selection types. Views render and handle input. Subscribers can be anything (views, TOC sidebars, structure visualizers, presence indicators).

**Why:** Today's "views own everything" (option A in brainstorming) produces the cross-leaf duplication the audit found. Pure inversion-of-control (option B) is academically clean but every "view" you actually want IS interactive. C minimizes duplication; the dash of D acknowledges that the foundation has no concept of "the view" at all — just signals and types.

**How to apply:** When designing or evaluating an addition to the foundation, ask: "does this assume there's a single 'view' with privileged access?" If yes, redesign.

### D3. Heavy CRDT mode from day one

**Decision:** Foundation depends directly on `CollabText::Crdt::Buffer`. No `InMemoryCanonicalBuffer`. No abstract `CanonicalBuffer` interface.

**Why:** The audit identified offline-first sync as a future-must-have, and the affordances CRDT requires (anchors, replica-aware undo, version vectors) are useful even single-user. Building the in-memory layer first and migrating later means duplicating the work and carrying API debt during the transition.

**How to apply:** All canonical-text decisions check against collabtext primitives. Anchors are `CollabText::Crdt::Anchor` directly, not aliased. Operations are `CollabText::Crdt::Operation` directly.

### D4. Sessions on the document; plural selections; anchor-bound everything

**Decision:** Each view's per-document state lives in a `Session` owned by `MarkoffDocument`. Sessions hold a primary `Selection` plus a kinded list of secondaries (search hits, multi-cursor, presence). All position references — selection anchors, scroll position, fold start — are `CollabText::Crdt::Anchor`s.

**Why:** Anchors survive concurrent CRDT edits. Sessions cleanly extend to remote presence (a remote user is just a session with a remote `participantId`). Plural selections from day one because search hits and multi-cursor are real today, not a hypothetical future.

**How to apply:** Any per-view state that should survive hot-swap or replicate to remote sessions goes into `Session`. Per-view state that is purely local UI ephemerality (open find-bar text, current popup index) stays in the view.

### D5. Drop the Qt `QUndoStack` model

**Decision:** Undo lives in `CollabText::Crdt::Buffer`'s replica-aware undo stack, not in a Qt `QUndoStack` of `QUndoCommand`s.

**Why:** Buffer's undo is CRDT-correct (your undo reverses your edit, even after concurrent peer edits). Qt's `QUndoStack` would conflict with replica semantics. `MarkdownDelta` becomes `MarkoffEdit` — an edit description, not a `QUndoCommand` subclass.

**How to apply:** `doc->undo()` and `doc->redo()` route to Buffer. `coalesceLastUndo()` exposes the editor-aware coalescing primitive Buffer provides.

### D6. Theme as `Q_GADGET` value type

**Decision:** Theme is a value type with `Q_GADGET` (not `Q_OBJECT`). Each view holds its own copy; theme changes propagate by the host calling `setTheme(theme)` on each view.

**Why:** Toolkit-portable, cheaply copyable, naturally bindable in QML. Avoids reference-counted ownership.

**How to apply:** When adding a styling concern to Theme, add a slot to the `Slot` enum or extend `FontRole`; never grow Theme into a service.

### D7. Code-block highlighting and special processors as foundation services

**Decision:** `SyntaxHighlightService` (default Kf6 impl) and `CodeBlockProcessorRegistry` are foundation-tier. Every view consults the same services; same code block renders the same way in every view.

**Why:** The audit found three different highlighting stacks across leaves and only one of three views consulting the registry. Promoting to foundation eliminates this entire class of cross-view divergence.

**How to apply:** When building a view, the resolution order is always: registry → highlight service → monospace fallback. Don't reimplement.

### D8. Completion detection in foundation; candidates from host

**Decision:** `CompletionDetector` is foundation (AST-aware). `CompletionProvider`s are host-implemented. `CompletionRegistry` aggregates.

**Why:** Detection benefits from AST awareness (`#` is a tag in body but a heading marker at line start). Candidate sources (vault index, tag list) are host-knowledge.

**How to apply:** Foundation ships `EmojiCompletionProvider` as a default; everything else is host. POC uses emoji only.

### D9. POC = QML simple editor

**Decision:** The POC uses Qt Quick Controls' `TextArea` (via `QQuickTextDocument`). No live preview. ~1,200–1,900 LOC.

**Why:** Smallest scope that exercises every load-bearing piece of the foundation API (read, write, cursor, selection, theme, AST, search, undo, completion). Tests cross-toolkit binding without committing to live-preview rendering.

**How to apply:** Live preview, multi-cursor UX, presence UX, code-block special rendering — all OUT of POC scope. Add as future view phases if desired.

### D10. KF6's QML `SyntaxHighlighter` for markdown source colorization

**Decision:** Use `org.kde.syntaxhighlighting`'s QML `SyntaxHighlighter` component for markdown source highlighting in the POC. Custom `EditorHighlighter` only for code-block content.

**Why:** Marknote already proves this works. KF6's QML component is well-tested. Saves ~100 LOC of custom highlighter code.

**How to apply:** When adding a future view, prefer KF6 QML helpers where they exist before writing a custom equivalent.

### D11. Three playbook sketches; no fourth

**Decision:** Sketches cover Qt Widget, TUI (Tui Widgets), WebEngine. Don't sketch additional toolkits in this spec.

**Why:** Three are enough to validate cross-toolkit feasibility (one with `QTextDocument`, one without, one with serialization). More would be busywork.

**How to apply:** When evaluating a future view, check the relevant sketch's notes for known integration concerns; don't extrapolate beyond the sketches without revisiting the foundation API.

---

## 15. Open questions / future work

Items that surfaced during design but are deferred:

1. **Persistent replica IDs.** POC uses random `quint16`. For sharing, a stable per-machine or per-identity ID is needed. Host concern; specifics depend on identity infrastructure.
2. **CRDT transport.** Future host work. The foundation's `applyRemoteOps` entry point is ready.
3. **Plugin ABI for code-block processors.** The registry exists; making it stable enough for third-party plugins is a separate spec.
4. **Live-preview view design.** Explicitly future. Likely uses a custom block-flow renderer rather than reusing `TextArea`'s formatting model.
5. **Multi-cursor UX in views.** Foundation supports it; views need a UX design (input gestures, rendering of secondaries) that is view-specific.
6. **Presence UX in views.** Same.
7. **Hot-swap with toolkit change.** Sessions transfer cleanly between two views of the same toolkit. Cross-toolkit hot-swap (e.g., QML editor → Widget reader) needs validation against actual built views; may surface foundation API tweaks.
8. **`coalescingIdleMs` tuning.** Buffer's undo coalescing depth is exposed; default value needs measurement.
9. **Theme presets beyond light/dark.** The audit didn't motivate more; future themes are easy additions.
10. **Whether to also build `markoff-view-widget` in this exploration.** Currently a sketch; promoting to a built view would strengthen viability evidence at the cost of doubling implementation. Decision deferred.

---

## 16. References

- Audit: [`docs/2026-04-28-codebase-audit.md`](../2026-04-28-codebase-audit.md)
- Existing key-dispatch architecture spec: [`docs/specs/2026-04-21-editor-key-dispatch-architecture.md`](2026-04-21-editor-key-dispatch-architecture.md)
- collabtext architecture: `~/dev/collabtext/docs/ARCHITECTURE.md`
- collabtext README: `~/dev/collabtext/README.md`
- Marknote (KDE-family inspiration): `~/src/marknote`
- Phase C status board: [`docs/phase-c-status.md`](../phase-c-status.md)

---

*End of design document.*
