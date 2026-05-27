# `markoff-styled` v0 dogfood fixes — design

**Date:** 2026-05-27
**Status:** Draft, awaiting plan
**Branch:** `master`
**Predecessor:** `docs/specs/2026-05-26-markoff-styled-leaf-design.md` (v0 leaf design)

## 0. Context

The `markoff-styled` leaf shipped v0 on 2026-05-26. Initial user dogfood on a
real markdown file surfaced three bugs:

1. **Headings render as body text.** The leaf's `applyHeading` correctly
   sets the `fontPointSize` per the H1–H6 ratio table, but real-world
   documents don't trigger it. Root cause: `MarkoffDocument::blockKind()`
   returns the CRDT-stored kind; `markoff-styled` has no equivalent to
   `markoff-live`'s `KindTransition::inferBlockKind` pass, so prefix-only
   kind changes (e.g. typing `## ` to convert a Paragraph to Heading) never
   propagate to the styler. This is the `QEXPECT_FAIL` slot
   `remote_edit_replays_text_and_restyles` in
   `tst_styled_d2_integration.cpp`. The same gap also bites at load time
   when a heading block was previously written through a path that didn't
   set its CRDT kind.

2. **Scroll lag (~200ms after mouse wheel).** Two contributors:
   - `StyleApplier::applyFormats()` runs unconditionally on every
     `d2DocumentChanged`, walking all N blocks and applying `setBlockFormat`
     + `setBlockCharFormat` to each. Even though `QSignalBlocker` +
     `beginEditBlock`/`endEditBlock` coalesce repaints, the full set of
     format changes still produces a whole-document layout invalidation at
     `endEditBlock`. On a real-sized document this costs ~100–200 ms.
   - `LinkInteraction::handleMove` runs `resolveLinkAt` on every
     `MouseMove` (including scroll-synthetic moves), walking every block
     and every span linearly to find a covering link span.

3. **Viewport jumps wildly on edit.** Same load-bearing pathology the
   `markoff-live` team grappled with at length. Walking every block in
   `applyFormats()` produces N `setBlockFormat` calls; `endEditBlock`
   batches them into one whole-document layout pass; Qt's "keep cursor
   visible" logic then repositions the viewport based on the new layout,
   which often differs measurably from where the user was reading.

`markoff-live` avoids all three by **per-block QTextDocument isolation**
(one delegate per row in a QML `ListView`, each with its own
`QTextDocument` + `InlineHighlighter`). Single-document QWidget editors
don't have that escape hatch. Instead, this spec ports the two specific
techniques `markoff-live` uses on top of its per-block isolation:

- **Same-value guard on per-block formatting** —
  `InlineHighlighter::setInlineSpans` only `rehighlight()`s when spans
  actually changed (`libs/markoff-live/src/InlineHighlighter.cpp:21–28`).
- **`KindTransition::inferBlockKind` issuing `Cmd::changeKind`** —
  `markoff-live`'s `LiveListModelBinding::onD2Changed` reconciles
  prefix-rule-inferred kind against the stored kind and corrects the
  model on disagreement.

Combined with two QWidget-specific additions (scroll-position preserve
around `applyFormats` and a `LinkInteraction::resolveLinkAt` fast path),
these techniques fix all three bugs without changing the leaf's
architecture.

## 1. Goals and non-goals

**Goals:**

- v0.1 quality dogfood: editing a real markdown file does not jump the
  viewport and does not lag on scroll.
- Headings render at heading sizes whether the document is loaded with
  pre-set kinds (frontmatter, programmatic) or built up via in-editor
  typing.
- The CRDT model stays authoritative for block kind — view-side kind
  inference issues `Cmd::changeKind`, doesn't render around a stale
  model.
- All existing v0 tests stay green, including the `QEXPECT_FAIL` slot
  (`remote_edit_replays_text_and_restyles`) which becomes a regular
  passing slot.

**Non-goals (this spec):**

- Per-block `QTextDocument` re-architecture. The `markoff-live`-style
  isolation is far more invasive than this branch warrants; keep the
  single-document model and just make it incremental.
- Cursor-aware delimiter visibility (still v0.1 work, separate spec).
- FindBar / FindController integration.
- Math, image, table, callout rendering.

## 2. Architecture changes summary

Five targeted changes, all inside `libs/markoff-styled/`. No public-API
changes. No new files.

| Change | File | Effect |
|---|---|---|
| 1. Per-block hash gate | `StyleApplier.cpp` (+ private member in `.h`) | Skip per-block format application when `(kind, text, spans, fontScale)` hash unchanged |
| 2. Kind-transition pass | `StyleApplier.cpp` | Infer kind from text prefix; on mismatch, emit `Cmd::changeKind` to MarkoffDocument |
| 3. Scroll-position preserve | `StyleApplier.cpp` | Capture `verticalScrollBar()->value()` before per-block walk; restore after `endEditBlock()` when no structural change occurred |
| 4. `resolveLinkAt` fast path | `LinkInteraction.cpp` | Bisect to the containing block first; only walk that block's spans for link hits |
| 5. Test promotion | `tst_styled_d2_integration.cpp` | `QEXPECT_FAIL` removed once Change 2 lands; new tests for hash gating, scroll preserve, and kind transition |

## 3. Per-block content hash gating

### 3.1 Hash composition

Per-block hash combines:
- Block kind (`Markoff::BlockKind` as `int`)
- Block text bytes (`MarkoffDocument::blockText(id)`)
- Inline span count
- Per-span: `charOffset`, `charLength`, the inline-flag bitmask
  (`bold | italic<<1 | strikethrough<<2 | code<<3 | highlight<<4 |
  isLink<<5 | isWikilink<<6 | isTag<<7 | isFootnoteRef<<8`)
- Global `m_fontScale` (since headings/code scale per font)

Theme is **not** in the hash. `setTheme` calls a manual `rerender()` that
clears `m_blockHashes` entirely, so subsequent passes restyle everything.
Same for `setMarkoffDocument` and `setFontScale`.

### 3.2 Data structure

```cpp
QHash<Markoff::BlockId, quint64> m_blockHashes;
```

Stored in `StyleApplier`. Stale entries pruned at the end of each
`applyFormats()` pass: iterate `iterateBlocks()` keys, compare against
hash-map keys, erase any entry whose key isn't present in the current
block list.

### 3.3 Loop pattern

```cpp
for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
    // ... compute startQt/endQt as before ...

    const Markoff::BlockKind kind = m_markoffDocument->blockKind(id);
    const QByteArray text = m_markoffDocument->blockText(id);
    const auto spans = m_markoffDocument->inlineSpansFor(id);

    const quint64 h = computeBlockHash(kind, text, spans, m_fontScale);
    if (m_blockHashes.value(id, 0) == h) continue;       // skip
    m_blockHashes[id] = h;

    // ... existing block-format + inline-format application ...
}
// Prune stale entries
pruneHashesFor(currentBlockIds);
```

`computeBlockHash` lives in the anonymous namespace alongside the format
helpers. Uses Qt's `qHash` + bit-mix; no cryptographic strength needed.
A hash collision causes one missed restyle until the next edit; visually
benign.

### 3.4 Why hash, not pointer-identity?

`SourceSpan` is a `QList`-stored value type returned by-value from
`inlineSpansFor`; pointer identity is not preserved across calls. The
parser's `InlineParseCache` reuses underlying storage for unchanged
blocks but exposes only by-value access.

### 3.5 Correctness

- **First call after `setMarkoffDocument`:** `m_blockHashes` is empty,
  so every block applies. Same as v0 today.
- **In-place edit on one block:** that block's hash changes; the other
  N-1 blocks skip. Layout invalidation shrinks to that one block's
  height range. Whole-document layout pass doesn't recompute N-1 blocks
  that didn't change.
- **Structural change (block inserted/removed):** new BlockId means no
  cache entry, so the new block applies. Removed BlockIds get pruned at
  pass end. Surviving blocks all hash-match and skip.
- **`setTheme` / `setFontScale` / `setMarkoffDocument`:** `rerender()`
  is called from each setter, which clears the hash map. Subsequent
  pass restyles everything.

## 4. Kind-transition pass

Port of `markoff-live::KindTransition::inferBlockKind` semantics.

### 4.1 Inference rules

Implemented in a `inferKindFromPrefix(const QByteArray &text, Markoff::BlockKind currentKind)` helper in the anonymous namespace:

| Prefix | Inferred kind |
|---|---|
| `# `, `## `, `### `, `#### `, `##### `, `###### ` | `Heading` |
| `- `, `* `, `+ ` (followed by space or list-item content) | `ListItem` |
| `1. `, `2. ` … (numbered) | `ListItem` |
| `> ` | `BlockQuote` |
| ` ```` ` (fenced code) | `CodeBlock` — but only at start-of-block + fence-end recognition is fence-state, not prefix; for v0.1 we DON'T infer to `CodeBlock`. The CRDT load path sets it correctly; in-editor toggling to/from CodeBlock is rare and a v0.2 concern. |
| `---`, `***`, `___` (horizontal rule sequence on its own) | `HorizontalRule` — similar fence-state concern; v0.2. |
| Otherwise | `Paragraph` |

`inferKindFromPrefix` covers Heading, ListItem, BlockQuote, Paragraph
inference in v0.1. CodeBlock and HorizontalRule inference is deferred
(those rarely flip in normal editing; the CRDT load path handles them).

### 4.2 When to issue `Cmd::changeKind`

Inside the `applyFormats()` block-walk loop, after a block's hash
mismatched (i.e., the block actually changed):

```cpp
const Markoff::BlockKind inferred = inferKindFromPrefix(text, kind);
if (inferred != kind) {
    m_pendingKindChanges.push_back({id, inferred});
}
// ...continue with format application using `kind` (not `inferred`)...
```

After the block-walk + prune step is finished and we're outside the
edit-block / signal-blocker scope, **defer kind-change application to
the next event-loop tick** via `QTimer::singleShot(0, ...)` to avoid
re-entering `d2DocumentChanged` synchronously while we're still inside
its slot:

```cpp
if (!m_pendingKindChanges.empty()) {
    auto changes = std::move(m_pendingKindChanges);
    QTimer::singleShot(0, this, [this, changes = std::move(changes)]() {
        if (!m_markoffDocument) return;
        for (const auto &chg : changes) {
            // markoff-live uses Cmd::changeKind(doc, BlockId, kind, attrs, linkRefs)
            // — see libs/markoff-live/src/LiveListModelBinding.cpp:477 / 507 / 548
            // for the call shape. Empty attrs/linkRefs match the inferred-kind
            // path (no metadata transfer).
            Markoff::Cmd::changeKind(
                *m_markoffDocument, chg.id, chg.kind, {}, {});
        }
    });
}
```

The structural op will fire a fresh `d2DocumentChanged`; the next
`applyFormats` pass will see the updated kind and apply the right
format. The hash for that block will also recompute (since `kind`
changed), so the format does get applied — even if `text` and `spans`
didn't change between the two passes.

### 4.3 Avoiding kind-change loops

`inferKindFromPrefix(text, currentKind)` must be **idempotent**: if the
inferred kind matches `currentKind`, return `currentKind` (no change
queued). The implementation above already does this via `if (inferred
!= kind)`.

### 4.4 `Qt.callLater` / `QTimer::singleShot(0, ...)` discipline

Per `docs/INVARIANTS.md` §6, this is a smell. Justification recorded
here in §4.2: synchronous re-entry into `applyStructural` from inside a
`d2DocumentChanged` slot would compound CRDT mutations within an
already-running cascade. The deferred dispatch breaks the re-entrance
chain cleanly. Same pattern `markoff-live::LiveListModelBinding`
uses (see `docs/handoff/2026-05-07-live-binding-developmental-history.md`).
A `docs/queue.md` Discipline Log entry is added when the code lands.

## 5. Scroll-position preservation

`applyFormats()` captures the scrollbar value before any modifications
and restores it after `endEditBlock()`, **conditionally**.

### 5.1 The condition

Two categories of restyle:
- **In-place:** all block IDs from the previous pass still present;
  none added, none removed. Layout heights may change slightly but the
  document's macro structure is identical. Restore scroll.
- **Structural:** at least one block was added or removed. Cursor is
  likely in the new block; Qt's natural "ensure cursor visible"
  behavior is correct. Don't restore scroll.

Detection: compare current `iterateBlocks()` against `m_blockHashes`
keys (snapshot the keys before the walk).

### 5.2 Implementation pattern

```cpp
void StyleApplier::applyFormats() {
    if (m_applyingFormats) return;
    if (!m_textDocument || !m_markoffDocument) return;
    m_applyingFormats = true;

    QTextEdit *edit = textEditForDocument(m_textDocument);  // see §5.3
    const int savedScroll = edit
        ? edit->verticalScrollBar()->value() : 0;
    const auto previousBlockIds = QSet<Markoff::BlockId>(
        m_blockHashes.keyBegin(), m_blockHashes.keyEnd());

    bool structural = false;
    {
        QSignalBlocker block(m_textDocument);
        QTextCursor cursor(m_textDocument);
        cursor.beginEditBlock();

        QSet<Markoff::BlockId> currentBlockIds;
        for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
            currentBlockIds.insert(id);
            // ... hash + apply formats as in §3 ...
        }
        // Detect structural change.
        if (currentBlockIds != previousBlockIds && !previousBlockIds.isEmpty()) {
            structural = true;
        }
        pruneHashesFor(currentBlockIds);

        cursor.endEditBlock();
    }

    if (!structural && edit) {
        edit->verticalScrollBar()->setValue(savedScroll);
    }
    ++m_restyleCount;
    m_applyingFormats = false;
}
```

The `!previousBlockIds.isEmpty()` guard exists so the first pass after
`setMarkoffDocument` (when `m_blockHashes` was just cleared and is
empty) doesn't get mis-categorized as structural — the first pass is
"load," and natural scroll behavior is fine there.

### 5.3 `textEditForDocument` helper

`StyleApplier` doesn't currently know which `QTextEdit` owns the
`QTextDocument` it's pointed at. Two options:

- **A:** Add a `QTextEdit *m_textEdit` to `StyleApplier`, set via a new
  `setTextEdit()` call from `Editor::Editor`. Public surface unchanged.
- **B:** Use `m_textDocument->parent()` and cast — fragile (parent is
  the `QTextEdit` by Qt convention but not guaranteed).

**Pick A.** One extra setter is cheap and explicit.

`Editor::Editor` (the constructor) gains one line:
```cpp
m_styleApplier->setTextEdit(m_editor);
```

`StyleApplier::setTextEdit(QTextEdit *)` simply stashes the pointer
(no rerender needed; it's an out-of-band capability).

## 6. `LinkInteraction::resolveLinkAt` fast path

Current `resolveLinkAt` iterates every block via `iterateBlocks()`
accumulating byte ranges, then checks if `charPos` falls within that
block's range, then iterates spans. On every `MouseMove` this is
O(blocks + spans_in_containing_block).

### 6.1 Find the containing block first

Use the existing `MarkoffDocument::blockAt(TextAnchor)` accessor:
```cpp
const Markoff::TextAnchor anchor = m_doc->textAnchorAt(
    qtPosToByte(charPos), /*rightBias=*/false);
const auto blockAnchorOpt = m_doc->blockAt(anchor);
if (!blockAnchorOpt) return std::nullopt;
const Markoff::BlockId id = *blockAnchorOpt;
const int blockOffset = m_doc->offsetInBlock(id, anchor);
```

Now we have the containing block in O(log N) (the CRDT's internal
indexing) and the byte offset within it.

### 6.2 Convert block-byte-offset to span-char-offset

`SourceSpan::charOffset/charLength` are UTF-16 char positions within
the block. `blockOffset` is a UTF-8 byte offset within the same block.
Convert:

```cpp
const int blockCharPos = utf8ByteOffsetToCharOffset(
    m_doc->blockText(id), blockOffset);
```

Helper goes in `LinkInteraction.cpp` (or reuse
`SourceTextDocumentBinding::byteOffsetToQtPos` if it's parameterized
correctly).

### 6.3 Walk only that block's spans

```cpp
for (const Markoff::SourceSpan &span : m_doc->inlineSpansFor(id)) {
    if (!span.isLink && !span.isWikilink) continue;
    if (blockCharPos < span.charOffset) continue;
    if (blockCharPos >= span.charOffset + span.charLength) continue;
    // hit — build LinkActivation and return
}
return std::nullopt;
```

### 6.4 Cycles + null-guards

`m_doc->textAnchorAt`/`blockAt` return `std::nullopt` for positions
outside any block (e.g., trailing newline). Guard appropriately;
return `std::nullopt` from `resolveLinkAt`.

### 6.5 Effect

Per-`MouseMove`: O(log N + spans_in_one_block) instead of
O(blocks + spans_in_one_block). On large documents this is the
difference between perceptible scroll lag and not.

## 7. Data flow + cycle correctness

Single new cycle: `applyFormats` → infers kind change → schedules
`Cmd::changeKind` via timer → next event loop → `applyStructural` →
`d2DocumentChanged` → `applyFormats` again → block's hash recomputed
with new kind → format applied.

**Termination:** because `inferKindFromPrefix` is idempotent (returns
`currentKind` when no rule fires), the second `applyFormats` pass after
a kind change finds `inferred == kind` and queues no further changes.
Loop terminates after one round-trip per text edit that crosses a kind
boundary.

**Guard:** `m_applyingFormats` (already exists in v0) prevents
re-entrance during the format pass itself. The deferred timer dispatch
in §4.2 means kind-change application happens on a separate event-loop
tick, after `m_applyingFormats` has been released.

## 8. Testing

### 8.1 Promotion: `QEXPECT_FAIL` slot becomes passing

`tst_styled_d2_integration.cpp::remote_edit_replays_text_and_restyles`
loses its `QEXPECT_FAIL` line and becomes a regular passing slot once
kind-transition lands. This is the primary verification that Bug A is
fixed.

### 8.2 New test binary: `tst_styled_dogfood_invariants`

Three slots:

- **`hash_gate_skips_unchanged_blocks`** — Load a 10-block document.
  Call `applier->restyleCount()` after initial load (N=1). Re-apply
  formats by tickling the doc with a no-op (e.g., an
  `applyFlatEdit` that inserts and immediately removes a character —
  net zero). Verify formats applied to changed blocks only, not all
  10. Implementation note: add `quint64 hashSkips()` accessor on
  StyleApplier that counts how many blocks were hash-skipped during
  the most recent pass.

- **`kind_transition_paragraph_to_heading`** — Load a Paragraph
  block `"plain"`. `applyFlatEdit` to prefix it with `## ` →
  `"## plain"`. Wait for the deferred kind-change to fire. Assert
  `doc.blockKind(id) == Heading` and the QTextBlock's
  `fontPointSize() > 11.0`.

- **`scroll_preserved_on_inplace_edit`** — Load a 50-block document.
  Resize editor to force scroll. Scroll to middle. Make a single-char
  edit in one of the visible blocks. Assert
  `verticalScrollBar()->value()` unchanged after the d2 cycle (within
  a small tolerance, say ±5 pixels for any layout-height drift in
  the edited block).

### 8.3 LinkInteraction perf regression test

Optional and deferred unless flaky. Could add a stress test
(`tst_styled_link_perf` with 1000 blocks + lots of links, mouseMove
1000 times, assert under a timing budget). Not v0.1 must-have.

### 8.4 Existing test integrity

All current `tst_styled_*` binaries must still pass with no
modifications other than the `QEXPECT_FAIL` removal in §8.1. If any
existing test fails because of these changes, the change is wrong —
fix the change, not the test.

## 9. Risks and open questions

- **Hash collisions.** XOR-mix can collide for small block-text
  changes that don't shift offsets. Mitigation: include `text.size()`
  explicitly in the mix; in practice typing changes `text.size()` on
  most keystrokes. The visual cost of a collision is one missed
  restyle on a single block until the next genuine edit.

- **Inline-span data identity.** `SourceSpan` doesn't override
  `operator==` for hashing purposes (we hash its members manually).
  Adding new flags to `SourceSpan` requires extending the bit-pack
  in `computeBlockHash`. Flag this in
  `libs/markoff-styled/CLAUDE.md`.

- **`Cmd::changeKind` API surface.** Confirmed during spec self-review:
  `Markoff::Cmd::changeKind(doc, BlockId, kind, attrs, linkRefs)` —
  free function, not a method on MarkoffDocument. Live-binding call
  sites: `libs/markoff-live/src/LiveListModelBinding.cpp:477`, `:507`,
  `:548`, `:628`, `:669`. Empty `{}, {}` for attrs/linkRefs is the
  inferred-kind path (no metadata transfer).

- **`QSet<Markoff::BlockId>` ordering.** `Markoff::BlockId` must
  satisfy `qHash` for `QSet`. If it doesn't, fall back to
  `std::unordered_set<Markoff::BlockId>` or a `QHash<BlockId, bool>`.

- **Detached fontScale Discipline Log entry.** v0 recorded
  `Editor::m_applyingFontScale` as a possibly-dead guard. This spec
  doesn't change that. The guard either becomes load-bearing if
  scroll preserve interacts with fontScale changes, or stays as a
  pending Discipline Log audit. No action this spec.

## 10. References

- v0 leaf spec: `docs/specs/2026-05-26-markoff-styled-leaf-design.md`
- Engineering invariants: `docs/INVARIANTS.md`
- `markoff-live` patterns we're porting:
  - Same-value guard: `libs/markoff-live/src/InlineHighlighter.cpp:21–28`
  - Kind transition: `libs/markoff-live/src/KindTransition.h`,
    `KindTransition.cpp`
  - Driving site: `libs/markoff-live/src/LiveListModelBinding.cpp`
    (`onD2Changed` handler)
- v0 known gaps (per-leaf): `libs/markoff-styled/CLAUDE.md`
- Discipline Log: `docs/queue.md`
