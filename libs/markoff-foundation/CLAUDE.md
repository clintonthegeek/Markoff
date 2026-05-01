# markoff-foundation — library guide

The CRDT-backed document + sessions + parse-pool layer. Exposes a
view-layer-safe public API; CRDT primitives stay internal.

## Public-boundary types (no `<crdt/...>` dependency)

- **`Markoff::TextAnchor`** — opaque wrapper for a CRDT byte anchor
  (replicaId + charValue + bias). Defined in
  `include/markoff-foundation/TextAnchor.h`. View layers hold these
  by value; conversion to/from `CollabText::Crdt::Anchor` is foundation-
  internal (`src/AnchorConversion.h`, `Markoff::Detail` namespace).
- **`Markoff::BlockAnchor`** — wrapper struct holding a `TextAnchor`
  at a top-level block's first byte. Type-distinct from `TextAnchor`
  so signatures self-document. Defined in
  `include/markoff-foundation/BlockAnchor.h`.
- **`Markoff::Selection`** — `(anchor, active)` are `TextAnchor`-typed
  (no longer `Crdt::Anchor`). `Selection.h` does NOT include
  `<crdt/Anchor.h>`.

## Public `MarkoffDocument` accessors (CRDT-free)

- `quint64 editSequence() const noexcept` — bumps on every state-
  change op (applyLocalEdit, undo, redo, applyRemoteOps, resetContent).
  Use for dirty-tracking ("[modified]" window title) without holding
  a `Crdt::Global`.
- `quint64 parseSequence() const noexcept` — bumps each time
  `parseUpdated` fires. Use for parse-ordering ("is this a newer
  parse than what I rendered?").
- `TextAnchor textAnchorAt(quint32, bool rightBias)` /
  `quint32 resolveTextAnchor(TextAnchor)` — companions to the
  CRDT-typed `anchorAt`/`resolveAnchor` (which stay foundation-
  internal).
- Block-aware queries: `blockAnchorAt(int)`, `blockByteRange(BlockAnchor)`,
  `blockAt(TextAnchor)`, `offsetInBlock(BlockAnchor, TextAnchor)`,
  `textAnchorAt(BlockAnchor, int offset, bool rightBias)`.

## `parseUpdated` signal shape

```cpp
void parseUpdated(const Markoff::Document *parsed,
                  quint64 parseSequence,
                  QList<Markoff::BlockAnchor> blockAnchors);
```

The signal is fired on the main thread from a relay lambda in
`MarkoffDocument`'s constructor; BlockAnchors are computed there
against the current CRDT buffer (see
`docs/specs/2026-04-30-block-anchor-foundation-design.md` §3 for
the staleness caveat).

## CRDT internals (foundation-only)

- `version() → Crdt::Global` is foundation-internal — public boundary
  uses the sequence accessors above.
- `anchorAt(quint32, Crdt::Bias)` / `resolveAnchor(Crdt::Anchor)` are
  the underlying CRDT-typed primitives the TextAnchor versions delegate to.
- `applyLocalEdit` returns `Crdt::Operation` (legacy public surface;
  consumers don't typically use the return value).

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
