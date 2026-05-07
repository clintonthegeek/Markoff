# markoff-core — library guide

The CRDT-backed document + sessions layer. Exposes a view-layer-safe
public API; CRDT primitives stay internal.

## Status (2026-05-07)

D2 (foundation reshape) + D3 (view-layer adaptation, foundation side) +
D4 (parser scope reduction, foundation side) all complete.

D4 landed: `ParsePool`, `ParsePoolWorker`, `IncrementalParseSession`,
`RenderPhases`, `parseUpdated`, `parseSequence`, `MarkoffEdit`,
`applyLocalEdit` all deleted. `applyFlatEdit` is the new flat-text
entry point for source-widget-style edits (see below).

## Public-boundary types (no `<crdt/...>` dependency)

- **`Markoff::TextAnchor`** — opaque wrapper for a CRDT byte anchor
  (replicaId + charValue + bias). Defined in
  `include/markoff/core/TextAnchor.h`. View layers hold these
  by value; conversion to/from `CollabText::Crdt::Anchor` is core-
  internal (`src/AnchorConversion.h`, `Markoff::Detail` namespace).
- **`Markoff::BlockAnchor`** — wrapper struct holding a `TextAnchor`
  at a top-level block's first byte. Type-distinct from `TextAnchor`
  so signatures self-document. Defined in
  `include/markoff/core/BlockAnchor.h`.
- **`Markoff::Selection`** — `(anchor, active)` are `TextAnchor`-typed
  (no longer `Crdt::Anchor`). `Selection.h` does NOT include
  `<crdt/Anchor.h>`.

## Public `MarkoffDocument` accessors (CRDT-free)

- `quint64 editSequence() const noexcept` — bumps on every legacy undo/redo
  and `resetContent` call. Use for dirty-tracking ("[modified]" window title)
  when the legacy undo stack is in use.
- `quint64 d2EditSequence() const noexcept` — bumps on every `applyFlatEdit`
  and block-level D2 edit. Use for dirty-tracking in D2-native views.
- `TextAnchor textAnchorAt(quint32, bool rightBias)` /
  `quint32 resolveTextAnchor(TextAnchor)` — companions to the
  CRDT-typed `anchorAt`/`resolveAnchor` (which stay foundation-
  internal).
- Block-aware queries: `blockAnchorAt(int)`, `blockByteRange(BlockAnchor)`,
  `blockAt(TextAnchor)`, `offsetInBlock(BlockAnchor, TextAnchor)`,
  `textAnchorAt(BlockAnchor, int offset, bool rightBias)`.

## `applyFlatEdit` — flat-text entry point (D4)

```cpp
void applyFlatEdit(int startByte, int endByte,
                   const QByteArray &newText, Origin origin);
```

Decomposes a global byte-range edit into per-block D2 buffer ops.
This is the interface for flat-text consumers (source widget, paste,
programmatic edits) that don't have per-block structure. On an empty
document it auto-creates one Paragraph block. Emits `d2DocumentChanged`
after the edit lands.

- Bumps `d2EditSequence()` (NOT `editSequence()`).
- Undo/redo via `undoD2()` / `redoD2()` (NOT legacy `undo()`/`redo()`).
- Block content does NOT get an auto-appended `\n`; content is exactly
  what `loadFromMarkdown` / `applyFlatEdit` puts in.

## v1.0 public surface (since 2026-05-07)

The consumer-facing primitives in `markoff-core` are:

- `Markoff::CursorPos` — `{line, column}`, 1-based. Header: `<markoff/core/CursorPos.h>`.
- `Markoff::Theme` — opaque value type for editor colours and fonts. Header: `<markoff/core/Theme.h>`.
- `Markoff::EditorContext` — `{blockKind, headingLevel, inTable, tableRow, tableCol}`. `blockKind` is a `QString`; canonical names are `Markoff::BlockKindNames::*`. Header: `<markoff/core/EditorContext.h>`.
- `Markoff::ActionId` — toolbar / formatting action identifier (`Q_ENUM_NS`). Header: `<markoff/core/ActionId.h>`.
- `Markoff::MarkdownView` — concrete QWidget base class for live-edit, live-read-only, and source widgets. Header: `<markoff/core/MarkdownView.h>`.

The CRDT internals (`Markoff::Cmd::*`, `BlockId`, `IdList`, `UndoLog`, `applyFlatEdit`) are in the same `Markoff::` namespace but are not part of the consumer-facing UI surface — they are used by `markoff-live` and `markoff-source` internally.

## CRDT internals (foundation-only)

- `version() → Crdt::Global` is foundation-internal — public boundary
  uses the sequence accessors above.
- `anchorAt(quint32, Crdt::Bias)` / `resolveAnchor(Crdt::Anchor)` are
  the underlying CRDT-typed primitives the TextAnchor versions delegate to.

## Conventions

- Foundation-internal helpers go in `Markoff::Detail` namespace,
  headers in `src/`.
- View-qml does NOT include foundation `src/` headers.
- `Q_DECLARE_METATYPE(Markoff::BlockAnchor)` and runtime
  `qRegisterMetaType<QList<Markoff::BlockAnchor>>()` are wired in
  `MarkoffDocument`'s constructor for cross-thread queued connections.

## See also

- Spec: `docs/specs/2026-04-30-block-anchor-foundation-design.md`
- Plan: `docs/plans/2026-04-30-block-anchor-foundation.md`
- Driving consumer: `docs/plans/2026-04-30-live-editing.md`
- Perf gap: `docs/handoff/2026-04-30-collabtext-crdt-join-perf-handoff.md`
