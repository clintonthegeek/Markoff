# Flat-view paradigm split — WP styled, TE source — design

> **2026-05-28. Branch `master`.** Follow-up to the 2026-05-27 caret-authority
> work
> ([`2026-05-27-flat-view-enter-and-caret-authority-design.md`](2026-05-27-flat-view-enter-and-caret-authority-design.md)).
> That fix made bare Enter create a transient empty block + caret in it,
> resolving the headline "Enter does nothing" symptom. Dogfood revealed a
> deeper paradigm collision: pressing Enter at end of paragraph produced
> *three* visible blank lines in `markoff-styled`, and a subsequent backspace
> "jumped" the next paragraph up by less than the user expected. Root cause:
> we were doing word-processor semantics on a text-editor display — the model
> creates a structural empty block, but the view renders blocks joined by
> literal `\n\n` whitespace, so an empty block contributes its own `\n\n`
> separators on each side and we get 3 blank-lines for one Enter. The two
> paradigms have to be picked-and-committed-to, not blended.
>
> Required reading:
> [`../VIEW-IMPLEMENTORS-GUIDE.md`](../VIEW-IMPLEMENTORS-GUIDE.md),
> [`../INVARIANTS.md`](../INVARIANTS.md),
> [`2026-05-27-flat-view-enter-and-caret-authority-design.md`](2026-05-27-flat-view-enter-and-caret-authority-design.md).

## 1. The two paradigms (formal)

Modern text editors and word processors disagree about what a "paragraph" *is*
and how the gap between paragraphs is represented. The two paradigms are
mutually exclusive in their assumptions; mixing them produces exactly the
symptom dogfood reported.

**Text-editor paradigm (TE).** A paragraph is *emergent* from a byte pattern.
The buffer is a literal flat byte sequence. Blank lines between paragraphs
are real `\n` bytes. The caret can sit on any code-unit position, including
the middle of a blank-line run. `Enter` inserts exactly one `\n`; two
consecutive Enters insert two `\n`s. Backspace deletes one code unit. WYSIWYG
means "what you see is exactly what's in the file." (vim, raw `QPlainTextEdit`,
Markoff's intended `markoff-source`.)

**Word-processor paradigm (WP).** A paragraph is a *first-class object*. The
document is a list of paragraph objects, each holding its own text. The
visible gap between paragraphs is **programmatic styling**
(`margin-top`/`margin-bottom`, "space after"), not whitespace bytes. The
caret cannot land in the margin — it is always *in* a paragraph object.
`Enter` is a structural operation: it creates a new paragraph object and
moves the caret to offset 0 of it. Backspace at offset 0 of a paragraph is a
structural merge (dissolves the boundary, joins the predecessor and successor
into one object). WYSIWYG means "what you see is the rendered document over a
structural model." (MS Word, Obsidian live preview, Markoff's `markoff-live`
and intended `markoff-styled`.)

A full text vs. table comparison is in the appendix of this spec for the
guide port (§9).

## 2. Lane assignment (confirmed)

| Leaf | Paradigm | Visual gap mechanism | Caret in gap? | Enter |
|------|----------|----------------------|---------------|-------|
| `markoff-live` | WP | per-block QML delegate layout/margins | impossible | new delegate |
| `markoff-styled` | WP | `QTextBlockFormat::topMargin/bottomMargin` per QTextBlock | impossible (margin has no position) | new (possibly empty) model block; caret at offset 0 of it |
| `markoff-source` | TE | literal `\n` bytes between blocks | yes — blank lines are real | inserts one `\n`'s worth of structure into the model; vim-faithful (two Enters needed for a visible blank line) |

`markoff-source` is the **exception that proves the rule** — all other present
and future Markoff view leaves are WP. The guide is restructured to make this
explicit (§9).

## 3. Architecture: paradigm flag on the binding + one chokepoint

`SourceTextDocumentBinding` gains a paradigm flag:

```cpp
enum class Paradigm { WordProcessor, TextEditor };
Paradigm paradigm() const noexcept;
void     setParadigm(Paradigm p);   // default WordProcessor
```

Each widget sets the paradigm once at construction (styled →
`WordProcessor`; source → `TextEditor`). The forward path
(`onQtContentsChange`), the chokepoint (`m_pendingCaret`, `caretResolved`),
and `applyInteractiveNewline` are **shared by both paradigms** — a
bare Enter still creates a possibly-empty model block, and the caret is still
re-asserted at the tail of `onD2DocumentChanged`. The single Q_PROPERTY-style
authority decision from the previous spec stays intact.

**One refinement to `applyInteractiveNewline`** (§3.1 below) tightens its
boundary resolution so that pressing Enter at the start of an *existing*
empty model block inserts the new block in the right place (after the empty
block, so the cursor moves down a line vim-style) rather than before it.

What's paradigm-specific is **how the binding renders the block list into the
QTextDocument** and **how it translates between sep-view (`QTextDocument`)
coordinates and no-sep (model) coordinates**. Two policies, dispatched by the
flag.

The canonical `MarkoffDocument::flatView()` is **unchanged**: it is the
canonical save/parse form (`\n\n` between content blocks) and remains used by
`serializeForSave`. The new methods do not displace it; they sit beside it.

### 3.1 `applyInteractiveNewline` boundary refinement

The existing resolution walks blocks accumulating sizes and picks the first
block whose `cumulativeEnd >= atByte`. For the canonical paragraph-end case
(`atByte == end of last content block`) that picks the content block at its
end, which is correct. But when one or more *empty* model blocks already sit
at that boundary — the case of pressing Enter at the start of an existing
empty line — picking the content block puts the new empty block *before* the
existing one, leaving the caret at a sep-view position that the user reads
as "no movement." The vim-faithful behavior is the opposite: cursor moves
*down* a line (to a newly-created sibling below the displaced empty block).

The refined rule: when `atByte == cumulativeEnd of block i` and block(s)
`i+1, i+2, …` are also empty (size 0), attribute to the **last** such empty
block (its offset 0). Concretely, replace the inner `<=` test with a
strict-`<` "within-block" branch and an explicit `==` branch that scans
forward through empties:

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

This preserves all existing Task 1 cases (no empties adjacent → same idx as
before) and fixes the empty-block-Enter case (cursor moves down a line, the
new sibling is inserted *after* the existing empty in the IdList).

The cosmetic Task 1 unit test additions
- `enter_at_start_of_existing_empty_line_pushes_existing_empty_down`
- `enter_with_run_of_empties_inserts_in_last_position`
go alongside the existing four slots.

## 4. The two runtime flat-view formulas

### 4.1 WP runtime view — single-`\n` separator + paragraph margins

Each model block becomes exactly one `QTextBlock`. Blocks are joined by a
single `\n`. The visible gap between paragraphs is **margins**, not
whitespace. An empty model block renders as an empty `QTextBlock` whose
visible presence is its own margins.

| Model | `wpFlatView` | QTextBlocks | Visual |
|-------|--------------|-------------|--------|
| `[Hello]` | `Hello` | 1 | one paragraph |
| `[Hello, World]` | `Hello\nWorld` | 2 (`Hello`, `World`) | standard paragraph gap (margin sum) |
| `[Hello, "", World]` | `Hello\n\nWorld` | 3 (`Hello`, `""`, `World`) | ~2× gap — the empty block contributes its own margins |
| `[Hello, "", "", World]` | `Hello\n\n\nWorld` | 4 | ~3× gap |
| `[Hello, ""]` | `Hello\n` | 2 (`Hello`, `""`) | "trailing" empty paragraph (cursor in it after one Enter at doc-end) |

Boundary width in code-units = **1**. `findBlockAtSepByte` separator zone for
WP is exactly one `\n`. `sepViewPosOf` adds 1 per crossed boundary (UTF-16).

### 4.2 TE runtime view — content-anchored gap formula

The rule is anchored on content blocks. For a model `[…, C_a, E_1, E_2, …,
E_k, C_b, …]` where `C_a` and `C_b` are content blocks separated by `k ≥ 0`
empty blocks, the rendered gap between them is `"\n" * (k + 2)`. Empties
beyond the first content block at the *start* of the doc emit one `\n` each
(no canonical-break to add); empties after the *last* content block at the
*end* emit one `\n` each. Concretely:

```
Walk the blocks.
output = ""
last_content_index = -1
for i in 0..n-1:
    if blocks[i] is content:
        if last_content_index == -1:
            # First content block — emit leading-empty newlines then text.
            output += "\n" * i        # one \n per leading empty
            output += blocks[i].text
        else:
            empties_between = i - last_content_index - 1
            output += "\n" * (empties_between + 2)
            output += blocks[i].text
        last_content_index = i

# Trailing empties after the last content block.
if last_content_index >= 0:
    output += "\n" * (n - 1 - last_content_index)
else:
    # No content at all — pure empty doc.
    output += "\n" * (n - 1) if n > 0 else ""
```

Verified cases:

| Model | `teFlatView` | Visual blank lines |
|-------|--------------|--------------------|
| `[Hello]` | `Hello` | 0 |
| `[Hello, World]` | `Hello\n\nWorld` | 1 (canonical paragraph break) |
| `[Hello, "", World]` | `Hello\n\n\nWorld` | 2 (canonical + one user-added Enter) |
| `[Hello, "", "", World]` | `Hello\n\n\n\nWorld` | 3 |
| `[Hello, ""]` | `Hello\n` | 0 — one Enter at doc-end = cursor on next line, **not** a blank line yet (vim-faithful) |
| `[Hello, "", ""]` | `Hello\n\n` | 1 — two Enters = visible blank line + cursor on the one after |
| `["", Hello]` | `\nHello` | 0 — leading newline only |
| `["", "", Hello]` | `\n\nHello` | 1 |

### 4.3 The phantom QTextBlock in TE

In QTextDocument terms TE produces **more QTextBlocks than model blocks**.
For `[Hello, World]` (2 model blocks), `teFlatView` is `"Hello\n\nWorld"` — 3
QTextBlocks: `Hello`, `""` (the *phantom* from the canonical-break second
`\n`), `World`. For `[Hello, "", World]` (3 model blocks), `teFlatView` is
`"Hello\n\n\nWorld"` — 4 QTextBlocks: `Hello`, `""` (model empty), `""`
(phantom), `World`.

This asymmetry is the price of the canonical paragraph-break convention. The
binding's QTextBlock↔model-block convention:
- The first QTextBlock of any gap between two content blocks is the leftmost
  model empty (or the phantom, if there are no model empties — the
  canonical-break case).
- Subsequent QTextBlocks in the gap, up to and including the second-to-last,
  are the remaining model empties.
- The last QTextBlock of the gap is the phantom (the canonical-break blank
  line).
- A gap with 0 model empties has exactly 1 phantom.

For forward edits: caret in a phantom is treated by `findBlockAtSepByte`'s
existing separator-zone bias (`biasForward=false` → previous content block at
its end; `biasForward=true` → next content block at offset 0). Typing in a
phantom inserts a character *at the start of the next content block* (this is
the "soft" thing the user might not consciously want but is the least-bad
default for a coordinate space without a model home for the phantom).

For backspace: see §7.

### 4.3 Symmetry summary

Both paradigms call the same model state by the same name — `[Hello, "",
World]` is "three model blocks: a content block 'Hello', an empty block, a
content block 'World'" in both. They differ only in the string each emits to
its QTextDocument and in the visual styling. The model is paradigm-agnostic.

## 5. Coordinate translation

`Markoff::Detail::findBlockAtSepByte` becomes paradigm-aware:

```cpp
std::optional<BlockHit> findBlockAtSepByte(const MarkoffDocument *doc,
                                           quint32 sepOffset,
                                           bool biasForward,
                                           Paradigm paradigm);
```

(Existing callers either thread the binding's paradigm flag through, or use a
WP-default overload. We choose the explicit-parameter route to make the
paradigm an honest input to every coordinate translation.)

Implementation: the function still walks blocks with a running sep-view
cursor; the separator-zone width is `1` (WP) or paradigm-formula-driven (TE:
`2` between two content blocks, `1` if either neighbour is empty). The same
bias rules apply (interior-boundary previous-block for `biasForward=false`,
next-block for `biasForward=true`).

`sliceByBlocks` is the read-only sibling used in less coordinate-sensitive
places; it gets the same paradigm parameter.

`SourceTextDocumentBinding::sepViewPosOf` and `noSepByteToSepViewPos` become
paradigm-aware via the binding's flag (they call into the binding-internal
helpers, which know the paradigm via `this->m_paradigm`).

`sepViewToNoSepByteForEdit` (the binding-internal helper used in
`onQtContentsChange`) becomes paradigm-aware. Forward-path dispatch in
`onQtContentsChange` is unchanged in shape (single-block fast path,
cross-block non-structural, structural newline) but reads sep-view through
the paradigm flag.

## 6. Styled view paragraph margins

`Markoff::Styled::StyleApplier::applyFormats` (or a sibling pass) sets
`QTextBlockFormat::topMargin` and `bottomMargin` on each `QTextBlock`. Values
come from `Markoff::Theme` (theme-driven; theme default is a reasonable
half-line-height each, tuned in dogfood). The same margins apply to empty
blocks so a transient empty paragraph contributes its visible gap.

`Markoff::Source::Editor` does **not** add paragraph margins. Its blocks are
visually adjacent unless separated by literal `\n`s in the buffer
(empty/blank QTextBlocks).

## 7. Backspace behavior

The forward-path dispatch in `onQtContentsChange` already distinguishes:
single-block fast path, cross-block non-structural (separator-spanning
deletes), structural. With paradigm-aware coordinate translation, both
paradigms route correctly without new branches *except* for one TE-specific
case:

**TE backspace on a separator-zone byte adjacent to an empty model block.**
In TE, the user's intent on backspace at start of a paragraph preceded by a
blank line is "remove the blank line" — not "merge the paragraphs." The
binding detects this case by:

1. resolving `hitStart` and `hitEnd` for the deleted range with TE
   paradigm-aware `findBlockAtSepByte`;
2. if the range spans a separator that is *adjacent to* an empty model block
   (i.e. one of `hitStart` / `hitEnd` is on an empty block boundary);
3. dispatching to `d2RemoveBlock(emptyBlockId, t)` — delete the empty,
   stage `m_pendingCaret` at the new start-of-content position.

WP backspace at offset 0 of a paragraph keeps current behavior: merge with
predecessor (which, if the predecessor is an empty block, dissolves it just
like any other merge — both work through the same primitive).

## 8. Save canonicalisation

`MarkoffDocument::serializeForSave()` continues to emit canonical CommonMark
content: blocks joined by `\n\n`. Runs of empty blocks compress to nothing
(adjacent content blocks already have their canonical `\n\n` separator).

Concretely: walking `iterateBlocks()`, the serializer
- skips empty Paragraph blocks entirely (does not emit them),
- joins surviving (content) blocks with the per-kind serializer output
  + `interBlockSeparator() == "\n\n"`,
- appends `finalDocumentTerminator() == "\n"`.

This implements "extra blank lines compress on save" without touching the
runtime flat-view formulas or the model. Lossy round-trip is intentional and
accepted per user.

Empty blocks of non-Paragraph kinds (e.g. an empty Heading — pathological)
are still serialized as their kind dictates; this serializer change is
narrow ("skip empty Paragraph"), not "skip every empty block."

## 9. View Implementor's Guide restructure

A new top-level **§0.2 Paradigm of paragraph delineation** is added between
§1 (bimodal foundation) and §A (text-sync correctness), holding the formal
two-paradigm comparison from §1 of this spec and naming each leaf's lane.

Each cross-cutting concern gains a per-paradigm interpretation where it
matters:
- **§A.3 canonical structure on edit:** WP enforces no-empty-blocks at the
  ingress (well — admits transient empties as the deliberate exception per
  the 2026-05-27 spec); TE does not enforce empty-block constraints at all
  (the model freely holds extra empties; canonicalisation happens at save).
- **§B.1 caret re-assert after structural edit:** identical mechanism in
  both paradigms (same chokepoint, same `caretResolved`); the *effect* the
  user perceives differs (caret-in-margin-paragraph vs. caret-on-blank-line)
  but the code is shared.
- **§B.3 multi-block selection-delete caret:** same mechanism.
- **§D.1 scroll preserved across an edit:** unchanged framing; WP styled
  becomes more correct here because paragraph margins make the
  "ensure-cursor-visible" behaviour map onto sensible visual targets.

The §B status table gains no new columns — both source and styled remain
distinct entries — but the prose accompanying each row gets a paradigm tag.

## 10. Tests

INVARIANTS §4 (falsifiable, at the widget level). New tests proven to fail
on HEAD first.

**`tst_styled_dogfood_invariants` updates** (WP under new policy):
- Update existing structural slots (`enter_at_paragraph_end…`,
  `enter_at_document_end…`, etc.) — caret positions shift because WP's
  separator is now `\n` not `\n\n`. In `[Alpha, "", Bravo]` with `\n`
  separator, the new empty block lands at sep-view pos 6, not 7. Rewrite
  assertions accordingly.
- Add `enter_at_paragraph_end_shows_one_extra_gap` — load
  `"Alpha\n\nBravo"`, caret at end of Alpha, keyClick(Return); assert
  block count 3 AND the QTextDocument plain text equals `"Alpha\n\nBravo"`
  (new total gap is the margins, not new chars). Visually: only one
  additional gap, not three blank lines.

**New `tst_source_dogfood_invariants`** (TE):
- `loaded_document_renders_with_paragraph_breaks` — load
  `"Hello\n\nWorld"`, assert source view's QTextDocument plain text equals
  `"Hello\n\nWorld"` (one visible blank line between paragraphs).
- `one_enter_at_end_of_paragraph_moves_cursor_to_next_line` — caret at end
  of Hello in `[Hello, World]`, keyClick(Return); assert model becomes
  `[Hello, "", World]`, QTextDocument plain text `"Hello\n\n\nWorld"`,
  caret position 6 (= start of new empty in `\n\n\n`-counting), NOT a
  visible new blank-line yet — the new empty is on its own line, but no
  visible double-blank.
- `two_enters_create_a_visible_blank_line` — two Returns at end of Hello;
  assert model `[Hello, "", "", World]`, plain text
  `"Hello\n\n\n\nWorld"`, caret at start of the second empty block.
- `backspace_on_blank_line_deletes_empty_block` — load
  `"Hello\n\n\nWorld"` (model `[Hello, "", World]`), caret at start of
  World (sep-view pos 8 in TE coords), keyClick(Backspace); assert model
  collapses to `[Hello, World]`, plain text `"Hello\n\nWorld"`, caret at
  start of World.
- `save_canonicalises_runs_of_empties` — model
  `[Hello, "", "", World]`; `doc.serializeForSave()` returns `"Hello\n\nWorld\n"`
  (the empties elide).

**`tst_styled_binding_caret`** existing slots: the position assertions for
the styled-binding tests shift the same way as the dogfood ones. Update.

**Falsifiability gate.** The TE backspace test is proven to fail on HEAD
first (current cross-block merge would join Hello and World instead of just
removing the empty), demonstrating the new TE empty-block-delete branch is
load-bearing.

## 11. Out of scope / follow-ups

- **`markoff-live` is unchanged.** It is already WP via its per-block delegate
  shape; no new code touches it.
- **Theme tuning for styled paragraph margins** — defaults baked in (e.g.
  0.4 line-height top, 0.4 bottom); dialing them is dogfood follow-up.
- **TE source paragraph-fill rendering** — no syntax-highlighting of `\n\n`
  separators or visual line-numbers tweaks; out of scope.
- **Mid-empty caret nav** — the user's keyboard arrow up/down through a
  multi-empty-run in TE source uses the QPlainTextEdit's native navigation;
  no special handling.
- **Selection + Enter** (from the previous spec) remains untouched.

## 12. Definition of done

- `Paradigm` enum + `setParadigm`/`paradigm()` accessor on
  `SourceTextDocumentBinding`. Styled and source Editors set the right value
  at construction.
- `wpFlatView()` / `teFlatView()` (or a single paradigm-keyed method) on
  `MarkoffDocument`, distinct from `flatView()`. Used by the binding's
  `syncQtDocumentFromMarkoff`, `onQtContentsChange`, and
  `onD2DocumentChanged`.
- `findBlockAtSepByte`, `sliceByBlocks`, `sepViewToNoSepByteForEdit`,
  `sepViewPosOf`, `noSepByteToSepViewPos` all paradigm-aware.
- Styled `StyleApplier` applies paragraph margins per block (theme-driven).
- TE backspace adjacent to an empty model block deletes the empty (new
  branch in the cross-block-non-structural path).
- `serializeForSave` skips empty Paragraph blocks (the "compress on save"
  rule).
- All §10 tests green; the falsifiability gates proven on HEAD first.
- Fast suite green except the three known pre-existing live-side failures.
- `docs/VIEW-IMPLEMENTORS-GUIDE.md` §0.2 added, status prose updated.
  `libs/markoff-styled/CLAUDE.md`, `libs/markoff-source/CLAUDE.md`, and
  `libs/markoff-core/CLAUDE.md` updated to reference the paradigm split.
- User dogfood confirms: one Enter in styled = one extra paragraph gap (not
  three blank lines); one Enter in source = cursor on next line (vim
  semantics); two Enters in source = one visible blank line + cursor;
  backspace in source on a blank line removes the blank line (not the
  paragraphs around it).
