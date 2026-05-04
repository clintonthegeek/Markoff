# v2 holes — design

> **ARCHIVED 2026-05-03.** Superseded by the marker-paragraph design at
> `docs/specs/2026-05-03-marker-paragraph-design.md` (R5.5 architectural-
> review §3.1 chose approach (c) — marker character — over the hole
> abstraction this document specified). The §3.1 spike findings are at
> `docs/handoff/2026-05-03-section-3-1-spike-findings.md`. The original
> document text follows verbatim for historical reference.

**Date:** 2026-05-03
**Branch:** `exploration/new-foundation`
**Status:** design — supersedes v1 hole design (`docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.5) for paragraph holes; the v1 spec's §3.6 v0 forensics carry forward unchanged.
**Predecessors (read first):**
- `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md` (call for design)
- `docs/handoff/2026-05-03-r5-holes-postmortem.md` (post-mortem)
- `docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.6 (v1 design + v0 post-mortem)
- `docs/specs/2026-05-02-live-render-restoration-design.md` (the C-architecture spec to be amended)
- `docs/2026-05-02-live-view-architectural-audit.md` §c L9 (the structural correction this design adopts)

**Audience:** the implementer; the user (review gate); the next session that derives the R5.5 plan.

**Bound:** this design covers paragraph holes only. The full hole inventory (list-items, fence interiors, blockquote, callout, table cells, links, wikilinks, footnotes, math) is deferred to follow-on phases as in v1's "Out of scope (for v0)."

---

## 0. TL;DR

The post-mortem ratified seven resolutions, the audit's L9 structural correction (phantom-rows + concatenating proxy), and v1's commit/abandon trigger sets. This document operationalises them.

Three new C++ classes:

- `LiveHoleLayer` — owns `BlockHole` items; emits hole lifecycle signals.
- `LiveProxyBlockModel` — `QAbstractListModel` composing parser-pure `LiveBlockModel` rows + hole rows by anchor; the model the QML ListView binds to.
- `LiveRealisticInputHarness` — test utility for async-UX assertions; required first task of R5.5.

Two existing classes extended:

- `LiveCursorState::requestTextCaretAtRow` operates in proxy-row coordinates; resolves on `LiveProxyBlockModel::rowsInserted` for both parser-row and hole-row arrivals.
- `LiveStructuralKeyHandler` adds a hole-row dispatch path; paragraph-kind's EOB-Enter, start-of-paragraph-Enter, mid-buffer-Enter, Esc, Backspace-at-0-empty, Delete-at-end-empty are routed through it.

One existing class lightly touched:

- `LiveEditBinding` exposes IME composition state to `LiveHoleLayer`'s idle-commit timer.

No structural changes to `MarkoffDocument`, `LiveBlockModel` (parser-pure stays parser-pure), `LiveSelectionView` (operates on proxy rows; the projection logic is unchanged in shape), or `BlockKindRegistry`.

R5.5 is packaged as its own phase (10–14 tasks). R5 closes correctness-clean with the EOB-Enter limitation documented; R5.5 closes the gap; R6 picks up unchanged.

---

## 1. Premises (binding inputs from the post-mortem)

These are decided. The design's job is consequence, not re-litigation.

| # | Premise | Source |
|---|---|---|
| H1 | Holes are first-class for paragraph-EOB-Enter, paragraph-start-Enter, and paragraph-mid-buffer-Enter inside an existing hole. | post-mortem §3.4, §5.1 |
| H2 | The hole layer never writes to the CRDT until commit. | v1 §3.2; post-mortem §1.1 (F1, F5 mitigation) |
| H3 | Commit triggers: idle 250 ms after last user keystroke; focus-out with non-empty buffer; save (Ctrl-S); explicit Enter on non-empty buffer (end-of-buffer = commit-then-new-hole; mid-buffer = split). | v1 §3.3; post-mortem §5.1, §3.4 |
| H4 | Abandon triggers: Esc; focus-out with empty buffer; Backspace at qtPos 0 with empty buffer; Delete at qtPos `bufferText.length()` with empty buffer. | v1 §3.3; post-mortem §5.2, §5.3 |
| H5 | `LiveBlockModel` stays parser-pure. Holes live in `LiveHoleLayer`. The QML ListView binds to a concatenating proxy `LiveProxyBlockModel`. | audit §c L9; post-mortem §4 |
| H6 | Cursor delivery into a hole or into a post-reification real row uses `LiveCursorState::requestTextCaretAtRow` operating in proxy-row coordinates; resolves on `LiveProxyBlockModel::rowsInserted`. No `Qt.callLater` retry. | C-spec §5.3; post-mortem §2 (F2, F4 strengthened) |
| H7 | Idle-commit timer is paused during IME composition for the hole's delegate. | post-mortem §3.2 |
| H8 | While a hole is open, Ctrl-Z runs undo within the hole's local buffer (snapshot stack with `UndoCoalescer`-style coalescing); on empty buffer, next Ctrl-Z drops the hole. | post-mortem §3.3 |
| H9 | Selection across holes is "include": hole rows participate in cross-row selections; `serializeForCopy` returns bufferText for hole rows; on reification anchors hold. | post-mortem §3.5 |
| H10 | Save with multiple pending holes commits in ascending `reifyAnchor` byte order, recomputing offsets between commits. | post-mortem §3.6 |
| H11 | Every async-UX test in R5.5+ uses `LiveRealisticInputHarness`. The R5.5 plan's first task is the harness, gated by a synthetic-broken-stub test that proves the harness sees v0-style F2 race. | post-mortem §3.7, §6 |

---

## 2. Architecture overview

### 2.1 Layer position

The post-mortem proposed adding holes as a sibling to `LiveSpeculationLayer` rather than extending it (predictions are within-row overlays; holes add/remove rows; categorically different reconciliation). Under the C-spec's L0–L8 layering, holes sit at L6.5 — one rung above paragraph editing (L4) and structural keys (L5), one rung below other text blocks (L6), parallel to speculation:

```
┌──────────────────────────────────────────────────────────────────┐
│ L8  Interactive blocks                                           │
├──────────────────────────────────────────────────────────────────┤
│ L7  Structured text blocks                                       │
├──────────────────────────────────────────────────────────────────┤
│ L6  Other text blocks                                            │
│       LiveSpeculationLayer (predictions: inline-format, fence)   │
│       LiveHoleLayer (phantom rows: paragraph holes)        ← new │
│       LiveProxyBlockModel (concatenating proxy)            ← new │
├──────────────────────────────────────────────────────────────────┤
│ L5  Structural keys                                              │
├──────────────────────────────────────────────────────────────────┤
│ L4  Paragraph editing                                            │
├──────────────────────────────────────────────────────────────────┤
│ L3  Cursor + selection                                           │
├──────────────────────────────────────────────────────────────────┤
│ L2  Diff-driven model    (LiveBlockModel — parser-pure)          │
├──────────────────────────────────────────────────────────────────┤
│ L1  Read-only render                                             │
├──────────────────────────────────────────────────────────────────┤
│ L0  Coordinate primitives                                        │
└──────────────────────────────────────────────────────────────────┘
```

The proxy belongs at L6 alongside speculation: it's the layer between L2's parser-pure model and the QML ListView's binding. L3 (cursor + selection) and L5 (structural keys) consume the proxy through public-API row-index calls; they don't need to know about the proxy's internals beyond its row count and `IsHoleRole`.

### 2.2 Data flow at a glance

```
MarkoffDocument (CRDT, authoritative source)
    │
    │ parseUpdated(parsed, parseSeq, blockAnchors, parseInputEditSeq)
    ▼
LiveListModelBinding
    ├─ runs AstBlockDiff → applyOps
    │      ▼
    │   LiveBlockModel  (parser-pure rows)
    │
    └─ holds LiveHoleLayer
           ▼
        LiveHoleLayer (zero or more BlockHole items)
                ▼
            LiveProxyBlockModel  (parser rows ⊕ hole rows, anchor-ordered)
                ▼
            QML ListView binds here
```

The proxy is downstream of both inputs. It re-emits `rowsInserted` / `rowsRemoved` / `dataChanged` as either input changes. The QML ListView binds to it; delegates render against its roles.

### 2.3 Lifecycle in one paragraph

User presses Enter at end of paragraph P. `LiveStructuralKeyHandler`'s paragraph-kind handler:

1. Resolves the cursor's CRDT-anchored byte offset → reify byte position = end of P's source range.
2. Calls `LiveHoleLayer::createBlockHole(Kind::Paragraph, anchorAt(reifyByte))`.
3. The layer creates a `BlockHole` with empty `bufferText`, emits `holeInserted(holeId)`.
4. The proxy translates: `holeInserted` → proxy `rowsInserted` at `proxyRowForHole(holeId)`.
5. `LiveCursorState::requestTextCaretAtRow(proxyRowForHole(holeId), 0)` — already pending or fired now; resolves on the proxy's `rowsInserted` for that row.
6. The hole row's `ParagraphDelegate` materialises with `isHole === true`. Its TextEdit is empty; it has activeFocus.
7. User types. The delegate's `LiveEditBinding` (per-delegate, R4) routes `contentsChange` not to `MarkoffDocument::applyLocalEdit` (the source rope), but to `LiveHoleLayer::setBlockHoleBuffer(holeId, newText)`. The layer emits `holeBufferChanged(holeId)`. The proxy emits `dataChanged(proxyRow, BufferTextRole)`. The TextEdit re-binds; the user sees their typing.
8. Each keystroke restarts the layer's per-hole 250 ms idle timer.
9. At idle: `commitBlockHole(holeId)` → `MarkoffDocument::applyLocalEdit({insert "\n\n" + bufferText at reifyByte})` → drops the hole synchronously → emits `holeReified(holeId, newRowAnchor)` → proxy emits `rowsRemoved` for the hole row + propagates `LiveBlockModel`'s upcoming `rowsInserted` for the new parser row.
10. `LiveCursorState`, watching for the reified hole's transition, calls `requestTextCaretAtRow(proxyRowForReifiedRow, bufferText.length())`. Resolves on the new parser row's arrival in the proxy. Cursor lands at the end of the typed text in the now-real paragraph. Caret blink continues; user keeps typing.

The user-visible trace: press Enter, see new paragraph, type into it, idle, paragraph stays, keep typing. From their POV, indistinguishable from typing into a normal paragraph. From the architecture's POV, the row was view-side state for ~250 ms and source-side state from then on.

---

## 3. `LiveHoleLayer` API

### 3.1 Header sketch

```cpp
// libs/markoff-live-render/include/markoff-live-render/LiveHoleLayer.h

namespace Markoff::LiveRender {

enum class HoleKind {
    Paragraph,                         // v2 ships only this
    // ListItem, FenceInterior, ... — future
};

class LiveBlockModel;                  // forward
class UndoCoalescer;                   // forward

class LiveHoleLayer : public QObject {
    Q_OBJECT
public:
    explicit LiveHoleLayer(Markoff::MarkoffDocument *doc,
                           LiveBlockModel *blockModel,
                           UndoCoalescer *undoCoalescer,
                           QObject *parent = nullptr);

    // ---------- Hole lifecycle ----------
    /// Creates a hole at the given CRDT-anchored reify position.
    /// Returns a non-zero holeId. The hole begins with an empty bufferText.
    /// Emits holeInserted(holeId).
    quint64 createBlockHole(HoleKind kind, Markoff::TextAnchor reifyAnchor);

    /// Replaces the hole's bufferText. Restarts the per-hole idle timer
    /// (unless IME composition is active for the hole's delegate, per H7).
    /// Emits holeBufferChanged(holeId).
    void setBlockHoleBuffer(quint64 holeId, const QString &text);

    /// Atomically: applyLocalEdit("\n\n" + bufferText) at reifyAnchor;
    /// drops the hole; emits holeReified.
    /// If buffer is empty, behaves as abandon() instead.
    void commitBlockHole(quint64 holeId);

    /// Drops the hole; no source mutation. Emits holeAbandoned.
    /// Caller is responsible for routing focus to a live neighbor.
    void abandonBlockHole(quint64 holeId);

    /// Save-flush path. Commits all pending holes in ascending
    /// reifyAnchor byte order (per H10).
    void commitAllPendingHoles();

    // ---------- IME guard (H7) ----------
    /// LiveEditBinding calls this when its delegate enters/leaves
    /// IME composition. While true for the hole's delegate, the
    /// hole's idle-commit timer is paused.
    void setHoleComposition(quint64 holeId, bool composing);

    // ---------- Lookups ----------
    int holeCount() const noexcept;
    bool exists(quint64 holeId) const noexcept;
    HoleKind kind(quint64 holeId) const;
    QString bufferText(quint64 holeId) const;
    Markoff::TextAnchor reifyAnchor(quint64 holeId) const;

    /// Returns hole IDs in ascending reifyAnchor byte order.
    QList<quint64> holesInOrder() const;

    /// Returns the hole whose reifyAnchor falls between parser blocks
    /// at innerRow and innerRow+1 (or after the last block when
    /// innerRow == LiveBlockModel::rowCount() - 1). Empty list if none.
    QList<quint64> holesBetweenInnerRows(int innerRow) const;

    // ---------- Per-hole undo (H8) ----------
    /// Pushes a snapshot of the hole's current bufferText onto its
    /// per-hole undo stack. Coalescing policy mirrors UndoCoalescer
    /// (consecutive printables in same focus context coalesce; broken
    /// by non-printables, idle threshold, paste, structural change).
    void recordHoleUndoPoint(quint64 holeId);

    /// Pops the last snapshot. If the stack is empty (or restoring
    /// would yield empty bufferText after an explicit prior snapshot
    /// said otherwise), returns false; caller (UndoCoalescer host)
    /// then drops the hole via abandonBlockHole.
    bool undoBlockHole(quint64 holeId);

    /// Symmetric.
    bool redoBlockHole(quint64 holeId);

Q_SIGNALS:
    void holeInserted(quint64 holeId);
    void holeBufferChanged(quint64 holeId);
    void holeReified(quint64 holeId, Markoff::TextAnchor newRowAnchor);
    void holeAbandoned(quint64 holeId);

private Q_SLOTS:
    void onIdleTimerFired();             // per-hole timer; QHash<quint64, QTimer*>
    void onParseUpdated(/*...*/);        // recompute reifyAnchor invariants

private:
    struct HoleEntry {
        HoleKind kind;
        Markoff::TextAnchor reifyAnchor;
        QString bufferText;
        bool composing = false;
        QTimer *idleTimer = nullptr;
        QStack<QString> undoStack;       // snapshots; coalesced per H8
        QStack<QString> redoStack;
        QElapsedTimer lastEditTimer;     // for coalescing decisions
        QString lastFocusContext;
    };
    QHash<quint64, HoleEntry> m_holes;
    quint64 m_nextHoleId = 1;

    Markoff::MarkoffDocument *m_doc;
    LiveBlockModel *m_blockModel;
    UndoCoalescer *m_undoCoalescer;
};

}  // namespace Markoff::LiveRender
```

### 3.2 Behaviour notes

- **Anchor stability across remote edits.** `reifyAnchor` is `Markoff::TextAnchor` — already CRDT-anchored. Foundation translation (`MarkoffDocument::resolveTextAnchor`) handles remote-edit invalidation. If a remote edit deletes the byte range containing `reifyAnchor`, the resolution returns `nullopt` on the next parse arrival; the layer drops the hole (per v1 spec §9 collab rule).
- **Idle timer mechanics.** One `QTimer` per hole; restart on `setBlockHoleBuffer` (unless `composing == true`); fire interval 250 ms. On `setHoleComposition(holeId, false)` after a `setHoleComposition(holeId, true)`, the timer restarts with the full 250 ms (post-composition, it is as if a keystroke just landed).
- **Reification synchrony.** `commitBlockHole`'s ordering: (1) `applyLocalEdit` → `editSequence` bumps; (2) drop the hole from internal storage; (3) emit `holeAbandoned` for the proxy to remove the hole row; (4) emit `holeReified` for cursor delivery. Parse-back arrives later on its own clock; the proxy emits `rowsInserted` from `LiveBlockModel`'s own signal when the parser block lands. The pending `requestTextCaretAtRow` resolves on that arrival.
- **Save-flush ordering.** `commitAllPendingHoles` iterates `holesInOrder()` (ascending reifyAnchor); each commit shifts subsequent reifyAnchors by `2 + bufferText.length()` bytes. Anchor stability handles this naturally — `reifyAnchor` survives the previous commit's edit because it's anchored, not absolute.
- **No singleton.** One `LiveHoleLayer` per `LiveListModelBinding`; lifetime equals the binding's. (Mirrors C-spec invariant 14.)

---

## 4. `LiveProxyBlockModel` API

### 4.1 Shape

A `QAbstractListModel` (not `QAbstractProxyModel` — the proxy model class assumes a stable 1:1 mapping with one source, which doesn't fit two-sourced row composition cleanly). Implementation pattern: hold a flat `QVector<ProxyRow>` recomputed on input changes; emit Qt model signals (`beginInsertRows`/`endInsertRows`, etc.) around mutations.

```cpp
// libs/markoff-live-render/include/markoff-live-render/LiveProxyBlockModel.h

namespace Markoff::LiveRender {

class LiveProxyBlockModel : public QAbstractListModel {
    Q_OBJECT
public:
    LiveProxyBlockModel(LiveBlockModel *inner,
                        LiveHoleLayer *layer,
                        QObject *parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Roles (extend LiveBlockModel's roles)
    enum Roles {
        // First, all of LiveBlockModel::Roles values are passed through.
        IsHoleRole = LiveBlockModel::CustomRoleStart + 100,  // bool
        BufferTextRole,                                      // QString — alias of TextRole for hole rows
        HoleIdRole,                                          // quint64; 0 for non-hole rows
    };

    // Mapping helpers (used by LiveCursorState, LiveSelectionView,
    // LiveStructuralKeyHandler)
    int innerRowForProxy(int proxyRow) const;            // -1 if hole
    int proxyRowForInner(int innerRow) const;
    int proxyRowForHole(quint64 holeId) const;            // -1 if hole gone
    bool proxyRowIsHole(int proxyRow) const noexcept;
    quint64 holeAtProxyRow(int proxyRow) const noexcept;  // 0 if not a hole

private Q_SLOTS:
    void onInnerRowsInserted(const QModelIndex &, int first, int last);
    void onInnerRowsAboutToBeRemoved(const QModelIndex &, int first, int last);
    void onInnerDataChanged(const QModelIndex &tl, const QModelIndex &br,
                            const QVector<int> &roles);
    void onInnerModelReset();
    void onHoleInserted(quint64 holeId);
    void onHoleBufferChanged(quint64 holeId);
    void onHoleAbandoned(quint64 holeId);
    void onHoleReified(quint64 holeId, Markoff::TextAnchor newRowAnchor);

private:
    struct ProxyRow {
        bool isHole;
        union {
            int innerRow;          // when !isHole
            quint64 holeId;        // when isHole
        };
    };
    QVector<ProxyRow> m_rows;

    LiveBlockModel *m_inner;
    LiveHoleLayer *m_layer;

    void rebuildMapping();        // full recompute on parse arrival or model reset
    void insertHoleRow(quint64 holeId, int proxyRow);
    void removeHoleRow(int proxyRow);
};

}  // namespace Markoff::LiveRender
```

### 4.2 Mapping rule

A hole `H` with `reifyAnchor` resolving to byte `B`:

- Find inner block `K` such that `K.startByte ≤ B < K.endByte` OR (`B == K.endByte` AND no `K' > K` exists with `K'.startByte ≤ B`). The hole sits *after* K.
- Proxy-row position of H = `proxyRowForInner(K's innerRow) + 1 + (count of holes H' with reifyAnchor < H's reifyAnchor whose K-target == K)`.

Stable ordering requires hole-IDs to break ties when two holes share the same `reifyAnchor` byte (rare; can happen if two holes are created in the same parse cycle without intervening commit). Use insertion order: lower `holeId` sits earlier.

### 4.3 Signal propagation

| Inner signal | Proxy action |
|---|---|
| `LiveBlockModel::rowsInserted(first, last)` | Recompute mapping; emit `rowsInserted` for the same proxy-row range, adjusted for any hole rows that should now sit between the new inner rows. |
| `LiveBlockModel::rowsAboutToBeRemoved(first, last)` | Emit `rowsAboutToBeRemoved` for the proxy rows mapping to these inner rows; recompute mapping. |
| `LiveBlockModel::dataChanged(tl, br, roles)` | If `roles` includes role indices that the proxy maps to its own roles, re-emit `dataChanged` against proxy rows. |
| `LiveBlockModel::modelReset()` | `beginResetModel`; clear mapping; rebuild; `endResetModel`. **All open holes are dropped on reset** (per the design doc's behaviour at `MarkoffDocument::resetContent` — loading a different file abandons in-flight holes). |
| `LiveHoleLayer::holeInserted(holeId)` | Compute proxy-row position from `reifyAnchor`; `beginInsertRows` / `endInsertRows`. |
| `LiveHoleLayer::holeBufferChanged(holeId)` | `dataChanged(proxyRow, [TextRole, BufferTextRole])` to refresh the delegate. |
| `LiveHoleLayer::holeAbandoned(holeId)` | `beginRemoveRows` / `endRemoveRows`. |
| `LiveHoleLayer::holeReified(holeId, newAnchor)` | `beginRemoveRows` / `endRemoveRows` for the hole row. The forthcoming `LiveBlockModel::rowsInserted` from the parse-back will trigger the new parser row's appearance separately — this is two distinct mutations, not one swap. |

### 4.4 Roles passthrough

Most roles pass through inner `LiveBlockModel` roles unchanged. For hole rows:

- `IsHoleRole` returns `true`.
- `TextRole` returns `bufferText` (so an existing delegate written to `text: model.text` works without modification).
- `BufferTextRole` returns `bufferText` (explicit alias for the hole-aware delegate path).
- `HoleIdRole` returns `holeId`.
- `BlockKindRole` returns `"paragraph"` (so the QML `DelegateChooser` picks `ParagraphDelegate.qml`).
- `BlockAnchorRole` returns `reifyAnchor` (used by selection serialization; the anchor is the "where in source this projects from").
- `LastEditEditSequenceRole` returns `0` — the freshness rule is N/A for hole rows; their content is not in the CRDT.

---

## 5. `LiveCursorState` integration

### 5.1 Proxy-row coordinates

`LiveCursorState::requestTextCaretAtRow(proxyRow, qtPos)` already operates on the model the QML ListView binds to. With `LiveProxyBlockModel` sitting at that point, the API takes proxy rows by construction. No signature change.

### 5.2 Resolution against hole rows vs parser rows

The pending mechanism (`requestTextCaretAtRow` resolves on `rowsInserted`) is uniform: the proxy emits `rowsInserted` for both hole and parser-row insertions. The pending request resolves on the matching proxy-row arrival regardless of source.

One refinement: a request targeting a hole row can resolve *immediately* (no pending state) when `createBlockHole` has already run and the hole row is already in the proxy. Most callers schedule the request before creating the hole (the structural-key handler issues `request` then `createBlockHole` in sequence); the request resolves on the `holeInserted`-driven `rowsInserted` synchronously with the model change.

### 5.3 Reification cursor delivery

`LiveStructuralKeyHandler` issues `commitBlockHole`. The layer:

1. Emits `holeAbandoned` (proxy removes the hole row).
2. Emits `holeReified(holeId, newRowAnchor)`.

`LiveCursorState`, listening to `holeReified`, schedules `requestTextCaretAtRow(proxyRowForAnchor(newRowAnchor), bufferText.length())`. The proxy-row index is computed against `newRowAnchor` — at this moment the parse-back has not yet arrived, so the proxy-row may not exist; the request goes pending, waits on `LiveProxyBlockModel::rowsInserted` for the matching anchor.

When the parse-back arrives: `LiveListModelBinding` runs `applyOps` on `LiveBlockModel`; the new paragraph row is inserted at innerRow K; `rowsInserted` propagates through the proxy; the pending `LiveCursorState` request resolves; the new delegate's `routeFocusIn` places the caret at qtPos `bufferText.length()`. Cursor is now in the new real paragraph at the end of the typed text. User can keep typing.

The cross-window timing is bounded by parse latency (~30–100 ms typical). During this window the proxy has no hole row and no parser row at that anchor — the row count is one less than it was. The QML ListView relayouts; subsequent rows shift up by one for the duration of the window. This is unavoidable and acceptable: the user sees a single brief flicker, not the v0 character scramble.

---

## 6. `LiveStructuralKeyHandler` integration

### 6.1 Hole-row dispatch

The handler's existing dispatch table is keyed on `(BlockKindDescriptor, key, modifiers)`. Holes route through the same descriptor (paragraph kind) but via a separate code path triggered by `proxyRowIsHole(focusedProxyRow) == true`.

Hole-aware key handling — paragraph kind only for v2:

| Key + state | Action |
|---|---|
| Enter, end-of-buffer (qtPos == bufferText.length()), buffer non-empty | Commit hole (idle path runs synchronously); create new hole below; route cursor into new hole at qtPos 0. |
| Enter, mid-buffer, buffer non-empty | Split: commit prefix hole as `applyLocalEdit("\n\n" + prefix)`; replace remaining hole's bufferText with suffix; cursor moves to qtPos 0 of the (post-split) hole. |
| Enter, buffer empty | No-op (per v1 §3.3 "stacked Enter on empty hole"). |
| Esc, any state | Abandon; route focus to nearest live neighbor (previous parser row's end). |
| Backspace, qtPos == 0, buffer empty | Abandon; route focus to nearest live neighbor (previous parser row's end). |
| Backspace, qtPos == 0, buffer non-empty | Default text-edit behaviour (no-op at qtPos 0; nothing to delete in front of cursor). |
| Delete, qtPos == bufferText.length(), buffer empty | Abandon; route focus to next parser row's start. |
| Other characters | Pass through to `LiveEditBinding` → `setBlockHoleBuffer`. |

### 6.2 Source-row dispatch (currently in R5 Tasks 4–10)

Existing R5 dispatch for paragraph EOB-Enter changes from:

```
applyLocalEdit("\n\n" at currentBlockEnd);
requestTextCaretAtRow(blockIndex + 1, 0);
```

to:

```
LiveHoleLayer::createBlockHole(Paragraph, anchorAt(currentBlockEnd));
// hole layer emits holeInserted; proxy emits rowsInserted;
// LiveCursorState's requestTextCaretAtRow(proxyRowForHole, 0) resolves.
LiveCursorState::requestTextCaretAtRow(proxyRowForHole(holeId), 0);
```

Start-of-paragraph Enter is symmetric (currently `applyLocalEdit("\n\n" at blockStart)`), changed to `createBlockHole(...)` with reify anchor at the previous block's end.

Mid-block Enter (the case that *does* work today via parser-driven row creation) is unchanged. The R5 task 5 implementation stands.

### 6.3 Backspace at the start of a hole row's neighbor

A real paragraph follows a hole row in the proxy. User has caret in the real paragraph at qtPos 0 and presses Backspace. The structural-key handler dispatches against the *focused* row (the real paragraph) — its handler is the existing R5 Backspace-merge with the *previous* block. The previous proxy row is the hole. Two sub-cases:

- Hole is empty: abandon the hole; merge real paragraph with what was the hole's previous block (the original paragraph above the hole).
- Hole is non-empty: commit the hole (idle path); then the standard Backspace-merge runs against the now-real previous paragraph.

This is a small extension to R5 Task 6's Backspace-merge handler.

---

## 7. `LiveSelectionView` integration

### 7.1 Operates on proxy rows

`LiveSelectionView` projects `Session::primarySelection` ↔ Shape-1 form. Its row arithmetic uses proxy rows (the model the QML ListView binds to). No structural changes; the projection naturally handles hole rows because they are rows in the proxy.

### 7.2 `serializeForCopy` for hole rows

The `ParagraphDelegate.qml`'s `serializeForCopy()` returns `model.text` (which is `bufferText` for hole rows via the role passthrough in §4.4). A multi-row selection that crosses a hole produces clipboard text with the hole's typed content embedded. From the user's POV, copying [paragraph A, hole B (typed), paragraph C] yields `"A\n\nB-buffertext\n\nC"` — what they see on screen.

### 7.3 Anchor preservation across reification

Selection endpoints are `Cursor` values (Shape 1: `TextCaret | BlockSelected | BlockInternalEdit`) holding CRDT-anchored positions. A `TextCaret` whose `block` was a hole's `BlockId` (synthetic — see §10) is invalidated when the hole reifies; the layer issues a remap signal that `LiveSelectionView` consumes to refresh.

The handling is structurally identical to the C-spec's §3.5 collab-survival rule: when `block` is fully deleted, collapse to nearest neighbor. For hole reification, the "deleted" hole's content is now in a new parser row at the same anchor, so the collapse target is that parser row at the appropriate qtPos (0 for the start of the new row; `bufferText.length()` for the end). The implementation in `LiveSelectionView` handles this in one slot listening on `holeReified`.

---

## 8. Freshness-rule integration

Holes do not participate in the freshness rule. The rule (`row.lastEditEditSequence ≤ parseInputEditSequence`) governs whether parser text-role updates are applied to a row whose CRDT bytes the user has been typing into. Holes have no CRDT bytes corresponding to their `bufferText`; there is nothing for the parser to update.

`LiveBlockModel`'s per-row `lastEditEditSequence` field is unchanged in semantics. It applies to parser rows. The proxy passes it through for parser rows; for hole rows it returns `0` (always-fresh, but the role is not consulted for hole rows because parser updates don't target them).

This is the cleanest possible integration: by keeping `LiveBlockModel` parser-pure, the freshness rule remains a parser-pure concept.

---

## 9. `UndoCoalescer` integration

### 9.1 Two undo regimes

Per H8: while a hole is open, undo runs against the hole's local buffer. After commit (or for any non-hole context), undo runs against the CRDT through `MarkoffDocument::undo`.

`UndoCoalescer` already has a single dispatch site (R5 Task 12 — paused — wires it into `LiveEditBinding`). The host adds a check before dispatching: if the focused row is a hole, route to `LiveHoleLayer::undoBlockHole(holeId)`; otherwise route to `MarkoffDocument::undo` (the existing path).

### 9.2 Per-hole undo stack mechanics

`HoleEntry::undoStack` holds bufferText snapshots. Snapshots are pushed when:

- Idle 250 ms after the most recent edit (matches the `UndoCoalescer` policy of breaking coalescence on idle).
- Explicit non-printable (e.g. paste) lands.
- Mode switch (focus enters/leaves, but in the hole's case focus-leaving triggers commit-or-abandon, so this case is moot).
- Composition end (IME committed glyph is one undo group).

Between snapshot points, consecutive printables coalesce — they update the head of the stack rather than pushing.

`undoBlockHole(holeId)` pops the head into `bufferText` (and pushes the previous head onto the redo stack). Returns `true`. If the stack is empty *and* `bufferText.isEmpty()`, returns `false` to signal that the next undo should drop the hole. `UndoCoalescer`'s host handles the `false` return by calling `abandonBlockHole` and routing focus to the previous neighbor.

### 9.3 Undo across reification

After `commitBlockHole`, the hole's per-hole undo stack is discarded. The reification produces one CRDT undo entry (the `applyLocalEdit` for `"\n\n" + bufferText`). Pressing Ctrl-Z after reification restores the CRDT to its pre-commit state (the buffer text disappears from source); the hole does *not* reappear. This matches v1 §6.case-2 ("Ctrl-Z runs normal CRDT undo on the commit edit").

---

## 10. `BlockId` for holes

The C-spec's cursor model (§3.1) uses `BlockId = Markoff::BlockAnchor`, a CRDT-anchored value. Holes have no CRDT block — the parser produced no block for them. Two options:

A. **Synthetic `BlockAnchor`.** Issue a sentinel anchor that is not derived from any CRDT byte. Foundation's `MarkoffDocument::resolveTextAnchor` would refuse to translate it; the view layer is the only consumer.

B. **Use the hole's `reifyAnchor` as the `BlockId`.** Two holes at the same reifyAnchor share an ID; the cursor variant additionally carries `holeId` for disambiguation.

Option A is cleaner — it makes the type-distinct, prevents accidental foundation translation. Option B reuses an existing type but couples hole identity to anchor identity, which fails the "two holes at same anchor" case (rare but real per §4.2's tie-break).

**Resolution: Option A with a tagged value.** Define:

```cpp
struct HoleBlockId {
    quint64 holeId;
};
struct BlockId {
    std::variant<Markoff::BlockAnchor, HoleBlockId> id;
};
```

The cursor model's `BlockId` becomes a discriminated union. `TextCaret::block` accepts both; the variant tag is what `LiveStructuralKeyHandler`, `LiveSelectionView`, and `LiveCursorState` dispatch on for hole-vs-parser routing.

This is a small change to the C-spec's §3.1 type definition and a §15 open question gets a concrete answer. Documented as a spec amendment delta in §13.

---

## 11. `LiveRealisticInputHarness` API

### 11.1 Header

```cpp
// libs/markoff-live-render/tests/LiveRealisticInputHarness.h

namespace Markoff::LiveRender::Test {

class LiveRealisticInputHarness {
public:
    explicit LiveRealisticInputHarness(QQuickWindow *window,
                                       int defaultGapMs = 30);

    /// keyClick + qWait(gapMs) + processEvents.
    void keyClick(Qt::Key key,
                  Qt::KeyboardModifiers mods = Qt::NoModifier);
    void keyClick(Qt::Key key,
                  Qt::KeyboardModifiers mods,
                  int gapMs);

    /// Types each character with the default gap. Convenience for
    /// "type a paragraph at human speed."
    void typeString(const QString &text);

    /// Burst (no gap between chars within burst), then qWait(pauseMs)
    /// + processEvents. Models "type-pause-type" patterns.
    void burst(const QString &chars, int pauseMs);

    /// Idle period (no input, just qWait + processEvents).
    /// Used to wait for idle-commit timers.
    void idle(int durationMs);

private:
    QQuickWindow *m_window;
    int m_defaultGapMs;
};

}  // namespace Markoff::LiveRender::Test
```

### 11.2 The gate test

R5.5 Task 1 builds the harness *and* a synthetic-broken delegate stub (a `ParagraphDelegate`-shaped QML file under tests/ that destroys itself on first keystroke and re-materialises on parse-back, mimicking v0's reify-on-first-keystroke). The gate test:

```cpp
TEST_CASE("harness sees v0-style F2 character scramble") {
    SyntheticBrokenDelegate delegate;          // v0 behaviour
    LiveRealisticInputHarness h(window);

    delegate.focus();
    h.typeString("this is interesting");

    QString delegateText = delegate.allDeliveredKeys();
    QVERIFY2(delegateText != "this is interesting",
             "Harness failed to expose v0 race; tests under this "
             "harness will not catch real async-UX regressions.");
}
```

If this assertion fails (i.e., `delegateText == "this is interesting"`), the harness is too lenient — fix the harness (longer gaps, more processEvents, etc.) until the assertion passes. Then delete the synthetic-broken-delegate stub before any v2 hole code lands.

This converts the brief's *"Why will my test see the v0 race?"* gate into a concrete artifact in the repository. Future R6/R7/R8 tests using the harness inherit the gate's coverage; the synthetic stub is the proof-of-coverage one-time.

### 11.3 Default gap

30 ms default; sustained 30 ms intervals model ~33 chars/s ≈ 60 wpm typed prose. The R5.5 stress test uses 30 ms; tests for slower interaction patterns (consider/click/type) use longer per-call values via the explicit-gap overload.

---

## 12. Test plan (informs the R5.5 plan, not exhaustive)

| Test | What it asserts | Discipline |
|---|---|---|
| Gate: harness sees v0 race | The harness reproduces F2 against a synthetic-broken stub | Required first; deletes the stub on pass |
| `LiveProxyBlockModel`: insert hole between parser rows | Proxy row count, IsHoleRole, BufferTextRole, ordering | Unit; no harness needed |
| `LiveProxyBlockModel`: parse-back inserts new parser row above pending hole | Hole row's proxy-row index updates correctly | Unit |
| `LiveProxyBlockModel`: model reset drops all holes | After `MarkoffDocument::resetContent`, layer holes count is 0 | Unit |
| `LiveHoleLayer`: idle commit at 250 ms | Source contains buffered text; hole is gone | Harness (`idle(300)`) |
| `LiveHoleLayer`: commit aborted by IME composition | Idle 250 ms during composition does NOT commit; commit happens after composition end + 250 ms | Harness; QInputMethodEvent injection |
| `LiveHoleLayer`: focus-out commit with content | bufferText flushes to source | Harness |
| `LiveHoleLayer`: focus-out abandon with empty buffer | No source mutation; hole gone | Harness |
| `LiveHoleLayer`: Esc abandons | No source mutation | Harness |
| `LiveHoleLayer`: Backspace at qtPos 0 with empty buffer abandons | No source mutation; cursor at end of previous parser row | Harness |
| `LiveHoleLayer`: Delete at end with empty buffer abandons | No source mutation; cursor at start of next parser row | Harness |
| `LiveHoleLayer`: stress-typing into hole | Source after idle commit equals typed string in order | **Harness — load-bearing**; types 200 chars at 30 ms gap |
| `LiveHoleLayer`: undo within hole | Per-hole undo stack pops snapshots; final empty buffer + Ctrl-Z drops hole | Harness |
| `LiveHoleLayer`: undo after commit | Ctrl-Z restores pre-commit CRDT; hole does NOT reappear | Harness |
| `LiveStructuralKeyHandler`: paragraph EOB-Enter creates hole | Hole layer count goes 0 → 1; proxy rows go N → N+1 | Unit |
| `LiveStructuralKeyHandler`: paragraph mid-buffer Enter splits | Prefix committed; suffix in fresh hole; cursor at qtPos 0 of new hole | Harness |
| `LiveStructuralKeyHandler`: stacked Enter on empty hole = no-op | Single hole persists | Unit |
| `LiveStructuralKeyHandler`: stacked Enter on non-empty hole | Commit + new hole | Harness |
| `LiveSelectionView`: cross-row selection includes hole | `serializeForCopy` returns concatenated text including bufferText | Harness |
| `LiveSelectionView`: selection survives reification | Anchor preserved; row resolution updates | Harness |
| `LiveCursorState`: pending request resolves on hole creation | Cursor lands at qtPos 0 of new hole | Unit |
| `LiveCursorState`: pending request resolves on reified row | After commit + parse-back, cursor at end of typed text in new real row | Harness |
| Save flushes pending hole | After `Ctrl-S`, source contains bufferText; hole is gone; saved file matches | Harness |
| Multi-hole save commits in anchor order | Edge case; offsets recompute correctly | Unit |
| Concurrent parse-back during hole life | Hole's proxy row recomputes correctly when an unrelated parse-back arrives | Harness with deliberate parse-injection |
| Dogfood gate (R5.5) | User types 200 words across 10 paragraphs, every Enter creates a hole that reifies to a real paragraph | Manual |

---

## 13. Spec amendment deltas

For the next step (spec-amendment proposal); listed for completeness, not edited yet.

| Spec section | Current | Amended |
|---|---|---|
| Premise 6 | "Notion-style: Enter creates a new block; Shift-Enter inserts a soft break (`\n`) within the current block. **EOB-Enter hole feature is deleted.**" | "Notion-style: Enter creates a new block; Shift-Enter inserts a soft break (`\n`). EOB-Enter and start-of-paragraph Enter create a `LiveHoleLayer` hole that reifies on idle / focus-out / save / explicit Enter (per the v2 design at `docs/specs/2026-05-03-v2-holes-design.md`). The v0 hole implementation is permanently retired; v2 is structurally distinct — IME-preedit pattern + concatenating proxy model." |
| §3.1 type definition | `using BlockId = Markoff::BlockAnchor` | `struct BlockId { std::variant<Markoff::BlockAnchor, HoleBlockId> id; };` (per §10 of v2 design) |
| §4.4 cycle-guards-retired table | Holes-related rows say "Holes retired" | Restore those rows; annotate "v2 design — see `docs/specs/2026-05-03-v2-holes-design.md` §4.3, §6.1; no detach/reattach because applyOps runs against the parser-pure inner model below the proxy" |
| §5.4 structural keys | Implies `\n\n` insertion for paragraph-EOB-Enter | "Paragraph EOB-Enter and start-of-paragraph Enter create a hole via `LiveHoleLayer::createBlockHole`, not `applyLocalEdit('\n\n')`. Mid-block Enter is unchanged." |
| §6.1 L6 | "Predictions (the surviving half of the old projection layer)" | Add `LiveHoleLayer` and `LiveProxyBlockModel` as L6 components alongside `LiveSpeculationLayer`. Architecture diagram updated. |
| §7.2 structural-edit data flow | "applyLocalEdit at edit time → parse-back → rowsInserted" | Replace with hole-create → local-typing → commit-on-trigger flow; mid-block split flow unchanged. |
| §11 R5 acceptance criteria | "caret lands in the new empty paragraph each time" | Caveat: "for end-of-paragraph and start-of-paragraph Enter, this requires R5.5 (paragraph holes); R5 ships with these cases as documented limitations." |
| §11 (new R5.5 phase) | n/a | Add R5.5 — paragraph holes — between R5 and R6. Plan: `docs/plans/2026-05-03-live-render-r5-5-holes.md`. |
| §15 open questions | "consumed structural keys mechanism", "applyTextUpdate shape", … | Add: "v2 holes — paragraph-only initially; full hole inventory deferred per v1's out-of-scope list. The list-item / fence-interior holes for R7 inherit this design's shape." Resolve: "BlockId variant tag for holes — answered §10 of v2 design." |

---

## 14. Out of scope (for v2)

Carried forward from v1 §8 / §9 unchanged:

- Full hole inventory beyond paragraph (list items, checklists, fence interior, blockquote, callout, table cells, links, wikilinks, footnotes, math). Each is a follow-on plan in R7+.
- Configurable abandonment timeouts. (250 ms idle is hard-coded for v2; revisit during dogfood.)
- Hole serialisation across process exit. (Holes do not survive process exit; abandoned at shutdown.)
- Multi-cursor / secondary-selection holes. (Single primary selection only.)
- Heuristic create-on-click (clicking past EOB to create-on-click).
- Cross-block selection refusal/special-rendering when the selection includes a hole; v2 handles the include case (§7.2).
- Soft-Enter (Shift-Enter) inside a hole: defaults to inserting a literal `\n` in `bufferText`; the line-break renders inline within the hole's TextEdit. Reification commits the multiline buffer as-is — `applyLocalEdit("\n\n" + bufferText)` where bufferText may contain `\n`. The parser splits accordingly (single `\n` is a soft break inside paragraph; double `\n` would create two paragraphs, which is not the intended Shift-Enter semantics — therefore we strip any `\n\n` collapse to `\n` on commit, defending the soft-break-only contract). This is a small implementation note for the plan.

Newly explicit out-of-scope (not in v1):

- **Block-kind speculation across hole boundaries.** A speculative fence flip that happens above a hole row should not be invalidated by the hole's existence; reconciliation is unchanged. No special handling required.
- **Inline-format predictions inside a hole.** The hole's TextEdit can host inline formatting overlays via `InlineFormatHighlighter` if desired. v2 does not enable this — predictions are off for hole content; the user types plaintext into the hole, and predictions kick in once the hole reifies. (A hole row's `BlockKindRole` is "paragraph", but the highlighter checks `IsHoleRole` and bails. R5.5 plan task documents this.)
- **Multi-pane / split-window holes.** N/A; the live render is single-pane.

---

## 15. Acceptance criteria

R5.5 ships when:

1. All §12 unit tests pass.
2. The harness gate test passes (synthetic broken stub fails → delete stub).
3. All §12 harness-driven tests pass.
4. Dogfood gate: user types ≥200 words across ≥10 paragraphs in `markoff-live-render-app`; every Enter creates a hole that reifies into a real paragraph; no character scramble; no double-spacing; no source leak (saved file equals on-screen content); arrow keys navigate freely; Esc/Backspace-at-0-empty/Delete-at-end-empty abandon as specified.
5. C-restoration spec amendments (§13) are landed and approved.
6. R5 closes (Tasks 12–18 land independently of R5.5; the EOB-Enter limitation goes away once R5.5 lands).
7. Restoration-status updated: phase board shows R5.5 as `complete`; recent-changes log includes the dogfood-pass entry.

---

## 16. Open questions for the implementation plan

Things this design deliberately does not pin precisely; the R5.5 plan resolves them:

1. **Idle-timer implementation.** One `QTimer` per hole vs single `QTimer` cycling per the head-of-queue hole. v2 spec sketches per-hole; plan picks one with a justification.
2. **Per-hole undo coalescing-policy details.** Idle-threshold for snapshot pushes; whether IME composition commits its own snapshot; whether paste pushes a snapshot eagerly.
3. **Cursor refresh on `holeBufferChanged`.** When `bufferText` is updated externally (e.g., via paste), should cursor preserve its qtPos or move to end-of-buffer? Default: preserve qtPos using LiveEditBinding's anchor protocol; plan validates.
4. **Anchor precision for `reifyAnchor`.** Foundation's `TextAnchor` is byte-precise; for paragraph holes, the anchor is the byte position of the previous paragraph's end. Plan specifies the conversion (`MarkoffDocument::anchorAtByte(byte)`).
5. **`LiveProxyBlockModel` performance.** Full mapping rebuild on every parse-back is O(parserRows + holeCount). Holes are sparse; rebuild is cheap. Plan validates against the §9.3 perf budget; if a problem, switch to incremental update.
6. **Synthetic broken stub format.** v2 design names the stub but doesn't specify its QML; plan writes the stub.
7. **Test harness gap-time tuning.** 30 ms is the v2 default; the gate test may require a different value to reproduce v0's race deterministically. Plan tunes during gate-test development.

These are flagged so the plan picks them up explicitly rather than smuggling decisions into implicit code.

---

## 17. References

- `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md` — call for design.
- `docs/handoff/2026-05-03-r5-holes-postmortem.md` — post-mortem (the input to this design).
- `docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.6 — v1 design + v0 forensics.
- `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md` — v1 redesign brief.
- `docs/specs/2026-05-02-live-render-restoration-design.md` — C architecture spec (to be amended).
- `docs/2026-05-02-live-view-architectural-audit.md` §c L9 — the L9 prescription this design adopts.
- `docs/plans/2026-05-02-live-render-r5-structural-keys.md` — R5 plan (Tasks 1–11 executed).
- `docs/handoff/2026-05-02-restoration-session-brief.md` §3.6 — spec-amendment protocol governing the next step.

---

*End of v2 holes design.*
