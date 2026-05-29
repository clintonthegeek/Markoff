# BlockQuote multi-paragraph split + depth attrs (queue #8.1)

**Date:** 2026-05-29
**Status:** design ready for implementation
**Queue:** #8.1
**Related:**
- `docs/specs/2026-05-18-b1-buffer-convention-design.md` (B1 — block buffers
  hold content only; no internal `\n`)
- `docs/specs/2026-05-28-flat-view-wp-unification-design.md` (WP unification
  — `widgetFlatView()` joins blocks with a single `\n`; per-kind margins
  carry the visible gap)
- `docs/specs/2026-05-29-styled-hash-gate-over-attrs-design.md` (attr hash
  in styled — new attrs added here ride that gate for free)
- `libs/markoff-core/CLAUDE.md` "Load ingress — Paragraph kind only"
- `libs/markoff-styled/CLAUDE.md` "WP unification" + v0.1 invariants

## 1. Problem

`MarkoffDocument::buildD2FromBytes` skips BlockQuote in the
Paragraph/ListItem/Setext `\n→space` collapse. The parser emits one
`TopLevelBlock` per top-level `block_quote` AST node covering its full
source range, so the resulting block buffer contains:

- per-line `> ` markers,
- internal `\n` between lines,
- blank-quoted lines (`>` alone) between multiple inner paragraphs,
- `> >` markers for nested quotes,
- the inner text of any non-paragraph children (headings, code blocks,
  list items).

Three resulting failures in `markoff-styled` (the flat-view widget leaf):

1. The internal `\n`s become spurious `QTextBlock` boundaries in
   `widgetFlatView()`, so a multi-line quote renders as multiple
   stacked blocks.
2. The literal `> ` markers display as text.
3. `applyBlockquote` styling applies to a malformed buffer; the depth
   information is unavailable, so nesting can't be reflected in
   left-margin scaling.

`markoff-live` is unaffected (consumes the parser AST through
`LiveBlockModel`, not the flat-view buffer) — its `BlockQuote` delegate
already renders correctly for top-level single-paragraph quotes. This
spec keeps live's contract intact: the existing
`kind === "blockquote"` matcher continues to fire for the common case.

## 2. Decisions

The brainstorm settled four scope axes:

| Axis | Choice |
|------|--------|
| Multi-paragraph split | **Yes** — `> p1\n>\n> p2` → 2 model blocks |
| Nested depth | **Yes** — per-block `BlockQuoteDepth` attr (≥ 1) |
| Non-paragraph inner kinds | **Yes** — walk children, preserve native kind, attach attrs |
| Where the split happens | **Parser-driven** (Approach A) — `TreeSitterParser` walks `block_quote` children, emits one `TopLevelBlock` per child with new fields |

`BlockKind::BlockQuote` is **retained, narrowed**: emitted only for
paragraph-shaped children of `block_quote` nodes (the common case +
live-view continuity). Non-paragraph inner children land with their
native kind (`Heading`, `CodeBlock`, `ListItem`, ...) + the new attrs.

## 3. Model schema

Two new entries in `Markoff::AttrNames` (header:
`libs/markoff-core/include/markoff/core/AttrNames.h`):

```cpp
inline const AttrName BlockQuoteDepth = "blockQuoteDepth"; // int, ≥ 1
inline const AttrName BlockQuoteRunId = "blockQuoteRunId"; // int, ≥ 1, doc-local
```

**`BlockQuoteDepth`** (`int`, ≥ 1) — nesting level. `1` for a
top-level quote, `2` for `> > ...`, etc. Set on every block whose
parser ancestor chain includes a `block_quote` node. Absent (or
absent-equivalent) on blocks outside any quote.

**`BlockQuoteRunId`** (`int`, ≥ 1, document-local) — groups blocks that
came from the *same* parser `block_quote` node. Two adjacent
`BlockQuote`-kind blocks with the same RunId came from one parser node
(serializer emits the quoted-blank-line separator `>\n` between them);
different RunIds → ordinary block separator `\n\n`.

**Counter scope.** `MarkoffDocument::d` gains
`quint32 nextBlockQuoteRunId = 1;`. `buildD2FromBytes` bumps it
once per parser `block_quote` node encountered (i.e. the same RunId is
shared by all children of that one node, and by any nested
`block_quote`'s children — nested children get the *outer*'s RunId
plus +1 depth). Both ids are reset by `wipeD2State()`.

**No CRDT replication semantics in v0.** Both attrs replicate through
the existing `CausalLwwMap<BlockAttrKey>`, but RunId values are not
coordinated across replicas. The v0 contract: RunId is meaningful only
to `serializeForSave` on the document where it was assigned. D5 collab
work is the place to decide cross-replica semantics; until then, two
replicas independently quoting-and-merging is an undefined-behaviour
edge case (no worse than today, where it doesn't work either).

**Hash gate.** Both attrs ride the existing attr-XOR in
`computeBlockHash` (closed by #8.5). Changing depth or RunId triggers
restyle automatically.

## 4. Parser API change

`TopLevelBlock` (header
`libs/markoff-parser/include/markoff/parser/Document.h`) gains two
fields:

```cpp
/// For blocks emitted from a child of a `block_quote` node:
/// nesting level (1 = top-level quote, 2 = `> > ...`, etc.).
/// 0 for blocks outside any quote.
int blockQuoteDepth = 0;

/// For blocks emitted from a child of a `block_quote` node:
/// a parser-assigned id (≥ 1) shared by all siblings of one
/// parser `block_quote` node. 0 for blocks outside any quote.
/// Used by serializer to distinguish "one quote split into N
/// paragraphs" from "two adjacent quotes".
int blockQuoteRunId = 0;
```

**Emission semantics.** Today `TreeSitterParser::buildDocumentQueries`
emits one `TopLevelBlock` per top-level AST node, and treats
`block_quote` as one such node. New behaviour: when the walker
encounters a `block_quote` node, it **does not emit a TLB for the
node itself**; instead it recurses into the node's children and emits
one TLB per child, with:

- `kind` = the child's native kind (Paragraph / AtxHeading /
  SetextHeading / FencedCodeBlock / ListItem / ...),
- `blockQuoteDepth` = outer-quote-count + 1,
- `blockQuoteRunId` = a fresh counter value taken when this
  `block_quote` node is first entered (shared by all this node's
  children),
- `byteStart` / `byteEnd` = the child's source byte range (NOT the
  outer quote's range — each child owns its own slice).

Nested `block_quote` children recurse the same way: when the walker
re-enters a nested `block_quote`, it bumps depth, takes a *new* RunId
(nested children belong to their own run), and recurses into that
node's children. Result: `> > p` produces one TLB with depth=2 and a
RunId distinct from the outer's.

**Existing fields unchanged.** A paragraph child of a quote emits a
TLB indistinguishable from a free-standing paragraph except for the
two new fields and the byte range. Inline-span computation, list-item
indent-depth, code-block info-string, all flow through as today.

`Kind::BlockQuote` is **retained on `TopLevelBlock::Kind` enum** so
the parser API doesn't change shape — but no walker path emits it
post-this-spec. Kept for downstream identity continuity (avoiding a
churning enum); marked deprecated in a `///` comment.

**Empty / marker-only lines.** A `>` line by itself between paragraphs
of a quote is a structural separator, not content; the parser already
treats it that way (the inner paragraph children flank it). A
quote that is *only* a marker (`>\n` with nothing inside) is currently
parsed as a `block_quote` with no children; under the new walker that
emits zero TLBs. Acceptable for v0 (the source survives intact via
the next adjacent block's byte range hand-off; if a dogfood case
proves otherwise, emit a single empty `BlockQuote` placeholder).

## 5. Load path (`buildD2FromBytes`)

For each emitted `TopLevelBlock` with `blockQuoteDepth > 0`:

1. **Kind routing.** If `tb.kind == TLB::Kind::Paragraph`, set
   `BlockKind::BlockQuote` (preserves live's existing matcher).
   Otherwise use the existing `topLevelKindToBlockKind` mapping for
   `tb.kind`. Heading/CodeBlock/ListItem inside a quote take their
   native kind.

2. **Buffer canonicalisation.** Strip the per-line `> ` markers
   from `bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart)`:

   ```
   For each '\n'-delimited line in the slice:
     skip leading spaces (≤ 3, per CommonMark),
     skip exactly `blockQuoteDepth` occurrences of '> ' (or '>'
       at line-end-without-space, which means "empty quoted line").
   Re-join the lines (with '\n').
   ```

   Then apply the existing Paragraph-class `\n→space` collapse if
   the resulting kind would do so (Paragraph, ListItem, setext
   Heading). Non-collapsing kinds (FencedCodeBlock) preserve their
   internal `\n`s as today — CodeBlock buffers are exempt from B1
   collapse by existing convention.

3. **Attr writes.** After the existing attr writes for the block's
   kind (Heading.level, ListItem.markerStyle, etc.), append:

   ```cpp
   d->blockAttrsMap.setWithNextStamp(
       BlockAttrKey{newId, AttrNames::BlockQuoteDepth},
       AttrValue{tb.blockQuoteDepth});
   d->blockAttrsMap.setWithNextStamp(
       BlockAttrKey{newId, AttrNames::BlockQuoteRunId},
       AttrValue{tb.blockQuoteRunId});
   ```

4. **`blockLoadTimeBytes`.** Stores the canonicalised buffer
   (markers stripped, `\n→space` collapsed). Untouched-block
   round-trip via `blockLoadTimeBytes` will NOT byte-match the
   original source for quoted blocks — they must always re-serialize.
   Implemented by adding `blockQuoteDepth > 0` to the
   `isSetextHeading`-style "always re-serialize" gate in
   `serializeForSave` (§6 below).

## 6. Serialization (`serializeForSave` + per-kind serializers)

`serializeBlockQuote` is rewritten to be depth-aware (callable for
`BlockKind::BlockQuote` blocks):

```cpp
QByteArray serializeBlockQuote(BlockKind, const QHash<AttrName, AttrValue> &attrs,
                                const QByteArray &content)
{
    int depth = 1;
    auto it = attrs.constFind(AttrNames::BlockQuoteDepth);
    if (it != attrs.cend()) depth = std::max(1, std::get<int>(it.value()));
    const QByteArray prefix = QByteArray("> ").repeated(depth);
    // content has no internal '\n' (B1 + Paragraph collapse), so a
    // single-line prefix is sufficient. Empty content -> "> " (well-
    // formed marker-only line; CommonMark-legal).
    return prefix + content;
}
```

**Non-BlockQuote blocks inside a quote** (`BlockQuoteDepth > 0` with
kind ≠ `BlockQuote`) get a *line-wrap pass* in `serializeForSave`
after the kind's native serializer runs:

```cpp
QByteArray bytes = nativeSerializerFor(kind)(kind, attrs, blockText(id));
if (depth > 0 && kind != BlockKind::BlockQuote) {
    const QByteArray prefix = QByteArray("> ").repeated(depth);
    // Prepend prefix to every line of `bytes`.
    bytes = prefixEveryLine(bytes, prefix);
}
```

Helper `prefixEveryLine` is a small lambda local to `serializeForSave`
(or a free function in the anonymous namespace alongside
`interBlockSeparator`).

**Inter-block separator becomes RunId-aware.** The separator between
block `i` and block `i+1`:

| `runId(i)` | `runId(i+1)` | Separator |
|------------|--------------|-----------|
| 0 | 0 | `\n\n` (existing) |
| 0 | > 0 | `\n\n` (entering a quote) |
| > 0 | 0 | `\n\n` (exiting a quote) |
| X | X (same) | `>` × depth + `\n` (quoted blank line) |
| X | Y (different, both > 0) | `\n\n` (two adjacent quotes) |

The quoted-blank-line separator uses `i`'s depth (since both blocks
in the same run share depth in v0; nested-run children get their own
RunId, see §4). Final shape for a same-RunId pair at depth=1:
`"\n>\n"` between block contents.

**Untouched-block fast path.** Extended to bypass the
`blockLoadTimeBytes` shortcut whenever `BlockQuoteDepth > 0`:

```cpp
bool isBlockQuoted = (depth > 0);
if (!isBlockTouched(id) && !isSetextHeading && !isBlockQuoted) {
    bytes = d->blockLoadTimeBytes.value(id);
    ...
} else {
    bytes = nativeSerializer(kind, attrs, blockText(id));
    if (isBlockQuoted && kind != BlockKind::BlockQuote)
        bytes = prefixEveryLine(bytes, prefix);
}
```

This means **untouched quoted blocks always go through reconstruction**.
Acceptable cost: the buffer is already small + canonical; correctness
beats byte-identical round-trip on hard-wrap formatting (matching the
Paragraph/Setext decisions in earlier #8 work).

**ListItem-in-quote.** The existing ListItem branch in
`serializeForSave` (special-cased above the generic loop) gains the
same wrap pass: after the `indentBytes + marker + " " + content`
assembly, if the ListItem has `BlockQuoteDepth > 0`, wrap each line
with `> ` × depth. The same-RunId/different-RunId separator logic
applies to the ListItem-vs-next-block branching too.

## 7. `markoff-styled` wiring

`StyleApplier::applyBlockquote` reads `BlockQuoteDepth` from attrs and
scales `leftMargin` linearly:

```cpp
void applyBlockquote(QTextCursor &cursor, int depth, qreal fontScale);
//                                       ^^^^^^^^^ already a parameter
```

The walk at `StyleApplier.cpp:562` already passes a `depth` value; the
spec requires reading it from the block's attrs instead of the current
hardcoded `1`. Concretely:

```cpp
int depth = 1;
auto it = attrs.constFind(Markoff::AttrNames::BlockQuoteDepth);
if (it != attrs.cend()) depth = std::max(1, std::get<int>(it.value()));
applyBlockquote(blkCursor, depth, m_fontScale);
```

**Non-BlockQuote-kind blocks inside a quote.** After each per-kind
`apply*` runs, the walk applies a depth-overlay:

```cpp
if (kind != Markoff::BlockKind::BlockQuote) {
    int depth = readBlockQuoteDepth(attrs);
    if (depth > 0) {
        QTextBlockFormat bf = blkCursor.blockFormat();
        bf.setLeftMargin(bf.leftMargin() + emPt(m_fontScale) * depth);
        // Optional v0.1: tint foreground with Theme::Quote at low blend
        blkCursor.setBlockFormat(bf);
    }
}
```

Kept minimal in v0: left-margin overlay only, no extra char-format
shift. The user perceives "this heading / code block sits inside a
quote" by indent, the same way CommonMark renderers commonly do.

**Hash gate.** Closed #8.5 already mixes `attrs` into `computeBlockHash`
via XOR. The two new attrs flow through automatically; depth/RunId
changes trigger restyle.

**No change to `inferBlockKind`.** The styled prefix-rule inference is
for *typing-time* kind transitions on a touched block; it doesn't need
to recognize quote depth. (A user typing `> ` at the start of a
paragraph still promotes to `BlockKind::BlockQuote` with depth=1 via
the existing path; depth>1 quotes can't be entered by typing in v0 —
that's a v0.1 concern for `LiveStructuralKeyHandler` + styled's
equivalent.)

## 8. `markoff-live` impact

**Common case (paragraph children) unchanged.** The live load path
maps `TopLevelBlock::Kind::Paragraph` with `blockQuoteDepth > 0`
to `Markoff::BlockKind::BlockQuote` via the existing
`topLevelKindToBlockKind` mapping (which already maps `BlockQuote →
BlockQuote`, and Paragraph→Paragraph — the routing decision happens
in `buildD2FromBytes`). Live's `kind === "blockquote"` QML matcher
keeps firing for the common case.

**Non-paragraph children (heading/code/list inside a quote).** Live's
`UnifiedInlineTextDelegate` will render these as their native kind
*without* the blockquote left-bar — a minor visual regression for an
already-rare case (the parser previously folded the whole quote into
one BlockQuote block which live's blockquote delegate handled by
showing the literal `> ` content; the new behaviour shows correctly
styled inner content sans quote bar). Acceptable v0; tracked as
v0.2 follow-up if dogfood surfaces it.

**Nested depth.** Live's blockquote delegate ignores
`BlockQuoteDepth` in v0; renders all nested quotes as depth=1 visually.
Round-trip on save still produces correct `> > ` markers (serializer
reads depth attr). Acceptable v0.

**`LiveStructuralKeyHandler::d2InsertBlock(..., BlockQuote, ...)`** —
unchanged. Live's "type `> ` to create a quote" path creates a
`BlockKind::BlockQuote` block with no `BlockQuoteDepth` attr; depth
defaults to 1 in the serializer (the `std::max(1, ...)` clamp), and
the missing attr is fine — at save time it round-trips as a single
`>` quote. v0.1 work in `LiveStructuralKeyHandler` would set
`BlockQuoteDepth = 1` explicitly for clarity.

## 9. Tests

New corpus in `tst_block_buffer_invariant.cpp` (extending the existing
file):

| Test | Source | Assertion |
|------|--------|-----------|
| `blockquote_buffer_strips_marker_and_collapses_newlines` | `> line one\n> line two\n` | 1 block, kind=BlockQuote, text="line one line two", depth=1, runId>0 |
| `blockquote_multi_paragraph_splits_into_two_blocks` | `> p1\n>\n> p2\n` | 2 blocks, both kind=BlockQuote, same runId, both depth=1 |
| `blockquote_two_separate_quotes_have_distinct_runids` | `> p1\n\n> p2\n` | 2 blocks, kind=BlockQuote, distinct runIds, both depth=1 |
| `blockquote_nested_carries_depth_2_and_new_runid` | `> > deep\n` | 1 block, kind=BlockQuote, depth=2, runId distinct from outer (no outer block emitted) |
| `blockquote_heading_inside_quote_uses_native_kind` | `> # H1\n` | 1 block, kind=Heading, level=1, depth=1, runId>0 |
| `blockquote_codeblock_inside_quote_uses_native_kind` | ``` > ```\n> code\n> ```\n ``` | 1 block, kind=CodeBlock, depth=1 |

New round-trip tests (separate slots; add fixtures to `kCorpus` if
they fit the normalize helper, else dedicated slots):

| Test | Source | Save shape |
|------|--------|------------|
| `blockquote_round_trip_single_paragraph` | `> hello\n` | `> hello\n` |
| `blockquote_round_trip_multi_paragraph` | `> p1\n>\n> p2\n` | `> p1\n>\n> p2\n` |
| `blockquote_round_trip_two_adjacent_quotes` | `> p1\n\n> p2\n` | `> p1\n\n> p2\n` |
| `blockquote_round_trip_nested` | `> > deep\n` | `> > deep\n` |
| `blockquote_round_trip_heading_inside_quote` | `> # H1\n` | `> # H1\n` |
| `blockquote_touched_paragraph_uses_canonical_form` | (edit a single-line quote, save) | depth=1 prefix re-emitted |

New StyleApplier invariant in `tst_styled_dogfood_invariants.cpp`:

| Test | Assertion |
|------|-----------|
| `blockquote_depth_2_has_double_left_margin` | depth=2 block's `QTextBlockFormat.leftMargin` is ≈ 2× the depth=1 block's left-margin |
| `heading_inside_quote_renders_with_left_margin_overlay` | Heading-kind block with `BlockQuoteDepth=1` has non-zero left-margin (heading styling + quote overlay) |

Parser-side fixtures in `tst_document_top_level_blocks.cpp`:

| Test | Source | Assertion |
|------|--------|-----------|
| `block_quote_emits_per_child_tlbs` | `> p1\n>\n> p2\n` | 2 TLBs, both `Kind::Paragraph`, `blockQuoteDepth=1`, same `blockQuoteRunId` |
| `nested_block_quote_bumps_depth_and_runid` | `> > deep\n` | 1 TLB, `Kind::Paragraph`, depth=2, runId distinct from outer counter seed |
| `block_quote_with_heading_child_emits_atx_heading` | `> # H\n` | 1 TLB, `Kind::AtxHeading`, depth=1 |

## 10. Definition of done

- Parser emits per-child TLBs with `blockQuoteDepth` + `blockQuoteRunId`
  set; existing parser tests still green.
- `buildD2FromBytes` strips `> ` markers + applies Paragraph-class
  collapse for quoted blocks; sets the two attrs.
- `serializeBlockQuote` depth-aware; `serializeForSave` RunId-aware
  separator + non-BlockQuote line-wrap; untouched-quoted bypass added.
- `StyleApplier::applyBlockquote` reads depth from attrs; depth overlay
  applied to non-BlockQuote inner kinds.
- All new tests in §9 green.
- `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` baseline
  count (249/254 at session start) doesn't regress.
- CLAUDE.md banner updated; queue #8.1 closed with closeout commit.
- Discipline-log entries scanned: the existing 2026-05-29 banner-line
  note about "BlockQuote retains internal `\n`s — its byte range
  includes per-line `> ` markers that need marker-aware stripping"
  gets struck out in the closeout commit.

## 11. Out of scope (v0.2 follow-ups)

- Live view's blockquote delegate consuming `BlockQuoteDepth` for
  nested rendering.
- Live view's non-paragraph-inside-quote rendering (currently loses
  the quote-bar visual).
- Source view (markoff-source) — `widgetFlatView()` for quoted blocks
  shows the canonicalised content without `> ` markers. Source view's
  open queue item is #8.3 (list-item markers); blockquote markers
  belong in the same conversation.
- Typing-time depth promotion (typing `> ` inside a depth-1 quote to
  enter depth-2). v0 supports load + render + save of nested quotes
  but not interactive depth manipulation.
- D5 collab semantics for `BlockQuoteRunId` (cross-replica counter
  coordination).

## 12. Risks

- **Parser walker change is structural.** The block-tree walk in
  `TreeSitterParser::buildDocumentQueries` (around line 642 per the
  earlier grep — `// Keep blockquote markers and other block-level
  delimiters`) currently has a `block_quote`-aware branch. Changing
  it to recurse-into-children-but-emit-no-self risks touching the
  `blockquoteDepth` post-processing at line 676–687 that already
  propagates depth into inline-span metadata. Implementation must
  verify the existing depth-propagation logic still receives the
  same inputs (the AST walk for inline spans is independent of TLB
  emission).
- **`blockLoadTimeBytes` invalidation.** Quoted blocks always
  re-serialize; if a future change adds the byte-identical guarantee
  to quoted blocks, this assumption needs a comment refresh.
- **Empty quote (`>\n`).** Parser emits zero TLBs; the source's
  blank-quoted-line vanishes on round-trip. Acceptable v0 (no
  dogfood pressure); fallback to "emit a single empty BlockQuote
  placeholder" if a real document needs it.
