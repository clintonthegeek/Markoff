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

## Canonical text egress — use `serializeForSave()`, not `toMarkdown()`

`toMarkdown()` / `toMarkdownUtf8()` read the legacy `d->buffer` text store.
`loadFromMarkdown()` and all D2 edits do NOT update that store, so on a
D2-loaded document these accessors return stale-or-empty content. Used
as a save source they silently write empty files
(surfaced 2026-05-21 by Corbomite Vault; see
`docs/handoff/2026-05-21-save-path-data-loss.md`). Both methods carry
`[[deprecated]]` annotations since `bb2e5c0`'s successor.

For any consumer that wants "the document's current canonical bytes"
(save, export, diff-against-disk, copy whole doc to clipboard, etc.)
use:

```cpp
QByteArray bytes = doc->serializeForSave();
```

This walks `iterateBlocks()`, uses per-kind serializers, applies the B1
inter-block separator convention, handles frontmatter and listitem
markers, and uses `blockLoadTimeBytes` for byte-identical round-trip on
untouched blocks.

The legacy buffer remains a real store (`resetContent()` populates it,
legacy `undo()`/`redo()` mutate it, `version()` reads it) but it is no
longer the source of truth for the document's text content. Internal
callers still using `toMarkdownUtf8`/`toMarkdown` (notably
`SourceTextDocumentBinding`'s legacy fallback at
`SourceTextDocumentBinding.cpp:205`, `SearchController.cpp:55`, and a
handful of tests) are tracked for migration as a follow-up.

**Runtime flat view — `widgetFlatView()`.** The flat-text view leaves
(`markoff-styled`, `markoff-source`) consume `MarkoffDocument::widgetFlatView()`
in their QTextDocument, NOT `flatView()`. `widgetFlatView()` joins blocks with
a single `\n` instead of `\n\n`; the visible paragraph gap comes from
`QTextBlockFormat::topMargin`/`bottomMargin` applied at the widget layer (WP
unification, spec `docs/specs/2026-05-28-flat-view-wp-unification-design.md`).
`flatView()` remains the canonical save/parse form and is used by
`serializeForSave` and the binding's reverse-path expected-string-build.

## Single-document binding: canonical structure invariant

Spec: `docs/specs/2026-05-27-markoff-core-binding-robustness-design.md`

**Invariant (enforced ONLY on the `applyFlatEdit` ingress):**
- No internal `\n` in any block buffer (each buffer is a single logical paragraph/block; newlines are structure, not content).
- No unintended empty blocks.
- Inter-block separators in `flatView` are exactly single `\n\n`; the flat representation ends with a single `\n`.

This invariant is enforced by `applyFlatEdit`'s canonicalization pass (splits inserted text on any newline-run, collapsing runs; never creates empty blocks). It is NOT enforced on the per-block path (`d2ApplyBufferEdit`, `d2InsertBlock`), so the live-view leaf's intentional empty-paragraph blocks are safe.

**Load ingress canonicalisation (2026-05-29).** `loadFromMarkdown` (via `buildD2FromBytes`) collapses internal `\n` → space in `Paragraph`, `ListItem`, setext `Heading`, and `BlockQuote` block buffers, so the "no internal `\n`" rule (B1) holds on the load ingress for those kinds. Setext headings additionally have their underline line stripped before the collapse; `serializeHeading` reconstructs the underline from `(content.size(), level)` on save, and `serializeForSave` bypasses its untouched-block fast path for setext so reconstruction always runs (width drift toward title length on touched and untouched blocks is accepted; original underline width is not preserved). `BlockQuote` is split at load (queue #8.1): each parser `block_quote` node's children are emitted as per-child top-level blocks tagged with `BlockQuoteDepth` + `BlockQuoteRunId` attrs. Paragraph children land as `BlockKind::BlockQuote` (keeps live's matcher); non-paragraph children (heading/code/list) land as their native kind plus the same attrs. The buffer is `> `-stripped + `\n→space` collapsed. The serializer reconstructs depth × `> ` prefixes and uses RunId to emit `\n>\n` between same-run paragraphs vs `\n\n` between separate quotes. Untouched quoted blocks always re-serialize (their `blockLoadTimeBytes` is the post-canonicalisation buffer, not source-byte-identical). Spec `docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md`. See guide §0 "Load-side enforcement".

**Interactive-ingress exception — `applyInteractiveNewline`.** The canonical
"no empty blocks" rule holds for the programmatic ingress (`applyFlatEdit`).
The *interactive* ingress `applyInteractiveNewline` (WYSIWYG Enter, routed from
`SourceTextDocumentBinding::onQtContentsChange` for a bare `\n`) deliberately
MAY create a transient empty block — pressing Enter at end of a paragraph must
produce a new empty paragraph with the caret in it. A still-empty paragraph
collapses on serialize/reload (it round-trips as the ordinary `\n\n`
separator), so it never persists. See
`docs/specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`.

**Forward path — `SourceTextDocumentBinding::onQtContentsChange`:**

Edits arrive in separator-view (the flat text the QTextDocument holds). The binding resolves them via `Markoff::Detail::findBlockAtSepByte` (declared in `include/markoff/core/Detail/FlatBlockResolve.h`) and dispatches:
- **Single-block, structure-neutral** → `d2ApplyBufferEdit` (boundary-correct; preserves B1 buffer convention).
- **Cross-block non-structural** (separator-spanning deletes, selection deletes) → direct D2 primitives that merge blocks without going through the flat text.
- **Structural** (newline insertion) → `applyFlatEdit`.

`Markoff::Detail::findBlockAtSepByte` and `sliceByBlocks` are the shared sep-view ↔ block helpers used by both the binding and `markoff-source`.

**Reverse path — `onD2DocumentChanged`:**

When the model changes, the binding pushes content back to the QTextDocument via an incremental common-prefix/suffix text-diff executed through `QTextCursor`. This preserves formatting, cursor position, and scroll. Only the initial-load path (`syncQtDocumentFromMarkoff`) does a full `setPlainText` (nothing to preserve at load time).

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

## Consumer-facing types (kept; v1.0-plan retired)

These types landed before the v1.0 plan was retired (2026-05-07; see
`docs/handoff/2026-05-07-pivot-to-d5-first.md`). They survive into the
post-D5 codebase as **internal-to-markoff** types — `markoff-live` and
`markoff-source` consume them. They are not yet part of a frozen public
API; the public-API freeze is deferred until §4.6 of the pivot doc.

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

### Block buffer convention

Per `docs/INVARIANTS.md` "Block buffer convention" + spec
`docs/specs/2026-05-18-b1-buffer-convention-design.md`: block buffers
hold content only. No trailing structural `\n`. Separators are the
serializer's responsibility (`interBlockSeparator() == "\n\n"`,
`finalDocumentTerminator() == "\n"`).

## See also

- Spec: `docs/specs/2026-04-30-block-anchor-foundation-design.md`
- Plan: `docs/plans/2026-04-30-block-anchor-foundation.md`
- Driving consumer: `docs/plans/2026-04-30-live-editing.md`
- Perf gap: `docs/handoff/2026-04-30-collabtext-crdt-join-perf-handoff.md`
