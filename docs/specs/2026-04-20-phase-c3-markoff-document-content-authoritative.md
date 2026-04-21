# Phase C3 — `MarkoffDocument` Content-Authoritative

**Status:** Draft
**Depends on:** Phase A (`2026-04-20-tri-view-unified-api-design.md`), Phase B (`2026-04-20-phase-b-corbomite-migration.md`), Phase C1 (`2026-04-20-phase-c1-di-seam.md` — landed `v0.3.0`), Phase C5 (`2026-04-20-phase-c5-reading-interaction-parity.md` — landed `v0.4.0`), Phase C6 (`2026-04-20-phase-c6-editor-state-context-menu.md` — landed `v0.5.0`). Assumes the Markoff submodule in Corbomite is pinned at `v0.5.0` or later.
**Audience:** The implementer (Corbomite agent, Markoff agent, or fresh-context agent) executing C3 across both `/home/clinton/dev/Corbomite/libs/markoff-family/` and `/home/clinton/dev/Corbomite/` proper. Produces Markoff `v0.6.0` and a paired Corbomite adapter commit.
**Absorbs Corbomite-side:** the Phase-A-deferred async parse worker plumbing, source-offset↔per-block cursor translation for cross-mode undo, and the flush-on-mode-swap machinery in `NoteEditorWidget` (four call sites).

---

## 1. Goal

Make `Markoff::MarkoffDocument` **content-authoritative**: it owns the canonical markdown bytes, owns the single `QUndoStack`, drives the async parse, and fires signals that all three leaves (`Markoff::Source::SourceEditor`, `Markoff::Editor`, `Markoff::Reading::ReadingView`) subscribe to. Leaves hold private scratch view-state, project canonical into their native representation, and push all edits back as undo-stack commands.

Phase A shipped `MarkoffDocument` as a stub ("attaching a `MarkoffDocument` stores the pointer but doesn't yet bind text. Phase C flips that."). C3 is the phase that flips it, for real, across all three leaves.

The design is **symmetric-B**: canonical content is markdown bytes (`QString` behind a `CanonicalBuffer` interface); every leaf subscribes at the markdown-bytes level; undo lives at the canonical-markdown level; no leaf holds a privileged `QTextDocument *` coupling to the canonical buffer. Source (Qutepart-backed) and Reading (AST-render-tree) already work this way; C3 brings Live (scene-graph) onto the same footing.

Scope is deliberately **not** a Live-side scene-graph rewrite. Retiring Live's per-block `TextControl` model onto a single `QTextEdit` ("A(i)" in the brainstorm) is a separately-scoped future phase with its own spec + plan; C3 delivers the shared-canonical contract while Live's scene-graph survives intact.

## 2. Non-goals

- **Live scene-graph rewrite.** The "retire scene-graph, rebuild Live on one `QTextEdit`" option is a future phase, not C3. C3 preserves Live's per-block `TextControl` machinery; per-block offset maps are the translation layer that keeps symmetric-B working without touching Live's rendering internals.
- **Renderer unification.** Code-block / math / mermaid double-dispatch is C4.
- **Theme / ResourceProvider / LinkResolver consolidation.** That's C2.
- **Find/replace public API on `SourceEditor`.** That's C7.
- **Fold-gutter coordinator.** That's C7.
- **Plugin-facing `MarkoffDocument` ABI.** `MarkoffDocument` stays library-internal. Corbomite plugins continue to see `NoteDocument` through `VaultProxy` / `FileManagerProxy`; whether to expose finer-grained surfaces is deferred (plugin-ready-surfaces discipline from Cluster N: don't expose internals until the library is stable standalone).
- **`HoverPopover` live-binding on the Corbomite side.** Cheap follow-up after C3 lands; tracked separately to keep the Markoff-library-phase boundary clean.
- **CRDT-backed canonical storage.** C3 ships the `CanonicalBuffer` interface that makes a future Phase E (`collabtext` integration) a clean swap. The swap itself is not C3.

## 3. Current state (what C3 rewires)

### 3.1 `MarkoffDocument` today (Phase A stub shape)

`libs/markoff-core/include/markoff/MarkoffDocument.h` exposes:

- `plainText()` / `setPlainText(QString)` — owned `QString` mirror.
- `textDocument()` — returns a private `QTextDocument *`.
- `replace(offset, removeLen, insert)` / `insert(offset, text)` / `remove(offset, len)` — direct mutators.
- `beginTransaction()` / `endTransaction()` — coalescing.
- `parsed()` / `parseIsPending()` — synchronous parse cache.
- `contentsChanged` / `parseUpdated` signals.

But per the Phase A docstring: "attaching a `MarkoffDocument` stores the pointer but doesn't yet bind text." None of the three leaves actually reads or writes canonical content through it today. Each leaf owns its own text, and `NoteEditorWidget` shuffles text between leaves on mode swap via `NoteDocument::setMarkdown`.

### 3.2 Four flush/restore call sites in `NoteEditorWidget` (Corbomite side)

```
src/editor/NoteEditorWidget.cpp:176  m_doc->setMarkdown(text);
src/editor/NoteEditorWidget.cpp:186  m_doc->setMarkdown(text);
src/editor/NoteEditorWidget.cpp:433  m_doc->setMarkdown(m_editor->toPlainText());
src/editor/NoteEditorWidget.cpp:609  m_doc->setMarkdown(m_editor->toPlainText());
```

These manually sync leaf text into `Corbomite::NoteDocument` on every mode swap and every close. They are the machinery C3 replaces with signal-driven subscription.

### 3.3 Phase-A-deferred plumbing C3 absorbs

From `docs/plans/2026-04-20-tri-view-phase-a.md` §"Deferred to later phases":

- Async parse worker (`ReadingParseWorker` → `MarkoffDocument`).
- `MarkoffDocument::parsed()` emits the `parsed(const Document *)` signal on a worker thread.
- Source-offset ↔ per-block cursor translation for undo across mode switch.
- Precise pixel ↔ visual-line float scroll conversion (`ScrollPosition.h`).

The first three land in C3. The fourth (`ScrollPosition.h` precise conversion) stays per-leaf in C3 — visual lines are inherently per-leaf (Source's ≠ Live's ≠ Reading's), and no canonical-level scroll concept is needed.

## 4. Target architecture

### 4.1 Ownership chain under C3

```
Corbomite::Vault
  └── owns + caches (per relpath)
      └── Corbomite::NoteDocument            [wrapper: vault path, modified flag, save hook, word counts]
            └── owns
                └── Markoff::MarkoffDocument [canonical content owner]
                      ├── std::unique_ptr<Markoff::CanonicalBuffer>
                      │     └── QString (markdown bytes) + anchor table
                      ├── cached Markoff::Document *      [tree-sitter AST]
                      └── QUndoStack *                    [single undo source]

Markoff::ParsePool  (vault-scoped single worker thread; one per app)
   ↑
   posted to by MarkoffDocument after idle-coalesce on every MarkdownDelta
```

Leaves bind via `MarkdownView::setDocument(MarkoffDocument *)` and project:

- `Source::SourceEditor` — splices `toMarkdown()` into Qutepart's buffer on `contentsChanged`; pushes deltas via `undoStack()->push(new MarkdownDelta(...))`.
- `Editor` (Live) — rebuilds scene-graph blocks from `parsedDocument()` on `parseUpdated`; per-block offset map translates local edits into canonical deltas.
- `Reading::ReadingView` — rebuilds section layout from `parsedDocument()` on `parseUpdated`; read-only, never pushes deltas.

No leaf accesses `CanonicalBuffer`, `QString`, or `QUndoStack` directly. No leaf owns a pointer into canonical storage.

### 4.2 `CanonicalBuffer` interface (Phase-E-hedge)

Lives in `libs/markoff-core/include/markoff/CanonicalBuffer.h`:

```cpp
namespace Markoff {

enum class CursorBias { Left, Right };

class CanonicalBuffer {
public:
    virtual ~CanonicalBuffer() = default;

    // Reads
    virtual const QString &toMarkdown() const = 0;
    virtual qsizetype      length() const = 0;
    virtual QString        substring(qsizetype offset, qsizetype len) const = 0;

    // Writes — sole mutation path
    virtual void applyDelta(qsizetype offset,
                            qsizetype removedLength,
                            const QString &inserted) = 0;
    virtual void reset(const QString &newContent) = 0;

    // Anchors — for persistent cursor state across edits
    virtual quint64   createAnchor(qsizetype offset, CursorBias bias) = 0;
    virtual qsizetype resolveAnchor(quint64 handle) const = 0;
    virtual void      releaseAnchor(quint64 handle) = 0;
};

}
```

C3 ships one concrete: `InMemoryCanonicalBuffer` (a `QString` plus an anchor-handle-to-`(offset, bias)` table; `applyDelta` updates each anchor per bias rules). A future Phase E may add `CrdtCanonicalBuffer` backed by `collabtext::Buffer` — the abstraction keeps that door open at small real cost (see §11).

**Honest cost note:** the interface is small-but-real new surface. The "we'd build anchor machinery anyway" argument is weaker than the brainstorm framing — Source inherits cursor-on-edit adjustment from `QTextCursor` inside Qutepart, Reading is read-only, Live's per-block offset map lives in `SceneCoordinator` rather than on canonical. The actual CanonicalBuffer-level anchor consumer in C3 is `SearchController`'s current-match position. The interface is justified by clean abstraction + Phase-E optionality, not "free refactor."

### 4.3 `CursorPosition` opaque handle

Lives in `libs/markoff-core/include/markoff/CursorPosition.h`:

```cpp
namespace Markoff {

class MarkoffDocument;

class CursorPosition {
public:
    CursorPosition() = default;
    bool isValid() const;

    // No copy (anchor is unique RAII); move-only.
    CursorPosition(const CursorPosition &) = delete;
    CursorPosition &operator=(const CursorPosition &) = delete;
    CursorPosition(CursorPosition &&) noexcept;
    CursorPosition &operator=(CursorPosition &&) noexcept;
    ~CursorPosition();

private:
    friend class MarkoffDocument;
    CursorPosition(MarkoffDocument *doc, quint64 handle);
    MarkoffDocument *m_doc = nullptr;
    quint64 m_handle = 0;
};

}
```

Used by `SearchController` (current-match). Leaves use raw `int` offsets for transient positions. No API change on `MarkdownView` — `cursorPosition()` still returns `CursorPos` (line/column) as a point-in-time query.

### 4.4 `MarkoffDocument` public API

Replaces the Phase A header:

```cpp
namespace Markoff {

class CanonicalBuffer;
class ParsePool;
class Document;  // markoff-parser

enum class Origin {
    FirstOpen,              // empty stack, no command pushed
    ExternalReloadClean,    // stack cleared
    ExternalReloadResolved, // stack cleared (post-merge-modal, any outcome)
    UserRevertToSaved,      // pushes one mega MarkdownDelta
    TestFixture,            // stack cleared
};

class MarkoffDocument : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarkoffDocument)
public:
    explicit MarkoffDocument(QObject *parent = nullptr);
    MarkoffDocument(std::unique_ptr<CanonicalBuffer> buffer,
                    ParsePool *pool = nullptr,
                    QObject *parent = nullptr);
    ~MarkoffDocument() override;

    // Reads
    const QString  &toMarkdown() const;
    qsizetype       length() const;
    QString         substring(qsizetype offset, qsizetype len) const;
    const Document *parsedDocument() const;
    bool            parseIsPending() const;

    // Writes — undo-stack only
    QUndoStack *undoStack() const;
    void        resetContent(const QString &newContent, Origin origin);

    // Anchors
    CursorPosition trackCursor(qsizetype offset, CursorBias bias);
    qsizetype      resolveCursor(const CursorPosition &) const;

    // Coalescing (preserved from Phase A; applies to MarkdownDelta::mergeWith)
    void setCoalescingIdleMs(int ms);
    int  coalescingIdleMs() const;

Q_SIGNALS:
    void contentsChanged(qsizetype offset, qsizetype removed, qsizetype inserted);
    void parseUpdated(const Markoff::Document *parsed);  // queued from ParsePool
    void documentReloaded();                             // wholesale buffer replacement

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}
```

Removed from Phase A: `plainText()` / `setPlainText(QString)` / `textDocument()` / `replace` / `insert` / `remove` / `beginTransaction` / `endTransaction` / `parsed()`.

Replaced by: `toMarkdown()` / `resetContent(QString, Origin)` / `undoStack()->push(new MarkdownDelta(...))` / `undoStack()->beginMacro("...")` + `endMacro()` / `parsedDocument()`.

### 4.5 `Origin` → stack behavior

| Origin | Stack behavior | Signals emitted |
|---|---|---|
| `FirstOpen` | No-op (stack already empty) | `contentsChanged(0, 0, N)`, then `parseUpdated` from pool |
| `ExternalReloadClean` | `undoStack()->clear()` | `documentReloaded`, then `parseUpdated` from pool |
| `ExternalReloadResolved` | `undoStack()->clear()` | `documentReloaded`, then `parseUpdated` from pool |
| `UserRevertToSaved` | Pushes one `MarkdownDelta(0, oldLength, savedContent)` | Via the command: `contentsChanged`, then `parseUpdated` |
| `TestFixture` | `undoStack()->clear()` | `documentReloaded`, then `parseUpdated` from pool |

**`documentReloaded` precedes the next `contentsChanged`** in all reload paths, so leaves can discard in-flight edit batches, search/fold/scroll state, and partial transactions before reacting to the new content. `UserRevertToSaved` does *not* emit `documentReloaded` — a revert is a user-intent edit, not a wholesale external event, and the undo stack remains meaningful post-revert.

### 4.6 `MarkdownDelta` command

Lives in `libs/markoff-core/include/markoff/MarkdownDelta.h` (sole concrete `QUndoCommand` the library ships):

```cpp
namespace Markoff {

class MarkoffDocument;

class MarkdownDelta : public QUndoCommand {
public:
    MarkdownDelta(MarkoffDocument *doc,
                  qsizetype offset,
                  qsizetype removedLength,
                  QString inserted,
                  QUndoCommand *parent = nullptr);

    void redo() override;   // canonical->applyDelta(offset, removedLength, inserted)
    void undo() override;   // canonical->applyDelta(offset, inserted.size(), removedSnapshot)

    int  id() const override;                              // for mergeWith
    bool mergeWith(const QUndoCommand *other) override;    // keystroke coalescing within
                                                           // coalescingIdleMs window

private:
    MarkoffDocument *m_doc;
    qsizetype m_offset;
    QString   m_removed;    // captured on first redo() for undo() replay
    QString   m_inserted;
};

}
```

Composite structural operations (insert table row, heading level change across a selection, paste-with-format) wrap as `QUndoStack::beginMacro("...")` groups of 1..N `MarkdownDelta`s. Leaves build the composite at their own layer; the library does not ship named subclasses per structural op.

### 4.7 `ParsePool`

Lives in `libs/markoff-core/include/markoff/ParsePool.h`:

```cpp
namespace Markoff {

class ParsePool : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ParsePool)
public:
    explicit ParsePool(QObject *parent = nullptr);
    ~ParsePool() override;

    // MarkoffDocument calls postJob(this, snapshotText). Newer job for the same
    // sender supersedes older pending jobs for that same sender.
    void postJob(MarkoffDocument *sender, QString snapshot);
    void cancelJobsFor(MarkoffDocument *sender);

Q_SIGNALS:
    // Queued (QueuedConnection) to MarkoffDocument's parseUpdated.
    void jobCompleted(MarkoffDocument *sender, Markoff::Document *parsed);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}
```

Single `QThread` owned by the pool; one pool per vault (Corbomite side) or one per `MarkoffDocument` in standalone / test (via `DefaultParsePool` — C1-precedent pattern). The pool is a `MarkoffDocument` constructor arg; default-construct creates a per-doc `DefaultParsePool` so tests don't have to set one up.

Cancellation: a per-sender generation counter. The worker thread checks "am I still the current generation?" before dispatching `jobCompleted`; old runs self-abandon.

### 4.8 Signal flow end-to-end

```
User types in Source:
  Qutepart QTextDocument::contentsChange(pos, removed, added)
    → Source's onLocalChange slot (guarded by m_applyingCanonicalDelta)
        → undoStack()->push(new MarkdownDelta(doc, pos, removed, insertedText))
            → MarkdownDelta::redo()
                → canonical->applyDelta(pos, removed, insertedText)
                → emit MarkoffDocument::contentsChanged(pos, removed, added)
                    → Live's onContentsChanged: finds affected block via offset map,
                      splices block's local QTextDocument (with guard set)
                    → Reading: does not subscribe to contentsChanged — no-op
                    → Source's onContentsChanged: guard is set (we originated) — rejects reflection
                → MarkoffDocument::schedulePoolPost() debounces; on settle
                    → pool->postJob(this, toMarkdown())
                        → worker parses
                        → pool emits jobCompleted(this, parsedDoc)
                            → MarkoffDocument emits parseUpdated(parsedDoc)
                                → Live's onParseUpdated: reconcile scene graph
                                → Reading's onParseUpdated: reconcile section layout
                                → Source: does not subscribe to parseUpdated — no-op
```

`documentReloaded` fires only on `resetContent` in reload/fixture origins, and flushes in-flight state on every subscriber before the subsequent parse lands.

## 5. Leaf adaptation contracts

### 5.1 `Markoff::Source::SourceEditor`

**Subscribe** (`setDocument(MarkoffDocument *)`): connect `contentsChanged` and `documentReloaded`. Disconnect `parseUpdated` — Source renders text verbatim, not AST.

**Attach:**
1. Existing Qutepart widget stays; `qutepartDoc->setUndoRedoEnabled(false)`.
2. Load `markoff->toMarkdown()` into Qutepart verbatim.
3. Drop any prior document bindings.

**On external `contentsChanged(offset, removed, inserted)`** (guard `m_applyingCanonicalDelta` is false): translate offset→Qutepart-cursor, splice. Qutepart's internal cursor/selection state adjusts natively.

**On local Qutepart `contentsChange(pos, removed, added)`** (guard false): compute `insertedText` from Qutepart's buffer; set guard; `undoStack()->push(new MarkdownDelta(markoff, pos, removed, insertedText))`; clear guard. (The push calls `redo()` which re-applies to canonical; the canonical `contentsChanged` that fires back is ignored by the guard.)

**On `documentReloaded`:** tear down Qutepart selection/highlights; reload from `markoff->toMarkdown()`.

**IME composition:** `undoStack()->beginMacro("IME")` on composition start; one `MarkdownDelta` at commit; `endMacro()`. If composition is aborted mid-stream, `beginMacro`/`endMacro` collapses to a no-op macro (no canonical delta pushed).

### 5.2 `Markoff::Editor` (Live)

**Subscribe:** connect `contentsChanged`, `parseUpdated`, and `documentReloaded`.

**Attach:**
1. Every per-block `TextControl`'s `QTextDocument::setUndoRedoEnabled(false)`.
2. `SceneCoordinator` asks for `markoff->parsedDocument()`. If non-null, build scene graph off AST. If null (parse pending on first attach), show Phase-A "parse in flight" placeholder until `parseUpdated` fires.
3. `SceneCoordinator` maintains the **per-block offset map**: each block → `(canonicalStart, canonicalEnd)`.

**On external `contentsChanged(offset, removed, inserted)`** (guard false):
1. Find affected block via offset map binary-search.
2. Compute block-local offset = canonicalOffset - block.canonicalStart.
3. Splice the block's local `QTextDocument` (with guard set during the splice, to prevent local-change cascade).
4. Shift all blocks after the affected one by `(inserted - removed)` in their `(canonicalStart, canonicalEnd)`.
5. Debounced reparse fires via `parseUpdated` from the pool; scene graph reconciles then.

**On local per-block edit** (guard false): translate block-local offset + block.canonicalStart → canonical offset; push `MarkdownDelta`. Redo applies to canonical → fires `contentsChanged` → guard rejects reflection → parse pool kicks → `parseUpdated` later reconciles.

**Composite ops** (table row insert, heading-level change over selection): `undoStack()->beginMacro(...)` + N `MarkdownDelta`s + `endMacro`. `TableConverter` / `TableSerializer` build the delta macro instead of mutating the scene directly; the scene reconciles after the parse completes.

**On `parseUpdated`:** diff-and-patch scene graph against new AST; update offset map; surviving blocks keep their `TextControl` instances where possible (Phase A diff-and-patch path survives).

**On `documentReloaded`:** discard scene, offset map, in-flight reparse debouncer; await next `parseUpdated`.

### 5.3 `Markoff::Reading::ReadingView`

**Subscribe:** connect `parseUpdated` and `documentReloaded`. Does *not* subscribe to `contentsChanged` — reads never see partial parse states.

**Attach:** ask `markoff->parsedDocument()`; build section layout if non-null, else await first `parseUpdated`.

**On `parseUpdated`:** diff-and-patch section layout (existing Cluster-E code path).

**On `documentReloaded`:** tear down section layout; await next `parseUpdated`.

**Writes:** none. `hasEditing() == false`. Any accidental edit attempt is a programming error, caught in debug by an assertion in `ReadingView::setDocument`.

### 5.4 Shared conventions across all three leaves

- **Re-entrance guard.** Each leaf holds `bool m_applyingCanonicalDelta = false`. Set true before applying an inbound `contentsChanged` to its local view; cleared after. Local-edit signals fired while the guard is true are ignored (the view catching up, not a user edit).
- **Detachment order.** `setDocument(nullptr)` or `setDocument(otherDoc)` disconnects signals from the old `MarkoffDocument` *before* clearing local state; prevents cross-wiring during mode swaps.
- **Ephemeral state.** `MarkdownView::ephemeralState()` / `setEphemeralState(QJsonObject)` contract from Phase A survives unchanged. Mode swap: outgoing leaf snapshots, detaches; incoming leaf attaches, restores.

## 6. Corbomite-side adaptation

### 6.1 `NoteDocument` becomes a wrapper (1:1, via `Vault` cache)

| Today | After C3 |
|---|---|
| `QString m_markdown` field | Deleted. |
| `QString markdown() const` | `return m_markoff->toMarkdown();` |
| `void setMarkdown(QString)` | `m_markoff->resetContent(text, Origin::*)` (caller determines origin). |
| `textChanged` signal | Relays `m_markoff->contentsChanged` (collapsed to 0-arg shape for back-compat). |
| `isModified()` / `setModified(bool)` | Unchanged. Driven by first `contentsChanged` post-save. |
| `modificationChanged(bool)` | Unchanged. |
| `saved()` | Unchanged — emitted from `Vault::saveDocument`. |
| `wordCount()` / `characterCount()` | Unchanged — computed from `m_markoff->toMarkdown()`. |

New accessor: `NoteDocument::markoff() -> Markoff::MarkoffDocument *` (non-owning, const + non-const). Entry point for leaves: `leaf->setDocument(note->markoff())`.

`NoteDocument` constructs its `MarkoffDocument` with the app-wide shared `Markoff::ParsePool *` injected by `Vault`.

### 6.2 `Vault` wiring

- `Vault` owns one `Markoff::ParsePool` for its lifetime. Passed into every `NoteDocument`.
- `Vault::openDocument` reads disk and calls `noteDoc->markoff()->resetContent(bytes, Origin::FirstOpen)` once post-construction.
- `Vault::saveDocument(NoteDocument *)` → `QFile::write(noteDoc->markoff()->toMarkdown())`. No `QTextDocumentWriter`, no format coercion.
- **Echo suppression** — existing "we-wrote-this" flag survives. **Plus** a byte-equality defense-in-depth: on a watcher event, if `QFile::read(path) == noteDoc->markoff()->toMarkdown()`, suppress (no reload) even if the flag leaks. Made possible specifically because canonical is `QString` and save is a raw byte write — no coercion step means canonical bytes == disk bytes exactly.
- **External reload path:**
  - Clean (`!doc->isModified()`): `doc->markoff()->resetContent(newBytes, Origin::ExternalReloadClean)`.
  - Dirty (`doc->isModified()`): open merge modal → user picks keep-mine / take-theirs / merged → `doc->markoff()->resetContent(resolvedBytes, Origin::ExternalReloadResolved)`.

### 6.3 `NoteEditorWidget` flush/restore retires

All four call sites (lines 176, 186, 433, 609) **delete**.

Replaced by the mode-swap contract:
1. Outgoing leaf snapshots `ephemeralState()`.
2. Outgoing leaf `setDocument(nullptr)`.
3. Incoming leaf `setDocument(m_doc->markoff())`.
4. Incoming leaf `setEphemeralState(snapshot)`.

Canonical content never round-trips through leaves during the swap.

`NoteEditorWidget::setNoteDocument(NoteDocument *)` still takes a `NoteDocument *` — façade preserved. Internal implementation binds leaves to `doc->markoff()`.

### 6.4 Autosave

No changes. `AutosaveReactor` observes `NoteDocument::modificationChanged(true)` and calls `Vault::saveDocument(doc)` on debounce. Under C3 the trigger chain is `MarkoffDocument::contentsChanged` → `NoteDocument::setModified(true)` → `modificationChanged(true)` → reactor.

### 6.5 `HoverPopover` — out of C3 scope

Stays on snapshot model (`HoverPopover.cpp:152` `m_view->setPlainText(doc->markdown())` unchanged). Live-binding to `Vault::openDocument(targetRelPath)->markoff()` is a cheap post-C3 Corbomite-side follow-up; pulling it into a Markoff-library phase muddies the boundary and isn't worth the scope creep.

### 6.6 Breaking-change manifest for CorbomiteApp call sites

| Site | Today | After C3 |
|---|---|---|
| `NoteEditorWidget.cpp:176,186,433,609` | `m_doc->setMarkdown(text)` flush/restore | Deleted. Mode-swap uses `setDocument(markoff)` subscription. |
| `MainWindow.cpp:135,150` | `doc->markdown()` reads | Unchanged. |
| `HoverPopover.cpp:152` | `m_view->setPlainText(doc->markdown())` | Unchanged in C3; follow-up. |
| `Vault::saveDocument` | `QFile::write(doc->markdown())` | `QFile::write(doc->markoff()->toMarkdown())`. |
| `Vault::openDocument` hydrate | `doc->setMarkdown(bytes)` | `doc->markoff()->resetContent(bytes, Origin::FirstOpen)`. |
| External-reload watcher | `doc->setMarkdown(newBytes)` | Origin-aware `resetContent` (clean vs. resolved). |
| Test fixtures on `NoteDocument` | `doc->setMarkdown(fixture)` | `doc->markoff()->resetContent(fixture, Origin::TestFixture)`. |

`VaultProxy` / `FileManagerProxy` plugin facade: no change. Plugins see `NoteDocument` shapes only.

## 7. Acceptance criteria

Must all hold on the `v0.6.0` Markoff tag + paired Corbomite adapter commit.

### 7.1 Markoff side (standalone build)

1. `cmake -S . -B build-dev && cmake --build build-dev -j && cd build-dev && ctest --output-on-failure -j` on a fresh checkout passes with zero external deps.
2. `MarkoffDocument` exposes no `QTextDocument` accessor. `grep -r "textDocument()" libs/markoff-*/include/markoff/` returns zero hits in `MarkoffDocument.h`.
3. No leaf links `setUndoRedoEnabled(true)` on any private `QTextDocument` at construction time (grep-enforced in a static test).
4. **`tst_canonical_interop`** (new): bind all three leaves to one `MarkoffDocument`; edit in Source; assert Live's scene graph reflects it; assert Reading's section layout rebuilds; assert `parseUpdated` fires exactly once per edit burst within `coalescingIdleMs`.
5. **`tst_cross_mode_undo`** (new): edit in Source, swap to Live, edit, swap to Source, `Ctrl+Z` three times; stack reverses all three edits in order regardless of active mode at edit time.
6. **`tst_origin_reset`** (new): all five `Origin` values exercised; stack behavior matches §4.5 table exactly; `documentReloaded` fires once in reload branches, zero in `UserRevertToSaved`.
7. **`tst_parse_pool`** (new): 100 rapid `MarkdownDelta` pushes inside `coalescingIdleMs` produce exactly one `parseUpdated`; a pending parse cancels cleanly on a newer delta.
8. **`tst_cursor_anchor`** (new): `trackCursor(N, bias)`; apply deltas that precede, straddle (both biases), and follow N; `resolveCursor` returns the mathematically correct post-edit offset in each case.
9. **`tst_markdown_delta_merge`** (new): two `MarkdownDelta`s within `coalescingIdleMs` merge into one on `QUndoStack`; two outside the window do not.
10. All Markoff tests from `v0.5.0` continue to pass. Expected test-count delta: `+7` minimum (the new tests above).

### 7.2 Corbomite side

11. `NoteEditorWidget`'s four flush/restore call sites are deleted (grep-enforced).
12. **`tst_modeswap_preserves_ephemeral`** (new/updated): same-frame mode swap Source→Live→Source; `MarkoffDocument::toMarkdown()` byte-identical before and after; cursor/scroll/fold preserved via `ephemeralState`.
13. **`tst_external_reload_clean`** (new/updated): external change while note clean; watcher fires; reload clears undo stack; `documentReloaded` observed once.
14. **`tst_external_reload_dirty`** (new/updated): external change while note dirty; merge modal opens; for each of keep-mine / take-theirs / merged outcomes: resolved content applied, stack cleared, `documentReloaded` observed once.
15. **`tst_saveload_byte_equality`** (new): post-save disk bytes equal `toMarkdown()` byte-for-byte; no trailing-whitespace change, no newline normalization.
16. `./build/Corbomite` on a test vault: open a note, edit in Source, swap to Live, edit, swap to Reading, swap back to Source, `Ctrl+Z` four times — all edits revert in LIFO order; save; disk content equals editor content.
17. Full `ctest -j 10` green modulo the two documented flakes (`tst_benchmark_layout`, `tst_editorsuggest`).

## 8. Explicitly out of scope (not acceptance criteria)

1. `HoverPopover` live-binding (post-C3 Corbomite follow-up).
2. Live's scene-graph rewrite on single `QTextEdit` (future phase, separate spec).
3. Phase E CRDT-backed canonical (scouting doc only; see §11).
4. Mermaid / math / code-block renderer unification (C4).
5. Theme / ResourceProvider / LinkResolver consolidation (C2).
6. Find/replace public API + fold-gutter coordinator (C7).
7. Plugin-visible `MarkoffDocument` ABI.

## 9. Follow-ups tracked

- `HoverPopover` live-binding (Corbomite side, post-C3).
- Sync-chattiness mitigation — `(c)` clears undo on every external reload; chatty sync services will disrupt undo. Phase E motivator.
- `libs/markoff-live/CLAUDE.md` rename (cosmetic; opportunistic).
- Re-enable the four `MARKOFF_READING_USE_REAL_COREDEPS`-gated-then-retired tests from C1b; C3 makes injection concrete enough that they become revivable against real concretes (tracked as Phase-C closeout item).

## 10. Decisions recorded

Eight design calls made during brainstorming, recorded so future readers don't relitigate:

1. **Wrapper (1:1), not pool.** `NoteDocument` owns one `MarkoffDocument`; Vault's existing NoteDocument cache provides de-facto pooling at vault granularity. Pool mode deferred indefinitely — no concrete use case without `CanonicalBuffer` in place, and with `CanonicalBuffer` the pool shape is irrelevant.
2. **Symmetric-B undo.** One `QUndoStack` on `MarkoffDocument`; native Qt undo disabled on every leaf-internal `QTextDocument`; all edits push `MarkdownDelta`; composite ops are macros. Rejected A (single-`QTextDocument` rewrite of Live) as a separately-scoped future phase on honest-cost grounds — no comparable Qt editor has shipped folding + `QTextTable` + rich decorations + images in one `QTextDocument`.
3. **Shared single-worker `ParsePool`**, not per-doc thread or `QThreadPool`. Matches Cluster I's `MetadataWorker` precedent; head-of-line blocking isn't real for tree-sitter on markdown.
4. **No internal `QTextDocument` on `MarkoffDocument`.** Canonical is `QString` behind `CanonicalBuffer`. Deprecate-and-carry was the footgun; removal-in-C3 is the fix. Migration path for existing `textDocument()` callers in §6.6.
5. **`Origin` enum on `resetContent`**, not hardcoded stack-clear. Covers first-open, external-clean, external-resolved, user-revert, test-fixture with differing stack semantics.
6. **`documentReloaded` signal distinct from `contentsChanged`.** Leaves need "discard everything" semantics separate from "apply this delta."
7. **Byte-equality defense-in-depth in echo suppression.** Enabled specifically by no-internal-`QTextDocument` → raw-byte save path. Belt-and-braces.
8. **HoverPopover out-of-scope for C3.** Cheap Corbomite-side follow-up; live-binding is a nice upgrade but doesn't belong in a Markoff-library phase.

## 11. Phase-E hedge (CRDT-backed canonical)

C3 is shaped to leave a future CRDT-backed canonical swap as a clean internal refactor. Two pieces of C3 surface carry the hedge:

1. **`CanonicalBuffer` interface.** `MarkoffDocument::Private` holds a `std::unique_ptr<CanonicalBuffer>`, not a `QString` field. C3 ships one concrete: `InMemoryCanonicalBuffer`. A future Phase E may ship a second: `CrdtCanonicalBuffer` backed by `~/dev/collabtext/`'s `collabtext::Buffer`. No leaf sees either concrete.
2. **`CursorPosition` opaque handle.** Persistent cursor anchors (search current-match) go through `CursorPosition` and `MarkoffDocument::trackCursor`/`resolveCursor`. The handle mechanism generalizes to CRDT anchors without leaf-side changes.

Scouting doc landed on the Corbomite side at `docs/superpowers/plans/2026-04-20-phase-e-crdt-canonical-SCOUTING.md`, alongside the V.2 / W / P scouting docs. Contents sketch motivation (Syncthing/Dropbox/iCloud conflict-free sync), technical path (second `CanonicalBuffer` concrete), explicit non-commits (not on-disk CRDT format, not real-time collab UI, not a sync product), and gating (Phase C done + abstraction stable + sync pain signal).

## 12. Signatures at a glance

| File | Added / modified |
|---|---|
| `libs/markoff-core/include/markoff/CanonicalBuffer.h` | **new** |
| `libs/markoff-core/include/markoff/CursorPosition.h` | **new** |
| `libs/markoff-core/include/markoff/MarkdownDelta.h` | **new** |
| `libs/markoff-core/include/markoff/ParsePool.h` | **new** |
| `libs/markoff-core/include/markoff/MarkoffDocument.h` | **rewrite** — new API per §4.4 |
| `libs/markoff-core/src/CanonicalBuffer.cpp` / `InMemoryCanonicalBuffer.cpp` | **new** |
| `libs/markoff-core/src/CursorPosition.cpp` | **new** |
| `libs/markoff-core/src/MarkdownDelta.cpp` | **new** |
| `libs/markoff-core/src/ParsePool.cpp` / `DefaultParsePool.cpp` | **new** |
| `libs/markoff-core/src/MarkoffDocument.cpp` | **rewrite** |
| `libs/markoff-core/tests/tst_canonical_buffer.cpp` | **new** |
| `libs/markoff-core/tests/tst_markdown_delta_merge.cpp` | **new** |
| `libs/markoff-core/tests/tst_parse_pool.cpp` | **new** |
| `libs/markoff-core/tests/tst_cursor_anchor.cpp` | **new** |
| `libs/markoff-core/tests/tst_origin_reset.cpp` | **new** |
| `libs/markoff-source/src/SourceEditor.cpp` | **modified** — `setDocument` binds canonical; disable-undo on Qutepart doc; translation slots |
| `libs/markoff-live/src/Editor.cpp` + `SceneCoordinator.cpp` | **modified** — per-block offset map; translation slots; disable-undo on block docs |
| `libs/markoff-reading/src/ReadingView.cpp` | **modified** — subscribe to `parseUpdated` / `documentReloaded` on bound `MarkoffDocument` |
| `tests/markoff/tst_canonical_interop.cpp` (top-level) | **new** — three-leaf interop |
| `tests/markoff/tst_cross_mode_undo.cpp` (top-level) | **new** |
| Corbomite `libs/vault/src/Vault.cpp` | **modified** — ParsePool ownership; openDocument/saveDocument byte path; watcher Origin dispatch |
| Corbomite `libs/core/src/NoteDocument.cpp` | **rewrite** — wrapper delegates; owns `Markoff::MarkoffDocument` |
| Corbomite `libs/core/include/corbomite/core/NoteDocument.h` | **modified** — add `markoff()` accessor; drop `m_markdown` field |
| Corbomite `src/editor/NoteEditorWidget.cpp` | **modified** — four flush/restore sites deleted; `setDocument` binding; ephemeralState swap |
| Corbomite tests: `tst_notedocument`, `tst_vault_save_reload`, `tst_modeswap_preserves_ephemeral` | **modified / new** |

## 13. Breaking changes since `v0.5.0`

For any external Markoff-as-library consumer (none known today; list for completeness and future-proofing):

1. **`MarkoffDocument::textDocument()` removed.** Migrate: read via `toMarkdown()`; subscribe to `contentsChanged` on the `MarkoffDocument` directly; search via `SearchController` in `markoff-core` (which already doesn't go through `QTextDocument::find`).
2. **`MarkoffDocument::setPlainText(QString)` removed.** Replaced by `resetContent(QString, Origin)`. First-open callers pass `Origin::FirstOpen`; test fixtures pass `Origin::TestFixture`.
3. **`MarkoffDocument::plainText()` removed.** Replaced by `toMarkdown()` (const ref, not copy).
4. **`MarkoffDocument::replace/insert/remove` removed.** Edits go through `undoStack()->push(new MarkdownDelta(...))` or `beginMacro` groups.
5. **`MarkoffDocument::beginTransaction/endTransaction` removed.** Use `QUndoStack::beginMacro` / `endMacro` directly (same coalescing semantics; `MarkdownDelta::mergeWith` honors `coalescingIdleMs`).
6. **`MarkoffDocument::parsed()` renamed to `parsedDocument()`** for symmetry with `toMarkdown()`.
7. **`parseUpdated(const Document*)` signal semantics change.** Emitted on the main thread via `QueuedConnection` from `ParsePool`, not synchronously on every mutator. `parseIsPending()` returns true between a mutation and the subsequent `parseUpdated`.

All other additions are additive: `undoStack()`, `trackCursor/resolveCursor`, `CanonicalBuffer`, `CursorPosition`, `MarkdownDelta`, `ParsePool`, `Origin`, `documentReloaded`.
