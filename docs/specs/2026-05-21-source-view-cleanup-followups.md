# Source view cleanup — deferred follow-ups

**Date:** 2026-05-21
**Status:** Backlog (not blocking active work)
**Branch:** `exploration/new-foundation`

## Context

The dogfood fix that prompted this doc (Markoff `0b133b9`) replaced the
QTextCursor-based `Editor::setHeadingLevel` with a block-aware version that
mutates the target block's buffer directly via `d2ApplyBufferEdit`. A
companion commit (immediate follow-up to this doc) did the same for the
`toggleBold` / `toggleItalic` / `toggleStrikethrough` / `toggleInlineCode`
family. Both changes bypass the lossy sep→no-sep coordinate translation
that drives `SourceTextDocumentBinding::onQtContentsChange` →
`MarkoffDocument::applyFlatEdit`.

Auditing the rest of the source-view surface turned up three remaining
classes of issue that share the same root cause but were left for a
follow-up. They are listed here so the next time we touch this code we
can pull them in deliberately rather than rediscover them under dogfood
pressure.

## Root cause (shared)

`SourceTextDocumentBinding` holds the **sep-bearing** flat view in its
inner `QTextDocument` (so line/column matches the on-disk markdown), but
the foundation's edit and anchor APIs operate in **no-sep** block-buffer
coordinates. Every translation between the two spaces is lossy at block
boundaries:

* `sepViewToNoSepByte` clamps offsets inside the `\n\n` separator span to
  the same no-sep byte. The direction (was this end-of-block-N or
  start-of-block-N+1?) is unrecoverable post-translation.
* `applyFlatEdit`'s range-edit boundary bias (`oldStart <= blkEnd`)
  resolves the lost direction backward (previous block) for range edits
  and forward (next block) for cursor edits. The mismatch with user
  intent is what caused the heading-merge bug.

The strategic fix is **block-aware edits**: source-side ops resolve the
cursor/selection to a `(BlockId, byteInBlock)` pair before calling the
foundation, and use `d2ApplyBufferEdit` directly. `applyFlatEdit` is then
only used for genuine multi-block flat-text edits (paste, programmatic
file replacement) where the cross-block branch is actually wanted.

## Deferred follow-ups

### 1. `Editor::insertLink` — still uses QTextCursor range edits

**File:** `libs/markoff-source/src/Editor.cpp`, `Editor::insertLink`.

`insertLink` does its work via `c.insertText("](url)")` and
`c.insertText("[")` — both **cursor** edits. They bias forward at
boundaries via `applyFlatEdit`'s isCursorEdit branch, so the merge bug
does not fire today. However, the underlying translation is still lossy,
and if the implementation ever grows a removeSelectedText step (e.g. to
strip an existing markdown link before re-inserting) the boundary bug
returns silently.

**Action:** port to the same block-aware pattern as `wrapToggle` for
consistency, even though it's not currently buggy. Low-priority; the bug
class is latent, not active.

### 2. `SourceTextDocumentBinding::syncFromSession` legacy fallback — ✅ done

> Closed (this session, immediately after this doc was written). The
> fallback was dead code post-`861196c` (resetContent now populates D2
> blocks); removed in the same session. See commit history.

**File:** `libs/markoff-core/src/SourceTextDocumentBinding.cpp:205`.

```cpp
QByteArray utf8;
for (Markoff::BlockId id : m_markoffDocument->iterateBlocks())
    utf8 += m_markoffDocument->blockText(id);
if (utf8.isEmpty())
    utf8 = m_markoffDocument->toMarkdownUtf8();  // legacy fallback
```

The fallback reaches for `toMarkdownUtf8()`, the deprecated legacy
accessor that reads the pre-D2 `d->buffer` text store. `markoff-core`'s
CLAUDE.md explicitly flags this path as stale on any D2-loaded document
(it was the silent-empty-save bug Vault surfaced 2026-05-21). Cursor
mirroring through `syncFromSession` will quietly fall behind on docs
that pass through this branch.

**Action:** the fallback is effectively dead — `iterateBlocks()` is empty
only when the document itself is empty, in which case the result of
either accessor is also empty. Remove the fallback and let `utf8` stay
empty for the empty-document case. Confirm by reading the surrounding
code (the `byteOffsetToQtPos` walk handles `utf8.isEmpty()` fine).

Adjacent: `SearchController.cpp:55` is on the same migration list per the
CLAUDE.md note. Worth folding in the same commit.

### 3. Separator-delete known-gap in `sepViewToNoSepByte`

**File:** `libs/markoff-core/src/SourceTextDocumentBinding.cpp:305`
(comment), `sepViewToNoSepByte` plus `onQtContentsChange`.

The existing TODO comment captures it:

> Edits that delete separator bytes (e.g. backspace at the start of a
> block, removing the `\n\n` between two blocks) currently translate to
> a zero-length cursor edit in no-separator space, so the model retains
> both blocks while the QTextDocument has them merged. The subsequent
> `onD2DocumentChanged` then reverts the user's edit.

This is the **inverse** of the merge bug we fixed: a user backspace at
the start of a block should merge it into the previous block, but
currently bounces back. The fix lives below the source-widget surface —
either:

* Detect the separator-byte delete in `onQtContentsChange` (the binding
  knows the delta straddled the separator span) and dispatch a
  block-aware merge via a foundation API that doesn't yet exist (e.g.
  `mergeBlockIntoPrevious(blockId)`), or
* Add the bias hint to `applyFlatEdit` so the binding can request a
  forward-bias range edit at the boundary, then synthesize the merge
  there.

**Action:** spec-grade work; needs its own design. Pre-condition is a
decision about whether the source view should be able to drive
structural block edits (merge/split) at all, or whether structural
changes are always live-view-only. Until that decision lands, document
the user-visible behavior: "in source view, deleting across a paragraph
boundary visually merges but reverts on next tick."

### 4. Cursor / selection translation in
`setCursorPosition` / `pushSelectionToSession` / `syncFromSession`

**File:** `libs/markoff-core/src/SourceTextDocumentBinding.cpp:131-223`.

Every one of these paths routes through `qtPosToByteOffset` to compute a
sep-view byte, then passes it to `MarkoffDocument::textAnchorAt` (which
operates in no-sep coordinates). At intra-block positions the answer is
correct; at block boundaries the sep-view byte resolves to one of two
no-sep positions and the choice is implicit in `textAnchorAt`'s own bias
parameter, not driven by which side of the boundary the cursor visually
sits on.

Symptoms are subtle off-by-N cursor jumps after edits that move blocks,
not data loss. They have not been concretely reported but are likely
contributors to flake in some `tst_source_widget_*` tests.

**Action:** rewrite the cursor/selection translation in two layers:

1. **qt-pos → (BlockId, qtPosInBlock)** — block-aware, lossless. Uses
   the same `findBlockAtSepByte` helper landed for the format ops.
2. **(BlockId, qtPosInBlock) → TextAnchor** — uses
   `MarkoffDocument::textAnchorAt(BlockAnchor, int offset, bool rightBias)`
   which already exists and is boundary-aware.

The reverse path is symmetric. This eliminates the lossy intermediate
representation entirely.

This is the largest of the four follow-ups and the one most likely to
shake out latent test failures when landed. Worth a separate spec +
plan.

## Sequencing suggestion

When we come back to this:

1. ✅ **#2 (toMarkdownUtf8 fallback removal)** — done, same session.
2. **#1 (insertLink block-aware port)** — small, mechanical, gets the
   format-op family fully consistent.
3. **#4 (cursor translation rewrite)** — its own spec + plan; expect to
   uncover test drift.
4. **#3 (separator-delete merge)** — last, because it depends on the
   structural-edit-from-source policy decision.

## Related

* Markoff commit `0b133b9` — heading toggle block-aware fix.
* (Companion commit, this session) — `wrapToggle` block-aware port.
* `libs/markoff-core/CLAUDE.md` — "Canonical text egress" section,
  migration tracker for legacy buffer accessors.
* `libs/markoff-source/CLAUDE.md` — current source widget public
  surface.
