> **Status: completed.** Landed via `b01f913` / `e08c512` / `bc8dca1`. `TreeSitterParser::inlineTreeReuseCount()` is wired and tested. Do not execute.

# Inline-Tree Reuse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Phase 2 of incremental parsing — after the block-tree incremental reparse, reuse `m_inlineTrees[i]` for inline regions whose post-edit byte ranges are unchanged from the prior parse, instead of always reparsing every inline region.

**Architecture:** Snapshot old inline byte ranges and tree pointers before applying `ts_tree_edit`. Compute each old range's post-edit byte range by shifting through the sorted `ByteEdit` list (or invalidating it if any edit overlaps). After block reparse, match each new inline range against unconsumed shifted-old ranges by exact byte equality; matched regions reuse the old `TSTree *`, unmatched regions parse fresh. Add a public `inlineTreeReuseCount()` observability hook so tests can assert reuse actually happens (fingerprint equivalence alone could not distinguish reuse from a no-op).

**Tech Stack:** C++20, Qt6, tree-sitter C API (`ts_tree_edit`, `ts_parser_parse_string`, `ts_parser_set_included_ranges`).

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/TreeSitterParser.h` (add `inlineTreeReuseCount()` getter + `m_lastInlineReuseCount` member)
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp` (add reuse logic in `parseIncremental`; reset counter in `parse`)
- Modify: `libs/markoff-parser/tests/tst_incremental_parse.cpp` (4 new tests)
- Modify: `docs/TODO.md` (mark Phase 2 done, drop follow-up #1)

**Test contract:** All 8 existing fingerprint-equivalence tests in `tst_incremental_parse` must continue to pass. New tests assert reuse-count behavior.

**Build/test loop:**
```bash
cmake --build build-dev -j --target tst_incremental_parse
cd build-dev && ctest -R tst_incremental_parse --output-on-failure
```

Full fast suite gate before commit:
```bash
cd build-dev && ctest -j -E "tst_realistic|tst_benchmark"
```

---

### Task 1: Add `inlineTreeReuseCount()` API scaffolding

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/TreeSitterParser.h`
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp`
- Modify: `libs/markoff-parser/tests/tst_incremental_parse.cpp`

- [ ] **Step 1: Write failing tests for the counter contract.**

Add to `tst_incremental_parse.cpp` in the `private Q_SLOTS` block (after the existing slot list):

```cpp
    void freshParse_resetsReuseCountToZero();
    void parseIncremental_noPriorTree_reportsZeroReuse();
```

And add the implementations at the bottom (before `QTEST_APPLESS_MAIN`):

```cpp
void TstIncrementalParse::freshParse_resetsReuseCountToZero()
{
    TreeSitterParser p;
    QVERIFY(p.parse(QStringLiteral("# Heading\n\npara")));
    QCOMPARE(p.inlineTreeReuseCount(), 0);
}

void TstIncrementalParse::parseIncremental_noPriorTree_reportsZeroReuse()
{
    TreeSitterParser p;
    QVERIFY(p.parseIncremental({}, QByteArrayLiteral("# Heading\n\npara")));
    QCOMPARE(p.inlineTreeReuseCount(), 0);
}
```

- [ ] **Step 2: Build to verify the tests fail to compile.**

Run: `cmake --build build-dev -j --target tst_incremental_parse 2>&1 | tail -15`
Expected: compile error mentioning `inlineTreeReuseCount` is not a member of `TreeSitterParser`.

- [ ] **Step 3: Add the member + getter to the header.**

In `TreeSitterParser.h`, add after the `hasTree()` declaration (around line 78):

```cpp
    /// Number of inline regions whose tree was reused (not reparsed) on
    /// the most recent `parseIncremental()` call. Reset to 0 on `parse()`.
    /// Primarily an observability hook for tests/benchmarks.
    int inlineTreeReuseCount() const { return m_lastInlineReuseCount; }
```

And add to the private members section (after `m_byteToChar`, around line 108):

```cpp
    int m_lastInlineReuseCount = 0;
```

- [ ] **Step 4: Reset the counter in `parse()`.**

In `TreeSitterParser.cpp`, inside `parse()`, add this line at the very top of the function (just inside the opening brace at line 184):

```cpp
    m_lastInlineReuseCount = 0;
```

- [ ] **Step 5: Build and run the new tests.**

Run: `cmake --build build-dev -j --target tst_incremental_parse && cd build-dev && ctest -R tst_incremental_parse --output-on-failure`
Expected: all 10 tests pass (8 existing + 2 new).

- [ ] **Step 6: Commit.**

```bash
git add libs/markoff-parser/include/markoff-parser/TreeSitterParser.h \
        libs/markoff-parser/src/TreeSitterParser.cpp \
        libs/markoff-parser/tests/tst_incremental_parse.cpp
git commit -m "feat(parser): inlineTreeReuseCount() observability hook"
```

---

### Task 2: Implement inline-tree reuse in `parseIncremental`

**Files:**
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp`
- Modify: `libs/markoff-parser/tests/tst_incremental_parse.cpp`

- [ ] **Step 1: Write the failing reuse test.**

Add to the slot list in `tst_incremental_parse.cpp`:

```cpp
    void singleParagraphEdit_reusesUnchangedInlineRegions();
```

And implement at the bottom (before `QTEST_APPLESS_MAIN`):

```cpp
void TstIncrementalParse::singleParagraphEdit_reusesUnchangedInlineRegions()
{
    // Three paragraphs. Edit only the middle one. Expect the two outer
    // inline regions to be reused.
    QByteArray oldSrc = QByteArrayLiteral(
        "para alpha here.\n\npara beta here.\n\npara gamma here.");
    QByteArray newSrc = QByteArrayLiteral(
        "para alpha here.\n\npara **beta** here.\n\npara gamma here.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    const int at = oldSrc.indexOf(QByteArrayLiteral("beta"));
    QVERIFY(at >= 0);
    // Replace "beta" (4 bytes) with "**beta**" (8 bytes).
    ByteEdit e{ static_cast<quint32>(at),
                static_cast<quint32>(at + 4),
                8u };
    QVERIFY(p.parseIncremental({e}, newSrc));

    QCOMPARE(p.inlineTreeReuseCount(), 2);

    // Span output must still match a fresh parse exactly.
    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}
```

- [ ] **Step 2: Run to verify it fails.**

Run: `cmake --build build-dev -j --target tst_incremental_parse && cd build-dev && ctest -R tst_incremental_parse --output-on-failure`
Expected: `singleParagraphEdit_reusesUnchangedInlineRegions` fails — reuse count is 0 (current behavior reparses all inline regions).

- [ ] **Step 3: Replace the inline-rebuild block in `parseIncremental` with reuse logic.**

In `TreeSitterParser.cpp`, replace lines 277–294 (the comment `// Phase 1: full reparse of inline regions...` through the closing `}` of the for-loop that fills `m_inlineTrees`) with:

```cpp
    // Phase 2: reuse inline trees for regions whose post-edit byte range
    // is unchanged. Snapshot old ranges + tree pointers BEFORE we touched
    // the block tree (we did so above by walking the old tree before
    // ts_tree_edit). For each old range, shift through the sorted edits
    // to derive its post-edit byte range, or mark it invalid if any edit
    // overlaps. Then match new ranges against unconsumed shifted-old
    // ranges by exact byte equality; reuse on match, fresh-parse on miss.
    TSNode root = ts_tree_root_node(m_blockTree);
    std::vector<TSRange> newInlineRanges;
    collectInlineRanges(root, newInlineRanges);

    m_lastInlineReuseCount = 0;

    struct ShiftedRange {
        quint32 start;
        quint32 end;
        int oldIdx;
        bool valid;
    };
    std::vector<ShiftedRange> shifted;
    shifted.reserve(oldInlineRanges.size());
    for (size_t i = 0; i < oldInlineRanges.size(); ++i) {
        const quint32 s = oldInlineRanges[i].start_byte;
        const quint32 e = oldInlineRanges[i].end_byte;
        bool valid = true;
        qint64 delta = 0;
        for (const ByteEdit &ed : sortedEdits) {
            if (ed.oldEnd <= s) {
                delta += static_cast<qint64>(ed.newLength)
                       - static_cast<qint64>(ed.oldEnd - ed.oldStart);
            } else if (ed.oldStart >= e) {
                // edit lies entirely past this range — no impact.
            } else {
                valid = false;
                break;
            }
        }
        ShiftedRange sr{};
        sr.oldIdx = static_cast<int>(i);
        sr.valid = valid;
        if (valid) {
            sr.start = static_cast<quint32>(static_cast<qint64>(s) + delta);
            sr.end   = static_cast<quint32>(static_cast<qint64>(e) + delta);
        }
        shifted.push_back(sr);
    }

    std::vector<bool> consumed(oldInlineTrees.size(), false);
    for (const TSRange &nr : newInlineRanges) {
        int reuseIdx = -1;
        for (const ShiftedRange &sr : shifted) {
            if (!sr.valid || consumed[sr.oldIdx])
                continue;
            if (sr.start == nr.start_byte && sr.end == nr.end_byte) {
                reuseIdx = sr.oldIdx;
                break;
            }
        }
        if (reuseIdx >= 0) {
            m_inlineTrees.append(oldInlineTrees[reuseIdx]);
            consumed[reuseIdx] = true;
            ++m_lastInlineReuseCount;
        } else {
            ts_parser_set_included_ranges(m_inlineParser, &nr, 1);
            TSTree *tree = ts_parser_parse_string(m_inlineParser, nullptr,
                                                   m_utf8.constData(),
                                                   static_cast<uint32_t>(m_utf8.size()));
            if (tree)
                m_inlineTrees.append(tree);
            ts_parser_set_included_ranges(m_inlineParser, nullptr, 0);
        }
    }

    for (size_t i = 0; i < oldInlineTrees.size(); ++i) {
        if (!consumed[i])
            ts_tree_delete(oldInlineTrees[i]);
    }

    return true;
}
```

- [ ] **Step 4: Capture old inline state at function entry and lift `sorted` to function scope.**

We need `oldInlineRanges`, `oldInlineTrees`, and `sortedEdits` visible at the new reuse block. Restructure the body of `parseIncremental` so it looks like the following. Replace the entire current body (lines 224–296, i.e. everything inside the function after the opening brace) with:

```cpp
    // No prior tree → full parse of the new buffer. Callers don't need
    // a first-parse branch.
    if (!m_blockTree) {
        return parse(QString::fromUtf8(newUtf8));
    }

    // Snapshot inline state from the OLD block tree before any edit, so we
    // can reuse trees whose post-edit byte range is unchanged.
    std::vector<TSRange> oldInlineRanges;
    collectInlineRanges(ts_tree_root_node(m_blockTree), oldInlineRanges);
    QList<TSTree *> oldInlineTrees = m_inlineTrees;
    m_inlineTrees.clear();

    QList<ByteEdit> sortedEdits;

    if (edits.isEmpty()) {
        // No edits but caller still asked for incremental: nothing to do
        // for the block tree (it's already valid). Refresh the buffer-of-
        // record (caller may have replaced it with an identical-looking
        // newUtf8) and fall through to the inline reuse pass — every
        // inline region's range will match exactly, so all trees reuse.
        m_utf8       = newUtf8;
        m_byteToChar = buildByteToCharMap(m_utf8);
    } else {
        // Sort by ascending oldStart, then apply ts_tree_edit in DESCENDING
        // order. Each ts_tree_edit shifts node positions to a new frame; by
        // editing right-to-left, each edit's old-frame offsets remain valid
        // against the tree's then-current state (because we haven't touched
        // anything to the right yet).
        sortedEdits = edits;
        std::sort(sortedEdits.begin(), sortedEdits.end(),
                  [](const ByteEdit &a, const ByteEdit &b) {
                      return a.oldStart < b.oldStart;
                  });

        for (auto it = sortedEdits.rbegin(); it != sortedEdits.rend(); ++it) {
            const ByteEdit &e = *it;
            TSInputEdit ed{};
            ed.start_byte    = e.oldStart;
            ed.old_end_byte  = e.oldEnd;
            ed.new_end_byte  = e.oldStart + e.newLength;
            // Points are unused downstream (we read trees by byte only),
            // so leave them zero. tree-sitter uses points for some
            // decisions but byte offsets dominate; this is the documented
            // safe shortcut for byte-only consumers.
            ts_tree_edit(m_blockTree, &ed);
        }

        // Now reparse the block tree against the new buffer, supplying the
        // edited prior tree. tree-sitter reuses unchanged subtrees.
        m_utf8       = newUtf8;
        m_byteToChar = buildByteToCharMap(m_utf8);

        TSTree *newTree = ts_parser_parse_string(m_blockParser, m_blockTree,
                                                  m_utf8.constData(),
                                                  static_cast<uint32_t>(m_utf8.size()));
        if (!newTree) {
            // Block reparse failed. Restore the old inline-tree handles so
            // the parser stays in a valid state, and report failure.
            m_inlineTrees = oldInlineTrees;
            return false;
        }
        ts_tree_delete(m_blockTree);
        m_blockTree = newTree;
    }

    // Phase 2: reuse inline trees for regions whose post-edit byte range
    // is unchanged. For each old range, shift through the sorted edits
    // to derive its post-edit byte range, or mark it invalid if any edit
    // overlaps. Then match new ranges against unconsumed shifted-old
    // ranges by exact byte equality; reuse on match, fresh-parse on miss.
    TSNode root = ts_tree_root_node(m_blockTree);
    std::vector<TSRange> newInlineRanges;
    collectInlineRanges(root, newInlineRanges);

    m_lastInlineReuseCount = 0;

    struct ShiftedRange {
        quint32 start;
        quint32 end;
        int oldIdx;
        bool valid;
    };
    std::vector<ShiftedRange> shifted;
    shifted.reserve(oldInlineRanges.size());
    for (size_t i = 0; i < oldInlineRanges.size(); ++i) {
        const quint32 s = oldInlineRanges[i].start_byte;
        const quint32 e = oldInlineRanges[i].end_byte;
        bool valid = true;
        qint64 delta = 0;
        for (const ByteEdit &ed : sortedEdits) {
            if (ed.oldEnd <= s) {
                delta += static_cast<qint64>(ed.newLength)
                       - static_cast<qint64>(ed.oldEnd - ed.oldStart);
            } else if (ed.oldStart >= e) {
                // edit lies entirely past this range — no impact.
            } else {
                valid = false;
                break;
            }
        }
        ShiftedRange sr{};
        sr.oldIdx = static_cast<int>(i);
        sr.valid = valid;
        if (valid) {
            sr.start = static_cast<quint32>(static_cast<qint64>(s) + delta);
            sr.end   = static_cast<quint32>(static_cast<qint64>(e) + delta);
        }
        shifted.push_back(sr);
    }

    std::vector<bool> consumed(oldInlineTrees.size(), false);
    for (const TSRange &nr : newInlineRanges) {
        int reuseIdx = -1;
        for (const ShiftedRange &sr : shifted) {
            if (!sr.valid || consumed[sr.oldIdx])
                continue;
            if (sr.start == nr.start_byte && sr.end == nr.end_byte) {
                reuseIdx = sr.oldIdx;
                break;
            }
        }
        if (reuseIdx >= 0) {
            m_inlineTrees.append(oldInlineTrees[reuseIdx]);
            consumed[reuseIdx] = true;
            ++m_lastInlineReuseCount;
        } else {
            ts_parser_set_included_ranges(m_inlineParser, &nr, 1);
            TSTree *tree = ts_parser_parse_string(m_inlineParser, nullptr,
                                                   m_utf8.constData(),
                                                   static_cast<uint32_t>(m_utf8.size()));
            if (tree)
                m_inlineTrees.append(tree);
            ts_parser_set_included_ranges(m_inlineParser, nullptr, 0);
        }
    }

    for (size_t i = 0; i < oldInlineTrees.size(); ++i) {
        if (!consumed[i])
            ts_tree_delete(oldInlineTrees[i]);
    }

    return true;
```

- [ ] **Step 5: Run the incremental-parse test suite.**

Run: `cmake --build build-dev -j --target tst_incremental_parse && cd build-dev && ctest -R tst_incremental_parse --output-on-failure`
Expected: all 11 tests pass (8 existing fingerprint + 2 counter scaffolding + 1 reuse).

- [ ] **Step 6: Run the full fast suite to verify no regression.**

Run: `cmake --build build-dev -j && cd build-dev && ctest -j -E "tst_realistic|tst_benchmark"`
Expected: same green count as before this task (76/76).

- [ ] **Step 7: Commit.**

```bash
git add libs/markoff-parser/src/TreeSitterParser.cpp \
        libs/markoff-parser/tests/tst_incremental_parse.cpp
git commit -m "feat(parser): reuse inline trees for unchanged regions"
```

---

### Task 3: Add edge-case tests (regression protection)

**Files:**
- Modify: `libs/markoff-parser/tests/tst_incremental_parse.cpp`

These tests are expected to pass without further code changes — Task 2's logic is general. Add them anyway so future changes don't silently break each branch.

- [ ] **Step 1: Add three edge-case tests.**

Add to the slot list:

```cpp
    void editInsideRegion_invalidatesOnlyThatRegion();
    void editsInTwoRegions_reusesTheRegionBetween();
    void noEdits_reusesAllInlineRegions();
```

Implementations:

```cpp
void TstIncrementalParse::editInsideRegion_invalidatesOnlyThatRegion()
{
    QByteArray oldSrc = QByteArrayLiteral(
        "first paragraph.\n\nsecond paragraph here.\n\nthird paragraph.");
    // Insert "**" pair inside "second" — this edit overlaps the second
    // paragraph's inline region, so only that one must rebuild; first and
    // third are reused.
    QByteArray newSrc = QByteArrayLiteral(
        "first paragraph.\n\nsecond **paragraph** here.\n\nthird paragraph.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    const int at = oldSrc.indexOf(QByteArrayLiteral("paragraph here"));
    QVERIFY(at >= 0);
    ByteEdit e{ static_cast<quint32>(at),
                static_cast<quint32>(at + 9),  // length of "paragraph"
                13u };  // length of "**paragraph**"
    QVERIFY(p.parseIncremental({e}, newSrc));

    QCOMPARE(p.inlineTreeReuseCount(), 2);

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::editsInTwoRegions_reusesTheRegionBetween()
{
    QByteArray oldSrc = QByteArrayLiteral(
        "alpha line.\n\nbeta line.\n\ngamma line.");
    // Bold "alpha" and "gamma" (in old-frame coords, out of order to
    // exercise the sort path). beta line is untouched, must reuse.
    const int alphaAt = oldSrc.indexOf(QByteArrayLiteral("alpha"));
    const int gammaAt = oldSrc.indexOf(QByteArrayLiteral("gamma"));
    QVERIFY(alphaAt >= 0 && gammaAt >= 0);

    QByteArray newSrc = oldSrc;
    newSrc.replace(gammaAt, 5, QByteArrayLiteral("**gamma**"));
    newSrc.replace(alphaAt, 5, QByteArrayLiteral("**alpha**"));

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(oldSrc)));

    QList<ByteEdit> edits = {
        ByteEdit{ static_cast<quint32>(gammaAt),
                  static_cast<quint32>(gammaAt + 5),
                  9u },
        ByteEdit{ static_cast<quint32>(alphaAt),
                  static_cast<quint32>(alphaAt + 5),
                  9u },
    };
    QVERIFY(p.parseIncremental(edits, newSrc));

    QCOMPARE(p.inlineTreeReuseCount(), 1);  // beta line reused

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(newSrc)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}

void TstIncrementalParse::noEdits_reusesAllInlineRegions()
{
    QByteArray src = QByteArrayLiteral(
        "# Heading\n\nfirst paragraph.\n\nsecond paragraph.");

    TreeSitterParser p;
    QVERIFY(p.parse(QString::fromUtf8(src)));

    QVERIFY(p.parseIncremental({}, src));

    // 3 inline regions: heading text, paragraph 1, paragraph 2.
    QCOMPARE(p.inlineTreeReuseCount(), 3);

    TreeSitterParser ref;
    QVERIFY(ref.parse(QString::fromUtf8(src)));
    QCOMPARE(fingerprintAll(p.buildSpanMap()),
             fingerprintAll(ref.buildSpanMap()));
}
```

- [ ] **Step 2: Build and run.**

Run: `cmake --build build-dev -j --target tst_incremental_parse && cd build-dev && ctest -R tst_incremental_parse --output-on-failure`
Expected: all 14 tests pass.

- [ ] **Step 3: Commit.**

```bash
git add libs/markoff-parser/tests/tst_incremental_parse.cpp
git commit -m "test(parser): edge cases for inline-tree reuse"
```

---

### Task 4: Final full-suite verification + TODO update

**Files:**
- Modify: `docs/TODO.md`

- [ ] **Step 1: Run the full suite (slow tail included) to confirm 78/78.**

Run: `cmake --build build-dev -j && cd build-dev && ctest -j --output-on-failure`
Expected: 78/78 green (or whatever `tst_benchmark` + `tst_realistic` add to the new fast count). The slow tests are ~9 minutes total — let it run.

- [ ] **Step 2: Update `docs/TODO.md`.**

In the `## 2026-04-29 — Incremental parser + old-leaf deletion (this branch)` section, append a new bullet to the "Work landed on `exploration/new-foundation`" list:

```markdown
- `<short-hash>` — Inline-tree reuse (Phase 2 of incremental parsing). After
  block-tree incremental reparse, `parseIncremental` matches new inline
  regions against shifted-old ranges and reuses unchanged trees. Public
  `TreeSitterParser::inlineTreeReuseCount()` exposes per-call reuse for
  benchmarks. 6 new tests in `tst_incremental_parse` cover the reuse
  paths (single-paragraph edit, edit-inside-region, edits-in-two-regions,
  no-edits, plus two counter-scaffolding tests).
```

(Use the actual short hashes from `git log --oneline -3` for the three commits this plan landed.)

In the "### Open follow-ups (priority order)" subsection, delete bullet **1** ("Inline-tree reuse (Phase 2 of incremental parsing)") and renumber **2** → **1** and **3** → **2**.

- [ ] **Step 3: Commit the TODO update.**

```bash
git add docs/TODO.md
git commit -m "docs: TODO — Phase 2 inline-tree reuse landed"
```

---

## Self-review notes

- **Spec coverage:** TODO follow-up #1 names "diff inline-region byte ranges against the prior parse; reuse `m_inlineTrees[i]` for regions whose byte range is unchanged." Task 2 implements that exactly via `ShiftedRange` matching. Counter API is justified — without it, we cannot test reuse-vs-no-op (fingerprint equivalence is necessary but insufficient; it would pass even for a no-op).
- **Type consistency:** `m_lastInlineReuseCount` (member), `inlineTreeReuseCount()` (getter), `sortedEdits` (renamed from `sorted` to clarify when lifted to function scope), `oldInlineRanges` / `oldInlineTrees` / `newInlineRanges` (parallel naming). All consistent across tasks.
- **Risk:** the failure path in Task 2 Step 4 restores `m_inlineTrees` from `oldInlineTrees` if `ts_parser_parse_string` returns null. This keeps the parser self-consistent on a partial failure. We do not own the `oldInlineTrees` pointers in that case — they are still alive and now back in `m_inlineTrees` to be cleaned up by destructor or next call.
- **Frequency of commits:** 3 functional commits + 1 docs commit. Each is independently revertible.
