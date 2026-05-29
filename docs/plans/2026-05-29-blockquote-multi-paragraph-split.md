# BlockQuote multi-paragraph split + depth attrs — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `markoff-styled` render multi-paragraph, nested, and mixed-kind blockquotes correctly by splitting them at parse time into per-inner-child top-level blocks tagged with `BlockQuoteDepth` + `BlockQuoteRunId` attrs.

**Architecture:** Parser-driven split (`TreeSitterParser` recurses into `block_quote` AST children instead of emitting one TLB for the node). `MarkoffDocument::buildD2FromBytes` strips `> ` markers per line, applies the existing Paragraph-class `\n→space` collapse, and writes the two attrs. `serializeForSave` reads depth (prepends `> ` × depth, line-wraps non-BlockQuote inner kinds) and uses RunId to choose `\n>\n` vs `\n\n` separators. `StyleApplier` scales `leftMargin` by depth and overlays the same margin on non-BlockQuote inner kinds.

**Tech stack:** C++20, Qt 6.8+, tree-sitter-markdown, CMake 3.19+. Tests use QTest under `QT_QPA_PLATFORM=offscreen`.

**Spec:** `docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md`

---

## File map

**Create:** none (all changes land in existing files).

**Modify:**
- `libs/markoff-parser/include/markoff/parser/Document.h` — add two `TopLevelBlock` fields
- `libs/markoff-parser/src/TreeSitterParser.cpp` — walker recursion into `block_quote` children, depth + runId threading
- `libs/markoff-parser/tests/tst_document_top_level_blocks.cpp` — new slots + adjust existing `blockQuote` slot
- `libs/markoff-core/include/markoff/core/AttrNames.h` — declare two attrs
- `libs/markoff-core/src/MarkoffDocument.cpp` — buffer canonicalisation + attr writes in `buildD2FromBytes`; `nextBlockQuoteRunId` field reset in `wipeD2State`; `serializeForSave` RunId-aware separator + non-BlockQuote line-wrap; untouched-bypass condition update
- `libs/markoff-core/src/Pimpl.h` (or wherever `MarkoffDocument::Private` is defined — confirmed at runtime) — add `quint32 nextBlockQuoteRunId = 1` field
- `libs/markoff-core/src/BlockSerializers.cpp` — `serializeBlockQuote` depth-aware
- `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp` — new buffer + round-trip slots
- `libs/markoff-styled/src/StyleApplier.cpp` — depth read at BlockQuote branch + non-BlockQuote depth overlay
- `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` — new render invariants
- `libs/markoff-core/CLAUDE.md` — banner update (close out the "BlockQuote retains internal `\n`s" caveat)
- `libs/markoff-styled/CLAUDE.md` — note depth read in v0.1 invariants
- `CLAUDE.md` (project-root) — strike the BlockQuote bullet under "Still open from this arc"
- `docs/queue.md` — strike #8.1, add closeout banner; struck Discipline-log entry if applicable

---

## Phase 1 — Parser surface

### Task 1: Add `blockQuoteDepth` + `blockQuoteRunId` fields to `TopLevelBlock`

**Files:**
- Modify: `libs/markoff-parser/include/markoff/parser/Document.h` (around `TopLevelBlock` definition, ~line 132–162)

- [ ] **Step 1: Add the fields**

Insert immediately after `looseRun` (currently around line 149) and before the inline-spans block:

```cpp
    /// For blocks whose AST ancestor chain includes one or more
    /// `block_quote` nodes: nesting level (1 = top-level quote, 2 =
    /// `> > ...`, etc.). `0` for blocks outside any quote.
    /// See `docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md`.
    int blockQuoteDepth = 0;

    /// For blocks emitted as a child of a `block_quote` node: a
    /// parser-assigned id (≥ 1) shared by all siblings of one parser
    /// `block_quote` node. `0` for blocks outside any quote.
    /// Distinguishes "one quote split into N paragraphs"
    /// (same runId) from "two adjacent quotes" (different runIds).
    int blockQuoteRunId = 0;
```

- [ ] **Step 2: Build to confirm no compile break**

Run:
```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target markoff_parser
```
Expected: PASS (defaulted POD additions).

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-parser/include/markoff/parser/Document.h
git commit -m "feat(parser): add blockQuoteDepth + blockQuoteRunId to TopLevelBlock

Defaulted to 0 (outside any quote). Walker will populate these in a
subsequent commit per docs/specs/2026-05-29-blockquote-multi-paragraph-
split-design.md §4."
```

---

### Task 2: Write failing parser test for single-paragraph quote

**Files:**
- Modify: `libs/markoff-parser/tests/tst_document_top_level_blocks.cpp`

- [ ] **Step 1: Add new slot declarations**

In the `private Q_SLOTS:` section (around line 13–33), add after `markerRunProducesMultiple();`:

```cpp
    void blockQuoteSingleParagraph_carriesDepth1AndRunId();
    void blockQuoteMultiParagraph_splitsIntoPerChildTlbs();
    void blockQuoteTwoAdjacentQuotes_distinctRunIds();
    void blockQuoteNested_bumpsDepthAndRunId();
    void blockQuoteHeadingChild_emitsAtxHeadingKind();
    void blockQuoteCodeChild_emitsFencedCodeBlockKind();
```

- [ ] **Step 2: Implement the first slot**

Add at the bottom of the file (before the `QTEST_APPLESS_MAIN` / `QTEST_MAIN` macro):

```cpp
void TestDocumentTopLevelBlocks::blockQuoteSingleParagraph_carriesDepth1AndRunId()
{
    auto blocks = blocksOf(QStringLiteral("> quoted line\n"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);          // inner child kind
    QCOMPARE(blocks[0].blockQuoteDepth, 1);
    QVERIFY(blocks[0].blockQuoteRunId >= 1);
}
```

- [ ] **Step 3: Configure + build + run, expect failure**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target tst_document_top_level_blocks
ctest --test-dir /home/clinton/dev/Markoff/build-dev -R 'tst_document_top_level_blocks' -V 2>&1 | tail -40
```
Expected: FAIL — `blocks[0].kind` is `BlockQuote` (legacy behaviour), not `Paragraph`; depth = 0.

- [ ] **Step 4: Commit the failing test**

```bash
git add libs/markoff-parser/tests/tst_document_top_level_blocks.cpp
git commit -m "test(parser): failing test for blockquote-child split (single para)

Pinning the new walker contract before implementation. Test will pass
after Task 3 lands."
```

---

### Task 3: Implement walker recursion into `block_quote` children

**Files:**
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp` — `collectTopLevelBlocks` and its caller

- [ ] **Step 1: Add `nextRunId` parameter + `block_quote` branch**

Modify the `collectTopLevelBlocks` signature (around line 1249–1252) to thread a runId counter + current depth/run id:

```cpp
static void collectTopLevelBlocks(TSNode node, const QByteArray &utf8,
                                  QList<TopLevelBlock> &out,
                                  int currentIndent = 0,
                                  bool currentLooseRun = false,
                                  int currentBlockQuoteDepth = 0,
                                  int currentBlockQuoteRunId = 0,
                                  quint32 *nextBlockQuoteRunId = nullptr)
```

Find every recursive call and add the three new args. For the existing `document`/`section` container branch (around line 1257-1263) and the `list` branch (line 1267-1275) and the nested-list-from-list_item branch (line 1287-1292), forward them unchanged.

Add a new branch **before** the "Block-level node — emit one TopLevelBlock" block at line 1304:

```cpp
    // BlockQuote: do NOT emit a TLB for the block_quote node itself.
    // Recurse into named children carrying depth+runId so each child
    // emits its own native-kind TLB tagged with the quote context.
    // See docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md §4.
    if (strcmp(type, "block_quote") == 0) {
        const int childDepth = currentBlockQuoteDepth + 1;
        // Take a fresh runId for this block_quote node's direct children;
        // nested block_quotes inside will take their own.
        Q_ASSERT(nextBlockQuoteRunId != nullptr);
        const int childRunId = static_cast<int>((*nextBlockQuoteRunId)++);
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_named_child(node, i);
            collectTopLevelBlocks(child, utf8, out, currentIndent,
                                  currentLooseRun, childDepth, childRunId,
                                  nextBlockQuoteRunId);
        }
        return;
    }
```

- [ ] **Step 2: Stamp the depth+runId on the emitted TLB**

At the "Block-level node — emit one TopLevelBlock" block (around line 1304-1328), just before `out.append(b);`, set:

```cpp
    b.blockQuoteDepth = currentBlockQuoteDepth;
    b.blockQuoteRunId = currentBlockQuoteRunId;
```

Also stamp in the `list_item` branch — find the `out.append(b);` near line 1284 and insert the same two lines just above it. (List items inside a quote inherit the quote context.)

- [ ] **Step 3: Initialise + thread the counter from the caller**

At the only caller in `TreeSitterParser::buildDocumentQueries` (search for `collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks);` near line 1396):

```cpp
    quint32 nextBlockQuoteRunId = 1;
    collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks,
                          /*currentIndent=*/0,
                          /*currentLooseRun=*/false,
                          /*currentBlockQuoteDepth=*/0,
                          /*currentBlockQuoteRunId=*/0,
                          &nextBlockQuoteRunId);
```

- [ ] **Step 4: Build + run the test from Task 2**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target tst_document_top_level_blocks
ctest --test-dir /home/clinton/dev/Markoff/build-dev -R 'tst_document_top_level_blocks' -V 2>&1 | tail -60
```
Expected: `blockQuoteSingleParagraph_carriesDepth1AndRunId` PASSES. **The existing `blockQuote` slot WILL FAIL** because it currently asserts `kind == Kind::BlockQuote`. That's expected; Task 4 rewrites it.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-parser/src/TreeSitterParser.cpp
git commit -m "feat(parser): recurse block_quote into per-child TLBs

The walker no longer emits a TLB for a block_quote node itself; it
recurses into named children, threading a fresh blockQuoteRunId per
node and bumping blockQuoteDepth. Existing blockQuote test is expected
to fail until Task 4 rewrites it to the new contract."
```

---

### Task 4: Rewrite the existing `blockQuote` slot + add the remaining new slots

**Files:**
- Modify: `libs/markoff-parser/tests/tst_document_top_level_blocks.cpp`

- [ ] **Step 1: Rewrite the existing `blockQuote()` slot**

Find the existing slot body (search for `void TestDocumentTopLevelBlocks::blockQuote()` — it likely asserts `kind == Kind::BlockQuote`). Replace its body with:

```cpp
void TestDocumentTopLevelBlocks::blockQuote()
{
    // Post-spec 2026-05-29-blockquote-multi-paragraph-split-design.md:
    // a single-line blockquote source emits its inner paragraph child
    // as its own TLB tagged with blockQuoteDepth=1.
    const QString src = QStringLiteral("> single quoted line\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);
    QCOMPARE(blocks[0].blockQuoteDepth, 1);
    QVERIFY(blocks[0].blockQuoteRunId >= 1);
}
```

- [ ] **Step 2: Implement the remaining new slots**

Append:

```cpp
void TestDocumentTopLevelBlocks::blockQuoteMultiParagraph_splitsIntoPerChildTlbs()
{
    // CommonMark: blank quoted line (`>` alone) separates paragraphs
    // inside a single block_quote node. Walker now emits one TLB per
    // inner paragraph; both share the same blockQuoteRunId.
    const QString src = QStringLiteral("> p1\n>\n> p2\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);
    QCOMPARE(blocks[1].kind, Kind::Paragraph);
    QCOMPARE(blocks[0].blockQuoteDepth, 1);
    QCOMPARE(blocks[1].blockQuoteDepth, 1);
    QCOMPARE(blocks[0].blockQuoteRunId, blocks[1].blockQuoteRunId);
}

void TestDocumentTopLevelBlocks::blockQuoteTwoAdjacentQuotes_distinctRunIds()
{
    // Truly blank line (no '>' prefix) between two quotes splits them
    // into separate block_quote nodes -> different runIds.
    const QString src = QStringLiteral("> p1\n\n> p2\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);
    QCOMPARE(blocks[1].kind, Kind::Paragraph);
    QCOMPARE(blocks[0].blockQuoteDepth, 1);
    QCOMPARE(blocks[1].blockQuoteDepth, 1);
    QVERIFY(blocks[0].blockQuoteRunId != blocks[1].blockQuoteRunId);
}

void TestDocumentTopLevelBlocks::blockQuoteNested_bumpsDepthAndRunId()
{
    // `> > deep` -> the outer block_quote contains a nested block_quote
    // which contains a paragraph. Outer block_quote emits no TLB; inner
    // block_quote emits no TLB; the inner paragraph emits one TLB at
    // depth=2 with a runId from the inner block_quote (distinct from
    // any outer counter seed).
    const QString src = QStringLiteral("> > deep\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);
    QCOMPARE(blocks[0].blockQuoteDepth, 2);
    QVERIFY(blocks[0].blockQuoteRunId >= 1);
}

void TestDocumentTopLevelBlocks::blockQuoteHeadingChild_emitsAtxHeadingKind()
{
    const QString src = QStringLiteral("> # Quoted H1\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::AtxHeading);
    QCOMPARE(blocks[0].headingLevel, 1);
    QCOMPARE(blocks[0].blockQuoteDepth, 1);
}

void TestDocumentTopLevelBlocks::blockQuoteCodeChild_emitsFencedCodeBlockKind()
{
    const QString src = QStringLiteral("> ```\n> code\n> ```\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::FencedCodeBlock);
    QCOMPARE(blocks[0].blockQuoteDepth, 1);
}
```

- [ ] **Step 3: Build + run; all six slots pass**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target tst_document_top_level_blocks
ctest --test-dir /home/clinton/dev/Markoff/build-dev -R 'tst_document_top_level_blocks' -V 2>&1 | tail -40
```
Expected: all slots PASS. If `blockQuoteCodeChild_emitsFencedCodeBlockKind` reveals a tree-sitter quirk (e.g. quoted fenced code is parsed as paragraph), DROP that slot from the file and log a Discipline-log entry; the styling/serialization paths still need to handle it via the existing fallback.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-parser/tests/tst_document_top_level_blocks.cpp
git commit -m "test(parser): pin blockquote split contract — 6 slots

Single-para, multi-para, two-adjacent (distinct runId), nested (depth=2),
heading-inside (AtxHeading kind), code-inside (FencedCodeBlock kind).
Replaces the prior blockQuote() slot which asserted the now-deprecated
Kind::BlockQuote emission for a top-level quote."
```

---

## Phase 2 — Markoff-core load path

### Task 5: Declare `BlockQuoteDepth` + `BlockQuoteRunId` in `AttrNames`

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/AttrNames.h`

- [ ] **Step 1: Add the entries**

Append before the closing `}` of the namespace (current contents end with `DisplayMode`):

```cpp
    inline const AttrName BlockQuoteDepth = "blockQuoteDepth"; // int ≥ 1
    inline const AttrName BlockQuoteRunId = "blockQuoteRunId"; // int ≥ 1, doc-local
```

- [ ] **Step 2: Build markoff-core**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target markoff_core
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-core/include/markoff/core/AttrNames.h
git commit -m "feat(core): declare BlockQuoteDepth + BlockQuoteRunId attrs

Consumers in the load path, serializer, and StyleApplier land in
subsequent commits per docs/specs/2026-05-29-blockquote-multi-
paragraph-split-design.md §3."
```

---

### Task 6: Add `nextBlockQuoteRunId` counter to `MarkoffDocument::Private` + wipe in `wipeD2State`

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` — the `Private` struct (search `struct MarkoffDocument::Private`) and `wipeD2State`

- [ ] **Step 1: Locate the Private struct**

Run:
```bash
grep -n "struct MarkoffDocument::Private\|struct .*::Private\b" /home/clinton/dev/Markoff/libs/markoff-core/src/MarkoffDocument.cpp | head -5
```
The struct is either inline in the .cpp or in a sibling header. If in a header (`Private.h` / similar), edit there.

- [ ] **Step 2: Add field next to other counters**

Add inside the struct (near other doc-local counters such as `editSequence`, `d2EditSequence`):

```cpp
    quint32 nextBlockQuoteRunId = 1;
```

- [ ] **Step 3: Wipe in `wipeD2State`**

Find `wipeD2State()` (was around line 1946 in pre-task reading). Add the reset near other plain-counter resets (next to `structuralEditSequence = 0`):

```cpp
    d->nextBlockQuoteRunId = 1;
```

- [ ] **Step 4: Build markoff-core**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target markoff_core
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp libs/markoff-core/src/*.h 2>/dev/null
git commit -m "feat(core): nextBlockQuoteRunId counter on MarkoffDocument

Reset by wipeD2State. Used by buildD2FromBytes to map parser-side
TopLevelBlock::blockQuoteRunId into doc-local stable ids (next commit)."
```

---

### Task 7: Failing buffer-invariant tests for blockquote shapes

**Files:**
- Modify: `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp`

- [ ] **Step 1: Add new slot declarations**

In the `private slots:` section (currently ends with `setext_untouched_roundtrip_is_byte_identical()`), append:

```cpp
    void blockquote_buffer_strips_marker_and_collapses_newlines();
    void blockquote_multi_paragraph_splits_into_two_blocks();
    void blockquote_two_separate_quotes_have_distinct_runids();
    void blockquote_nested_carries_depth_2();
    void blockquote_heading_inside_quote_uses_native_kind();
    void blockquote_round_trip_single_paragraph();
    void blockquote_round_trip_multi_paragraph();
    void blockquote_round_trip_two_adjacent_quotes();
    void blockquote_round_trip_nested();
    void blockquote_round_trip_heading_inside_quote();
```

- [ ] **Step 2: Add a small helper to read int attrs**

Add to the anonymous namespace at the top of the file (after the `Fixture` struct):

```cpp
int readIntAttr(MarkoffDocument &doc, BlockId id, const char *name)
{
    const auto attrs = doc.blockAttrs(id);
    auto it = attrs.constFind(name);
    if (it == attrs.cend()) return 0;
    return std::get<int>(it.value());
}
```

You may need to add `#include <markoff/core/AttrNames.h>` at the top.

- [ ] **Step 3: Implement the slot bodies**

Append at the end of the file (before `QTEST_MAIN`):

```cpp
void TstBlockBufferInvariant::blockquote_buffer_strips_marker_and_collapses_newlines()
{
    const QByteArray source = "> first line\n> second line\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::BlockQuote);
    QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("first line second line"));
    QCOMPARE(readIntAttr(doc, blocks[0], Markoff::AttrNames::BlockQuoteDepth), 1);
    QVERIFY(readIntAttr(doc, blocks[0], Markoff::AttrNames::BlockQuoteRunId) >= 1);
}

void TstBlockBufferInvariant::blockquote_multi_paragraph_splits_into_two_blocks()
{
    const QByteArray source = "> p1\n>\n> p2\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::BlockQuote);
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::BlockQuote);
    QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("p1"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArrayLiteral("p2"));
    const int run0 = readIntAttr(doc, blocks[0], Markoff::AttrNames::BlockQuoteRunId);
    const int run1 = readIntAttr(doc, blocks[1], Markoff::AttrNames::BlockQuoteRunId);
    QVERIFY(run0 >= 1);
    QCOMPARE(run0, run1);
}

void TstBlockBufferInvariant::blockquote_two_separate_quotes_have_distinct_runids()
{
    const QByteArray source = "> p1\n\n> p2\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::BlockQuote);
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::BlockQuote);
    const int run0 = readIntAttr(doc, blocks[0], Markoff::AttrNames::BlockQuoteRunId);
    const int run1 = readIntAttr(doc, blocks[1], Markoff::AttrNames::BlockQuoteRunId);
    QVERIFY(run0 >= 1 && run1 >= 1);
    QVERIFY(run0 != run1);
}

void TstBlockBufferInvariant::blockquote_nested_carries_depth_2()
{
    const QByteArray source = "> > deep\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::BlockQuote);
    QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("deep"));
    QCOMPARE(readIntAttr(doc, blocks[0], Markoff::AttrNames::BlockQuoteDepth), 2);
}

void TstBlockBufferInvariant::blockquote_heading_inside_quote_uses_native_kind()
{
    const QByteArray source = "> # H1\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    QCOMPARE(readIntAttr(doc, blocks[0], Markoff::AttrNames::BlockQuoteDepth), 1);
}

void TstBlockBufferInvariant::blockquote_round_trip_single_paragraph()
{
    const QByteArray source = "> hello\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);
    QCOMPARE(doc.serializeForSave(), source);
}

void TstBlockBufferInvariant::blockquote_round_trip_multi_paragraph()
{
    const QByteArray source = "> p1\n>\n> p2\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);
    QCOMPARE(doc.serializeForSave(), source);
}

void TstBlockBufferInvariant::blockquote_round_trip_two_adjacent_quotes()
{
    const QByteArray source = "> p1\n\n> p2\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);
    QCOMPARE(doc.serializeForSave(), source);
}

void TstBlockBufferInvariant::blockquote_round_trip_nested()
{
    const QByteArray source = "> > deep\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);
    QCOMPARE(doc.serializeForSave(), source);
}

void TstBlockBufferInvariant::blockquote_round_trip_heading_inside_quote()
{
    const QByteArray source = "> # H1\n";
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);
    QCOMPARE(doc.serializeForSave(), source);
}
```

- [ ] **Step 4: Build + run; all 10 new slots FAIL**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target tst_block_buffer_invariant
ctest --test-dir /home/clinton/dev/Markoff/build-dev -R 'tst_block_buffer_invariant' -V 2>&1 | tail -80
```
Expected: 10 new slots FAIL (load path + serializer don't yet exist). Pre-existing slots still pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp
git commit -m "test(core): failing tests for blockquote load+round-trip (10 slots)

Pins: marker-strip + \\n-collapse for single-line quote; multi-paragraph
split into 2 BlockQuote blocks with shared RunId; two adjacent quotes
get distinct RunIds; nested depth=2; heading-inside emits native Heading
kind; 5 round-trip-stability shapes. Will pass after Tasks 8 + 9 land."
```

---

### Task 8: Implement blockquote load path in `buildD2FromBytes`

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` — `buildD2FromBytes` (the body modified previously for Paragraph/Setext collapse)

- [ ] **Step 1: Add a runId remap map per parse call**

The parser emits `blockQuoteRunId` from its own counter; the load path needs to translate it into doc-local stable IDs sourced from `d->nextBlockQuoteRunId`. Since `buildD2FromBytes` runs once per `loadFromMarkdown`, use a local `QHash<int, int>` keyed by parser-side runId, value is doc-local runId.

At the top of the per-TLB loop body in `buildD2FromBytes` (around line 1750-ish — the function that iterates parsed blocks), add **before** the per-TLB code:

```cpp
    QHash<int, int> parserRunIdToDocRunId;
    auto docRunIdFor = [&](int parserRunId) -> int {
        if (parserRunId <= 0) return 0;
        auto it = parserRunIdToDocRunId.constFind(parserRunId);
        if (it != parserRunIdToDocRunId.cend()) return it.value();
        const int docId = static_cast<int>(d->nextBlockQuoteRunId++);
        parserRunIdToDocRunId.insert(parserRunId, docId);
        return docId;
    };
```

(Use whichever variable name the loop uses for the current TopLevelBlock; this snippet assumes it's `tb`. If not, swap accordingly.)

- [ ] **Step 2: Add a per-line marker-strip helper**

Above the per-TLB loop body, define a local lambda (or place in the file's anonymous namespace) that, given a UTF-8 byte slice and a target depth, peels up to `depth` repetitions of `> ` (allowing `>` alone for empty quoted lines, allowing up to 3 leading spaces per CommonMark):

```cpp
    auto stripBlockQuoteMarkers = [](QByteArray slice, int depth) -> QByteArray {
        QByteArray out;
        out.reserve(slice.size());
        int i = 0;
        const int n = slice.size();
        while (i < n) {
            // start of a line
            int lineStart = i;
            int lineEnd = slice.indexOf('\n', i);
            if (lineEnd < 0) lineEnd = n;
            // strip up to 3 leading spaces, then up to `depth` `> ` markers
            int p = lineStart;
            for (int k = 0; k < depth; ++k) {
                int sp = 0;
                while (sp < 3 && p + sp < lineEnd && slice[p + sp] == ' ') ++sp;
                if (p + sp < lineEnd && slice[p + sp] == '>') {
                    p += sp + 1;
                    // optional single space after '>'
                    if (p < lineEnd && slice[p] == ' ') ++p;
                } else {
                    break;
                }
            }
            out.append(slice.constData() + p, lineEnd - p);
            if (lineEnd < n) out.append('\n');
            i = (lineEnd < n) ? lineEnd + 1 : n;
        }
        return out;
    };
```

- [ ] **Step 3: Route kind + canonicalise buffer for quoted blocks**

In the kind-mapping block (where `kind` is currently derived via the existing `topLevelKindToBlockKind` switch around line 1809), wrap the assignment so that Paragraph-with-quote becomes `BlockKind::BlockQuote`:

```cpp
    BlockKind kind = topLevelKindToBlockKind(tb.kind);
    if (tb.blockQuoteDepth > 0 && tb.kind == TopLevelBlock::Kind::Paragraph) {
        kind = BlockKind::BlockQuote;
    }
```

In the buffer-canonicalisation block (around line 1890–1935, after `QByteArray content = ...; if (content.endsWith('\n')) content.chop(1);`), prepend a marker-strip pass when the block is quoted:

```cpp
    if (tb.blockQuoteDepth > 0) {
        content = stripBlockQuoteMarkers(content, tb.blockQuoteDepth);
        if (content.endsWith('\n')) content.chop(1);
    }
```

The existing setext/Paragraph `\n→space` collapse (just below this block) already handles Paragraph + ListItem + setext kinds — `BlockKind::BlockQuote` falls under the `BlockKind::Paragraph` case in the kind check. **Extend** the existing collapse condition to include `BlockKind::BlockQuote`:

```cpp
    if (kind == BlockKind::Paragraph
        || kind == BlockKind::ListItem
        || kind == BlockKind::BlockQuote
        || isSetext) {
        content.replace('\n', ' ');
    }
```

- [ ] **Step 4: Write attr writes**

After the existing attr writes for ListItem (around line 1872–1887), append (outside the ListItem `if`, applying to every quoted block):

```cpp
    if (tb.blockQuoteDepth > 0) {
        d->blockAttrsMap.setWithNextStamp(
            BlockAttrKey{newId, AttrNames::BlockQuoteDepth},
            AttrValue{tb.blockQuoteDepth});
        d->blockAttrsMap.setWithNextStamp(
            BlockAttrKey{newId, AttrNames::BlockQuoteRunId},
            AttrValue{docRunIdFor(tb.blockQuoteRunId)});
    }
```

- [ ] **Step 5: Build + run buffer-invariant tests**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target tst_block_buffer_invariant
ctest --test-dir /home/clinton/dev/Markoff/build-dev -R 'tst_block_buffer_invariant' -V 2>&1 | tail -80
```
Expected: the 5 **load-only** slots PASS (buffer + depth + runId assertions). The 5 **round-trip** slots still FAIL until Task 9 lands the serializer.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "feat(core): blockquote load — strip markers, collapse \\n, write attrs

buildD2FromBytes routes Paragraph-child-of-blockquote -> BlockKind::
BlockQuote, strips per-line '> ' markers, includes BlockQuote in the
Paragraph-class \\n->space collapse, and writes BlockQuoteDepth +
BlockQuoteRunId attrs (RunId remapped from parser-local to doc-local
via nextBlockQuoteRunId)."
```

---

## Phase 3 — Serializer

### Task 9: Implement depth-aware blockquote serialization + RunId-aware separator

**Files:**
- Modify: `libs/markoff-core/src/BlockSerializers.cpp` — `serializeBlockQuote`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` — `serializeForSave` (untouched-bypass gate, separator branch, line-wrap for non-BlockQuote inner kinds)

- [ ] **Step 1: Update `serializeBlockQuote`**

Replace the current implementation at `libs/markoff-core/src/BlockSerializers.cpp:123-129`:

```cpp
QByteArray serializeBlockQuote(BlockKind, const QHash<AttrName, AttrValue> &attrs,
                                const QByteArray &content)
{
    int depth = 1;
    auto it = attrs.constFind(AttrNames::BlockQuoteDepth);
    if (it != attrs.cend()) depth = std::max(1, std::get<int>(it.value()));
    QByteArray prefix;
    for (int i = 0; i < depth; ++i) prefix += "> ";
    // Buffer is content-only with no internal '\n' (B1 + Paragraph
    // collapse). Empty content -> "> " (well-formed marker-only line).
    return prefix + content;
}
```

Make sure the include for `AttrNames.h` exists at the top of the file.

- [ ] **Step 2: Helper + struct in `serializeForSave`**

In `MarkoffDocument.cpp`'s anonymous namespace (right next to `interBlockSeparator`), add:

```cpp
QByteArray blockQuotePrefix(int depth)
{
    QByteArray out;
    for (int i = 0; i < std::max(1, depth); ++i) out += "> ";
    return out;
}

QByteArray prefixEveryLine(const QByteArray &content, const QByteArray &prefix)
{
    QByteArray out;
    out.reserve(content.size() + prefix.size() * 4);
    int i = 0;
    const int n = content.size();
    bool atStart = true;
    while (i < n) {
        if (atStart) { out += prefix; atStart = false; }
        const char c = content[i++];
        out += c;
        if (c == '\n' && i < n) atStart = true;
    }
    if (out.isEmpty()) out += prefix;
    return out;
}
```

- [ ] **Step 3: Read quote context for each iteration of the block loop**

Inside `serializeForSave`'s block loop, replace the existing kind+attrs+untouched check with code that also computes the quote context for `i` and `i+1`. Define a small helper above the loop:

```cpp
    auto quoteContextFor = [&](BlockId id) -> std::pair<int, int> {
        const auto attrs = blockAttrs(id);
        int depth = 0, runId = 0;
        auto dIt = attrs.constFind(AttrNames::BlockQuoteDepth);
        if (dIt != attrs.cend()) depth = std::get<int>(dIt.value());
        auto rIt = attrs.constFind(AttrNames::BlockQuoteRunId);
        if (rIt != attrs.cend()) runId = std::get<int>(rIt.value());
        return {depth, runId};
    };
```

- [ ] **Step 4: Update untouched bypass + non-BlockQuote line-wrap**

In the existing block branch (not the ListItem early-continue branch), replace the lines that currently look like:

```cpp
    bool isSetextHeading = false;
    if (kind == BlockKind::Heading) { /* ... */ }
    if (!isBlockTouched(id) && !isSetextHeading) {
        bytes = d->blockLoadTimeBytes.value(id);
        if (bytes.endsWith('\n')) bytes.chop(1);
    } else {
        auto fn = reg.get(kind);
        bytes = fn(kind, blockAttrs(id), blockText(id));
    }
    out += bytes;
```

with the quote-aware version:

```cpp
    bool isSetextHeading = false;
    if (kind == BlockKind::Heading) {
        const auto attrs = blockAttrs(id);
        auto fmIt = attrs.constFind("headingForm");
        if (fmIt != attrs.cend()) {
            if (const QString *p = std::get_if<QString>(&fmIt.value()))
                isSetextHeading = (*p == QStringLiteral("setext"));
        }
    }
    const auto [depthI, runIdI] = quoteContextFor(id);
    const bool isBlockQuoted = (depthI > 0);
    if (!isBlockTouched(id) && !isSetextHeading && !isBlockQuoted) {
        bytes = d->blockLoadTimeBytes.value(id);
        if (bytes.endsWith('\n')) bytes.chop(1);
    } else {
        auto fn = reg.get(kind);
        bytes = fn(kind, blockAttrs(id), blockText(id));
        if (isBlockQuoted && kind != BlockKind::BlockQuote) {
            bytes = prefixEveryLine(bytes, blockQuotePrefix(depthI));
        }
    }
    out += bytes;
```

- [ ] **Step 5: Update separator branch**

Replace the existing `if (i + 1 < blocks.size()) out += interBlockSeparator();` (in the same non-ListItem branch, just below the lines above) with:

```cpp
    if (i + 1 < blocks.size()) {
        const auto [depthNext, runIdNext] = quoteContextFor(blocks[i + 1]);
        if (runIdI > 0 && runIdI == runIdNext) {
            // Same parser block_quote, two paragraphs of the run.
            out += "\n" + blockQuotePrefix(depthI);
            // Drop the trailing space from `> ` so the marker-only
            // blank-quoted-line shape matches CommonMark (`>\n` not `> \n`).
            if (out.endsWith(' ')) out.chop(1);
            out += "\n";
        } else {
            out += interBlockSeparator();
        }
    }
```

- [ ] **Step 6: Also update the ListItem branch's separator logic**

In the existing ListItem early-continue branch (above the generic loop body), the ListItem-vs-non-ListItem `nextKind == BlockKind::ListItem` separator block currently emits `"\n\n"` or `"\n"`. **No change needed in v0** for ListItem-inside-quote: the same-RunId case is rare (a quoted list with two list items can serialize with `\n` and round-trip correctly because the next-block context already flows through `interBlockSeparator()`). Document this as a v0.2 follow-up in a code comment:

```cpp
    // TODO(v0.2): ListItem-inside-quote inter-item separator does not
    // currently honour BlockQuoteRunId; the quoted-blank-line form
    // (`>\n` between items) is not synthesised. Two adjacent quoted
    // list items round-trip as `> - one\n> - two\n` which CommonMark
    // accepts. Spec §6 ListItem-in-quote bullet.
```

Place it just before the existing inter-block decision at line 2176-2182.

Then, in the ListItem branch, add the line-wrap pass to the **assembly line** itself: if `BlockQuoteDepth > 0`, wrap the `indentBytes + marker + " " + content` with `> ` × depth (no internal newlines, single-line, so a single prefix suffices). Replace:

```cpp
    out += indentBytes + marker + " " + content;
```

with:

```cpp
    const auto [liDepth, liRunId] = quoteContextFor(id);
    QByteArray line = indentBytes + marker + " " + content;
    if (liDepth > 0) line = blockQuotePrefix(liDepth) + line;
    out += line;
```

- [ ] **Step 7: Build markoff-core + run round-trip tests**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target tst_block_buffer_invariant
ctest --test-dir /home/clinton/dev/Markoff/build-dev -R 'tst_block_buffer_invariant' -V 2>&1 | tail -100
```
Expected: all 10 new blockquote slots PASS. Pre-existing slots still pass. If `blockquote_round_trip_heading_inside_quote` shows trailing-space drift (`> # H1` vs `>  # H1`), revisit `prefixEveryLine`'s prefix to use `"> "` (with trailing space) and the heading content's leading byte handling — the heading serializer produces `# H1` so `> # H1` is correct.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/src/BlockSerializers.cpp libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "feat(core): blockquote serialization — depth-aware + RunId separator

serializeBlockQuote reads BlockQuoteDepth and prepends '> ' x depth.
serializeForSave reads BlockQuoteRunId on both sides of each
inter-block boundary: same RunId emits a quoted-blank-line ('\n>\n')
between blocks; different/zero RunIds emit the ordinary '\\n\\n'.
Non-BlockQuote inner kinds (Heading/CodeBlock inside a quote) get a
line-wrap pass after their native serializer. Untouched-bypass gate
extended to always reconstruct quoted blocks."
```

---

## Phase 4 — `markoff-styled` wiring

### Task 10: Failing render invariants in `tst_styled_dogfood_invariants`

**Files:**
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`

- [ ] **Step 1: Inspect the existing file structure**

Run:
```bash
grep -n "void TstStyledDogfoodInvariants::\|private slots:" /home/clinton/dev/Markoff/libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp | head -20
```
Identify how the tests construct an `Editor` and inspect block-level QTextBlockFormat. Mirror that pattern.

- [ ] **Step 2: Add slot declarations + bodies**

In `private slots:` add:

```cpp
    void blockquote_depth_1_has_left_margin();
    void blockquote_depth_2_has_double_left_margin();
    void heading_inside_quote_renders_with_left_margin_overlay();
```

Implement bodies (place at the end before the moc include), copy-adapting the existing setup helpers:

```cpp
void TstStyledDogfoodInvariants::blockquote_depth_1_has_left_margin()
{
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown("> quoted line\n");
    Markoff::Styled::Editor editor;
    editor.setDocument(&doc);

    QTextDocument *qdoc = editor.findChild<QTextEdit *>()->document();
    QVERIFY(qdoc->blockCount() >= 1);
    const qreal margin1 = qdoc->firstBlock().blockFormat().leftMargin();
    QVERIFY2(margin1 > 0.0,
             qPrintable(QString("depth=1 leftMargin should be > 0, got %1").arg(margin1)));
}

void TstStyledDogfoodInvariants::blockquote_depth_2_has_double_left_margin()
{
    MarkoffDocument doc1(/*replicaId=*/1);
    doc1.loadFromMarkdown("> one\n");
    MarkoffDocument doc2(/*replicaId=*/1);
    doc2.loadFromMarkdown("> > two\n");

    Markoff::Styled::Editor e1;
    e1.setDocument(&doc1);
    Markoff::Styled::Editor e2;
    e2.setDocument(&doc2);

    const qreal m1 = e1.findChild<QTextEdit *>()->document()->firstBlock().blockFormat().leftMargin();
    const qreal m2 = e2.findChild<QTextEdit *>()->document()->firstBlock().blockFormat().leftMargin();
    // Margin scales linearly with depth; tolerate fontScale rounding.
    QVERIFY2(m2 > m1 * 1.5,
             qPrintable(QString("depth=2 (%1) should be ~2x depth=1 (%2)").arg(m2).arg(m1)));
}

void TstStyledDogfoodInvariants::heading_inside_quote_renders_with_left_margin_overlay()
{
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown("> # Quoted heading\n");
    Markoff::Styled::Editor editor;
    editor.setDocument(&doc);

    QTextDocument *qdoc = editor.findChild<QTextEdit *>()->document();
    QCOMPARE(qdoc->blockCount(), 1);
    const QTextBlockFormat bf = qdoc->firstBlock().blockFormat();
    // Heading-kind block tagged with BlockQuoteDepth=1 should have
    // a left-margin from the quote overlay (not the heading defaults).
    QVERIFY2(bf.leftMargin() > 0.0,
             qPrintable(QString("heading-in-quote leftMargin should be > 0, got %1")
                 .arg(bf.leftMargin())));
}
```

If the existing tests use a different setup harness (e.g. a `loadEditor(source)` helper), adapt the bodies to use it — the assertions are the contract, not the boilerplate.

- [ ] **Step 3: Build + run; all 3 slots FAIL**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target tst_styled_dogfood_invariants
ctest --test-dir /home/clinton/dev/Markoff/build-dev -R 'tst_styled_dogfood_invariants' -V 2>&1 | tail -60
```
Expected: 3 new slots FAIL. The depth-1 case may pass coincidentally because `applyBlockquote` already takes a depth parameter that currently gets a `qMax(1, ...)` from buffer prefix counting — but the buffer no longer starts with `>` after Task 8, so `depth` collapses to 1 from the `qMax`. That's the same value but for the wrong reason. The depth-2 case will definitely fail because the buffer no longer carries `> >`. Treat all 3 as "must be re-implemented to read from attrs."

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp
git commit -m "test(styled): failing render invariants for blockquote depth + overlay

depth=1 left-margin > 0; depth=2 left-margin ~ 2x depth=1; heading-
inside-quote has the quote overlay applied. All 3 will pass after
StyleApplier reads BlockQuoteDepth from attrs (next commit)."
```

---

### Task 11: Implement depth-from-attrs + non-BlockQuote overlay in `StyleApplier`

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp`

- [ ] **Step 1: Read depth from attrs at the BlockQuote branch**

Around line 562–569 (the `kind == BlockKind::BlockQuote` branch in the walk), replace:

```cpp
} else if (kind == Markoff::BlockKind::BlockQuote) {
    int depth = 1;
    if (!text.isEmpty()) {
        depth = 0;
        for (int bi = 0; bi < text.size() && text[bi] == '>'; ++bi) ++depth;
        depth = qMax(1, depth);
    }
    applyBlockquote(blkCursor, depth, m_fontScale);
}
```

with:

```cpp
} else if (kind == Markoff::BlockKind::BlockQuote) {
    int depth = 1;
    if (auto it = attrs.find(Markoff::AttrNames::BlockQuoteDepth);
        it != attrs.end() && std::holds_alternative<int>(*it))
        depth = qMax(1, std::get<int>(*it));
    applyBlockquote(blkCursor, depth, m_fontScale);
}
```

`attrs` is the existing lookup at the top of the per-block branch (the same one consumed by computeBlockHash; confirmed at lines 574–589 for ListItem). If it isn't in scope here, hoist the `blockAttrs(id)` call to cover both branches.

- [ ] **Step 2: Add the non-BlockQuote overlay**

Immediately after the per-kind dispatch (after `applyParagraph(...)` fallback in the `else` branch at line 594, but **before** the next `qblk = qblk.next();` at line 596), insert:

```cpp
    if (kind != Markoff::BlockKind::BlockQuote) {
        int overlayDepth = 0;
        if (auto it = attrs.find(Markoff::AttrNames::BlockQuoteDepth);
            it != attrs.end() && std::holds_alternative<int>(*it))
            overlayDepth = std::get<int>(*it);
        if (overlayDepth > 0) {
            QTextBlockFormat bf = blkCursor.blockFormat();
            // Match applyBlockquote's emPt(fontScale) * depth margin shape.
            bf.setLeftMargin(bf.leftMargin() + emPt(m_fontScale) * overlayDepth);
            blkCursor.setBlockFormat(bf);
        }
    }
```

`emPt` is defined at the top of the file (used by every `apply*` function). If not in scope inside this anonymous-namespace branch, hoist accordingly.

- [ ] **Step 3: Build + run**

```bash
cmake --build /home/clinton/dev/Markoff/build-dev -j 8 --target tst_styled_dogfood_invariants
ctest --test-dir /home/clinton/dev/Markoff/build-dev -R 'tst_styled_dogfood_invariants' -V 2>&1 | tail -60
```
Expected: all 3 new slots PASS plus the existing slots. If `blockquote_depth_2_has_double_left_margin` is borderline (m2 not > m1 * 1.5), inspect `applyBlockquote`'s margin formula (`bf.setLeftMargin(emPt(fontScale) * qMax(1, depth));`) — it already scales linearly with depth.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-styled/src/StyleApplier.cpp
git commit -m "feat(styled): blockquote depth from attrs + overlay for inner kinds

applyBlockquote branch reads BlockQuoteDepth from attrs (was peeling
'>' from buffer text — buffer now has no marker after the load-side
canonicalisation). Non-BlockQuote inner kinds (Heading/CodeBlock/
ListItem inside a quote) get a left-margin overlay of emPt(fontScale)
x depth on top of their native block format."
```

---

## Phase 5 — Suite check + docs

### Task 12: Full-suite regression sweep

- [ ] **Step 1: Run the fast suite baseline**

```bash
cd /home/clinton/dev/Markoff && scripts/run-tests.sh -E 'tst_realistic|tst_benchmark' 2>&1 | tail -30
```

- [ ] **Step 2: Compare to baseline (249/254 with 5 known failures)**

Known pre-existing failures from CLAUDE.md banner:
- `tst_styled_block_formats::heading_levels_descend_in_size`
- `tst_styled_block_formats::horizontal_rule_uses_monospace`
- 4 failures in `tst_source_widget_format_ops` (from the WP-unification arc — queue #8.6)

Expected: same 5 failures, no new ones. Pass count should be ≥ 249 + (new tests added in this plan: 6 parser + 10 buffer + 3 styled = 19). Target: **268/273**.

If new failures appear, investigate before proceeding. Most likely suspects: `tst_live_render_qml_integration` (live view's BlockQuote handling), `tst_markoff_doc_apply_structured_paste` (paste across quote boundaries).

- [ ] **Step 3: If live-view tests regress, investigate**

If `tst_live_render_*` has new failures, the most likely cause is `LiveListModelBinding`'s prefix-rule inference at line 549 (`else if (inferred == BlockKind::Blockquote) fk = Markoff::BlockKind::BlockQuote;`) treating the model's now-Paragraph-kind-with-attrs blocks differently. Check first whether the failures are about typing-time promotion (should still work — live load doesn't go through our new parser path for an already-loaded doc) or load-time identity.

If a regression is real and small, fix it; if it's structurally complex, defer with a Discipline-log entry and re-verify the styled-leaf tests are the only required win for closeout.

- [ ] **Step 4: If clean, no commit required**

If a small live-side fix lands, commit it separately:

```bash
git add libs/markoff-live/src/<file>
git commit -m "fix(live): <one-line root cause>

Fallout from blockquote multi-paragraph split. <one-line resolution>."
```

---

### Task 13: Documentation + closeout

**Files:**
- Modify: `libs/markoff-core/CLAUDE.md` — the "Load ingress canonicalisation (2026-05-29)" paragraph
- Modify: `libs/markoff-styled/CLAUDE.md` — append a v0.1 invariants bullet
- Modify: `CLAUDE.md` (project root) — strike the BlockQuote bullet under "Still open from this arc"
- Modify: `docs/queue.md` — strike #8.1, update banner

- [ ] **Step 1: Update `libs/markoff-core/CLAUDE.md`**

Find the paragraph that currently reads:

> `BlockQuote` still retains its internal `\n`s pending marker-aware handling of per-line `> ` markers; flat-view leaves will still see spurious QTextBlock boundaries inside BlockQuotes.

Replace with:

> `BlockQuote` is split at load: each parser `block_quote` node's children are emitted as per-child top-level blocks tagged with `BlockQuoteDepth` + `BlockQuoteRunId` attrs. Paragraph children land as `BlockKind::BlockQuote` (keeps live's matcher); non-paragraph children (heading/code/list) land as their native kind plus the same attrs. Buffer is `> `-stripped + `\n→space` collapsed. Serializer reconstructs depth × `> ` prefixes and uses RunId to emit `\n>\n` between same-run paragraphs. Spec `docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md`.

- [ ] **Step 2: Update `libs/markoff-styled/CLAUDE.md`**

Append after the "Hash gate covers attrs (2026-05-29)" bullet under "v0.1 invariants":

> - **BlockQuote depth from attrs (2026-05-29).** `StyleApplier::applyBlockquote` reads `BlockQuoteDepth` from attrs and scales left-margin linearly. Non-BlockQuote inner kinds (Heading/CodeBlock/ListItem with `BlockQuoteDepth > 0`) get a left-margin overlay of `emPt(fontScale) × depth` on top of their native block format. Spec `docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md`.

- [ ] **Step 3: Update project-root `CLAUDE.md`**

Find the bullet that begins "`BlockQuote` retains internal `\n`s — its byte range includes per-line `> ` markers that need marker-aware stripping." Replace its body with a closeout note (do not delete the bullet — keep the format, struck-through):

> - ~~**`BlockQuote` retains internal `\n`s** — its byte range includes per-line `> ` markers that need marker-aware stripping.~~ → closed 2026-05-29 by `docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md` (parser-driven per-child split + depth/runId attrs).

- [ ] **Step 4: Update `docs/queue.md`**

Edit #8.1 (lines around 820–831 — the `**BlockQuote internal \n collapse.**` bullet). Replace its body with the struck-out form:

> 1. ~~**BlockQuote internal `\n` collapse.**~~ → closed 2026-05-29 in `<COMMIT>`. Parser-driven per-child split lands `BlockQuoteDepth` + `BlockQuoteRunId` attrs on every quoted block; load strips `> ` markers + collapses `\n→space`; serializer reconstructs depth × `> ` and uses RunId to choose `\n>\n` (same run) vs `\n\n` (separate quotes); StyleApplier reads depth from attrs and overlays left-margin on non-BlockQuote inner kinds. Spec `docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md`. Plan `docs/plans/2026-05-29-blockquote-multi-paragraph-split.md`. Tests: 6 parser slots in `tst_document_top_level_blocks`, 10 buffer+round-trip slots in `tst_block_buffer_invariant`, 3 render-invariant slots in `tst_styled_dogfood_invariants`.

(Use the actual commit hash of Task 9's commit in `<COMMIT>` — you'll know it after Step 5.)

- [ ] **Step 5: Stage docs, commit**

```bash
git add CLAUDE.md libs/markoff-core/CLAUDE.md libs/markoff-styled/CLAUDE.md docs/queue.md
git commit -m "docs: blockquote multi-paragraph split closeout + queue #8.1

CLAUDE.md (root) — strike the BlockQuote 'still open' bullet.
markoff-core CLAUDE.md — rewrite the BlockQuote caveat paragraph
in the 'Load ingress canonicalisation' section.
markoff-styled CLAUDE.md — append the v0.1 invariant for BlockQuote
depth read from attrs + non-BlockQuote overlay.
queue.md #8.1 — closeout banner with commit + spec/plan references."
```

- [ ] **Step 6: Fast-suite final sanity**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark' 2>&1 | tail -10
```
Expected: same passing count as Task 12 (268/273 target).

---

## Self-Review notes

- **Spec §3 (Model schema)** — Task 5 declares both attrs; Tasks 8 + 9 write/read them. ✓
- **Spec §4 (Parser API change)** — Tasks 1 + 3 add fields + walker. ✓
- **Spec §5 (Load path)** — Task 8 marker-strip + collapse + attr writes + kind routing. ✓
- **Spec §6 (Serialization)** — Task 9 depth-aware serializer + RunId separator + non-BlockQuote line-wrap + untouched bypass. ✓
- **Spec §7 (`markoff-styled` wiring)** — Task 11 attr read + overlay. ✓
- **Spec §8 (`markoff-live` impact)** — no code change required; verified by Task 12's regression sweep.
- **Spec §9 (Tests)** — Tasks 2 + 4 (parser, 6 slots), Task 7 (core, 10 slots), Task 10 (styled, 3 slots). ✓
- **Spec §10 (Definition of done)** — Task 12 (suite check), Task 13 (CLAUDE.md banner + queue closeout + Discipline-log strike via §10's "discipline-log entries scanned"). The struck Discipline-log entry the spec references is the *banner text* in project-root CLAUDE.md, which is closed in Task 13 Step 3. The actual `docs/queue.md` Discipline Log entries don't currently include a BlockQuote-marker line (verified during context exploration); none to strike.
- **Spec §12 (Risks)** — Task 12 explicitly checks the `blockquoteDepth` post-process and live regressions; Task 9 documents the v0.2 follow-up for ListItem-in-quote separator.
