# Setext heading support — design (2026-05-09)

> **Status:** spec-approved (2026-05-09).
> **Scope:** the live-render leaf only. Source-widget and other consumers
> inherit the fix transparently via `markoff-core`'s heading representation.
> **Predecessor:** the E2.5 dogfood-fix arc closed in commit `c7da731`.

## 0. Background

CommonMark recognises two heading forms:

- **ATX**: `## Heading` — 1–6 levels, prefix-driven, single line.
- **Setext**: `Heading\n---` (H2) or `Heading\n===` (H1) — underline-driven,
  two (or more) lines per heading.

Tree-sitter-markdown distinguishes the two as `atx_heading` vs.
`setext_heading` block nodes; the latter exposes its level via
`setext_h{1,2}_underline` children. `Markoff::Parser`'s
`classifyTopLevelKind` correctly maps both, and
`MarkoffDocument::mapTopLevelKind` collapses them to
`Markoff::BlockKind::Heading`. Setext heading **load** therefore already
works at the kind level today.

What does **not** work:

1. **Typing** `---`/`===` after a paragraph never produces a setext
   heading. `KindTransition::inferBlockKind` (libs/markoff-live/src/
   `KindTransition.cpp:32`) recognises `---`/`***`/`___` as
   `HorizontalRule` whenever those characters are the only content of a
   paragraph — but it has no awareness of multi-line content within one
   block, so the underline can never be the second line of a setext
   heading.
2. **Demoting** a setext heading by deleting the underline doesn't work
   either: the kind-transition carve-out at `LiveListModelBinding.cpp:385`
   ("Only promote FROM Paragraph") explicitly never demotes a Heading
   block to Paragraph, regardless of buffer shape. The only path back to
   Paragraph today is the Backspace-at-start structural-key gesture.
3. There is **no** notion of a heading's *form* anywhere in the model —
   ATX vs. setext is reduced to the same `BlockKind::Heading` + `level`
   tuple, so the touched-heading save path (`serializeHeading` in
   `BlockSerializers.cpp`) can only emit ATX. A setext heading that gets
   edited and re-saved is silently rewritten as ATX.

This spec resolves all three.

## 1. Goals

1. **Loading** an Obsidian-style setext heading round-trips: it shows up
   as a single Heading block with both the text line and the underline
   line visible and editable.
2. **Saving** preserves the form. Untouched headings round-trip
   byte-identically (existing load-time-bytes path); touched setext
   headings re-emit as `text\n<underline>`; touched ATX headings re-emit
   as `<hashes> text` and the pre-existing double-prefix bug we surfaced
   in the clipboard work is closed at the same time.
3. **Typing flow:** the user types `Heading`, **Shift+Enter** for a soft
   newline within the same block, then `---` (or `===`). The single
   resulting block transitions from Paragraph to Heading with
   `level=2` (or `1`), `headingForm="setext"`, with the buffer
   `Heading\n---` left intact and visible.
4. **Demote flow:** the user deletes the underline (or all the leading
   `#`s, for ATX). The kind-transition pipeline observes that the buffer
   no longer matches its form's marker pattern and demotes to Paragraph.
   No special methods, no special UI, no Backspace-at-start needed —
   it's just the buffer shape driving the kind.
5. **HR via `---` typing** still works for the case that genuinely is a
   horizontal rule: the underline characters are alone in their block's
   buffer, with no text line preceding them.

## 2. Non-goals

- **Visual polish of the setext underline.** Today's `HeadingDelegate`
  renders the entire `model.text` in level-appropriate typography, so
  the underline line will render at heading size. That can look chunky.
  This spec accepts that as v1; a later visual pass can apply a smaller
  `QTextCharFormat` over the underline range via the inline highlighter
  to get an Obsidian-style thin rule.
- **Cross-form conversion via typing.** A setext heading whose buffer
  starts to gain `## ` markers (because the user typed them at the
  start) does *not* automatically flip to `headingForm="atx"`. The form
  stays as last set; the user's gesture for switching forms is to
  demote (Backspace at start, or delete the markers entirely) and
  re-promote in the desired form. This is a YAGNI cut — we can revisit
  if it bites.
- **Cross-block kind-transition.** Nothing in this design needs the
  kind-transition pipeline to look at adjacent blocks. The Shift+Enter
  affordance keeps the setext text and underline in the same block by
  construction; loading already produces a single block; demotion is
  intra-block. Cross-block awareness stays out of `LiveListModelBinding`.
- **`***` and `___` as setext.** CommonMark only permits `=` and `-`
  underlines. `***` and `___` continue to mean HR exclusively.

## 3. Storage convention (asymmetric)

Heading buffers carry their form's source markers. Form metadata lives
in attrs.

| Form | Buffer | `attrs.level` | `attrs.headingForm` |
|---|---|---|---|
| ATX | `## Heading` (raw, prefix in buffer) | 2 | `"atx"` (or absent ≡ "atx") |
| Setext H1 | `Heading\n===` | 1 | `"setext"` |
| Setext H2 | `Heading\n---` | 2 | `"setext"` |

Rationale for the asymmetry: ATX markers are inline with content and
the user expects to type/delete the `#`s directly to control level — a
deal-breaker if we hid them. Setext markers are on a separate line, and
the user-visible content in our editor is exactly what's in the buffer:
two lines, both editable. Form metadata is a derived label that
disambiguates *which* line the user has to mutate to demote (the hashes
for ATX, the underline for setext).

`headingForm` is a new `Markoff::AttrName` constant in
`<markoff/core/AttrNames.h>`.

## 4. Load path

`MarkoffDocument::materializeBlocksFromParsedDoc` already maps
`SetextHeading → BlockKind::Heading` and stores the raw byte range as
the buffer (`bodyUtf8.mid(byteStart, byteEnd - byteStart)`). The only
change is to record the form:

```cpp
if (kind == BlockKind::Heading) {
    if (tb.headingLevel > 0)
        d->blockAttrsMap.setWithNextStamp(
            BlockAttrKey{newId, AttrNames::Level},
            AttrValue{tb.headingLevel});
    if (tb.kind == TLB::Kind::SetextHeading) {
        d->blockAttrsMap.setWithNextStamp(
            BlockAttrKey{newId, AttrNames::HeadingForm},
            AttrValue{QString("setext")});
    }
    // ATX: leave headingForm unset (consumers default to "atx")
}
```

ATX headings get no `headingForm` attr. Consumers treat absent as
`"atx"`. This keeps the loaded representation backwards-compatible with
every existing test and code path that doesn't know about `headingForm`.

## 5. Render path

`HeadingDelegate.qml` shows `model.text` in level-appropriate
typography — no change. The buffer is now potentially multi-line for
setext headings. Both lines render at heading size in v1.

A new role `Markoff::Live::LiveBlockModel::HeadingFormRole` exposes
`attrs.headingForm` to QML so future visual polish (e.g. applying a
smaller `QTextCharFormat` over the underline span) has a hook. The
delegate doesn't need to consume it for v1 correctness.

## 6. Single-block kind-transition rules

`KindTransition::inferBlockKind` adds setext recognition. Pseudocode:

```
inferBlockKind(text):
    if text is empty:                         return Paragraph
    if countLeadingHashes(text) > 0:          return Heading [ATX, level = N]
    if text starts with ``` or ~~~:           return CodeBlock
    if text matches setext-shape:             return Heading [setext, level = 1 or 2]
    if text trimmed in {---, ***, ___}:       return HorizontalRule
    if text starts with ![ :                  return Image
    if text starts with $$ or $:              return Math
    if text matches list-marker prefix regex: return ListItem
    if text starts with > or is exactly >:    return Blockquote
    return Paragraph
```

Setext-shape match (all required):

- The buffer contains at least one `\n`, and
- The substring after the *last* `\n` is `[ \t]{0,3}(=+|-+)[ \t]*` (no
  other chars on that line), and
- The line *immediately* before the underline is non-blank ("blank" =
  empty or whitespace-only). This rules out `\n---` (empty heading
  text) and `Heading\n\n---` (blank line above underline) — both
  invalid setext per CommonMark.

Underline char `=` → level 1; `-` → level 2.

Setext is checked **before** the bare-`---`-as-HR rule so a multi-line
buffer with a trailing underline always wins over the HR fallback. A
single-line buffer that is just `---`/`***`/`___` still becomes HR.

The kind-transition code in `LiveListModelBinding::onD2Changed` already
calls `inferBlockKind` and passes the resulting kind through the
existing promotion machinery; setext just plugs in as another kind it
can return. The only new piece is **emitting the `headingForm` attr**
when promoting to setext (analogous to how ListItem promotion emits
`MarkerStyle`/`MarkerNumber`/etc.). For ATX promotion, emit
`headingForm="atx"` explicitly so subsequent demote logic has an
unambiguous form to look at.

### Demote path (carve-out relaxation)

The current rule is "Only promote FROM Paragraph; never demote from a
structural kind". The new rule keeps the spirit but adds form-aware
escape hatches for headings:

```
if rec.kind == Heading:
    form = rec.attrs.headingForm   // "atx" or "setext"
    if form == "atx" and countLeadingHashes(text) == 0:
        // user deleted all the leading hashes
        demote to Paragraph
    elif form == "setext" and not matches_setext_shape(text):
        // user deleted the underline (or made it stop matching)
        demote to Paragraph
    else:
        continue   // existing carve-out — content-only is normal for promoted kinds
```

ListItem, CodeBlock, Blockquote, HR, Image continue to honour the
existing carve-out (no demote via prefix-disappearance) — those kinds'
buffers are content-only after promotion, so the carve-out is correct
there.

### Level switch within a form

ATX heading "## H" → user types another `#` → "### H" → kind-transition
sees same kind (Heading) but `countLeadingHashes` differs from
`attrs.level` → updates `attrs.level`. This branch already exists at
`LiveListModelBinding.cpp:357-371`; no change.

Setext heading `Heading\n---` → user replaces `-` with `=` (e.g. by
selection-replace), buffer becomes `Heading\n===` → kind-transition
detects setext shape with underline char `=` → if attrs.level differs
from 1, update it. Same shape as ATX level-change, but driven by
underline char rather than hash count. This is a small addition to the
"same kind" branch.

## 7. Shift+Enter handling

`LiveStructuralKeyHandler::tryHandle` currently routes `Key_Return` /
`Key_Enter` (regardless of modifier) through the block-split path. The
new behaviour:

- `Key_Return` with `Qt::ShiftModifier` (and no other modifiers) **and**
  the current block kind supports `TextCaret` (paragraph, heading,
  list-item, blockquote, code-block — i.e. not HR/image): insert a
  literal `\n` into the buffer at `qtPos`. Do **not** split the block.
  The cursor advances past the inserted `\n`. The structural-edit
  pipeline does not fire — it's just a buffer mutation through
  `d2ApplyBufferEdit`.
- Plain `Key_Return`: existing block-split behaviour, unchanged.

This affordance is general-purpose, not setext-specific. It also
unlocks multi-line paragraphs for users who want them, multi-line
list-item content, etc. — all of which are valid in CommonMark.

## 8. Save path

`serializeHeading` in `libs/markoff-core/src/BlockSerializers.cpp`
becomes form-aware:

```cpp
QByteArray serializeHeading(BlockKind, const QHash<AttrName, AttrValue> &attrs,
                             const QByteArray &content)
{
    int level = lookup_int(attrs, AttrNames::Level, 1);
    QString form = lookup_string(attrs, AttrNames::HeadingForm, "atx");
    if (form == "setext" && (level == 1 || level == 2)) {
        // Buffer already contains "text\n<underline>"; emit verbatim.
        return content;
    }
    // ATX. Strip any leading `# ` markers from content first to avoid
    // double-prefix bug — the buffer has the markers in it; we only want
    // to emit them once.
    QByteArray stripped = stripLeadingHashes(content);
    return QByteArray(level, '#') + " " + stripped;
}
```

`stripLeadingHashes` is a new helper: drops leading `#`s up to 6, then
optional space, returning the remainder.

The untouched-block load-time-bytes path is unchanged and continues to
guarantee byte-identical round-trip for any heading the user hasn't
edited.

`reconstructFlatMarkdown` (the clipboard-paste-flat helper landed in
`c7da731`) needs the same form-aware treatment: heading payload's
`text` already contains the form's markers, so it should still emit
verbatim — but the `attrs` JSON now also carries `headingForm`, and
`LiveClipboardController::serializeSelection` needs to propagate it
through `attrsToJson` (which it already does for any attr it
encounters; no code change there, but a test-coverage check is
warranted).

## 9. Edge cases

- **Empty buffer + setext shape.** `\n---` (empty text line, then
  underline). The "at least one non-blank line above" rule rejects this;
  it stays Paragraph. (CommonMark also disallows empty-text setext.)
- **Setext underline with trailing whitespace.** `Heading\n---  ` —
  CommonMark allows trailing spaces on the underline. The shape regex
  permits `[ \t]*` trailing whitespace.
- **Setext underline with leading whitespace.** `Heading\n  ---` —
  CommonMark allows up to 3 leading spaces. Shape regex allows
  `[ \t]{0,3}`.
- **Setext with mixed `=` and `-` on the underline.** Not valid; treated
  as plain text (paragraph). Regex requires `(=+|-+)`, not `(=|-)+`.
- **Multi-line text before the underline.** `Line1\nLine2\n---` is a
  valid setext heading per CommonMark. Shape regex accepts any number
  of preceding non-blank lines. The "at least one non-blank above" rule
  is satisfied.
- **User puts a blank line into the heading via Shift+Enter.**
  `Heading\n\n---` — the line directly above the underline is blank.
  The shape rule requires the *line directly above* the underline to be
  non-blank for setext recognition. In this case it isn't, so the block
  stays Paragraph (and `---` is just literal content of a multi-line
  paragraph, not a heading underline).
- **HR via `---` typing in a fresh paragraph.** Buffer is just `---`,
  no `\n`. inferBlockKind hits the bare-rule branch → HorizontalRule.
  Unchanged.
- **Heading buffer touched but underline unchanged.** Setext heading
  `Heading\n---`, user changes `Heading` → `Edited heading`, buffer is
  `Edited heading\n---`. Setext shape still matches. Stays Heading,
  form=setext, level=2. Save (touched path) emits buffer verbatim.
  Round-trip: byte-identical.
- **Paste of a setext heading from the clipboard into a paragraph.**
  The structured-paste path in `applyStructuredPaste` /
  `reconstructFlatMarkdown` emits the buffer content verbatim for
  headings (per the post-clipboard-fix convention). The receiving side's
  kind-transition runs on the resulting paragraph buffer
  `Heading\n---` and promotes to setext heading. Works without
  additional code.

## 10. Compatibility & migration

- **Existing tests** asserting `rec.text.contains("## TL;DR")` for
  loaded ATX headings still pass — ATX storage is unchanged.
- **Existing tests** asserting that pasting/loading a setext heading
  produces an HR (if any exist) need to be revisited: setext now
  promotes to Heading, not HR. The `tst_d2_load` setext-related cases
  are the most likely site.
- **No data-format migration** is required for on-disk files. The
  ATX/setext distinction is recovered from the source bytes at load.
- **Inline highlighter spans** for setext headings: the parser already
  tags `setext_heading` with `isHeading=true` and underline children
  (`setext_h{1,2}_underline`) get appropriate spans. No span-format
  change needed for v1.

## 11. Tests (acceptance)

Per phase, in the implementation plan. High-level coverage:

- Load: setext H1 and H2 from disk → buffer + level + form correct.
- Save (untouched): setext H1/H2 round-trip byte-identical.
- Save (touched): edit setext content, save, reload → setext form preserved.
- Save (touched ATX): edit ATX content, save → no double-prefix.
- Type setext: paragraph + Shift+Enter + `---`/`===` → single Heading
  block with correct form/level.
- Type HR: paragraph "---" alone → HR (regression).
- Demote setext via underline-deletion: `Heading\n---` → `Heading` → Paragraph.
- Demote ATX via hash-deletion: `## Heading` → `Heading` → Paragraph.
- Level-switch ATX: `## H` → `### H` → level 3.
- Level-switch setext: `H\n---` → `H\n===` → level 1.
- Edge: `\n---` (empty text + underline) → Paragraph (not Heading, not HR).
- Edge: `Heading\n\n---` (blank line above underline) → Paragraph.
- Shift+Enter: in any text-bearing delegate, inserts `\n` without
  splitting the block. Cursor advances. `d2EditSequence` increments.

## 12. Phasing (implementation plan, sketch)

1. **Foundation:** add `AttrNames::HeadingForm` constant; thread the
   attr through `BlockRecord` + `LiveBlockModel::HeadingFormRole`;
   teach `materializeBlocksFromParsedDoc` to set
   `headingForm="setext"` for setext blocks at load.
2. **Save:** form-aware `serializeHeading` (+ `stripLeadingHashes`
   helper); fixes touched-setext save and the latent ATX
   double-prefix bug.
3. **Kind-transition:** setext recognition in `inferBlockKind`; emit
   `headingForm` on promotion (both ATX and setext); form-aware demote
   relaxation in `LiveListModelBinding::onD2Changed`.
4. **Shift+Enter:** soft-newline path in
   `LiveStructuralKeyHandler::tryHandle`. Generally useful, not
   setext-specific.
5. **Tests + dogfood:** per-phase test additions, plus a manual
   dogfood pass through Obsidian-style notes.

The detailed plan goes to `docs/plans/2026-05-09-setext-heading-support.md`.
