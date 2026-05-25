# B1 buffer convention — block buffers hold content, separators belong to the serializer (2026-05-18)

> **Status:** spec-approved (2026-05-18).
> **Scope:** project-wide. Affects `markoff-core` (load, edit decomposition,
> serialization), `markoff-live` (the read-side chop), the merge-cmd test
> contracts, and the QML integration regression that's been
> `QEXPECT_FAIL`-ing since 2026-05-13.
> **Retires:** the implicit "block buffers carry a trailing `\n` as
> delimiter" convention introduced in commits `30660a8` (chop, 2026-05-04),
> `72e14d6` (load, 2026-05-04), and `bdc836c` (merge cmds, 2026-05-05).
> Closes `docs/queue.md` §#4.

---

## 0. Background — why this took six dogfood passes to write down

The codebase has been telling itself a story about its own block-buffer
shape that the code itself does not honour. Three load-bearing sites:

- **Load** (`materializeBlocksFromParsedDoc`, `MarkoffDocument.cpp:1763`)
  stores tree-sitter's byte range verbatim. That range *usually* ends
  with `\n` (when the block isn't the document's last) and *doesn't*
  when it is. There is no explicit decision; the convention is "store
  what the parser gave us."
- **Runtime create** (`d2InsertBlock`, `MarkoffDocument.cpp:1364`)
  creates an empty buffer. No terminator is added on `Cmd::enterAtEnd`,
  `applyStructuredPaste`, or any of the new-block paths.
- **Edit decomposition** (`applyFlatEdit`, `MarkoffDocument.cpp:1487–1595`)
  hard-codes `+ QByteArray("\n")` in four places when synthesizing new
  blocks, post-hoc re-establishing a convention the loader doesn't
  uniformly produce and the new-block path doesn't produce at all.

The read side compensates with a defensive chop
(`LiveListModelBinding.cpp:404` — `if (raw.endsWith('\n')) raw.chop(1)`)
whose own conditional `endsWith` betrays the author's uncertainty about
the invariant. The merge cmds (`Cmd::backspaceMerge`,
`Cmd::deleteMerge`) wrap the same uncertainty in another conditional
strip with a comment that retcons the variability as a design choice
("the trailing '\n' is the structural inter-block delimiter,"
`bdc836c`).

`interBlockSeparator()` returns `"\n"` and claims, in a code comment,
that blocks already end in `\n`. That claim is false for any single-block
no-trailing-EOL source and for any block created at runtime via
`d2InsertBlock`. The function works today only because it isn't called
after the last block (and the last block is the most common one that
violates the claim).

The team has, in fact, already made the right decision *once* — for
`BlockKind::ListItem`, in commit `37661b5` (2026-05-06, per-item
ListItem blocks). The spec for that work named the failure pattern:

> *"That deferral is wrong on its premise. tree-sitter-markdown DOES
> expose list_item byte ranges — we just didn't wire them. We delete
> all of that by fulfilling the original plan: each list item is one
> CRDT block."*

Net result of the ListItem migration: ~60 LOC deleted, regex marker
parser killed, manual renumber killed, vestigial `indentLevel`-as-block-
attr killed, multi-trailing-`\n` strip killed, cursor-delivery race
killed. The serializer reconstructs the indent, the marker, and the
trailing `\n` from attrs and content. **ListItem already complies with
the convention this spec is about to make project-wide.** The other six
block kinds are the ones still in the fossil.

The 2026-05-16 attempt to retire the chop (recorded in
`docs/queue.md` §#4) failed 12 test binaries. The failure was not
evidence that the convention is hard to change — it was evidence that
*half* the convention was changed. The loader stripped its `\n` and the
chop was deleted, but `applyFlatEdit`'s four `+ "\n"` insertions stayed,
`interBlockSeparator()` stayed, and the merge-cmd tests' "endsWith"
preconditions stayed. Per invariant 3 (*"a new authority retires the old
one in the same plan"*), that attempt violated the rule. The right plan
retires all of them at once.

The visible symptom users hit, in the meantime, is that a soft break
(Shift+Enter at end of a paragraph) is invisible: the chop strips the
`\n` from the model, the delegate's `TextEdit.text` becomes the
unchopped paragraph, the cursor cannot park past the last character.
`tst_live_render_qml_integration::shift_enter_creates_visible_newline`
documents this with two `QEXPECT_FAIL(Continue)` markers pointing at
this spec.

## 1. The invariant

> **B1 — Block buffers are content.** A block buffer (`blockText(id)`)
> holds **content only**. It carries no trailing structural delimiter.
> The bytes returned are exactly what would appear *inside* that block
> in a serialized Markdown document — never the separator that ends the
> block, never the blank line that separates it from the next.

This applies to every `BlockKind`. ListItem is grandfathered in (it
already complies); paragraph, heading (ATX and setext), blockquote,
code block, horizontal rule, image, math, html block, and table all
come into alignment.

Consequences:

- `blockText(id).endsWith('\n')` is **legitimate** only when the user
  has authored a soft break or pasted content containing one. A `\n` at
  the end of a buffer, when present, is *content*. There is no
  protocol-level meaning attached to it.
- `interBlockSeparator()` returns the **full** gap between two emitted
  block bodies: `"\n\n"` (one line break ending the previous block plus
  the blank line opening the next).
- The serializer is the **sole owner** of structural newlines —
  separators between blocks and the document-final newline. No block
  contributes its own terminator.
- `applyFlatEdit`'s block-split decomposer treats parts as content. The
  "synthesize `\n` after each non-last new block" pattern disappears.
- A reader who needs "the block as it appears on disk" calls the
  serializer for that one block, not `blockText(id)`.

## 2. Save-time normalization

The widget enforces two CommonMark-conventional normalizations on every
save:

1. **Runs of 2+ blank lines collapse to a single blank line.** This is
   a side-effect of the canonical `interBlockSeparator() = "\n\n"`:
   blocks emit body-only, the separator emits the two newlines that
   produce one blank line, and there is no path by which more than one
   blank line can be reconstructed between two blocks.
2. **The document always ends with a single trailing `\n`.** This is a
   side-effect of `finalDocumentTerminator() = "\n"` appended after the
   block loop in `serializeForSave`.

Both are intentional. Other Markdown editors (Pandoc, prettier-markdown,
remark) do similar normalization on save. The cost is that round-trip
is not strictly byte-identical for files with irregular blank-line
spacing or no final newline; the gain is that the buffer convention is
uniform and the serializer is one rule.

## 3. Component contract changes

Every code site that touches the convention, with its current and target
behaviour:

| Site | Today | Under B1 |
|---|---|---|
| `materializeBlocksFromParsedDoc` (`MarkoffDocument.cpp:1763`) | Stores `bodyUtf8.mid(byteStart, byteEnd-byteStart)` verbatim — variable trailing `\n` | After the slice, strip exactly one trailing `\n` if present. ListItem is unaffected (parser already produces content-only ranges). |
| `d2InsertBlock` (`MarkoffDocument.cpp:1364`) | Empty buffer | Unchanged — already B1-compliant |
| `LiveListModelBinding::onD2Changed` chop (`LiveListModelBinding.cpp:404`) | Conditional `if (raw.endsWith('\n')) raw.chop(1)` | **Deleted.** `r.text = QString::fromUtf8(doc->blockText(id))` directly. |
| `applyFlatEdit` intra-block-with-newlines branch (`MarkoffDocument.cpp:1487–1537`) | `firstReplacement = parts.front() + "\n"`; non-last new blocks get `seed += "\n"` | All `+ "\n"` insertions deleted. Parts are content. |
| `applyFlatEdit` cross-block branch (`MarkoffDocument.cpp:1574–1595`) | Same pattern | Same removal |
| `Cmd::backspaceMerge` (`Cmd/D2.cpp:75–80`) | Conditional `if (prevText.endsWith('\n')) { --joinOffset; removeLen = 1; }` | **Conditional dead-code deleted.** `joinOffset = prevText.size(); removeLen = 0;` unconditional. Comment retired. |
| `Cmd::deleteMerge` (`Cmd/D2.cpp:100–106`) | Same conditional | Same removal |
| `interBlockSeparator()` (`MarkoffDocument.cpp:1889`) | Returns `"\n"`; comment claims blocks carry their own `\n` | Returns `"\n\n"`. Comment cites this spec. |
| `finalDocumentTerminator()` (new) | — | Returns `"\n"`. Helper local to `MarkoffDocument.cpp` anonymous namespace. |
| `serializeForSave` non-ListItem branch (`MarkoffDocument.cpp:1956–1967`) | Untouched: emit `blockLoadTimeBytes` verbatim. Touched: per-kind serializer. Then conditional `interBlockSeparator` | Untouched: strip trailing `\n` from `blockLoadTimeBytes` before emit (normalize separator while preserving content byte-identity). Touched: per-kind serializer (which already emits body-only — see §4 audit). After the block loop, append `finalDocumentTerminator()` unconditionally if `!blocks.empty()`. |
| `serializeForSave` ListItem branch (`MarkoffDocument.cpp:1930–1953`) | `out += indent + marker + " " + content + "\n"`; loose-run blank line conditional | `out += indent + marker + " " + content;` followed by `if (i+1 < blocks.size()) out += looseRun ? "\n\n" : "\n";` — separator ownership moves into the conditional |
| `blockLoadTimeBytes` storage (`MarkoffDocument.cpp:1769, 1812`) | Stores raw load-time bytes | Unchanged. The "what the file said" record is not a content store; B1 scopes to `blockText`. |
| `backspaceMerge_stripsTrailingNewlineAtBoundary` test (`tst_d2_cmd_decomposition.cpp:88`) | Pins symptom: load → blkA ends with `\n` precondition → merge → result is `"helloworld\n"` | Rewritten as `backspaceMerge_loadFromMarkdown_appendsCleanly`: no `endsWith` precondition; expected merged result is `"helloworld"` (no trailing `\n`). |
| `deleteMerge_stripsTrailingNewlineAtBoundary` test (`tst_d2_cmd_decomposition.cpp:132`) | Same shape | Same rewrite |
| `shift_enter_creates_visible_newline` (`tst_live_render_qml_integration.cpp:125`) | Two `QEXPECT_FAIL(Continue)` markers citing queue.md #4 | Both markers **deleted**. Test passes unmodified. |
| `matchesSetextShape` trim (`KindTransition.cpp`, landed `4f5be44`) | Defensive trim of trailing `\n`s | Unchanged. Belt-and-suspenders against any future regression that re-introduces a terminator from an unexpected path. |

## 4. Per-kind serializer audit

The B1 contract for `BuiltinBlockSerializerRegistry` entries is:

> The serializer emits the block body as it would appear on disk —
> **no trailing terminator**. The caller (`serializeForSave`) appends
> the separator and any final document newline.

One-line audit per kind:

- **Paragraph** — emits `blockText` verbatim. Already content-only.
- **Heading (ATX)** — `serializeHeading` (`3e91392`) strips the ATX
  prefix from `content` first, then re-prepends. Output is `"# " + bare`,
  no trailing `\n`. Already content-only.
- **Heading (setext)** — `serializeHeading` form-aware after `e857b67`.
  Output is `"Heading\n========"` — the internal `\n` is *content*
  (under-line break between heading text and underline). The output
  does not end with `\n`. Already content-only.
- **CodeBlock (fenced)** — `"```" + lang + "\n" + content + "\n```"`.
  Fences are body. No trailing `\n`. Already content-only.
- **CodeBlock (indented)** — content with each line prefixed by 4 spaces.
  No trailing `\n`. Already content-only.
- **BlockQuote** — `"> " + content` per content line. No trailing `\n`.
  Already content-only.
- **HorizontalRule** — `"---"` (or whichever rule chars). Already
  content-only.
- **Image / Math** — block-only kinds (see
  `2026-05-13-block-only-kinds-design.md`). No D2 edit path; content
  preserved from load. Body-only by inheritance.
- **HtmlBlock / Table** — D2-inert today; inherit the same rule when
  D2 wires them.

**No serializer is expected to need code changes.** The audit is a
verification step in the plan, not a refactor. If a serializer violates
the contract, the round-trip test (§5 Test 2) catches it on the relevant
kind's fixture.

## 5. Falsifiable invariant tests

Three test slots, two new binaries.

### Test 1 — direct load invariant (`tst_block_buffer_no_load_terminator`)

Lives in `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp`.
Corpus-driven: for each fixture in a representative-corpus list — single
block no-EOL, single block with-EOL, two-block with-blank-line, setext
heading, ATX heading, fenced code block with internal `\n`, tight list,
loose list, blockquote, paragraph-after-heading, paragraph-before-list,
multi-paragraph with trailing-EOL, multi-paragraph without trailing-EOL,
document ending in horizontal rule, document with frontmatter, document
with footnotes — load it, then iterate every block and
`QVERIFY(!blockText(id).endsWith('\n'))`.

### Test 2 — round-trip stability (`tst_block_buffer_roundtrip_normalizes`)

Same binary as Test 1. Per fixture, asserts the fixed-point:

```cpp
MarkoffDocument doc(1); doc.loadFromMarkdown(fixture);
QByteArray firstSave = doc.serializeForSave();

MarkoffDocument doc2(1); doc2.loadFromMarkdown(firstSave);
QByteArray secondSave = doc2.serializeForSave();

QCOMPARE(secondSave, firstSave);          // round-trip fixed-point
QCOMPARE(firstSave, normalize(fixture));  // first save = normalized source
```

Helper `normalize(QByteArray)` is local to the test file. It collapses
runs of 2+ blank lines to one and ensures a single trailing `\n`. The
helper is not in production code.

### Test 3 — interactive contract (`tst_block_buffer_interactive`)

Lives in `libs/markoff-live/tests/tst_block_buffer_interactive.cpp`,
uses `QmlIntegrationFixture` + `LiveRealisticInputHarness` (per
invariant 5 — production callsite, not synonym). Drives the harness
through a sequence and asserts at each step:

| Step | Action | Assertion |
|---|---|---|
| 1 | Load `"Heading"` (no EOL) | Buffer 0 = `"Heading"` |
| 2 | Cursor at pos 7, type Shift+Enter | Buffer 0 = `"Heading\n"` (the `\n` is now content) |
| 3 | Type `=` | Buffer 0 = `"Heading\n="` |
| 4 | Cursor at pos 8 (between `\n` and `=`), press Enter | Buffer 0 = `"Heading\n"`, Buffer 1 = `"="` — two blocks, the soft-break `\n` is preserved as content of block 0, block 1 does not end with `\n` |
| 5 | Backspace at pos 0 of Buffer 1 (merge) | Buffer 0 = `"Heading\n="` (the `\n` survives the merge as content; the merge cmd appends Buffer 1's content cleanly), 1 block total |
| 6 | Undo | Reverts to step 4 — Buffer 0 = `"Heading\n"`, Buffer 1 = `"="` |
| 7 | Reset to empty document; paste `"a\n\nb\n\nc"` via the clipboard path | Three blocks: `"a"`, `"b"`, `"c"`. None ends with `\n`. |

After each step, `serializeForSave()` is invariant under round-trip per
Test 2's property.

Steps 4–6 specifically exercise the merge-cmd paths under B1: a buffer
that *legitimately* ends with `\n` (because the user typed Shift+Enter)
must survive split-and-merge without the cmds mistaking the content
`\n` for a removed terminator. Today's conditional strip would
incorrectly delete the user's soft break on merge; B1 removes the
strip and the soft break is preserved. The test pins this.

### Falsifiability proofs

Per invariant 4, the tests must be proven falsifiable. Four throwaway
stubs, each committed-and-immediately-reverted on the spec's
implementation branch:

| Stub | Expected failure |
|---|---|
| Remove load-time strip in `materializeBlocksFromParsedDoc` | Test 1 fails on most fixtures; Test 2 fails on round-trip; Test 3 fails at step 1 |
| Re-introduce one `+ "\n"` in `applyFlatEdit` (line 1515) | Test 3 fails at step 7; Test 2 fails for fixtures going through `applyFlatEdit` |
| Re-introduce the chop in `onD2Changed` | Test 3 fails at step 2 |
| Change `interBlockSeparator()` back to `"\n"` | Test 2 fails on every multi-block fixture |

The proof commits stay in branch history (per tier-4b / tier-4c
precedent); the reverts immediately follow.

## 6. CRDT collaboration semantics

The per-block buffer is a `CollabText::Crdt::Buffer`. CollabText
guarantees **strong eventual convergence on operations**: two replicas
that have seen the same multiset of ops compute identical buffer
content, regardless of arrival order. It does *not* guarantee any
property of the resulting content beyond "what the ops applied
produce."

B1 is convergence-stable by construction: the only invariant claim is
"the buffer is content," and any content CollabText converges to is by
definition content. There is no protocol bit to preserve, no special
position to anchor to, no recovery story for "buffer ended up with two
trailing `\n`s" — because B1 doesn't ask the question.

This is why "always single trailing `\n`" (the A1 alternative) was
discarded: it requires every replica generating ops to follow a "insert
before the terminator" rule that CollabText's byte-keyed op anchors
cannot represent stably under concurrent edits. A1 asks the wrong layer
to enforce a content-shape invariant.

The B1 boundary enforcement is **two-site**:

1. `materializeBlocksFromParsedDoc` strips on load (local-only).
2. `Cmd::*` and `applyFlatEdit` never synthesize a trailing `\n`
   (local-only).

Between those boundaries, the CRDT layer is free to produce any
content; B1 imposes no runtime check, no defensive chop, no "is this
`\n` a terminator?" branch on the read side.

**Forward flag for D5.** When `MarkoffDocument` is wired to a transport
(per `2026-05-07-d5-collab-activation-design.md`), peers running
mismatched buffer conventions will diverge silently. Mitigation belongs
to the transport handshake: peers exchange a markoff-protocol version,
refuse to collaborate across versions. This spec does not implement
that; it is captured as an open issue (§9).

## 7. Migration order

### Commit structure

Two tight feature commits in series, both green at HEAD:

1. **`feat(markoff-core): B1 buffer convention`** — load strip,
   `applyFlatEdit` `+ "\n"` removal × 4, `interBlockSeparator()`
   rewrite, `finalDocumentTerminator()` helper,
   `serializeForSave` non-ListItem branch normalizes untouched-block
   emit, ListItem branch separator-ownership update,
   `backspaceMerge`/`deleteMerge` conditional removal, merge-cmd test
   contracts rewritten, new `tst_block_buffer_invariant` binary added.
   Markoff-core builds, all markoff-core tests pass.

2. **`fix(markoff-live): retire onD2Changed chop`** — delete the chop,
   delete the queue.md-#4 comment, remove both `QEXPECT_FAIL` markers
   in `shift_enter_creates_visible_newline`, add the interactive-
   contract slot in `tst_block_buffer_interactive`. Markoff-live
   builds, all markoff-live tests pass.

The split is along the library boundary, not along functional
boundaries. At no intermediate state does the codebase have mixed
conventions: after commit #1 the core invariant holds; markoff-live's
chop is now redundant but harmless (its `endsWith` conditional is
always false post-#1 except when the user authored a soft break — in
which case the chop is *wrong*, and the existing `QEXPECT_FAIL`
markers continue to mark the regression until #2 lands).

Bisectability motivates the split: the user-facing fix (chop removal)
is severable from the core-side reshape.

### Falsifiability proof commits

After commits #1 and #2, four proof cycles per §5 — each commits the
stub, runs the suite, observes the predicted failure, then reverts.
Proof commits stay in history forever; reverts follow immediately so
HEAD is never broken.

### Documentation commits

- **Spec commit** (this document): committed before any code.
- **Plan commit**: committed before any code, after the spec, produced
  by `superpowers:writing-plans` from this spec.
- **Closeout commit**: `INVARIANTS.md` adds "Block buffer convention"
  section; `queue.md` §#4 is `~~`-struck and closed with the
  implementing commit SHA; `docs/e-arc/e-arc-status.md` Last-updated and
  recent-changes log updated; per-library CLAUDE.mds cross-reference
  the new INVARIANTS section.

### Order of operations during development

1. Write `tst_block_buffer_invariant` slots against current code;
   observe each failing in a predicted way.
2. markoff-core changes, in this order:
   - `materializeBlocksFromParsedDoc` strip
   - `interBlockSeparator()` + `finalDocumentTerminator()` helper
   - `serializeForSave` non-ListItem branch
   - `serializeForSave` ListItem branch
   - `applyFlatEdit` (all four sites in one pass)
   - `backspaceMerge` / `deleteMerge`
   - Merge-cmd test rewrites
   - Run markoff-core suite. Commit #1.
3. markoff-live changes:
   - Delete chop in `onD2Changed`
   - Delete `QEXPECT_FAIL` markers
   - Add `tst_block_buffer_interactive` slot
   - Run markoff-live suite. Commit #2.
4. Falsifiability proofs (4 cycles, see above).
5. Documentation closeout.

### Risk surface + bounds

| Risk | Bound |
|---|---|
| A per-kind serializer secretly emits a trailing `\n` | §5 Test 2 catches it on any fixture using that kind. Per-kind audit (§4) is a verification step. |
| `applyFlatEdit`'s decomposer has a corner case missed by intra-block-with-newlines test fixtures | §5 Test 3 step 7 (paste path) exercises it. Existing `tst_d4_apply_flat_edit` fixtures also rerun. |
| Untouched-block normalization corrupts a round-trip with unusual whitespace | Test 2's fixed-point comparison catches drift across two saves regardless of input. |
| QmlIntegrationFixture's JS-exception trap (from `e8514eb`) fires because a QML callsite reads `model.text.endsWith('\n')` | Desired outcome. If it fires, the QML callsite that relied on the chop's behaviour surfaces as a test failure; fixed in commit #2. |
| Performance regression from extra `chop(1)` per block at load | One byte-trim per block per load is sub-microsecond; not measurable against parse cost. No benchmark gate. |
| D5 / future remote-peer code generates `+ "\n"` ops post-merge | Out of scope; flagged in §9. |

## 8. What this spec does *not* do

- Does **not** add a buffer validator that scans for trailing `\n`s and
  corrects them at runtime. A validator would create an op storm under
  concurrent edits and add a synchronisation hazard. The two-site
  boundary enforcement is sufficient.
- Does **not** modify CollabText. The convention is enforced at the
  markoff-core layer; the CRDT engine stays unchanged.
- Does **not** bump the on-disk format version. A B1-saved file loaded
  by old code would gain spurious `\n`s after a round-trip, but old
  code isn't expected to coexist (no remote-peer story yet).
- Does **not** address the touched/untouched serializer branching as a
  design smell. That's a follow-up worth investigating once B1 lands,
  but it's not in scope.

## 9. Open issues

- **D5 protocol versioning.** When the transport ships, peers must agree
  on a buffer convention version. The handshake belongs to D5; this
  spec is the convention they will agree on.
- **Pasted content with extra blank lines.** A user pasting markdown
  with `"a\n\n\n\nb"` will see it normalize to `"a\n\nb"` on the next
  save. This matches CommonMark serialization conventions; if a user
  reports surprise, the fix is documentation, not spec amendment.
- **No final newline preservation.** Files originally without a final
  `\n` will gain one on save. Same disposition: documentation, not
  spec amendment.

## 10. References

### Cited commits

- `30660a8` (2026-05-04) — chop introduced in C-restoration R2 path,
  inherited by D2 Phase 11 without redesign.
- `72e14d6` (2026-05-04) — D2 Phase 7 load convention emerged ("store
  what the parser gave us").
- `bdc836c` (2026-05-05) — merge cmds got their conditional strip with
  the retconned-rationale comment.
- `37661b5` (2026-05-06) — per-item ListItem spec; the right decision,
  scoped to one kind. Direct precedent for B1 project-wide.
- `4f5be44` (2026-05-16) — `matchesSetextShape` defensive trim + queue
  #4 plan write-up.
- `e8514eb` (2026-05-18) — Ctrl+Shift+D root-cause fix + QML
  JS-exception trap; the trap will surface any QML callsite that
  relied on the chop, as a bonus catch surface during commit #2.

### Cited specs / docs

- `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` —
  ListItem precedent, polemical framing.
- `docs/handoff/2026-05-07-live-binding-developmental-history.md`
  §A.1 — chop's inheritance from C-restoration R2.
- `docs/specs/2026-05-07-d5-collab-activation-design.md` — D5 transport
  context for §6 forward flag.
- `docs/INVARIANTS.md` — invariants 1 (cite developmental record), 2
  (decided in writing first), 3 (retire old authority in same plan), 4
  (falsifiable tests), 5 (production callsite, not synonym), 8
  (Discipline Log).
- `docs/queue.md` §#4 — investigation findings, 2026-05-16 failed
  attempt, B1/A1 candidate write-up.
