# WP unification for the flat-text views (and Shift+Enter soft break, v2)

> **2026-05-28. Branch `master`.** Supersedes the morning's
> WP-styled-vs-TE-source split draft of this file — the user's call after
> dogfood is "just give me the word-processor UX everywhere, like the QML
> live view; then Shift+Enter for an immediate (within-paragraph) line
> break."
>
> Required reading:
> [`2026-05-27-flat-view-enter-and-caret-authority-design.md`](2026-05-27-flat-view-enter-and-caret-authority-design.md),
> [`../VIEW-IMPLEMENTORS-GUIDE.md`](../VIEW-IMPLEMENTORS-GUIDE.md),
> [`../INVARIANTS.md`](../INVARIANTS.md).

## 1. Problem (recap)

Dogfood of the 2026-05-27 caret-authority fix surfaced a paradigm mismatch:
the model creates a transient empty block on Enter (word-processor-correct),
but the binding renders the block list into the QTextDocument with `\n\n`
between every adjacent pair (text-editor-style), so an empty block contributes
its own `\n\n` on each side. Net: one Enter at end-of-paragraph produces a
QTextDocument string `"…\n\n\n\n…"` — **four newlines / three blank lines** —
where the user expected one extra paragraph gap. Backspace into the gap then
collapses only one of those four newlines, giving the "jumps by less than
expected" feeling.

`markoff-live` does not have this bug because it has no flat-text rendering
at all — it lays out per-block QML delegates with margins, the way a word
processor does.

## 2. Decision

**Both flat-text view leaves (`markoff-styled` and `markoff-source`) adopt
the same word-processor structural model that `markoff-live` already uses:**

- One `QTextBlock` per model block, **separated by a single `\n`** in the
  QTextDocument (Qt's structural separator between QTextBlocks — not a
  visible character).
- The visible gap between paragraphs is **paragraph margins**
  (`QTextBlockFormat::topMargin`/`bottomMargin`), **not literal whitespace**.
- An empty model block renders as an empty `QTextBlock` whose visible
  presence is *only* its own margins — so one Enter at end-of-paragraph =
  one extra margin-gap (≈ 2× normal paragraph gap), not three blank lines.
- The caret cannot land "in the gap" because the gap is no longer a position
  — it's layout space.

This is "just word-processor everywhere." Source view does **not** become a
literal-byte text-editor; it remains a sibling of styled, distinguished only
by *not rendering inline formatting* (the markdown markers `**`, `_`, `==`,
etc. stay visible as characters; styled hides/styles them). At the structural
level (blocks, paragraphs, Enter, backspace), source and styled are
identical.

**Shift+Enter — within-paragraph line break — is a v2 feature** (§7). v1
ships the structural unification.

## 3. Architecture

`SourceTextDocumentBinding` is shared by both views as today. There is **no
paradigm flag** — both views consume the same rendered flat view. The
caret-authority chokepoint (`m_pendingCaret` → `caretResolved` →
`setTextCursor`) and the forward-path dispatch (single-block fast path,
cross-block non-structural, structural Enter) all stay intact. The only
delta in v1 is **rendering** plus one small refinement to
`applyInteractiveNewline`.

### 3.1 New runtime flat view: single-`\n` separator

The canonical `MarkoffDocument::flatView()` (with `\n\n` between content
blocks) is **unchanged** — it is the save/parse form, used by
`serializeForSave`. A new public accessor exposes the **runtime** flat view
that the QTextDocument actually holds:

```cpp
QByteArray MarkoffDocument::widgetFlatView() const;
```

Definition: blocks joined by a single `\n`. Each model block is exactly one
`QTextBlock` in the result. Empty model blocks contribute an empty
`QTextBlock`. Trailing-newline policy matches the current `flatView()`'s
contract (no final document terminator — `flatView` does not append one
either; the save form appends `\n` via `finalDocumentTerminator()`).

Verification table (the two flat views are *not* the same):

| Model | `widgetFlatView` (runtime) | `flatView` (save/parse, unchanged) |
|-------|----------------------------|-------------------------------------|
| `[Hello]` | `Hello` | `Hello` |
| `[Hello, World]` | `Hello\nWorld` | `Hello\n\nWorld` |
| `[Hello, "", World]` | `Hello\n\nWorld` | `Hello\n\n\n\nWorld` |
| `[Hello, "", "", World]` | `Hello\n\n\nWorld` | `Hello\n\n\n\n\n\nWorld` |
| `[Hello, ""]` | `Hello\n` | `Hello\n\n` |

The binding's three call sites switch from `flatView()` to `widgetFlatView()`:
`syncQtDocumentFromMarkoff` (initial seed), `onQtContentsChange` (forward
path's pre-edit string), `onD2DocumentChanged` (reverse-path expected).
`serializeForSave` is untouched.

### 3.2 Coordinate translation update — separator width = 1

`Markoff::Detail::findBlockAtSepByte` and the binding's `sepViewPosOf` /
`noSepByteToSepViewPos` are written against a 2-byte separator (`\n\n`) and
the 2026-05-27 underflow fix in `FlatBlockResolve.cpp` distinguishes
"in-block" vs "in separator zone" by `SEP_LEN`. With single-`\n`
separators, the constant becomes **1**. Specifically:

- `findBlockAtSepByte`: change the separator-zone branch to test `sepOff <
  blkEnd + 1` instead of `sepOff < blkEnd + SEP_LEN`. Since the separator
  is exactly one byte (one `\n`), "in the zone" means `sepOff == blkEnd`
  exactly — there is no interior position to be in. Conceptually the
  separator-zone special-case can be replaced by simple boundary handling.
- `sepViewPosOf`: change the per-block accumulator from `+= len + 2` to
  `+= len + 1`.
- `noSepByteToSepViewPos`: same — separator width 1.

Existing callers of `findBlockAtSepByte` in `markoff-source` (link template
insertion, etc.) and in the binding all stay correct because they pass the
bias parameter — the single-byte separator just shrinks the ambiguous zone.

### 3.3 `applyInteractiveNewline` boundary refinement

The Task 1 implementation walks blocks with `<=`, which picks the *first*
block whose `cumulativeEnd >= atByte`. For canonical Enter at end-of-paragraph
(`atByte == end of last content block, no empties after`) that's correct.
But when one or more empty model blocks already sit at the boundary — the
"Enter at start of an existing empty paragraph" case — the `<=` rule picks
the content block before them, and the new sibling lands *before* the
existing empties. The caret then stays at the same sep-view position visually,
which the user reads as "Enter did nothing."

The vim-faithful (and word-processor-faithful) behavior is the opposite:
the cursor moves *down* a line, the new sibling is inserted *after* the
existing empty(ies). Refined resolution:

```cpp
for (size_t i = 0; i < blocks.size(); ++i) {
    const uint32_t sz = static_cast<uint32_t>(blockText(blocks[i]).size());
    const uint32_t blkEnd = cursor + sz;
    if (atByte < blkEnd) {                          // strictly within
        idx = static_cast<int>(i);
        byteInBlock = atByte - cursor;
        break;
    }
    if (atByte == blkEnd) {                         // at boundary
        size_t j = i + 1;
        while (j < blocks.size() && blockText(blocks[j]).size() == 0) ++j;
        // No empties after i  → attribute to (i, end-of-i).
        // Empties after i    → attribute to (j-1, 0) — the last empty.
        if (j == i + 1) {
            idx = static_cast<int>(i);
            byteInBlock = sz;
        } else {
            idx = static_cast<int>(j - 1);
            byteInBlock = 0;
        }
        break;
    }
    cursor = blkEnd;
}
```

Preserves all existing Task 1 cases (no empties adjacent → same idx as
before) and fixes the empty-block-Enter case (cursor moves down, new sibling
inserted *after* the existing empty in the IdList).

### 3.4 Styled paragraph margins

`Markoff::Styled::StyleApplier::applyFormats` (or a new sibling pass) sets
`QTextBlockFormat::topMargin` and `bottomMargin` on each `QTextBlock` from
theme tokens. Defaults: ~0.4 line-height each (tune in dogfood). Empty
blocks get the same margins, so a transient empty paragraph contributes its
visible gap.

### 3.5 Source paragraph margins

`Markoff::Source::Editor` applies the same paragraph margins as styled —
also via a `QTextBlockFormat` pass in or alongside its existing format/style
machinery. The point of the source view is to show the markdown markers
unrendered (no bold styling, no `==highlight==` painting); paragraph-level
*structural* presentation matches styled. If you want a flatter, denser
source feel, the margins can be smaller (theme-tuned), but the structure is
the same.

## 4. Save canonicalisation

`MarkoffDocument::serializeForSave()` continues to emit canonical CommonMark
content: blocks joined by `\n\n`. Runs of empty Paragraph blocks compress
away (the serializer skips empty Paragraphs). On reload, the canonical form
parses back, so a user typing `Enter Enter Enter` and saving sees one
canonical paragraph break on reload. **Lossy round-trip for extra blank
paragraphs is intentional and accepted.**

## 5. Tests

(INVARIANTS §4 — falsifiable, widget-level. The structural slot is proven to
fail on HEAD first.)

**Update `tst_styled_dogfood_invariants` end-to-end slots:** positions
shift because the separator is `\n` now, not `\n\n`. In `[Alpha, "", Bravo]`
the new empty lands at sep-view pos **6**, not 7 (Alpha = 5 chars + 1 `\n`
= 6 = start of new empty QTextBlock).
- `enter_at_paragraph_end_creates_block_and_places_caret`: caret at 6, not 7.
- `enter_at_document_end_creates_block`: caret at 6, not 7.
- `enter_mid_paragraph_splits_with_caret_at_new_block`: caret at 6, not 7.
- `backspace_at_block_start_merges_with_caret_at_join`: caret at 5 unchanged
  (post-merge string is `"AlphaBravo"` regardless of separator policy).
- New `enter_at_paragraph_end_does_not_blow_up_the_visible_gap`: assert the
  resulting QTextDocument plain text length grows by exactly **1** (one
  `\n`), not by 2.

**Update `tst_styled_binding_caret`:** the binding-level slots assert
`caretResolved(7, 7)` for the Session-selection test — needs to become
`caretResolved(6, 6)` under single-`\n` separator (anchor at start of `World`
in `[Hello, World]` is no-sep byte 5 → sep-view pos 6 = `"Hello\n" + 0`).

**Update `applyInteractiveNewline` core unit tests:** add two slots to
`tst_d2_interactive_newline`:
- `enter_at_start_of_existing_empty_line_pushes_existing_empty_down` —
  model `[Hello, ""]`, `applyInteractiveNewline(noSep=5)` returns the *new*
  block which sits *after* the existing empty in the IdList; final model
  `[Hello, "" (old), "" (new)]`.
- `enter_with_run_of_empties_inserts_at_last_position` — model
  `[Hello, "", "", World]`, `applyInteractiveNewline(noSep=5)` returns a
  new block inserted just before World (after the run of empties).

**New `tst_source_dogfood_invariants`:** mirror the styled dogfood slots
through `Markoff::Source::Editor` (the same caret-position invariants hold —
both views are now WP-structural). Verifies the source widget's `caretResolved`
wiring works and produces the same outcomes.

**Falsifiability:** before the single-`\n` rendering switch, the existing
`enter_at_paragraph_end…` slots' assertions still pass at the OLD pos 7
(under `\n\n` separator). After switching the binding to `widgetFlatView`
but before updating the assertions, the slots fail with `position == 6
expected 7` — that's the gate that proves the rendering shift is the
load-bearing change.

## 6. Doc + invariant updates

- `libs/markoff-core/CLAUDE.md` — note that `flatView()` is the save/parse
  form and `widgetFlatView()` is the runtime form (single `\n` separator,
  margins-driven gap); both are valid, used by different callers. The
  interactive-ingress exception note (added 2026-05-27) stays — empty
  blocks are still legal under the WP paradigm; their *rendering* is the
  margin gap.
- `docs/VIEW-IMPLEMENTORS-GUIDE.md` — the §B status table needs no change
  (B.1/B.3 stay ✅ for source+styled). The §B.1 prose is updated to note
  that the dogfood follow-up (this spec) switched the *rendering* of empty
  blocks from "literal `\n\n` per boundary" to "margins-driven gap" — same
  caret-authority machinery underneath. §0.2 of the guide (paragraph
  delineation paradigms — material we discussed) is added in a much
  shorter form: "Markoff is word-processor everywhere. `markoff-source`
  is a *visual* sibling of `markoff-styled` that shows markdown markers
  unrendered; it is not a literal-byte text editor."
- `libs/markoff-styled/CLAUDE.md` and `libs/markoff-source/CLAUDE.md` —
  document the paragraph-margin styling decision; point to this spec.
- `docs/queue.md` — log the underflow-fix follow-up note (separator width
  is now 1, the underflow-can't-happen-anymore proof is the new tests).

## 7. v2 — Shift+Enter for within-paragraph line break

**Out of scope for v1; deferred to a follow-up spec.** Sketched here so the
v1 work doesn't make v2 harder to build later.

User intent: pressing Shift+Enter inserts a **line break inside the current
paragraph** (the same paragraph object in the model — not a new block).
Semantically a markdown "hard line break" (`<br>` in HTML, `  \n` or `\\\n`
in CommonMark source).

Mechanism sketch:
- Editor event filter intercepts `Shift+Return` and inserts `U+2028` (Unicode
  Line Separator) into the QTextDocument — Qt renders that as a soft break
  inside the current `QTextBlock`, no new `QTextBlock` created.
- Binding's `onQtContentsChange` detects an inserted `U+2028` and routes to
  `d2ApplyBufferEdit` (no structural change — content edit inside one
  block).
- **B1 invariant amended:** block buffers MAY contain a literal `\n`
  representing a within-paragraph line break. The binding stores the soft
  break as `\n` in the block buffer and translates `U+2028 ↔ \n` on the
  forward/reverse path.
- `serializeForSave`: each `\n` inside a block buffer emits as `"  \n"` (the
  CommonMark hard-line-break syntax).
- Parser (load): a CommonMark hard line break inside a paragraph stores
  `\n` in the block buffer.

The B1 amendment is the load-bearing decision. It surfaces several
implications (existing callsites that assume "blockText contains no `\n`"
need auditing; UTF-16 ↔ UTF-8 width changes around `U+2028`). All belongs
in the v2 spec.

## 8. Out of scope (v1)

- Shift+Enter (see §7).
- Theme tuning of paragraph margins (defaults baked in; tune in dogfood).
- Source-view inline-styling diff vs. styled (separate concern).
- The "phantom QTextBlock" / TE-vs-WP split from the prior draft of this
  file — *not happening*.

## 9. Definition of done

- `MarkoffDocument::widgetFlatView()` exists; canonical `flatView()`
  unchanged.
- `SourceTextDocumentBinding` uses `widgetFlatView()` in its three call sites
  (`syncQtDocumentFromMarkoff`, `onQtContentsChange`, `onD2DocumentChanged`).
- `findBlockAtSepByte`, `sepViewPosOf`, `noSepByteToSepViewPos` updated for
  single-`\n` separator (separator-zone constant 2 → 1).
- `applyInteractiveNewline` refined per §3.3 (skip-empties-at-boundary).
  Tests added.
- `StyleApplier` applies paragraph margins.
- `Markoff::Source::Editor` applies paragraph margins (theme-driven).
- Tests in §5 green; the falsifiability gate proven on HEAD first.
- Fast suite green except the three known pre-existing live-side failures.
- Docs in §6 updated.
- User dogfood confirms: one Enter in styled OR source = one extra
  paragraph margin-gap (not three blank lines); caret in the new empty
  paragraph; backspace dissolves the boundary; pressing Enter at the start
  of an existing empty paragraph moves the cursor down a line.
