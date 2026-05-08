# R1B — Parser: per-block inline span pre-bake

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `QList<SourceSpan> inlineSpans` to `Markoff::TopLevelBlock`, populated at parse time with the spans intersecting the block's byte range, with offsets translated to *block-relative* coordinates. This is the API surface that lets `InlineFormatHighlighter` (R6) consume pre-baked span data instead of constructing a fresh `TreeSitterParser` per delegate per keystroke — the single biggest perf-bug contributor identified in the audit and repair plan.

**Architecture:** In `TreeSitterParser::buildDocumentQueries` (the existing per-parse top-level walk), after `collectTopLevelBlocks` populates the byte-range-defined block list, run a single linear bucketing pass: for each `SourceSpan` from `buildSpanMap()`, find its containing `TopLevelBlock`, translate the span's offsets to block-relative, and append. Spans that don't fall within any top-level block are ignored (they belong to inter-block separators). Cost: O(spans + blocks) — the bake is a sweep over already-computed data, not a re-parse.

**Tech stack:** C++20, Qt6.8, CMake 3.19+. Tree-sitter via the existing parser binding.

**Reference spec:** `docs/specs/2026-05-02-live-render-restoration-design.md` §11 R1 (the spec phase R1 lists this as a foundation surface). Companion: `docs/2026-05-02-live-view-architectural-audit.md` §"Performance is downstream of the architecture" identifies the per-delegate-parser instantiation as the cost centre this plan eliminates.

**Out of scope for R1B:**
- The `InlineFormatHighlighter` refactor to *consume* the new field. That's R6.
- Lifecycle handling (when does `inlineSpans` invalidate?). The lifecycle is "same as the rest of `TopLevelBlock`": the `Markoff::Document` snapshot is value-typed and immutable; consumers receive a fresh snapshot per parse.
- View-side changes of any kind.

**Independence from R1A:** This plan is fully independent of R1A. The two foundation deltas don't share files (mostly). They can land in either order.

---

## Coordinate-space contract (read carefully)

`SourceSpan` as returned by `buildSpanMap()` uses **document-absolute** offsets (both `utf8Offset` and `charOffset` are positions in the whole post-frontmatter source).

`TopLevelBlock::inlineSpans`, after this plan, holds **block-relative** offsets:

- `span.utf8Offset` is in `[0, block.byteEnd - block.byteStart)`.
- `span.charOffset` is in `[0, block.charLength_in_qstring)`.
- `span.parentCharStart` / `span.parentCharEnd`, when non-`-1`, are also block-relative.

This contract matches what `InlineFormatHighlighter::setSource(blockText)` currently produces — the existing consumer protocol is preserved; only the *home* of the bake moves from per-delegate to the parser.

---

## File map

**Modified:**
- `libs/markoff-parser/include/markoff-parser/Document.h` — add `QList<SourceSpan> inlineSpans` to the `TopLevelBlock` struct (around line 76-128).
- `libs/markoff-parser/src/TreeSitterParser.cpp` — add the bake logic at the end of `buildDocumentQueries` (both no-arg and `(prior, edits)` overloads).

**New (test):**
- `libs/markoff-parser/tests/tst_parser_inline_span_bake.cpp` — exercises the bake with several block kinds and inline-formatted contents.
- `libs/markoff-parser/tests/CMakeLists.txt` — register the new test executable.

---

## Tasks

### Task 1: Read context

- [ ] **Step 1: Read the relevant code**

```
libs/markoff-parser/include/markoff-parser/Document.h          (struct TopLevelBlock around line 76)
libs/markoff-parser/include/markoff-parser/SourceSpan.h        (full file)
libs/markoff-parser/include/markoff-parser/TreeSitterParser.h  (focus on buildSpanMap & buildDocumentQueries)
libs/markoff-parser/src/TreeSitterParser.cpp                   (buildSpanMap ~742, buildDocumentQueries ~1287, collectTopLevelBlocks ~1235)
```

Confirm that:
- `buildSpanMap()` returns spans with `utf8Offset` / `charOffset` in document-absolute coordinates.
- `collectTopLevelBlocks()` populates `byteStart`/`byteEnd` in document-absolute UTF-8 byte coordinates.
- `buildUtf8ToCharMap(const QByteArray &utf8)` is declared in `SourceSpan.h:64`. We'll use it to convert `byteStart`/`byteEnd` into char-space block bounds.

No code changes in this task.

---

### Task 2: Add the field to `TopLevelBlock`

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/Document.h`

- [ ] **Step 1: Add the field**

In `Document.h`, find the `TopLevelBlock` struct declaration (around line 76). Add the new field after the existing `bool hasInlineContent = false;` line (around line 127):

```cpp
    /// True if the block's source has any nested children that the
    /// consumer might care about (e.g. inline code, formatting).
    /// v1 leaves this false; consumers re-walk via buildSpanMap()
    /// for fine-grained inline info.
    bool hasInlineContent = false;

    /// Inline structural spans (bold, italic, code, link, etc.) within
    /// this block's source range. Offsets are *block-relative*:
    ///   - `utf8Offset` is in [0, byteEnd - byteStart)
    ///   - `charOffset` is in [0, block-char-length)
    ///   - `parentCharStart` / `parentCharEnd`, when non-`-1`, are
    ///     also block-relative.
    /// Empty for blocks whose kind has no inline content (FencedCodeBlock,
    /// ThematicBreak, etc.) — code-block content uses `codeText`/`codeLanguage`,
    /// hr has no content. Populated by `TreeSitterParser::buildDocumentQueries`.
    /// Consumers (e.g. InlineFormatHighlighter) use these directly without
    /// re-parsing — see restoration spec §11 R1B.
    QList<SourceSpan> inlineSpans;
```

You'll need to ensure `<markoff-parser/SourceSpan.h>` is included from `Document.h`. Check the existing includes; if `SourceSpan.h` isn't included, add it near the top:

```cpp
#include <markoff-parser/SourceSpan.h>
```

- [ ] **Step 2: Build to verify the field is accepted**

```bash
cmake --build build-dev --target markoff_parser -j 8
```

Expected: clean build. If there's a circular-include problem (SourceSpan.h includes something Document.h doesn't have), resolve by adding the missing include.

---

### Task 3: Add the failing test

**Files:**
- Create: `libs/markoff-parser/tests/tst_parser_inline_span_bake.cpp`
- Modify: `libs/markoff-parser/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

Create `libs/markoff-parser/tests/tst_parser_inline_span_bake.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-parser/Document.h>
#include <markoff-parser/SourceSpan.h>

using namespace Markoff;

class TstParserInlineSpanBake : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void empty_doc_has_no_blocks() {
        auto doc = Document::fromMarkdown("");
        QVERIFY(doc != nullptr);
        QVERIFY(doc->topLevelBlocks().isEmpty());
    }

    void single_paragraph_no_formatting_has_empty_or_minimal_spans() {
        auto doc = Document::fromMarkdown("hello world");
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].kind, TopLevelBlock::Kind::Paragraph);
        // Spans may be empty (no formatting) or contain non-formatting
        // structural markers; we only assert offset bounds for any present.
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            QVERIFY(s.utf8Offset >= 0);
            QVERIFY(s.utf8Offset + s.utf8Length <= blocks[0].byteEnd - blocks[0].byteStart);
            QVERIFY(s.charOffset >= 0);
        }
    }

    void paragraph_with_bold_has_block_relative_bold_span() {
        auto doc = Document::fromMarkdown("**bold** trailing");
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 1);

        bool foundBold = false;
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            if (s.bold && !s.isDelimiter) {
                foundBold = true;
                // The bold content "bold" sits at chars [2, 6) of the block
                // ("**" delimiters + "bold" content). Block-relative offsets:
                QCOMPARE(s.charOffset, 2);
                QCOMPARE(s.charLength, 4);
                QVERIFY(s.utf8Offset >= 0);
            }
        }
        QVERIFY2(foundBold, "expected a bold non-delimiter span in the paragraph");
    }

    void multiple_paragraphs_each_have_their_own_spans() {
        auto doc = Document::fromMarkdown(
            "first **paragraph** here\n\nsecond *one* there");
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 2);

        // First paragraph contains a bold span; offsets relative to first block.
        bool firstHasBold = false;
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            if (s.bold && !s.isDelimiter) {
                firstHasBold = true;
                // "paragraph" sits at chars [8, 17) of "first **paragraph** here":
                //   "first " = 6, "**" = 2 → content starts at 8, length 9.
                QCOMPARE(s.charOffset, 8);
                QCOMPARE(s.charLength, 9);
            }
        }
        QVERIFY(firstHasBold);

        // Second paragraph contains an italic span; offsets relative to
        // *second* block. If the bake leaked offsets, this would be off
        // by sizeof(first paragraph) + 2 (the "\n\n").
        bool secondHasItalic = false;
        for (const SourceSpan &s : blocks[1].inlineSpans) {
            if (s.italic && !s.isDelimiter) {
                secondHasItalic = true;
                // "one" sits at chars [8, 11) of "second *one* there":
                //   "second " = 7, "*" = 1 → content starts at 8, length 3.
                QCOMPARE(s.charOffset, 8);
                QCOMPARE(s.charLength, 3);
            }
        }
        QVERIFY(secondHasItalic);
    }

    void heading_inline_spans_block_relative() {
        auto doc = Document::fromMarkdown("# title with **bold**\n");
        const auto blocks = doc->topLevelBlocks();
        QVERIFY(blocks.size() >= 1);
        QCOMPARE(blocks[0].kind, TopLevelBlock::Kind::AtxHeading);

        // The "# " prefix is part of the block's source, so the bold content
        // "bold" should sit at chars [15, 19) of "# title with **bold**":
        //   "# title with " = 13, "**" = 2 → content starts at 15, length 4.
        bool foundBold = false;
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            if (s.bold && !s.isDelimiter) {
                foundBold = true;
                QCOMPARE(s.charOffset, 15);
                QCOMPARE(s.charLength, 4);
            }
        }
        QVERIFY(foundBold);
    }

    void code_block_has_no_inline_spans_for_format() {
        // Fenced code block content shouldn't be inline-formatted.
        auto doc = Document::fromMarkdown("```\n**not bold**\n```\n");
        const auto blocks = doc->topLevelBlocks();
        QVERIFY(blocks.size() >= 1);
        QCOMPARE(blocks[0].kind, TopLevelBlock::Kind::FencedCodeBlock);

        // No bold-formatted non-delimiter span should appear; if any spans
        // are present, none should claim bold inline format on the
        // **not bold** content.
        for (const SourceSpan &s : blocks[0].inlineSpans) {
            QVERIFY(!(s.bold && !s.isDelimiter));
        }
    }
};

QTEST_MAIN(TstParserInlineSpanBake)
#include "tst_parser_inline_span_bake.moc"
```

- [ ] **Step 2: Register the test**

Open `libs/markoff-parser/tests/CMakeLists.txt`. Find the block where existing parser tests are registered (look for `add_executable` or a project-specific helper macro like `markoff_parser_test(...)`). Follow the pattern of one of the simpler existing tests to add:

```cmake
markoff_parser_test(tst_parser_inline_span_bake
    SOURCES tst_parser_inline_span_bake.cpp
)
```

(Or whatever the project's macro/per-test-boilerplate looks like — copy verbatim from a similar existing entry, replacing only the source filename.)

- [ ] **Step 3: Build and run the test, verify it fails**

```bash
cmake --build build-dev --target tst_parser_inline_span_bake -j 8
./build-dev/libs/markoff-parser/tests/tst_parser_inline_span_bake
```

Expected: `paragraph_with_bold_has_block_relative_bold_span` fails because `blocks[0].inlineSpans` is empty (the field exists per Task 2 but is not yet populated). The other tests may pass or fail; the bold-content cases definitely fail.

- [ ] **Step 4: Commit the failing test**

```bash
git add libs/markoff-parser/tests/tst_parser_inline_span_bake.cpp \
        libs/markoff-parser/tests/CMakeLists.txt \
        libs/markoff-parser/include/markoff-parser/Document.h
git commit -m "test(parser): TopLevelBlock::inlineSpans bake (failing)"
```

---

### Task 4: Implement the bake

**Files:**
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp`

The bake runs at the end of `buildDocumentQueries` — for both the no-arg (full-walk) and `(prior, edits)` (incremental) overloads. The two overloads share the post-walk slicing logic; we'll factor it into a file-local helper.

- [ ] **Step 1: Add a file-local helper**

In `TreeSitterParser.cpp`, near the existing file-local helpers (e.g. just above the `buildDocumentQueries` definition around line 1287), add:

```cpp
namespace {

/// Bucket spans into top-level blocks by byte-range containment, translating
/// each span's offsets from document-absolute to block-relative.
///
/// Pre: `spans` is in document-absolute coordinates (output of `buildSpanMap()`).
/// Pre: `blocks[i].byteStart`/`byteEnd` are document-absolute UTF-8 byte ranges
///      with `byteStart < byteEnd` and blocks ordered ascending.
/// Post: each `block.inlineSpans` contains spans whose `utf8Offset` is in
///       [block.byteStart, block.byteEnd), with offsets translated to be
///       block-relative.
void bakeInlineSpansIntoBlocks(QList<SourceSpan> spans,
                               QList<TopLevelBlock> &blocks,
                               const QByteArray &utf8)
{
    if (blocks.isEmpty() || spans.isEmpty()) return;

    // Build the byte→char map once for the whole source. Index = utf8 byte
    // offset; value = QString char offset (UTF-16 code units).
    const QList<int> u8ToChar = buildUtf8ToCharMap(utf8);

    // Sort spans by utf8Offset just in case (most produce sorted output, but
    // we don't want to depend on it). Small n; std::sort is fine.
    std::sort(spans.begin(), spans.end(),
              [](const SourceSpan &a, const SourceSpan &b) {
                  return a.utf8Offset < b.utf8Offset;
              });

    auto charOffsetAtByte = [&](int byte) -> int {
        if (byte < 0) return 0;
        if (byte >= u8ToChar.size()) return u8ToChar.isEmpty() ? 0 : u8ToChar.last();
        return u8ToChar[byte];
    };

    // Two-pointer walk through blocks and spans.
    qsizetype spanIdx = 0;
    for (TopLevelBlock &block : blocks) {
        const int blockCharStart = charOffsetAtByte(block.byteStart);

        // Advance past spans entirely before this block.
        while (spanIdx < spans.size()
                && spans[spanIdx].utf8Offset < block.byteStart) {
            ++spanIdx;
        }

        // Collect spans whose start is within [block.byteStart, block.byteEnd).
        // We do not split spans that straddle a block boundary; in practice
        // tree-sitter inline regions are bounded by block boundaries, so this
        // is a non-issue. If it ever produces a stray span, that's a parser
        // bug worth surfacing rather than silently truncating.
        for (qsizetype i = spanIdx; i < spans.size(); ++i) {
            const SourceSpan &s = spans[i];
            if (s.utf8Offset >= block.byteEnd) break;

            SourceSpan rel = s;
            rel.utf8Offset = s.utf8Offset - block.byteStart;
            rel.charOffset = s.charOffset - blockCharStart;
            if (s.parentCharStart >= 0) rel.parentCharStart = s.parentCharStart - blockCharStart;
            if (s.parentCharEnd   >= 0) rel.parentCharEnd   = s.parentCharEnd   - blockCharStart;
            block.inlineSpans.append(rel);
        }
    }
}

}  // namespace
```

This helper is ~40 lines and is the entire bake. It assumes `buildUtf8ToCharMap` and `SourceSpan` and `TopLevelBlock` are all visible — they are, via the parser's existing includes.

- [ ] **Step 2: Call the helper from `buildDocumentQueries()` (no-arg overload)**

Find the no-arg `TreeSitterParser::buildDocumentQueries` (around line 1287). It currently ends with something like:

```cpp
DocumentQueryResult TreeSitterParser::buildDocumentQueries() const
{
    DocumentQueryResult result;
    // ... existing population of result.headings, result.tags, result.links, etc.
    collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks);
    return result;
}
```

Add the bake call just before `return result;`:

```cpp
DocumentQueryResult TreeSitterParser::buildDocumentQueries() const
{
    DocumentQueryResult result;
    // ... existing population (unchanged) ...
    collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks);

    // Bake per-block inline spans (R1B). buildSpanMap is already O(N) over
    // the inline trees; bucketing is O(spans + blocks) on top.
    bakeInlineSpansIntoBlocks(buildSpanMap(), result.topLevelBlocks, m_utf8);

    return result;
}
```

- [ ] **Step 3: Call the helper from the `(prior, edits)` overload**

Find the second `TreeSitterParser::buildDocumentQueries` overload (around line 1310). It also ends with `collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks);` near line 1401. Add the same bake call before `return result;`:

```cpp
    collectTopLevelBlocks(blockRoot, m_utf8, result.topLevelBlocks);
    bakeInlineSpansIntoBlocks(buildSpanMap(), result.topLevelBlocks, m_utf8);
    return result;
```

- [ ] **Step 4: Build the parser**

```bash
cmake --build build-dev --target markoff_parser -j 8
```

Expected: clean build.

---

### Task 5: Run the test, verify pass

- [ ] **Step 1: Run the new test**

```bash
ctest --test-dir build-dev -R '^tst_parser_inline_span_bake$' --output-on-failure
```

Expected: pass — six test cases green.

If a test fails, the most likely causes:

- **Off-by-one in char offsets.** Verify `buildUtf8ToCharMap` returns offsets where index = byte position 0..N and value = char-position-up-to-and-including-that-byte. If it's "char position before this byte" vs "char position at this byte", the constants in the test cases may be off by 1.
- **Spans not sorted.** The two-pointer walk assumes ascending `utf8Offset`. The helper sorts; if you removed the sort, restore it.
- **Frontmatter offset shift.** If `Document::fromMarkdown` strips frontmatter before tree-sitter sees the source, `byteStart` is in *post-frontmatter* coordinates. For the test inputs (no frontmatter), this doesn't apply, but be aware for future test extensions.

If a span is unexpectedly missing (e.g. the test expects a bold span that isn't there), inspect the raw output of `buildSpanMap()` for that input — the parser may classify the span differently than expected (e.g. tree-sitter's `emphasis` vs `strong_emphasis` semantics). Adjust the test if and only if the actual parser output is correct and the test expectation was wrong.

- [ ] **Step 2: Run the parser regression suite**

```bash
ctest --test-dir build-dev -R '^tst_parser_' --output-on-failure -j 8
```

Expected: all green. Adding a new field to `TopLevelBlock` and populating it does not change any existing behaviour; existing tests pass.

- [ ] **Step 3: Run the full fast-tier suite**

```bash
ctest --test-dir build-dev -E "tst_realistic|tst_benchmark|tst_view_qml_live_view_qml" --output-on-failure -j 8
```

Expected: all green. New test count = previous baseline + 1 (the R1B test) + 1 if R1A's test landed.

---

### Task 6: Commit

- [ ] **Step 1: Review the diff**

```bash
git diff --stat
git diff libs/markoff-parser/src/TreeSitterParser.cpp
```

Expected files modified:
- `libs/markoff-parser/include/markoff-parser/Document.h` (already in the prior commit; should show no further change here unless you also adjusted includes)
- `libs/markoff-parser/src/TreeSitterParser.cpp`
- `libs/markoff-parser/tests/CMakeLists.txt` (already in the prior commit)
- `libs/markoff-parser/tests/tst_parser_inline_span_bake.cpp` (already in the prior commit)

- [ ] **Step 2: Commit**

```bash
git add libs/markoff-parser/src/TreeSitterParser.cpp
# include Document.h if you adjusted includes:
# git add libs/markoff-parser/include/markoff-parser/Document.h

git commit -m "$(cat <<'EOF'
feat(parser): bake per-block inline spans into TopLevelBlock

Adds a TopLevelBlock::inlineSpans field populated by buildDocumentQueries
with the inline structural spans (bold, italic, code, link, etc.) for
each top-level block, in block-relative coordinates.

Eliminates the need for InlineFormatHighlighter (R6) to construct a
fresh TreeSitterParser per delegate per source change — the audit's
single biggest perf-bug contributor for live editing.

Cost: one extra O(spans + blocks) sweep at the end of each parse,
sharing buildSpanMap's existing O(N) inline-tree walk. No behavioural
change to existing consumers; the field is additive.

Coordinates: utf8Offset and charOffset are block-relative. parentCharStart
and parentCharEnd, when non--1, are also block-relative.

Spec: docs/specs/2026-05-02-live-render-restoration-design.md §11 R1.
EOF
)"
```

Verify:

```bash
git log --oneline -2
git status
```

---

## Self-review

After completing all tasks:

- **Spec coverage.** §11 R1's "per-block inline span data pre-baked into a public-API value type" is the contract this plan delivers. ✓
- **Type consistency.** `inlineSpans` is `QList<SourceSpan>`; `SourceSpan` already has `utf8Offset`/`charOffset`/`bold`/etc. Coordinate-space contract is documented in the field's comment. ✓
- **No coupling to R1A.** This plan touches `markoff-parser`; R1A touches `markoff-foundation`. They share no files. Either can land first.
- **Cycle-guard impact.** None — this is a new field on a value-type AST snapshot. No reactive logic changes.
- **Performance.** The bake is O(spans + blocks) per parse. `buildSpanMap()` is already O(N) per parse. Net additive cost is small relative to the pre-existing parse cost. **Critical**: this enables R6 to *eliminate* the per-delegate-per-keystroke fresh-parser instantiation, which is asymptotically much larger. Expected outcome at R6: substantially-improved typing latency on long documents.

---

## Acceptance criterion

This plan is complete when:

1. `tst_parser_inline_span_bake` passes (6/6 green).
2. The full fast-tier suite passes — N+1 expected (where N is the pre-R1B count, which itself depends on whether R1A landed first).
3. Two commits on the branch (failing-test + bake implementation).

R1A and R1B together complete two of three R1 sub-projects. R1C (`docs/plans/2026-05-02-live-render-r1c-library-scaffold.md`) is the third; it's fully independent and can land in any order. R1 phase acceptance per the spec §11 requires all three.
