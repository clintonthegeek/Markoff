# Marker-paragraph design (replaces v2 holes)

**Date:** 2026-05-03
**Branch:** `exploration/new-foundation`
**Status:** design — supersedes the v2 holes design (`docs/archive/2026-05-03-v2-holes-design.md`) and the paused R5.5 plan (`docs/archive/2026-05-03-live-render-r5-5-holes.md`).
**Predecessors (read first):**
- `docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md` — architectural review whose §3.1 chooses approach (c).
- `docs/handoff/2026-05-03-section-3-1-spike-findings.md` — spike that verified the marker approach against the parser and enumerated leakage paths.
- `docs/specs/2026-05-02-live-render-restoration-design.md` — C-restoration spec (the architecture this design amends).

**Audience:** the implementer; the user (review gate); the next session that derives the new R5.5 plan.

**Bound:** paragraph holes only. Other block-kind hole cases (list-items, fence interiors, blockquote, table cells, etc.) are out of scope — same boundary v2 had. Their treatment may or may not generalise from this design; that is a separate spike when those phases land.

---

## 0. TL;DR

The hole abstraction is replaced with a single source-edit. Pressing Enter at the end (or start) of a paragraph inserts `"\n\n"` plus a single ZERO WIDTH SPACE (U+200B) into the canonical CRDT source. Tree-sitter sees a real paragraph block containing the marker; the existing parser-driven row pipeline delivers the new row to the QML `ListView`; the existing `requestTextCaretAtNewRow` mechanism lands the cursor on it. When the user types, an *atomic-bundled edit* inserts their character and removes the marker as one CRDT op — no race window. A small `MarkerScrubber` service strips any marker that leaks (focus moved away without typing, save with marker present, file loaded with markers from another tool).

The design eliminates: `LiveHoleLayer`, `LiveProxyBlockModel`, `BlockHole`, the `aboutToCommit` / `holeReified` / `holeBufferChanged` / `holeAbandoned` / `holeInserted` signals, the `BufferTextRole` / `IsHoleRole` / `HoleIdRole` model roles, the per-hole undo regime, the hole IME composition guard, the idle-commit timer, the `HoleBlockId` discriminator on `BlockId`. Approximately 690 LOC of production code and 870 LOC of tests retire.

The design adds: a marker constant, an atomic-bundled-edit primitive in `LiveEditBinding`, a `MarkerScrubber` (~100 LOC) wired at three event points (focus-out, pre-save, post-load), and a marker-aware initial-qtPos rule in cursor delivery.

The design *retains*: parser-pure `LiveBlockModel`, the `LiveRealisticInputHarness` test utility, the R5 mid-block-split path, `LiveCursorState::requestTextCaretAtNewRow`, the C-restoration spec's L0–L8 layering.

---

## 1. Premises (binding inputs)

These are decided. The design's job is consequence, not re-litigation.

| # | Premise | Source |
|---|---------|--------|
| M1 | The §3.1 architectural-review decision is approach (c), marker character. | review doc §3.1; spike findings §4 |
| M2 | The marker codepoint is U+200B (ZERO WIDTH SPACE), 3 UTF-8 bytes (`E2 80 8B`). Fallback codepoint U+E0100 (VARIATION SELECTOR-17) if a future renderer collision arises. The choice is a single named constant in the live-render library. | spike findings §1.1, §2.2 path 4 |
| M3 | `MarkerScrubber` runs at three deterministic events: focus-out from a marker-only paragraph, pre-save (host's save handler), post-load (foundation's `documentReloaded`). Idle timers are *not* used. | spike findings §2.2 (paths 1, 3, 6) |
| M4 | The first user keystroke into a marker-only paragraph applies *one* `MarkoffEdit` that both inserts the typed bytes and deletes the marker — atomic-bundled-edit. No two-step "type-then-scrub" sequence. | spike findings §2.2 (path 2); §4 |
| M5 | Stacked Enter on a marker-only paragraph is a no-op (consumed keystroke, no source edit, cursor unchanged). Markdown's CommonMark semantics collapse consecutive blank lines, so multi-row vertical gaps cannot survive a save/load cycle in any Markdown editor; the editor matches that constraint upstream. | conversation 2026-05-03; matches v2 spec §6.1 |
| M6 | The load-time and pre-save scrubbers also collapse *runs* of marker-only paragraphs (not just single markers). Defensive: covers files written by other tools or broken builds. | conversation 2026-05-03 |
| M7 | The QML `ListView` binds directly to the parser-pure `LiveBlockModel`. There is no proxy model. Marker paragraphs are normal paragraph rows from the model's perspective. | spike findings §3 |
| M8 | `BlockId == Markoff::BlockAnchor` (the C-restoration spec's original definition, restored). No `HoleBlockId` variant. `TextCaret::block` always references a real CRDT-anchored block. | review doc §3.1; spike findings §4 |
| M9 | `LiveRealisticInputHarness` (v2's harness) is retained unchanged — it is a general async-UX test utility, not hole-specific. | review doc §3.5; spike findings §3.4 |
| M10 | The R5 mid-block-Enter path (paragraphEnter mid-buffer split) is unchanged. So is the R5 soft-Enter (Shift-Enter) path. So is the R5 backspace-merge path. The marker design touches *only* the EOB-Enter and start-of-paragraph-Enter branches of `paragraphEnter`. | review doc §3.1 |

---

## 2. Architecture overview

### 2.1 Layer position

Under the C-spec's L0–L8 layering, the marker design's only *new* element is `MarkerScrubber`, a small service callable from L4 (paragraph editing) and from the host. There is no L6.5 layer; the proxy model is gone; `LiveBlockModel` at L2 is the model the QML `ListView` binds to.

```
┌──────────────────────────────────────────────────────────────────┐
│ L8  Interactive blocks                                           │
├──────────────────────────────────────────────────────────────────┤
│ L7  Structured text blocks                                       │
├──────────────────────────────────────────────────────────────────┤
│ L6  Other text blocks                                            │
│       LiveSpeculationLayer (predictions: inline-format, fence)   │
├──────────────────────────────────────────────────────────────────┤
│ L5  Structural keys                                              │
│       MarkerScrubber callable from here on focus-out / save /    │
│       load events. Not a layer; a stateless service.             │
├──────────────────────────────────────────────────────────────────┤
│ L4  Paragraph editing                                            │
│       LiveEditBinding gains marker-aware first-edit bundling.    │
├──────────────────────────────────────────────────────────────────┤
│ L3  Cursor + selection                                           │
│       LiveCursorState gains a marker-aware initial-qtPos rule.   │
├──────────────────────────────────────────────────────────────────┤
│ L2  Diff-driven model    (LiveBlockModel — parser-pure)          │
│       The QML ListView binds directly here. No proxy.            │
├──────────────────────────────────────────────────────────────────┤
│ L1  Read-only render                                             │
├──────────────────────────────────────────────────────────────────┤
│ L0  Coordinate primitives                                        │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 Data flow at a glance

```
MarkoffDocument (CRDT, authoritative — single source of truth)
    │
    │ parseUpdated(parsed, parseSeq, blockAnchors, parseInputEditSeq)
    ▼
LiveListModelBinding
    │
    │ AstBlockDiff → applyOps
    ▼
LiveBlockModel  (parser-pure rows; marker paragraphs are normal rows)
    ▼
QML ListView binds here directly
```

No proxy. No second authority. No commit step. The CRDT is the only place block content lives.

### 2.3 Lifecycle in one paragraph

User presses Enter at end of paragraph P. `LiveStructuralKeyHandler::paragraphEnter` (EOB branch):

1. Computes `byteOffset = P.endByte` (post-Bug-E `BlockWalker` semantics).
2. Calls `m_document->applyLocalEdit({{ byteOffset, byteOffset, "\n\n​"_ba }})`.
3. Records `setRowEditSequence(P.blockIndex, document->editSequence())`.
4. Schedules `requestTextCaretAtNewRow(P.blockIndex + 1, /*qtPos=*/0)` — the new row hasn't arrived yet; the request goes pending and resolves on the next `LiveBlockModel::rowsInserted` for the new block.
5. Records the structural step in `UndoCoalescer`.

Parse-back arrives ~30–100 ms later. `LiveListModelBinding` runs `applyOps`; `LiveBlockModel` emits `rowsInserted` for the new paragraph; the pending cursor request resolves; the QML `ParagraphDelegate` materialises with `text == "​"` and grants its TextEdit `activeFocus`. The cursor lands at qtPos 0 (before the marker). Caret blinks.

User types `"x"`. `LiveEditBinding::onContentsChange` sees the content change in a paragraph whose pre-edit content was exactly `"​"`. It builds a single `MarkoffEdit` that inserts `"x"` *and* deletes the marker, applies it as one batch via `applyLocalEdit({...})`, and bumps `editSequence`. Parse-back: paragraph contains `"x"`. `LiveBlockModel` emits `dataChanged` for the row's text role; the delegate refreshes; cursor stays at qtPos 1. User keeps typing as if it were any other paragraph.

The user-visible trace: press Enter, see new paragraph with cursor in it, type, paragraph fills in. From the architecture's POV, every change is a single CRDT edit; the marker never reaches a save or a parse-back of any consequence.

---

## 3. The marker character

```cpp
// libs/markoff-live-render/include/markoff/live-render/Marker.h
namespace Markoff::LiveRender {
constexpr QChar       kMarkerChar = QChar(0x200B);          // U+200B ZWSP
constexpr const char *kMarkerUtf8 = "\xE2\x80\x8B";         // 3 bytes
constexpr int         kMarkerUtf8Len = 3;
}
```

**Rationale.** ZWSP is invisible in every renderer (zero advance, no glyph), produces a `Paragraph` block when alone or surrounded (verified by the spike's `marker_probe`), is widely understood as a typesetting hint not as content, and round-trips cleanly through pandoc / git / cat (visible in hex dumps but otherwise inert).

**Fallback.** If a future renderer or an unforeseen consumer treats ZWSP as visible content, switch to U+E0100 (VARIATION SELECTOR-17, 4 UTF-8 bytes) — virtually never present in user content. Single-line constant change.

**Policy.** Markoff treats U+200B in source as a layout artifact and does not preserve it across save/load. Document this in the user-facing notes (release-note level, not in-app).

---

## 4. Source-edit contract

### 4.1 EOB-Enter

```cpp
// In LiveStructuralKeyHandler::paragraphEnter, EOB branch.
const quint32 byteOffset = c.currentBlockEnd;                // post-Bug-E semantics
Markoff::MarkoffEdit ed;
ed.oldStart = byteOffset;
ed.oldEnd   = byteOffset;
ed.newText  = QByteArrayLiteral("\n\n\xE2\x80\x8B");
c.document->applyLocalEdit({ ed });
c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
c.cursorState->requestTextCaretAtNewRow(c.blockIndex + 1, 0);
if (c.undoCoalescer) c.undoCoalescer->recordStructural();
return HR::Handled;
```

### 4.2 Start-of-paragraph Enter

Symmetric in *position* but not in *payload byte order*. The payload is `"<ZWSP>\n\n"` (marker first, then the separator), not `"\n\n<ZWSP>"` — so that the inserted marker becomes the *leading* paragraph and the existing block becomes the second paragraph. EOB-Enter (§4.1) keeps `"\n\n<ZWSP>"` (separator first, then marker) for the symmetric reason.

```cpp
// In LiveStructuralKeyHandler::paragraphEnter, start-of-block branch.
const quint32 byteOffset = c.currentBlockStart;
Markoff::MarkoffEdit ed;
ed.oldStart = byteOffset;
ed.oldEnd   = byteOffset;
ed.newText  = QByteArrayLiteral("\xE2\x80\x8B\n\n");          // <ZWSP>\n\n
c.document->applyLocalEdit({ ed });
c.model->setRowEditSequence(c.blockIndex, c.document->editSequence());
c.cursorState->requestTextCaretAtNewRow(c.blockIndex, 0);
if (c.undoCoalescer) c.undoCoalescer->recordStructural();
return HR::Handled;
```

The new row replaces the current row's index; the original block shifts down by one.

**(Spec correction, Task 6 finding.)** An earlier draft of §4.2 stated the start-of-paragraph payload was the same `"\n\n<ZWSP>"` as EOB. That was wrong — using EOB's byte order at the start of a paragraph splices a leading `\n\n` into the *previous* block (or, at document start, prepends two empty lines before the marker), producing the wrong block topology. The implementation in `LiveStructuralKeyHandler.cpp` got the byte order right; the spec text is corrected here to match.

### 4.3 Mid-block Enter (unchanged from R5)

The existing R5 path applies — `applyLocalEdit("\n\n")` at the cursor; the parser produces two real paragraph blocks; cursor goes to the second via the standard pending-row mechanism.

### 4.4 Soft Enter (Shift-Enter, unchanged)

Inserts `"\n"`; stays in block.

### 4.5 Stacked Enter on a marker-only paragraph

If `c.blockText == kMarkerChar` (pre-edit content exactly the marker), the keystroke is consumed with no source edit. No cursor change. Returns `HR::Handled`.

This rule is checked *before* the EOB / start-of-block / mid-block dispatch above.

---

## 5. Atomic-bundled-edit primitive

### 5.1 Where it lives

In `LiveEditBinding`. Each `LiveEditBinding` is per-delegate (R4); its existing `onContentsChange` slot already converts QML TextEdit deltas to `MarkoffEdit`. The marker-aware bundling lives here because this is the single funnel through which user keystrokes enter the CRDT.

### 5.2 Detection

On focus-in to a paragraph delegate, `LiveEditBinding` reads `m_blockText` from the model row. If `m_blockText == kMarkerChar`, set `m_pendingMarkerScrub = true`.

On the next `onContentsChange` (the user's first keystroke after focus-in):
- If `m_pendingMarkerScrub`: build a *single* `MarkoffEdit` whose `oldStart..oldEnd` covers `[blockStart, blockStart + kMarkerUtf8Len)` (the marker bytes) and whose `newText` is the user's typed bytes. This deletes the marker and inserts the typed content in one op.
- Else: normal per-keystroke `MarkoffEdit` as today.
- Either way: clear `m_pendingMarkerScrub`.

On focus-out without typing: the `m_pendingMarkerScrub` flag is irrelevant; the focus-out scrubber (§6) handles the leakage.

### 5.3 Why not a per-keystroke "is this paragraph marker-only" check

Because that check would race with intermediate parse-backs. The `m_pendingMarkerScrub` flag is set at the well-defined focus-in event when `m_blockText` is known to be exact, and consumed at the well-defined first-edit event. No async window.

### 5.4 IME composition

IME composition works without special handling. The composition events go through `LiveEditBinding::onContentsChange` like any keystroke; the `m_pendingMarkerScrub` flag bundles the first commit (whether one keystroke or one IME-committed glyph) with the marker removal. No `setHoleComposition` callback is needed; the v2 IME-pause-the-idle-timer mechanism doesn't apply because there is no idle timer.

---

## 6. `MarkerScrubber` service

### 6.1 Header sketch

```cpp
// libs/markoff-live-render/include/markoff/live-render/MarkerScrubber.h
namespace Markoff::LiveRender {

class MarkerScrubber : public QObject {
    Q_OBJECT
public:
    explicit MarkerScrubber(Markoff::MarkoffDocument *doc,
                             LiveBlockModel        *model,
                             QObject               *parent = nullptr);

    /// Called by LiveEditBinding when focus leaves a paragraph whose
    /// content matches the marker-only predicate.
    /// Edits the source to delete the marker(s) AND the leading "\n\n"
    /// separator that introduced them.
    void scrubOnFocusOut(int blockIndex);

    /// Called by the host's save path before the .md bytes are written
    /// to disk. Walks all paragraph blocks; collects every marker-only
    /// (or marker-run-only) paragraph; emits one batched applyLocalEdit
    /// removing all of them. Returns the byte count removed.
    int scrubBeforeSave();

    /// Called from MarkoffDocument::documentReloaded after a file load.
    /// Same logic as scrubBeforeSave but operates on the just-loaded
    /// document. Defends against marker-bearing files written by other
    /// tools or by a broken earlier build.
    int scrubAfterLoad();

    /// Predicate: returns true if the paragraph at blockIndex contains
    /// only marker characters (one or more), with no other content.
    bool isMarkerOnlyParagraph(int blockIndex) const;

private:
    Markoff::MarkoffDocument *m_doc;
    LiveBlockModel           *m_model;
};

}  // namespace Markoff::LiveRender
```

### 6.2 Predicate

`isMarkerOnlyParagraph(blockIndex)` returns true iff every codepoint of the block's content is `kMarkerChar`. Empty block returns false (an empty block has no marker; it's also a different beast — empty paragraphs don't normally exist post-parse). The predicate matches *runs* of markers (per M6) so that defensive cleanup of damaged files works.

### 6.3 Scrub edit shape

For a marker-only paragraph at byte range `[start, end)` whose preceding bytes are `"\n\n"` (the separator that introduced it):
- `MarkoffEdit{ start - 2, end, "" }` — removes the leading `\n\n` and the marker run.
- For a run of consecutive marker-only paragraphs, a single `MarkoffEdit` removes the entire range from the first `\n\n` through the end of the last marker.

Multi-paragraph batched scrubs run as one `applyLocalEdit({list})` to produce one undo entry and one parse-back.

### 6.4 Wiring

- `LiveEditBinding` holds a pointer to the `MarkerScrubber` (passed by `LiveListModelBinding` at construction). Its existing focus-tracking code calls `scrubber->scrubOnFocusOut(blockIndex)` on `focusOut` if the block matches the predicate at that moment.
- The host application's save path calls `scrubber->scrubBeforeSave()` before serializing bytes.
- `LiveListModelBinding` connects `MarkoffDocument::documentReloaded → MarkerScrubber::scrubAfterLoad`.

**Note (Task 16 finding — `scrubAfterLoad` timing).** `MarkoffDocument::documentReloaded` fires synchronously inside `resetContent`, **before** the parse worker has populated the model. The auto-scrub via that connection is therefore a no-op at load time — the model is empty when it runs and the predicate finds nothing.

For the load-time scrub to actually clean markers loaded from disk, the host must additionally call `binding.markerScrubber()->scrubAfterLoad()` (or the convenience wrapper `binding.flushPendingMarkers()` if exposed) **after the model populates from the first parse-back** — i.e., after `parseUpdated` delivers the first non-empty model state derived from the just-loaded source.

The integration contract is therefore two-step:

1. The `documentReloaded → scrubAfterLoad` wiring stays (cheap; harmless when it no-ops; documents the intent).
2. The host (or a future helper inside `LiveListModelBinding` that watches for the first `parseUpdated` after a load) calls `scrubAfterLoad()` post-parse.

A foundation-level fix — re-emitting `documentReloaded` after the parse settles, or adding a separate `documentReadyAfterLoad` signal — is out of scope for this spec but recorded as §17 open question 8.

The §13 "Load file with markers in source" test row reflects this two-step contract: the unit test must trigger the second call (synthetic post-parse `scrubAfterLoad`) to assert the document ends marker-free.

---

## 7. Cursor delivery

### 7.1 Initial qtPos rule

`LiveCursorState::requestTextCaretAtNewRow(row, qtPos)` is called with `qtPos = 0` for marker insertion. This places the cursor *before* the marker so that the first user keystroke inserts at byte 0 of the block — the atomic-bundled-edit then replaces the marker with the typed content cleanly (rather than producing `<marker><typed>` or `<typed><marker>`).

### 7.2 Pending-row resolution (unchanged)

Uses the existing `requestTextCaretAtNewRow` semantics (R5.5 Bug-B fix): the request is purely pending; it resolves on the matching `rowsInserted` arrival from `LiveBlockModel`. No immediate-resolve gate.

### 7.3 No `holeReified` event

Removed. The new row's arrival via `rowsInserted` is the only signal the cursor needs.

---

## 8. `LiveStructuralKeyHandler` integration

### 8.1 Touched code

- `paragraphEnter` lambda: replace the `createBlockHole` calls in the EOB and start-of-block branches with the `applyLocalEdit("\n\n​")` shape from §4.
- New stacked-Enter no-op rule from §4.5 — an early return at the top of `paragraphEnter` if `c.blockText == kMarkerChar`.
- Same dispatch for `BlockKind::Heading::Key_Return / Key_Enter` (the Heading kind reuses `paragraphEnter`).

### 8.2 Removed code

- Both `createBlockHole` call sites (currently lines ~151, ~175, ~331 of `LiveStructuralKeyHandler.cpp`).
- The hole-row dispatch table for paragraph kind (currently rows for "Enter on marker hole" / "Esc on hole" / "Backspace at qtPos 0 of hole" / etc.) — all unnecessary because there are no hole rows.

### 8.3 Backspace at start of a paragraph following a marker

Pre-marker design: the previous "block" was a hole; backspace either abandoned the hole or committed it. Marker design: the previous "block" is a real marker-only paragraph. Backspace at qtPos 0 of the following paragraph should:
- Delete the marker paragraph + its `\n\n` separator (i.e., apply the same edit `MarkerScrubber::scrubOnFocusOut` would apply for that block).
- The user's caret stays at the (now-shifted) qtPos 0 of their original paragraph.

This is a small extension to the R5 backspace-merge handler — when the previous block is marker-only, the merge edit is the scrub edit, not a paragraph-merge.

---

## 9. `LiveEditBinding` integration

### 9.1 Touched code

- `onContentsChange`: gain the `m_pendingMarkerScrub` branch from §5.
- `focusInEvent` (or its existing equivalent): set `m_pendingMarkerScrub` if `m_blockText == kMarkerChar`.

### 9.2 Removed code

- `setHoleComposition` forwarding to `LiveHoleLayer` (no IME guard for hole; no hole).
- All references to `BufferTextRole` / `IsHoleRole` / `HoleIdRole` / `HoleBlockId`.
- Hole-route dispatch in `applyLocalEdit` (the v2 path that routes to `setBlockHoleBuffer` instead of `MarkoffDocument::applyLocalEdit`).

---

## 10. `LiveSelectionView` integration

### 10.1 Cross-row selection

A marker paragraph is just a paragraph. `LiveSelectionView` already handles paragraphs. No code change.

### 10.2 Clipboard scrubber

`serializeForCopy` (in the QML delegate or its host) post-processes the assembled clipboard text to strip ZWSP characters. Same predicate as `MarkerScrubber::isMarkerOnlyParagraph` but applied to the serialized string — `text.remove(kMarkerChar)`. Single call.

### 10.3 Anchor preservation across reification

Removed. There is no reification — every block is parser-real from creation.

---

## 11. `UndoCoalescer` integration

### 11.1 Single regime

CRDT undo only. No per-hole snapshot stack. The `UndoCoalescer`'s host dispatches to `MarkoffDocument::undo` always.

### 11.2 Undo behaviour traced

- User presses EOB-Enter → CRDT op `applyLocalEdit("\n\n​")`.
- Cursor lands; user types `"x"` → CRDT op (atomic-bundled) replacing the marker with `"x"`.
- Ctrl-Z: undoes the second op → marker paragraph is back, with marker content; cursor in marker paragraph at qtPos 0.
- Ctrl-Z again: undoes the first op → original document state pre-Enter; cursor at end of original paragraph.

This matches the user's intuitive model and is one undo regime, not two.

### 11.3 Removed code

- `recordHoleUndoPoint` / `undoBlockHole` / `redoBlockHole` paths in `UndoCoalescer` and the dispatch in its host.

---

## 12. `BlockId`

```cpp
using BlockId = Markoff::BlockAnchor;  // restored to C-spec §3.1 original
```

No `HoleBlockId`; no variant. `TextCaret::block` always references a real CRDT-anchored block.

---

## 13. Test plan (informs the new R5.5 plan)

| Test | What it asserts | Discipline |
|------|-----------------|------------|
| Parser sees marker as paragraph | Already covered by spike's `marker_probe.cpp` outputs; promote a representative subset to a unit test in `tst_document_top_level_blocks.cpp` (`markerProducesParagraph`, `markerRunProducesMultiple`). | Unit |
| `LiveStructuralKeyHandler::paragraphEnter` EOB | Calling EOB-Enter on a paragraph emits one `applyLocalEdit({"\n\n​"})` and one `requestTextCaretAtNewRow(blockIndex+1, 0)`. | Unit (mock document + recording mock cursor state) |
| `LiveStructuralKeyHandler::paragraphEnter` start-of-block | Symmetric. | Unit |
| `LiveStructuralKeyHandler::paragraphEnter` stacked-Enter | Pressing Enter on a paragraph whose `blockText == kMarkerChar` produces no `applyLocalEdit` and no cursor request. | Unit |
| `LiveEditBinding` first-edit bundling | `focusIn` on a marker-only block sets the flag; first `onContentsChange` produces one `MarkoffEdit` whose `oldStart..oldEnd` covers the marker and whose `newText` is the typed bytes. Subsequent `onContentsChange` produces normal edits. | Unit |
| `MarkerScrubber::scrubOnFocusOut` | Calling on a marker-only block produces an `applyLocalEdit` removing the leading `\n\n` and the marker. | Unit |
| `MarkerScrubber::scrubBeforeSave` | A document containing a marker-only paragraph plus a marker-run yields one batched edit removing all of them. | Unit |
| `MarkerScrubber::scrubAfterLoad` | Same logic, triggered via a synthetic `documentReloaded` with marker-bearing source. | Unit |
| `MarkerScrubber::isMarkerOnlyParagraph` | Single-marker, marker-run, mixed (marker + content), empty — the four cases. | Unit |
| Backspace at start of paragraph following a marker block | The preceding marker block + its `\n\n` separator are deleted; cursor stays at the user's qtPos 0. | Harness |
| EOB-Enter → type → marker scrubbed atomically | After EOB-Enter and one keystroke, the source contains `"...\n\n<typed>"` with no marker. Single editSequence bump for the keystroke. | Harness — load-bearing |
| Race verification | Type 200 chars at 30 ms gap immediately after EOB-Enter. Source after settling equals `"...\n\n<all typed chars in order>"` with no marker, no scrambling. | Harness — load-bearing |
| Focus-out without typing scrubs | EOB-Enter, then click another paragraph. Source returns to pre-Enter state. | Harness |
| Save while marker present | Type EOB-Enter, immediately save. Saved bytes contain no marker; on-disk file matches the document's logical pre-Enter state. | Harness |
| Load file with markers in source | Open a `.md` file containing ZWSPs; `documentReloaded` fires (no-op — model still empty); after the first `parseUpdated` populates the model, the host calls `scrubAfterLoad()` and the in-memory document becomes marker-free. Two-step contract per §6.4. | Unit (no UI needed) |
| Ctrl-Z after typing into marker block | First Ctrl-Z restores marker block; second Ctrl-Z restores pre-Enter state. | Harness |
| Stacked Enter is no-op | Press Enter twice; second press produces no source change. Press a third time after typing into marker block; the third press creates a new marker block at the new EOB. | Harness |
| Cross-row selection across marker block | Select from paragraph above through paragraph below; clipboard text contains the marker block's content (which is just ZWSP); the clipboard scrubber strips it. | Harness |
| Dogfood gate | User types ≥200 words across ≥10 paragraphs in `markoff-live-render-app`; every Enter creates a paragraph; saved file is clean; reload preserves the document. | Manual |

The retained `LiveRealisticInputHarness` (per M9) drives the "Harness" rows.

---

## 14. Spec amendment deltas

Edits to `docs/specs/2026-05-02-live-render-restoration-design.md`:

| Spec section | Current | Amended |
|--------------|---------|---------|
| Premise 6 | "EOB-Enter hole feature is deleted" / "v2 holes ..." | "EOB-Enter and start-of-paragraph Enter insert `\"\\n\\n\\u200B\"` into source. The marker is invisible; the parser produces a real paragraph block; cursor lands via the standard parser-driven row path. A `MarkerScrubber` service (live-render lib) ensures the marker doesn't survive focus-out / save / load. Per `docs/specs/2026-05-03-marker-paragraph-design.md`. The v2 hole implementation is permanently retired." |
| §3.1 type definition | `struct BlockId { std::variant<BlockAnchor, HoleBlockId> id; };` | `using BlockId = Markoff::BlockAnchor;` |
| §4.4 cycle-guards table | Hole-related rows | Replace with: "Marker design — see `docs/specs/2026-05-03-marker-paragraph-design.md` §5; atomic-bundled-edit eliminates the hole authority; one named predicate (`isMarkerOnlyParagraph`) shared across three deterministic event points." |
| §5.4 structural keys | Implies hole creation for paragraph EOB-Enter | Replace with the §4 source-edit contract from this design. |
| §6.1 L6 | "Predictions + LiveHoleLayer + LiveProxyBlockModel" | "Predictions only. `MarkerScrubber` is a stateless service callable from L4/L5 and the host; it is not a layer." |
| §7.2 structural-edit data flow | hole-create → bufferText → commit | source-edit → parse-back → row arrival → cursor lands → optional scrub. |
| §11 R5 acceptance criteria | Caveat about EOB-Enter requiring R5.5 | Removed; the marker-paragraph R5.5 plan delivers EOB-Enter behaviour. |
| §11 (new R5.5 phase) | v2 plan reference | New plan reference: `docs/plans/2026-05-XX-live-render-r5-5-marker-paragraph.md` (to be written from this spec). |
| §15 open questions | Hole-related entries | Resolved per this design and the spike findings. |

The retired v2 doc is at `docs/archive/2026-05-03-v2-holes-design.md` and the retired R5.5 plan is at `docs/archive/2026-05-03-live-render-r5-5-holes.md`. Cross-references that point at the old paths must be updated.

---

## 15. Out of scope

- All non-paragraph block-kind hole cases (list-items, fence interiors, blockquote, table cells, callouts, links, wikilinks, footnotes, math). Out of scope for v2 too. Each is a follow-on plan in R7+. Whether the marker pattern generalises to those kinds is a separate spike.
- Configurable marker codepoint at runtime. Single named constant; build-time choice if it ever needs to change.
- Cross-process marker survival. Irrelevant — the load-time scrubber handles any survival from any source.
- Multi-cursor / secondary-selection markers. Single primary selection only (matches v2 §14).
- Heuristic "create marker on click past EOB." Out of scope (matches v2 §14).
- Soft-Enter (Shift+Enter) inside a marker paragraph. Treated identically to soft-Enter anywhere else — inserts `\n` into the marker paragraph's content. The atomic-bundled-edit fires on the next non-soft keystroke and removes the marker; if the user soft-Enters into a marker paragraph and then leaves, focus-out scrubs the now-multi-line marker paragraph (the predicate matches paragraphs whose content is *only* markers, not markers-plus-newlines, so a soft-Enter'd marker paragraph would leak — flagged in §17 open question).

---

## 16. Acceptance criteria

R5.5-marker ships when:

1. All §13 unit tests pass.
2. All §13 harness-driven tests pass.
3. The dogfood gate per §13 passes: user types ≥200 words across ≥10 paragraphs; every Enter creates a paragraph that the cursor lands in; no character scrambling; save produces a clean file; reload preserves the document.
4. `LiveHoleLayer.{h,cpp}`, `LiveProxyBlockModel.{h,cpp}`, `BlockHole.h` are deleted.
5. `tst_live_render_holes_layer.cpp`, `tst_live_render_holes_qml.cpp`, `tst_live_render_proxy_model.cpp` are deleted.
6. C-restoration spec amendments (§14) land and are approved.
7. `restoration-status.md` reflects the marker-paragraph design as the basis for R5.5; v2 doc paths point at the archive.

---

## 17. Open questions for the implementation plan

These are deliberately not pinned here; the plan resolves them.

1. **Soft-Enter in a marker paragraph (§15 last bullet).** Should the marker-only predicate match `^(​|\n)+$` instead of `^​+$`? Or should soft-Enter also bundle the marker scrub? The cleanest answer is probably to broaden the predicate; the plan picks and tests.
2. **`MarkerScrubber` placement.** In `libs/markoff-live-render` (with the rest of the editing concerns) or in `libs/markoff-foundation` (as a foundation-level affordance other consumers could reuse)? Default: live-render. The plan validates against any prospective second consumer.
3. **`scrubBeforeSave` integration with the host.** The host owns the save path; the wiring is whatever `MarkdownView::saveAs` (or its successor) calls into. The plan picks the exact integration site.
4. **Atomic-bundled-edit primitive: keystroke level vs IME-commit level.** §5.2 says "first `onContentsChange`"; for IME, "first commit" might be the wrong granularity if the IME emits intermediate composition events. The plan tests against IME composition fixtures.
5. **What happens if `setRowEditSequence` for the new (post-EOB-Enter) marker paragraph is needed.** The marker paragraph has just been created via `applyLocalEdit`; is its `lastEditEditSequence` correctly tagged so the freshness rule doesn't squash subsequent text-role updates? The plan validates.
6. **Performance of `scrubBeforeSave` on large documents.** O(parserRows) walk; should be cheap. The plan adds a benchmark gate if there's any concern.
7. **Test fixture taxonomy (per review §3.5).** This design's tests should follow the multi-fixture rule from the review's §3.5: short-snippet, llm-wrapped, human-long-line, multi-block-mixed, hundred-block-scroll. The plan instantiates each.
8. **Foundation-level fix for `scrubAfterLoad` timing (Task 16 finding).** §6.4 documents a two-step contract because `MarkoffDocument::documentReloaded` fires synchronously inside `resetContent` *before* the parse worker populates the model. A foundation change — re-emitting `documentReloaded` after the first post-load `parseUpdated`, or adding a separate `documentReadyAfterLoad` signal that fires when the model is in sync with the loaded source — would let the marker library auto-scrub on load without a host call. Out of scope for this spec (foundation-level change); flag for the foundation backlog.

---

## 18. References

- Spike: `docs/handoff/2026-05-03-section-3-1-spike-findings.md`.
- Architectural review: `docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md`.
- C-restoration spec (the architecture this design amends): `docs/specs/2026-05-02-live-render-restoration-design.md`.
- Retired v2 design (for historical reference): `docs/archive/2026-05-03-v2-holes-design.md`.
- Retired R5.5 plan: `docs/archive/2026-05-03-live-render-r5-5-holes.md`.
- R5 plan (Tasks 1–11 executed): `docs/plans/2026-05-02-live-render-r5-structural-keys.md`.
- Restoration session brief: `docs/handoff/2026-05-02-restoration-session-brief.md` §3.6 (spec-amendment protocol).
- R5-holes post-mortem (still valid for v0/v1 history; v2 portions superseded by the review): `docs/handoff/2026-05-03-r5-holes-postmortem.md`.

---

*End of marker-paragraph design.*
