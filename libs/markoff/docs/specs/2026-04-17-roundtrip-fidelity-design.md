# Round-Trip Fidelity — Design

> **Status:** Approved 2026-04-17.
>
> **Scope:** Markoff-internal + MarkoffParser-internal. Extends the
> partial 2026-04-17 `leadSeparator` work so that select-all + copy +
> paste reproduces the source `.md` file byte-for-byte, and the editor
> visually renders the exact blank-line layout of the source.
>
> **References:**
> - `docs/TODO.md` — "Round-trip fidelity: blank lines around block items" (2026-04-17 partial fix)
> - `docs/architecture.md` — §"Round-trip fidelity" future-work item
> - `libs/markoff-parser/src/MarkdownSplitter.cpp` — segment boundary logic
> - `libs/markoff/src/SceneCoordinator.cpp` — `toMarkdown()`, `interItemNewlines()`
> - `libs/markoff/src/SelectionManager.cpp` — `serializeAsMarkdown()` (the cross-boundary clipboard path)
> - `libs/markoff/src/SelectableItem.h` — `leadSeparator` field to be retired

---

## Problem Statement

Markoff does not roundtrip whitespace. Three specific failures:

1. **Clipboard fidelity.** `SelectionManager::serializeAsMarkdown()` joins
   items with hardcoded `"\n"` (text-text) or `"\n\n"` (any block involved),
   regardless of how many blank lines were in the source. Select-all +
   copy + paste of a document with 8 blank lines between blocks produces
   output with 2.
2. **Visual fidelity.** Blank lines between a text item and a block item
   (e.g., an image) are not rendered as actual empty lines on screen —
   they live only as a separator string on the next item. A file with
   8 blank lines before an image looks identical on screen to a file
   with 2.
3. **Line-number drift.** `SceneCoordinator::interItemNewlines(prevText,
   currText)` returns 1 if both are text items else 2. Every call site
   that counts source lines (heading map, `globalPositionOf`,
   `itemAtGlobalLine`, `ensureHeadingMap`) uses this heuristic. When the
   actual inter-item whitespace differs, heading/folding line numbers
   drift. Commit 7e60bfa fixed the "heading map drift" symptom by
   patching one call site; the underlying heuristic is still in place.

The 2026-04-17 partial fix (`leadSeparator`) captures per-item lead
separators from the splitter and uses them in
`SceneCoordinator::toMarkdown()` — so `toPlainText()` / file save paths
roundtrip. But the clipboard path, the visual rendering, and the
line-number math all still use the old normalization.

### User requirement

> "We are rendering markdown files. What's in the file, we show. We have
> an agreement that delimiters, etc. are hidden until being edited. The
> presence of double asterisks is shown by the bold contents, even if the
> asterisks are hidden. 8 blank lines has no semantic meaning; we have
> no contract with the user for hiding them."

Blank lines are ordinary editable content, not aesthetic decoration.
They must render on screen, be cursor-reachable, and survive every
serialization path.

---

## Non-Goals

- Changing the tree-sitter grammar or block-boundary detection.
- Fixing `TableConverter::reconcile` called with empty regions (separate
  TODO item).
- Fixing incremental table conversion in `reparse()` (separate TODO).
- Changing blank-line rendering height / typography.

---

## Approach A — "Every source byte lives inside a text item"

### Invariant

> **Concatenating every segment's `text` with a single `"\n"` between
> segments reproduces the source exactly.**

Equivalently: every source byte is owned by exactly one segment's
`text`, except for the single `\n` boundary between each pair of
consecutive segments, which is implicit in the join.

### Splitter contract

`MarkdownSplitter::split()` partitions the source into an ordered list
of segments. Each segment is either a **text segment** (a run of
markdown that renders inside a `MarkdownTextItem`'s `QTextDocument`) or
a **block segment** (a standalone non-text element — today only images;
tomorrow possibly fenced code, embeds, or similar).

Rules:

1. **Block segments' `text` is content only.** No leading or trailing
   `\n`. For an image, `text = "![alt](url)"`. The terminating `\n` on
   the block's line is the join separator to the next segment, not part
   of the block's content.
2. **Text segments' `text` absorbs all adjacent whitespace.** A text
   segment can begin with blank lines and can end with blank lines. The
   boundary `\n` that separates it from the preceding or following
   segment is not part of its content.
3. **Between any two consecutive segments the boundary is exactly one
   `\n`.** Blank lines never live "between" segments; they live inside a
   text segment.
4. **If two block segments would otherwise be adjacent with more than
   one `\n` between them (i.e., at least one blank line), the splitter
   inserts a text segment between them to hold those blank lines as
   empty paragraphs.** If the gap is exactly one `\n` (zero blank
   lines), the two block segments stay adjacent — no spacer.
5. **Leading blank lines at the start of the document become content of
   the first segment.** If the source begins with blank lines followed
   by a block, the splitter emits a leading text segment whose content
   is those blank lines. If the source begins with text, the first text
   segment already owns those blank lines naturally.
6. **Trailing whitespace at the end of the document becomes content of
   the last segment.** If the source ends with a block followed by
   `\n` or more, the splitter emits a trailing text segment whose
   content is whatever whitespace follows that wasn't the single join
   `\n` to the block.
7. **`leadSeparator` is retired.** The field is removed from
   `MarkdownSegment` and from `SelectableItem`.

### Byte-accounting reference

Given source `S`, a sequence of non-text block boundaries
`(b₀, b₁, …, bₙ)` where each `bᵢ` has `[startᵢ, endᵢ)` byte range.

For each block `bₖ`, consider the **pre-block region** `R = S[aₖ … startₖ)`
where `aₖ = 0` if `k = 0`, else `aₖ = endₖ₋₁`. The strip rule differs by `k`:

- **`k = 0`** (pre-first-block): strip at most one **trailing** `\n`
  from `R` (the join separator to the following block). There is no
  prior segment, so no leading boundary to strip. Emit the text segment iff:
  - (a) the stripped content is non-empty, **or**
  - (b) `|R| ≥ 1` (a lone leading `\n` becomes an empty text segment
    so the join can reconstruct that `\n`).
- **`k > 0`** (between two blocks): strip at most one **leading** `\n`
  *and* at most one **trailing** `\n` from `R`. The leading `\n` is the
  join separator from the prior block; the trailing `\n` is the join
  separator to the following block. Emit iff:
  - (a) the stripped content is non-empty, **or**
  - (c) `|R| ≥ 2` (two or more `\n`s between the blocks ⇒ at least
    one blank line ⇒ emit an empty-content spacer text segment).
  - If `|R| = 1` (the blocks are separated by a single `\n`), skip —
    the blocks are directly adjacent in the segment list.

**Block segment** emits `S[startₖ … endₖ)` with a trailing `\n` stripped
if present (same as today).

**Post-last-block region** `R = S[endₙ₋₁ … |S|)`: strip at most one
**leading** `\n` (join separator from the last block). There is no
following segment, so no trailing boundary to strip. Emit iff:
- (a) the stripped content is non-empty, **or**
- (b) `|R| ≥ 1` (a lone trailing `\n` becomes an empty trailing text
  segment).

**If there are no block boundaries at all**, emit one text segment
whose content is all of `S` (as today's "boundaries empty" branch
already does).

**If after all the above no segments have been emitted** (e.g., input
was empty), emit one empty text segment so cursor placement has an
anchor.

### Worked examples

| Source                         | Segments                                        |
|--------------------------------|-------------------------------------------------|
| `"A"`                          | `[text("A")]`                                   |
| `"A\n"`                        | `[text("A\n")]` (no blocks → whole thing is one text segment) |
| `"![i]"`                       | `[block("![i]")]`                               |
| `"![i]\n"`                     | `[block("![i]"), text("")]`                     |
| `"\n![i]"`                     | `[text(""), block("![i]")]`                     |
| `"A\n![i]"`                    | `[text("A"), block("![i]")]`                    |
| `"A\n\n![i]"`                  | `[text("A\n"), block("![i]")]` (1 trailing blank in text) |
| `"A\n\n\n![i]"`                | `[text("A\n\n"), block("![i]")]` (2 trailing blanks) |
| `"![i]\n\nA"`                  | `[block("![i]"), text("\nA")]` (1 leading blank) |
| `"![i]\n\n![j]"`               | `[block("![i]"), text(""), block("![j]")]` (spacer = 1 blank) |
| `"![i]\n![j]"`                 | `[block("![i]"), block("![j]")]` (no spacer; adjacent) |
| `"![i]\n\n\n![j]"`             | `[block("![i]"), text("\n"), block("![j]")]` (spacer = 2 blanks) |
| `""`                           | `[text("")]` (single empty segment as today)    |

Verification for each row: joining the segment texts with `"\n"` must
equal the source. Spot-check: row 7 → `"A\n" + "\n" + "![i]" = "A\n\n![i]"` ✓.
Row 11 → `"![i]" + "\n" + "" + "\n" + "![j]" = "![i]\n\n![j]"` ✓.
Row 13 → `"![i]" + "\n" + "\n" + "\n" + "![j]" = "![i]\n\n\n![j]"` ✓.

### How blank lines render

A text segment's `text` is loaded into a `MarkdownTextItem`'s
`QTextDocument` via the existing `createTextItem(seg.text)` path. A
`QTextDocument` built from text containing `N` newlines contains `N+1`
paragraph blocks; empty blocks render as empty rows with the
document's line height. So a text segment of `"\n\n"` renders as three
empty rows; a text segment of `"A\n\n\nB"` renders as `"A"`, two empty
rows, `"B"`.

Blank lines are therefore already cursor-reachable editable content
through the existing `QTextDocument` / `TextControl` machinery. No new
widget type, no painted-padding hack, no extra cursor-navigation code
is required.

### Edits to blank-line regions

Because blank-line regions are now ordinary text inside an ordinary
text item, editing them follows the existing rules:

- Pressing Enter on a blank line inserts an additional empty paragraph.
- Pressing Backspace at the start of a blank line removes one empty
  paragraph.
- Typing any character turns the blank paragraph into a content
  paragraph. Tree-sitter reparse picks this up on the next reparse
  tick as ordinary new text content in the segment.
- Deleting all content from a "spacer" text segment (one whose text
  content is only blank lines) leaves it as an empty text segment
  (`text = ""`, one empty block). This is valid — join of `"![i]" +
  "\n" + "" + "\n" + "![j]" = "![i]\n\n![j]"` still represents "two
  blocks with one blank line between." If the user presses Backspace
  at the start of the segment's single empty block, the segment has
  no paragraphs to delete; the key is consumed (no-op at the segment
  boundary) — this matches existing `MarkdownTextItem` behavior at
  document start. The next reparse will reconcile the resulting
  markdown source (`"![i]\n![j]"` if the user collapsed the gap)
  and the splitter will re-emit segments with no spacer.

### Call sites that change

1. **`libs/markoff-parser/include/markoff-parser/MarkdownSplitter.h`**
   - Remove `MarkdownSegment::leadSeparator` field.
   - Keep `sourceStart` / `sourceEnd` for consumers that need source
     byte offsets.
2. **`libs/markoff-parser/src/MarkdownSplitter.cpp`**
   - Rewrite `split()` to implement the new invariant (byte-accounting
     rules above).
3. **`libs/markoff/src/SelectableItem.h`**
   - Remove `leadSeparator() / setLeadSeparator() / m_leadSeparator`.
4. **`libs/markoff/src/SceneCoordinator.cpp`**
   - Remove `item->setLeadSeparator(seg.leadSeparator)` calls in
     `loadMarkdown()` (×2, one per segment type) and `reparse()` (×2).
   - `toMarkdown()`: replace `result += m_items[i]->leadSeparator()`
     with unconditional `result += "\n"` (between items, not before
     the first).
   - Remove `interItemNewlines(bool, bool)`.
   - `globalPositionOf()` / `itemAtGlobalLine()`: replace
     `line += interItemNewlines(...)` with `line += 1` (single
     separator `\n`). Within an item the line-counting already uses
     exact newline counts — those stay.
   - `ensureHeadingMap()` (line 826+ walk): replace the `+= 2 or 1`
     branch with `+= 1`.
5. **`libs/markoff/src/SelectionManager.cpp`**
   - `serializeAsMarkdown()`: replace the `"\n"` / `"\n\n"` branch with
     unconditional `result += "\n"` between items.
6. **Tests** — see "Testing" section.

### Public API surface

No public API changes. `Editor::toPlainText()`, `Editor::cursorLine()`,
`Editor::copy()`, etc. all continue to behave the same from the
consumer's perspective (and more correctly when whitespace is
non-standard).

---

## Edge cases

| Case                                         | Resolution                                                                                       |
|----------------------------------------------|--------------------------------------------------------------------------------------------------|
| Empty file                                   | Single empty text segment (as today).                                                            |
| File with only `\n`s                         | Single text segment whose content is those `\n`s.                                                |
| File starting with a block                   | No leading text segment (splitter emits block first).                                            |
| File starting with blank lines then a block  | Leading text segment with empty-paragraph content absorbs the blanks.                            |
| File ending with a block                     | No trailing text segment.                                                                        |
| File ending with block + `\n`                | Trailing empty text segment representing the single `\n`.                                        |
| File ending with block + `\n\n\n`            | Trailing text segment with content `"\n\n"` (3 empty paragraphs).                                |
| Two blocks separated by one `\n`             | No spacer; blocks are adjacent in the item list.                                                 |
| Two blocks separated by `\n\n` (1 blank)     | Spacer text segment with content `""` (one empty paragraph).                                     |
| Two blocks separated by `\n\n\n` (2 blanks)  | Spacer text segment with content `"\n"`.                                                         |
| Tables                                       | Unaffected — tables stay inside text segments and are converted to `QTextTable` as today.        |
| Fenced code blocks                           | Unaffected in practice — `findBlockBoundaries()` does not currently return fence boundaries, so fences render as text content. If that changes, this design handles them uniformly. |

### Reparse behavior

`SceneCoordinator::reparse()` already calls `toMarkdown()` →
`MarkdownSplitter::split()` → rebuild items. Under the new contract,
`toMarkdown()` emits the exact source, so `split()` on the result
re-produces the exact same segment structure (including any spacer
text segments). The existing segment-count-based "structure unchanged"
optimization continues to work.

If the user's edits collapse a spacer (e.g., deletes all blank
paragraphs between two images), segment count drops and the item-list
rebuild path runs — same as any other structural edit.

---

## Testing

All new tests live in their existing home files (parser tests under
`libs/markoff-parser/tests/`, scene tests under
`libs/markoff/tests/`). No new test binaries.

### `libs/markoff-parser/tests/tst_splitter.cpp`

Adds slots verifying the new splitter invariant:

- `testJoinIdentity_empty`: `join(split(""), "\n") == ""`.
- `testJoinIdentity_textOnly`: for `"line1\nline2\n\nline4\n\n\nline7"`.
- `testJoinIdentity_blockAtStart`: `"![i](a.png)"`.
- `testJoinIdentity_blockAtEnd`: `"A\n\n\n![i](a.png)"`.
- `testJoinIdentity_leadingBlanks`: `"\n\n![i](a.png)\n\nA"`.
- `testJoinIdentity_trailingNewline`: `"![i](a.png)\n"`.
- `testJoinIdentity_trailingBlanks`: `"A\n\n\n\n"`.
- `testJoinIdentity_twoBlocksAdjacent`: `"![a](a.png)\n![b](b.png)"`.
- `testJoinIdentity_twoBlocksOneBlank`: `"![a](a.png)\n\n![b](b.png)"`.
- `testJoinIdentity_twoBlocksManyBlanks`:
  `"![a](a.png)\n\n\n\n\n\n\n\n\n![b](b.png)"` (8 blank lines).
- `testJoinIdentity_threeBlocksMixedGaps`: a, b adjacent; b, c with
  3 blanks.

Each test runs: `auto segs = MarkdownSplitter::split(src, parser);
QString out; for (int i=0;i<segs.size();++i) { if (i) out += '\n';
out += segs[i].text; } QCOMPARE(out, src);`

Plus structural assertions verifying segment count and type per
example (e.g., "two blocks many blanks" should produce exactly 3
segments: block, text, block).

### `libs/markoff/tests/tst_scene_coordinator.cpp`

Keep the existing six `roundTripPreserves*` slots; augment their
source strings to also cover 8-blank-line gaps. Add:

- `toMarkdownExactSource_eightBlankLinesBeforeImage` — 8 blank lines
  between a paragraph and an image, exact byte-identity.
- `toMarkdownExactSource_leadingBlankLines`.
- `toMarkdownExactSource_singleTrailingNewline`.
- `toMarkdownExactSource_twoAdjacentImages_withBlanks`.
- `blankLinesAreEditableParagraphs` — construct a document with 3
  blank lines before an image; find the text item that owns the
  blanks; verify its `QTextDocument` has a corresponding number of
  empty `QTextBlock`s; position a `QTextCursor` in one of them; type
  a character; verify `toMarkdown()` reflects the edit in the expected
  source position.
- `clipboardRoundTrip_eightBlankLines` — call
  `SelectionManager::selectAll()` + `serializeAsMarkdown()` on a
  document with 8 blank lines between blocks, assert byte-identity
  to source.
- `headingLineNumber_survivesBlankLineGaps` — document with a heading
  followed by 5 blank lines and an image, then another heading. Assert
  `Editor::cursorLine()` (when cursor placed on the second heading)
  equals the source line of that heading (counting every `\n`,
  including the blanks).

### `libs/markoff/tests/tst_folding_integration.cpp` (existing)

Add:

- `foldGutter_alignsWithSourceLineCount_afterBlankGaps` — document with
  varying blank-line counts between headings; verify gutter-triangle
  positions correspond to source line numbers.

### Regression sweep

Before shipping: run the full `ctest -R markoff` suite. The splitter
change touches every consumer of `MarkdownSegment`; any test that
previously asserted `seg.text == "A"` when source was `"A\n\n![img]"`
must be updated to expect `"A\n"` (leading-trailing blanks absorbed).
Specifically check `tst_splitter.cpp` existing cases:

- `testNoBlocks`: unaffected (no block boundaries → whole doc is one
  text segment, as today).
- `testSingleTable`: unaffected (tables stay in text).
- `testSingleCodeBlock`: unaffected (fences not split today).
- `testTableBetweenText`: unaffected (table inside text segment).
- `testBlockAtStart` / `testBlockAtEnd`: may need updates — document
  boundary behavior changes (trailing `\n` now produces an empty
  trailing text segment).
- `testMultipleBlocks`: expected structure depends on input.
- `testShowcaseFile`: may need assertion updates.

Plan notes will list exact before/after assertions.

---

## Risk and rollback

**Risk of regression:** moderate. The splitter is consumed by every
document-loading path in Markoff. The new invariant is stricter (the
splitter output now has semantic meaning beyond "separate text from
blocks") and consumers like `SceneCoordinator::reparse()` depend on
item counts matching segment counts.

**Mitigation:**

- The join-identity invariant is directly testable and deterministic;
  the parser-level tests above catch any splitter regression before
  any editor code runs.
- The `leadSeparator` field removal is mechanical — the compiler
  points at every call site.
- The `interItemNewlines` removal collapses three call sites to a
  constant `1`; the change is local and testable via the heading-map
  test.

**Rollback:** single revert if the new splitter breaks. Leave the
heading-map simplification coupled to the splitter change — they're
consistent only together.

---

## Out of scope for this spec

- Incremental reparse skipping unchanged items (unrelated TODO).
- Heading-prefix visibility when cursor leaves the heading line
  (separate TODO).
- Sub-atomic fidelity of inline formatting (asterisk counts on bold,
  link title-vs-href quoting) — current implementation already preserves
  these via `QTextDocument` content; this spec does not touch them.
