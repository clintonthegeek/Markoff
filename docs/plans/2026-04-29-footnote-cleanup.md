# Footnote Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Option A of the footnote cleanup spec (`docs/specs/2026-04-29-footnote-cleanup-design.md`). Drop the dead `[^N]` → `<sup>N</sup>` substitution and the definition-line strip from `Markoff::Document::extract`. Add a new `Document::footnoteRefs()` metadata API for future live preview. After this work, `ExtractedSource::body` is the post-frontmatter source verbatim.

**Architecture:** Parser-only change inside `libs/markoff-parser/`. `Document::extract` keeps frontmatter strip and definition extraction; loses substitution and definition-line removal; gains a `refs` field on `ExtractedSource`. `Document` exposes `footnoteRefs()` reading from baked private state. No view or foundation code changes; `IncrementalParseSession`'s body diff implicitly becomes equal to the source diff modulo the frontmatter offset.

**Tech Stack:** C++20, Qt6, QRegularExpression, Qt Test (QTEST_MAIN).

**File structure:**
- Modify: `libs/markoff-parser/include/markoff-parser/Document.h` — add `FootnoteRefInfo` struct, extend `ExtractedSource` with `refs`, add `Document::footnoteRefs()`, refresh `ExtractedSource` doc comment.
- Modify: `libs/markoff-parser/src/Document.cpp` — drop substitution loop, drop definition-line strip, fold refs scan into numbering pass with definition-prefix skip, store refs on Private, return them via getter.
- Modify: `libs/markoff-parser/tests/tst_document.cpp` — add tests for new body contract and `footnoteRefs()` metadata.
- Modify: `docs/TODO.md` — mark "Stop pre-processing inside Document" follow-up as landed; renumber remaining follow-ups.

**Build/test loop:**
```bash
cmake --build build-dev -j --target tst_document tst_document_queries tst_incremental_parse
cd build-dev && ctest -R "tst_document|tst_document_queries|tst_incremental_parse" --output-on-failure
```

Final fast-suite gate before the last commit:
```bash
cmake --build build-dev -j && cd build-dev && ctest -j -E "tst_realistic|tst_benchmark"
```

Final slow-suite gate (only after all functional commits):
```bash
cd build-dev && ctest -j --output-on-failure
```

---

### Task 1: Drop the `<sup>N</sup>` substitution loop

**Files:**
- Modify: `libs/markoff-parser/src/Document.cpp`
- Modify: `libs/markoff-parser/tests/tst_document.cpp`

- [ ] **Step 1: Write the failing test.**

Append to `libs/markoff-parser/tests/tst_document.cpp` (locate the file's existing `class TestDocument` declaration; add a new slot to its `private slots:` section and an implementation at the bottom before `QTEST_MAIN`).

Add to the slot list:

```cpp
    void extract_doesNotInsertSupHtml();
```

Add the implementation at the bottom of the file, before `QTEST_MAIN`:

```cpp
void TestDocument::extract_doesNotInsertSupHtml()
{
    const QString src =
        QStringLiteral("Text with[^1] a reference.\n\n[^1]: defn.\n");
    const auto extracted = Markoff::Document::extract(src);
    QVERIFY2(!extracted.body.contains(QStringLiteral("<sup>")),
             qPrintable(QStringLiteral("body still contains <sup>: ")
                        + extracted.body));
}
```

- [ ] **Step 2: Build and run to confirm failure.**

Run: `cmake --build build-dev -j --target tst_document && cd build-dev && ctest -R tst_document --output-on-failure`
Expected: `extract_doesNotInsertSupHtml` fails with the body containing `<sup>1</sup>`.

- [ ] **Step 3: Delete the substitution loop in `Document::extract`.**

In `libs/markoff-parser/src/Document.cpp`, delete the entire block from the comment `// Replace [^label] references with superscript numbers.` through the closing `}` of the `processed = ...` rebuild, exactly:

```cpp
    // Replace [^label] references with superscript numbers.
    if (!footnoteMap.isEmpty()) {
        QString processed;
        int pos = 0;
        auto refIt2 = footnoteRef.globalMatch(markdown);
        while (refIt2.hasNext()) {
            auto match = refIt2.next();
            processed += markdown.mid(pos, match.capturedStart() - pos);
            const QString label = match.captured(1);
            if (footnoteMap.contains(label)) {
                int num = footnoteMap[label].number;
                processed += QStringLiteral("<sup>%1</sup>").arg(num);
            } else {
                processed += match.captured(0);  // unresolved → leave as-is
            }
            pos = match.capturedEnd();
        }
        processed += markdown.mid(pos);
        markdown = processed;
    }
```

Replace with nothing (delete the block entirely). The "Number footnotes in order of first reference" loop above and the "Sort referenced footnotes by assigned number" loop below stay intact.

- [ ] **Step 4: Build and run to confirm pass.**

Run: `cmake --build build-dev -j --target tst_document && cd build-dev && ctest -R "tst_document|tst_document_queries|tst_incremental_parse" --output-on-failure`
Expected: all tests pass, including the existing `testFootnotes` (which only checks `Document::footnotes()`, not body).

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-parser/src/Document.cpp \
        libs/markoff-parser/tests/tst_document.cpp
git commit -m "feat(parser): drop dead <sup>N</sup> substitution from Document::extract"
```

---

### Task 2: Stop stripping `[^label]:` definition lines from body

**Files:**
- Modify: `libs/markoff-parser/src/Document.cpp`
- Modify: `libs/markoff-parser/tests/tst_document.cpp`

After this task, `body == source` (no frontmatter case) and `body == source.mid(frontmatterBlockEnd)` (with frontmatter case), byte for byte.

- [ ] **Step 1: Write failing tests for the body invariant.**

Add to the `TestDocument` slot list in `tst_document.cpp`:

```cpp
    void extract_bodyEqualsSourceWhenNoFrontmatter();
    void extract_bodyEqualsPostFrontmatterSliceWithFrontmatter();
    void extract_keepsFootnoteDefinitionLinesInBody();
```

Add the implementations at the bottom of the file, before `QTEST_MAIN`:

```cpp
void TestDocument::extract_bodyEqualsSourceWhenNoFrontmatter()
{
    const QString src = QStringLiteral(
        "Plain text[^a].\n\n[^a]: definition lives here.\nMore text.\n");
    const auto extracted = Markoff::Document::extract(src);
    QCOMPARE(extracted.body, src);
}

void TestDocument::extract_bodyEqualsPostFrontmatterSliceWithFrontmatter()
{
    const QString src = QStringLiteral(
        "---\nkey: value\n---\nbody[^1] line.\n\n[^1]: defn.\n");
    const auto extracted = Markoff::Document::extract(src);
    QVERIFY(extracted.frontmatterBlockEnd > 0);
    QCOMPARE(extracted.body, src.mid(extracted.frontmatterBlockEnd));
}

void TestDocument::extract_keepsFootnoteDefinitionLinesInBody()
{
    const QString src = QStringLiteral(
        "ref[^1].\n\n[^1]: This is the definition content.\n");
    const auto extracted = Markoff::Document::extract(src);
    QVERIFY2(extracted.body.contains(
                 QStringLiteral("[^1]: This is the definition content.")),
             qPrintable(QStringLiteral("definition line missing from body: ")
                        + extracted.body));
}
```

- [ ] **Step 2: Build and run to confirm failure.**

Run: `cmake --build build-dev -j --target tst_document && cd build-dev && ctest -R tst_document --output-on-failure`
Expected: all three new tests fail (definition line is currently stripped from body).

- [ ] **Step 3: Delete the definition-line strip in `Document::extract`.**

In `libs/markoff-parser/src/Document.cpp`, delete the block:

```cpp
    // Remove footnote definition lines from the body.
    if (!footnoteMap.isEmpty())
        markdown.remove(footnoteDef);
```

Replace with nothing.

- [ ] **Step 4: Build and run to confirm pass.**

Run: `cmake --build build-dev -j --target tst_document && cd build-dev && ctest -R "tst_document|tst_document_queries|tst_incremental_parse" --output-on-failure`
Expected: all tests pass, including the existing `testFootnotes` and the three new invariant tests.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-parser/src/Document.cpp \
        libs/markoff-parser/tests/tst_document.cpp
git commit -m "feat(parser): keep [^label]: definition lines in body"
```

---

### Task 3: Add `Document::footnoteRefs()` metadata API

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/Document.h`
- Modify: `libs/markoff-parser/src/Document.cpp`
- Modify: `libs/markoff-parser/tests/tst_document.cpp`

- [ ] **Step 1: Write failing tests for the new API.**

Add to the `TestDocument` slot list in `tst_document.cpp`:

```cpp
    void footnoteRefs_emptyForDocWithNoFootnotes();
    void footnoteRefs_singleRefReturnsOneEntry();
    void footnoteRefs_twoLabelsTwoRefsEachInOrder();
    void footnoteRefs_definitionPrefixIsNotARef();
    void footnoteRefs_unresolvedRefHasZeroNumber();
```

Add the implementations at the bottom of the file, before `QTEST_MAIN`:

```cpp
void TestDocument::footnoteRefs_emptyForDocWithNoFootnotes()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral("plain text"));
    QVERIFY(doc->footnoteRefs().isEmpty());
}

void TestDocument::footnoteRefs_singleRefReturnsOneEntry()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "Text[^1] more.\n\n[^1]: definition.\n"));
    const auto refs = doc->footnoteRefs();
    QCOMPARE(refs.size(), 1);
    QCOMPARE(refs[0].label, QStringLiteral("1"));
    QCOMPARE(refs[0].number, 1);
    // sourceOffset points into body (== source here, no frontmatter).
    QCOMPARE(refs[0].sourceOffset, 4);  // "Text" = 4 chars; "[^1]" follows.
}

void TestDocument::footnoteRefs_twoLabelsTwoRefsEachInOrder()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "see[^a], also[^b], more[^a], end[^b].\n\n[^a]: A.\n[^b]: B.\n"));
    const auto refs = doc->footnoteRefs();
    QCOMPARE(refs.size(), 4);
    QCOMPARE(refs[0].label, QStringLiteral("a"));
    QCOMPARE(refs[0].number, 1);
    QCOMPARE(refs[1].label, QStringLiteral("b"));
    QCOMPARE(refs[1].number, 2);
    QCOMPARE(refs[2].label, QStringLiteral("a"));
    QCOMPARE(refs[2].number, 1);
    QCOMPARE(refs[3].label, QStringLiteral("b"));
    QCOMPARE(refs[3].number, 2);
    // Offsets must be strictly ascending.
    for (int i = 1; i < refs.size(); ++i)
        QVERIFY(refs[i].sourceOffset > refs[i - 1].sourceOffset);
}

void TestDocument::footnoteRefs_definitionPrefixIsNotARef()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "see[^1].\n\n[^1]: definition body.\n"));
    const auto refs = doc->footnoteRefs();
    QCOMPARE(refs.size(), 1);  // only the inline reference, not [^1]: prefix
    QCOMPARE(refs[0].label, QStringLiteral("1"));
}

void TestDocument::footnoteRefs_unresolvedRefHasZeroNumber()
{
    auto doc = Markoff::Document::fromMarkdown(QStringLiteral(
        "see[^missing] and continue.\n"));
    const auto refs = doc->footnoteRefs();
    QCOMPARE(refs.size(), 1);
    QCOMPARE(refs[0].label, QStringLiteral("missing"));
    QCOMPARE(refs[0].number, 0);  // no definition → no number
}
```

- [ ] **Step 2: Build and confirm compile failure.**

Run: `cmake --build build-dev -j --target tst_document 2>&1 | tail -10`
Expected: compile error mentioning `footnoteRefs` is not a member of `Markoff::Document`, plus a missing struct error for `FootnoteRefInfo`.

- [ ] **Step 3: Add `FootnoteRefInfo` struct, extend `ExtractedSource`, declare `footnoteRefs()` getter.**

In `libs/markoff-parser/include/markoff-parser/Document.h`, add the new struct after the existing `FootnoteInfo` declaration (around line 38, between `FootnoteInfo` and `FrontmatterProperty`):

```cpp
struct FootnoteRefInfo {
    QString label;        // e.g. "1", "bignote"
    int     number;       // 1-based, assigned by first-reference order;
                          // 0 if the label has no matching definition.
    int     sourceOffset; // QString char offset of '[' in body.
};
```

Replace the existing `ExtractedSource` doc comment and struct (lines 50–65) with:

```cpp
/// Frontmatter-aware extraction output. After this call:
///   - `frontmatter` holds the YAML body between the --- delimiters
///     (without delimiters or the surrounding newline).
///   - `body` is the source verbatim, with the frontmatter block removed
///     (== source.mid(frontmatterBlockEnd) when frontmatter is present,
///     == source otherwise). Footnote references and definition lines
///     remain in `body` exactly as written; the parser sees them in
///     their original textual form.
///   - `footnotes` is the canonical definition list (label → content,
///     numbered by first-reference order).
///   - `refs` records every `[^label]` reference occurrence in `body`,
///     in order of appearance, with the same numbering scheme. Refs
///     whose label has no definition carry number 0.
///
/// Used by long-lived parsers (e.g., foundation's IncrementalParseSession)
/// that want to share Document::extract()'s logic without going through the
/// fromMarkdown() one-shot path.
struct ExtractedSource {
    QString                  body;
    QString                  frontmatter;
    int                      frontmatterBlockStart = -1;
    int                      frontmatterBlockEnd   = -1;
    bool                     frontmatterEofClose   = false;
    QList<FootnoteInfo>      footnotes;  // numbered, in reference order
    QList<FootnoteRefInfo>   refs;       // ordered by sourceOffset (== first-occurrence order)
};
```

In the public section of class `Document`, after `QList<FootnoteInfo> footnotes() const;` (around line 141), add:

```cpp
    /// Footnote references in `body`, in order of occurrence. Each entry
    /// carries the label, the number assigned by first-reference order,
    /// and the QString char offset of the opening `[` in body coordinates.
    /// Refs whose label has no matching definition carry number 0.
    QList<FootnoteRefInfo> footnoteRefs() const;
```

- [ ] **Step 4: Update `Document::Private`, `extract`, and `fromComponents` in the .cpp to populate and surface refs.**

In `libs/markoff-parser/src/Document.cpp`, find `struct Document::Private` (around line 11) and add a new member after the existing `QList<FootnoteInfo> footnotes;` line:

```cpp
    QList<FootnoteRefInfo> refs;
```

Replace the entire body of `Document::extract` (lines 38–135) with:

```cpp
ExtractedSource Document::extract(const QString &source)
{
    ExtractedSource out;
    QString markdown = source;

    // Extract frontmatter — track byte spans per Cluster A contract.
    if (source.startsWith(QStringLiteral("---\n")) || source.startsWith(QStringLiteral("---\r\n"))) {
        int endPos = source.indexOf(QStringLiteral("\n---"), 3);
        if (endPos >= 0) {
            int fmStart = source.indexOf(QLatin1Char('\n')) + 1;
            out.frontmatter = source.mid(fmStart, endPos - fmStart);
            out.frontmatterBlockStart = 0;

            int afterFm = endPos + 4;  // "\n---"
            if (afterFm < source.size() && source[afterFm] == QLatin1Char('\r'))
                ++afterFm;
            if (afterFm < source.size() && source[afterFm] == QLatin1Char('\n'))
                ++afterFm;

            out.frontmatterEofClose = (afterFm >= source.size());
            out.frontmatterBlockEnd = afterFm;
            markdown = source.mid(afterFm);
        } else if (source.endsWith(QStringLiteral("\n---"))) {
            // Opening --- with closing --- at EOF, no trailing newline.
            int fmStart = source.indexOf(QLatin1Char('\n')) + 1;
            out.frontmatter = source.mid(fmStart, source.size() - fmStart - 4);
            out.frontmatterBlockStart = 0;
            out.frontmatterBlockEnd = source.size();
            out.frontmatterEofClose = true;
            markdown = QString();
        }
    }

    // Extract footnote definitions [^label]: content
    static const QRegularExpression footnoteDef(
        QStringLiteral(R"(^\[\^([^\]]+)\]:\s*(.+)$)"),
        QRegularExpression::MultilineOption);

    QHash<QString, FootnoteInfo> footnoteMap;
    auto defIt = footnoteDef.globalMatch(markdown);
    while (defIt.hasNext()) {
        auto match = defIt.next();
        FootnoteInfo fn;
        fn.label = match.captured(1);
        fn.content = match.captured(2);
        fn.number = 0;
        footnoteMap.insert(fn.label, fn);
    }

    // Single-pass scan over `[^label]` occurrences. Skip definition prefixes
    // (a `[^label]` immediately followed by `:`). Number on first sighting,
    // record every occurrence into `out.refs`.
    int nextNum = 1;
    static const QRegularExpression footnoteRef(QStringLiteral(R"(\[\^([^\]]+)\])"));
    auto refIt = footnoteRef.globalMatch(markdown);
    while (refIt.hasNext()) {
        auto match = refIt.next();
        const int afterClose = match.capturedEnd();
        // Skip definition prefixes: `]` followed (after optional whitespace
        // that is not a newline) by `:`.
        int p = afterClose;
        while (p < markdown.size()) {
            const QChar c = markdown[p];
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
                ++p;
                continue;
            }
            break;
        }
        if (p < markdown.size() && markdown[p] == QLatin1Char(':'))
            continue;

        const QString label = match.captured(1);
        FootnoteRefInfo ref;
        ref.label = label;
        ref.sourceOffset = match.capturedStart();
        ref.number = 0;
        if (footnoteMap.contains(label)) {
            if (footnoteMap[label].number == 0)
                footnoteMap[label].number = nextNum++;
            ref.number = footnoteMap[label].number;
        }
        out.refs.append(ref);
    }

    // Sort referenced footnotes by assigned number.
    for (auto &fn : footnoteMap) {
        if (fn.number > 0)
            out.footnotes.append(fn);
    }
    std::sort(out.footnotes.begin(), out.footnotes.end(),
              [](const FootnoteInfo &a, const FootnoteInfo &b) {
                  return a.number < b.number;
              });

    out.body = std::move(markdown);
    return out;
}
```

In `Document::fromComponents` (around line 137 in the original layout, after the function signature opens), add a single new line copying the refs into Private. After the existing `doc->d->footnotes = std::move(extracted.footnotes);` line, append:

```cpp
    doc->d->refs                  = std::move(extracted.refs);
```

Add a new method implementation after `Document::footnotes()` (around line 253):

```cpp
QList<FootnoteRefInfo> Document::footnoteRefs() const
{
    return d->refs;
}
```

- [ ] **Step 5: Build and run all parser tests.**

Run: `cmake --build build-dev -j --target tst_document tst_document_queries tst_incremental_parse && cd build-dev && ctest -R "tst_document|tst_document_queries|tst_incremental_parse" --output-on-failure`
Expected: all tests pass — old fingerprint tests, the existing `testFootnotes`, and the five new `footnoteRefs_*` tests.

- [ ] **Step 6: Run the full fast suite.**

Run: `cmake --build build-dev -j && cd build-dev && ctest -j -E "tst_realistic|tst_benchmark"`
Expected: 76/76 green.

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-parser/include/markoff-parser/Document.h \
        libs/markoff-parser/src/Document.cpp \
        libs/markoff-parser/tests/tst_document.cpp
git commit -m "feat(parser): Document::footnoteRefs() metadata API"
```

---

### Task 4: Full-suite verification + TODO update

**Files:**
- Modify: `docs/TODO.md`

- [ ] **Step 1: Run the slow tail (`tst_benchmark` ~7 min, `tst_realistic` ~90s) to confirm 78/78.**

Run: `cd build-dev && ctest -j --output-on-failure`
Expected: 78/78 green. The slow tail should be unchanged in count and roughly equivalent in wall-clock to the prior baseline (381s / 80s).

If the suite isn't green, stop and investigate. Do not commit Step 2 until tests pass.

- [ ] **Step 2: Update `docs/TODO.md`.**

In the `## 2026-04-29 — Incremental parser + old-leaf deletion (this branch)` section, append a new bullet to the "Work landed on `exploration/new-foundation`" list (use the actual short hashes from `git log --oneline -4` for the three implementation commits from Tasks 1–3):

```markdown
- `<hash1>` / `<hash2>` / `<hash3>` — Footnote cleanup (Option A of
  `docs/specs/2026-04-29-footnote-cleanup-design.md`). `Document::extract`
  no longer rewrites the body: the dead `[^N]` → `<sup>N</sup>`
  substitution and the definition-line strip are gone, so `body` is now
  `source.mid(frontmatterBlockEnd)` byte-for-byte. New
  `Document::footnoteRefs()` exposes per-reference label + number +
  source offset for the future live preview. `Document::footnotes()`
  unchanged. Body diff in `IncrementalParseSession` now equals the
  source diff — perf win on every footnote-related edit.
```

In the "### Open follow-ups (priority order)" subsection, **delete bullet 1** ("Stop pre-processing inside `Document`") and renumber the remaining bullet ("Hard benchmarks of the new pipeline vs. the old.") from `2.` to `1.`.

- [ ] **Step 3: Commit the TODO update.**

```bash
git add docs/TODO.md
git commit -m "docs: TODO — Footnote cleanup (Option A) landed"
```

---

## Self-review notes

- **Spec coverage:** All Option A goals are addressed. Goal 1 (drop substitution) → Task 1. Goal 2 (body == post-frontmatter source) → Task 2. Goal 3 (`footnoteRefs()` API) → Task 3. Goal 4 (`Document::footnotes()` preserved) → no code change to that path; verified by the existing `testFootnotes` test. Goal 5 (no view changes) → no view files in any task. Acceptance criteria 1–6 in the spec all map to test runs in Tasks 1–4.
- **Type consistency:** `FootnoteRefInfo` fields (`label`, `number`, `sourceOffset`) used identically across header declaration, .cpp population, and test assertions. `ExtractedSource::refs` populated in `extract`, moved into `Private::refs` in `fromComponents`, returned by `footnoteRefs()`.
- **Definition-prefix skip rule:** the spec calls for "peek at the character(s) immediately after the closing `]`; if the next non-whitespace character is `:`, skip." Task 3 Step 4 implements this exactly: a small loop advances past `' '` and `'\t'`, then checks for `:`. Newlines are not skipped (a definition prefix is on a single line by Markdown convention; a `]` followed by newline-then-`:` would not be a definition).
- **Risk handling:** the known item from the spec (tree-sitter's `isFootnoteRef` post-process now firing on `[^label]:` line prefixes) is documented in the spec but not engineered around in this plan — correct per the "do not engineer a fix in Option A" decision in the spec.
- **No placeholders:** every code step has full code. Every command step has the exact command. Every commit step has the exact message.
- **Frequency of commits:** 3 functional + 1 docs = 4 commits, each independently revertible.
