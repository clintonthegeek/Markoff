# Per-Item ListItem Blocks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the "one block = one whole list of multi-line text"
compromise with "one block = one list item" per the corrective spec
`docs/specs/2026-05-06-per-item-listitem-blocks-design.md`. Parser emits
one `TopLevelBlock::Kind::ListItem` per `list_item` node; foundation
materializes one CRDT block per item with marker/indent/etc. as attrs;
live-render dispatches structural keys against per-item granularity with
caller-driven renumbering.

**Architecture:** Bottom-up — parser-side first (the data source),
foundation-side next (the materialization + serialization + Cmd helpers),
live-render-side last (consumes the new shape, deletes regex marker
parsing). Renumbering is caller-driven via `Cmd::renumberRunStartingAt`,
called inside the originating action's `UndoLog::Transaction` so undo is
one step.

**Tech Stack:** C++20, Qt 6.8+, tree-sitter-markdown, collabtext CRDT,
QML, ctest. Build directory `build-dev`. Cap parallelism at `-j 8`.

**Predecessor reading:**
1. `docs/d-arc/2026-05-04-d-arc-roadmap.md`
2. `docs/d-arc/collabtext-scope-line.md`
3. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md`
4. `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` ← binding spec for this plan

**File structure (changed/created):**

| File | Action | Why |
|---|---|---|
| `libs/markoff-parser/include/markoff-parser/Document.h` | Modify | Add `Kind::ListItem`, retire `ListTight`/`ListLoose`; add fields `indentDepth`, `markerStyle`, `markerNumber`, `checked`, `looseRun`. |
| `libs/markoff-parser/src/TreeSitterParser.cpp` | Modify | Recurse into `list_item` children, harvest marker info, track indent depth, detect loose-run. |
| `libs/markoff-parser/tests/tst_parser_list_items.cpp` | Create | Verify per-item emission with correct attrs. |
| `libs/markoff-parser/tests/tst_document_top_level_blocks.cpp` | Modify | Update existing assertions that referenced the retired list kinds. |
| `libs/markoff-core/include/markoff-foundation/AttrNames.h` | Modify | Add `MarkerNumber`, `LooseRun`. |
| `libs/markoff-core/src/MarkoffDocument.cpp` | Modify | `mapTopLevelKind` Kind::ListItem→BlockKind::ListItem; `materializeBlocksFromParsedDoc` populates list-item attrs; delete the "deferred" comment; serialization reconstructs source. |
| `libs/markoff-core/include/markoff-foundation/Cmd/D2.h` | Modify | New helpers `insertListItemAfter`, `insertListItemBefore`, `renumberRunStartingAt`. |
| `libs/markoff-core/src/Cmd/D2.cpp` | Modify | Implement those helpers. |
| `libs/markoff-core/tests/d2/tst_d2_list_roundtrip.cpp` | Create | Source → blocks → source byte-equal round-trip. |
| `libs/markoff-core/tests/d2/tst_d2_list_renumber.cpp` | Create | Exercise renumber in mid-run insert/delete/indent-change. |
| `libs/markoff-core/tests/CMakeLists.txt` | Modify | Register the two new test executables. |
| `libs/markoff-live/include/markoff/live-render/LiveBlockModel.h` | Modify | New roles: `MarkerStyleRole`, `MarkerNumberRole`, `IndentLevelRole`, `CheckedRole`, `LooseRunRole`. |
| `libs/markoff-live/src/LiveBlockModel.cpp` | Modify | `data()` switch returns new roles; `roleNames()` registers them. |
| `libs/markoff-live/include/markoff/live-render/BlockRecord.h` | Modify | Drop `headingLevel`/`codeLanguage`-style ad-hoc fields if any are list-shaped; rely on `attrs` for marker/indent/etc. |
| `libs/markoff-live/src/LiveListModelBinding.cpp` | Modify | Revert the `while raw.endsWith('\n')` to `if`; remove ListItem detection from kind-transition equal-op path; promotion path (Paragraph→ListItem) extracts marker + sets attrs + content-only buffer. |
| `libs/markoff-live/src/KindTransition.cpp` | Modify | `inferBlockKind` ListItem regex still fires on Paragraph text (for promotion); no other change. |
| `libs/markoff-live/src/LiveStructuralKeyHandler.cpp` | Modify | ListItem section rewritten: regex/scan/manual-renumber gone; uses `Cmd::insertListItemAfter`, `Cmd::renumberRunStartingAt`, `Cmd::backspaceMerge`, `Cmd::deleteMerge`. |
| `libs/markoff-live/src/LiveCursorState.cpp` | Modify | Delete `requestTextCaretAtRow` immediate-resolve path + `resolvePendingForRow`; `requestTextCaretAtNewRow` and `requestTextCaretAtAnchor` survive. |
| `libs/markoff-live/include/markoff/live-render/LiveCursorState.h` | Modify | Remove the deleted method declaration. |
| `libs/markoff-live/qml/delegates/ListItemDelegate.qml` | Modify | Render marker label from `model.attrs`; padding from `IndentLevelRole`; task-list checkbox toggles `Checked`. |
| `libs/markoff-live/tests/tst_live_render_structural.cpp` | Modify | Replace ListItem tests with per-item-block assertions. |
| `CLAUDE.md`, `docs/d-arc/d-arc-status.md` | Modify | Status updates after dogfood. |

---

## Phase 1: Parser side

### Task 1: New TopLevelBlock fields + Kind::ListItem enum value + classify

**Files:**
- Modify: `libs/markoff-parser/include/markoff-parser/Document.h`
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp`

- [ ] **Step 1: Add Kind::ListItem (keep ListTight/ListLoose for now; retire in Task 3)**

In `Document.h` `TopLevelBlock::Kind`, add `ListItem` after `ListLoose`:

```cpp
enum class Kind {
    Paragraph,
    AtxHeading,
    SetextHeading,
    FencedCodeBlock,
    IndentedCodeBlock,
    BlockQuote,
    ListTight,                  // RETIRED in Task 3 (kept temporarily for build)
    ListLoose,                  // RETIRED in Task 3
    ListItem,                   // NEW: emitted per list_item node (Task 3)
    ThematicBreak,
    HtmlBlock,
    LinkReferenceDefinition,
    Table,
    Other
};
```

- [ ] **Step 2: Add new TopLevelBlock fields**

In `Document.h`, in the `TopLevelBlock` struct, after the existing fields and before `inlineSpans`, add:

```cpp
/// For Kind::ListItem: nesting depth from outer `list` ancestors.
/// 0 = top-level list. Otherwise unset (0).
int indentDepth = 0;

/// For Kind::ListItem: marker shape. One of "dot", "paren", "minus",
/// "plus", "star", "task". Empty for non-ListItem kinds.
QString markerStyle;

/// For Kind::ListItem with markerStyle in {"dot","paren"}: 1-based
/// sequence number from source (e.g. 1 for "1.", 3 for "3)"). 0 otherwise.
int markerNumber = 0;

/// For Kind::ListItem with markerStyle == "task": checkbox state.
/// Always false for non-task items.
bool checked = false;

/// For Kind::ListItem: true iff the parent `list` node was loose
/// (blank lines separated items in source). Always false for non-ListItem.
bool looseRun = false;
```

- [ ] **Step 3: Add "list_item" classifier (preserves "list" classifier for now)**

In `TreeSitterParser.cpp`, in `classifyTopLevelKind`:

```cpp
static TopLevelBlock::Kind classifyTopLevelKind(const char *type)
{
    if (strcmp(type, "paragraph") == 0)                 return TopLevelBlock::Kind::Paragraph;
    if (strcmp(type, "atx_heading") == 0)               return TopLevelBlock::Kind::AtxHeading;
    if (strcmp(type, "setext_heading") == 0)            return TopLevelBlock::Kind::SetextHeading;
    if (strcmp(type, "fenced_code_block") == 0)         return TopLevelBlock::Kind::FencedCodeBlock;
    if (strcmp(type, "indented_code_block") == 0)       return TopLevelBlock::Kind::IndentedCodeBlock;
    if (strcmp(type, "block_quote") == 0)               return TopLevelBlock::Kind::BlockQuote;
    if (strcmp(type, "list") == 0)                      return TopLevelBlock::Kind::ListTight;  // retired Task 3
    if (strcmp(type, "list_item") == 0)                 return TopLevelBlock::Kind::ListItem;   // NEW
    if (strcmp(type, "thematic_break") == 0)            return TopLevelBlock::Kind::ThematicBreak;
    if (strcmp(type, "html_block") == 0)                return TopLevelBlock::Kind::HtmlBlock;
    if (strcmp(type, "link_reference_definition") == 0) return TopLevelBlock::Kind::LinkReferenceDefinition;
    if (strcmp(type, "pipe_table") == 0)                return TopLevelBlock::Kind::Table;
    return TopLevelBlock::Kind::Other;
}
```

- [ ] **Step 4: Build to verify the changes compile**

Run: `cmake --build build-dev --target markoff_parser -j 8`
Expected: build succeeds (no behavior change yet — `collectTopLevelBlocks`
still emits `ListTight` for `list` nodes, never `ListItem`).

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-parser/include/markoff-parser/Document.h libs/markoff-parser/src/TreeSitterParser.cpp
git commit -m "$(cat <<'EOF'
parser: add TopLevelBlock::Kind::ListItem + per-item attribute fields

Adds the enum value and the five new fields (indentDepth, markerStyle,
markerNumber, checked, looseRun) needed for per-item materialization
in the foundation. Wire-up in collectTopLevelBlocks comes in Task 3.

ListTight/ListLoose are kept temporarily for build continuity; Task 3
retires them along with the recursion change.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Write failing tst_parser_list_items

**Files:**
- Create: `libs/markoff-parser/tests/tst_parser_list_items.cpp`
- Modify: `libs/markoff-parser/tests/CMakeLists.txt`

- [ ] **Step 1: Add the test source**

Create `tst_parser_list_items.cpp` with this content:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff-parser/Document.h>

using TLB = Markoff::TopLevelBlock;

class TstParserListItems : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void tight_ordered_emits_one_block_per_item() {
        const QString src = QStringLiteral("1. one\n2. two\n3. three\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(blocks[i].kind, TLB::Kind::ListItem);
            QCOMPARE(blocks[i].markerStyle, QStringLiteral("dot"));
            QCOMPARE(blocks[i].markerNumber, i + 1);
            QCOMPARE(blocks[i].indentDepth, 0);
            QCOMPARE(blocks[i].looseRun, false);
        }
    }

    void unordered_minus_emits_per_item() {
        const QString src = QStringLiteral("- a\n- b\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].markerStyle, QStringLiteral("minus"));
        QCOMPARE(blocks[1].markerStyle, QStringLiteral("minus"));
        QCOMPARE(blocks[0].markerNumber, 0);
    }

    void nested_two_deep_indent() {
        const QString src = QStringLiteral(
            "1. one\n"
            "   - sub a\n"
            "   - sub b\n"
            "2. two\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 4);
        QCOMPARE(blocks[0].indentDepth, 0);
        QCOMPARE(blocks[0].markerStyle, QStringLiteral("dot"));
        QCOMPARE(blocks[1].indentDepth, 1);
        QCOMPARE(blocks[1].markerStyle, QStringLiteral("minus"));
        QCOMPARE(blocks[2].indentDepth, 1);
        QCOMPARE(blocks[3].indentDepth, 0);
        QCOMPARE(blocks[3].markerNumber, 2);
    }

    void task_list_checked_unchecked() {
        const QString src = QStringLiteral(
            "- [ ] one\n"
            "- [x] two\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].markerStyle, QStringLiteral("task"));
        QCOMPARE(blocks[0].checked, false);
        QCOMPARE(blocks[1].markerStyle, QStringLiteral("task"));
        QCOMPARE(blocks[1].checked, true);
    }

    void loose_list_marks_all_items_loose() {
        const QString src = QStringLiteral(
            "1. one\n"
            "\n"
            "2. two\n"
            "\n"
            "3. three\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 3);
        for (const auto &b : blocks)
            QCOMPARE(b.looseRun, true);
    }

    void paren_marker() {
        const QString src = QStringLiteral("1) one\n2) two\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].markerStyle, QStringLiteral("paren"));
        QCOMPARE(blocks[1].markerNumber, 2);
    }

    void byte_range_is_content_only() {
        // For "1. hello", byteStart..byteEnd should cover "hello" (5 bytes),
        // not "1. hello" (8 bytes). The marker is in attrs, not the buffer.
        const QString src = QStringLiteral("1. hello\n");
        auto doc = Markoff::Document::fromMarkdown(src);
        QVERIFY(doc);
        const auto blocks = doc->topLevelBlocks();
        QCOMPARE(blocks.size(), 1);
        const QByteArray bodyUtf8 = src.toUtf8();
        const QByteArray content = bodyUtf8.mid(blocks[0].byteStart,
                                                 blocks[0].byteEnd - blocks[0].byteStart);
        QCOMPARE(content, QByteArrayLiteral("hello"));
    }
};

QTEST_GUILESS_MAIN(TstParserListItems)
#include "tst_parser_list_items.moc"
```

- [ ] **Step 2: Register the test in CMake**

In `libs/markoff-parser/tests/CMakeLists.txt`, add an executable
following the same pattern as `tst_document_top_level_blocks` (look at
that block in the file for the exact form). Add:

```cmake
add_executable(tst_parser_list_items tst_parser_list_items.cpp)
target_link_libraries(tst_parser_list_items PRIVATE markoff-parser Qt6::Test)
add_test(NAME tst_parser_list_items COMMAND tst_parser_list_items)
```

- [ ] **Step 3: Build and run; expect failures**

Run: `cmake --build build-dev --target tst_parser_list_items -j 8 && ctest --test-dir build-dev -R '^tst_parser_list_items$' --output-on-failure`

Expected: test compiles, runs, FAILS — every assertion that expects
`Kind::ListItem` will see `Kind::ListTight` because `collectTopLevelBlocks`
hasn't been rewritten yet.

- [ ] **Step 4: Commit (red state)**

```bash
git add libs/markoff-parser/tests/tst_parser_list_items.cpp libs/markoff-parser/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(parser): tst_parser_list_items — failing baseline for Task 3

Asserts per-item TopLevelBlock emission with markerStyle/markerNumber/
indentDepth/checked/looseRun and content-only byte ranges. Currently
fails because collectTopLevelBlocks still emits one ListTight per list
node. Task 3 makes it pass.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Recursive collectTopLevelBlocks + retire ListTight/ListLoose

**Files:**
- Modify: `libs/markoff-parser/src/TreeSitterParser.cpp`
- Modify: `libs/markoff-parser/include/markoff-parser/Document.h`
- Modify: `libs/markoff-parser/tests/tst_document_top_level_blocks.cpp`

- [ ] **Step 1: Read the current `collectTopLevelBlocks`**

Open `libs/markoff-parser/src/TreeSitterParser.cpp` around line 1235 to
understand the existing recursion shape (sections vs blocks).

- [ ] **Step 2: Add a list_item-aware walker**

Replace the `collectTopLevelBlocks` body with a walker that:
- For `list` nodes: recurse into children, incrementing `currentIndent`
- For `list_item` nodes: emit one `TopLevelBlock` with `Kind::ListItem`,
  populate the new fields by walking the item's children to find the
  marker child + content range
- For `section`: continue recursing (existing behavior)
- Other block-level nodes: emit as before

The control flow:

```cpp
static void collectTopLevelBlocks(TSNode node, const QByteArray &utf8,
                                  QList<TopLevelBlock> &out,
                                  int currentIndent = 0,
                                  bool currentLooseRun = false)
{
    const char *type = ts_node_type(node);

    // Section: recurse into children unchanged.
    if (strcmp(type, "section") == 0 || strcmp(type, "document") == 0) {
        const uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; ++i) {
            collectTopLevelBlocks(ts_node_named_child(node, i), utf8, out,
                                  currentIndent, currentLooseRun);
        }
        return;
    }

    // List: recurse into list_item children with bumped indent.
    if (strcmp(type, "list") == 0) {
        // Tree-sitter-markdown distinguishes loose vs tight via the parent
        // list node's children pattern: a loose list has block_continuation
        // siblings in its list_items. Detect by checking if any list_item
        // child contains an empty line in its source.
        // (Simpler heuristic: re-parse the source range and look for "\n\n";
        // tree-sitter-markdown emits this directly via field info — confirm
        // node type during implementation.)
        const bool isLoose = isListLoose(node, utf8);  // helper below
        const uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; ++i) {
            collectTopLevelBlocks(ts_node_named_child(node, i), utf8, out,
                                  currentIndent + (currentIndent > 0 || out.size() > 0 ? 1 : 1) - 1,
                                  isLoose);
        }
        // Actually: indent passed through is `currentIndent`. The +1 happens
        // when we go INTO a list, which is here. Use `currentIndent + 1`
        // when this list node is a CHILD of a list_item; otherwise 0.
        // Simpler: track depth via a separate parameter. See Step 3.
        return;
    }

    // List item: emit one TopLevelBlock with attrs.
    if (strcmp(type, "list_item") == 0) {
        TopLevelBlock b;
        b.kind = TopLevelBlock::Kind::ListItem;
        b.indentDepth = currentIndent;
        b.looseRun = currentLooseRun;
        // Walk children to find marker + content range
        harvestListItem(node, utf8, b);
        out.append(b);
        // Then recurse into nested `list` children (which are also children
        // of this list_item)
        const uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; ++i) {
            TSNode child = ts_node_named_child(node, i);
            const char *ctype = ts_node_type(child);
            if (strcmp(ctype, "list") == 0) {
                collectTopLevelBlocks(child, utf8, out, currentIndent + 1,
                                       currentLooseRun);
            }
        }
        return;
    }

    // Other block-level nodes — emit as TopLevelBlock per existing logic.
    TopLevelBlock b;
    b.kind = classifyTopLevelKind(type);
    b.byteStart = ts_node_start_byte(node);
    b.byteEnd   = ts_node_end_byte(node);
    // ... existing kind-specific harvest (heading level, code language, etc.)
    out.append(b);
}
```

Note: the existing `collectTopLevelBlocks` at TreeSitterParser.cpp:1235
already handles section recursion and per-kind harvest. The change is:
swap the `list` handling from "emit as one TLB" to "recurse into items"
and add `list_item` handling.

- [ ] **Step 3: Implement harvestListItem helper**

Add a static helper above `collectTopLevelBlocks`:

```cpp
static void harvestListItem(TSNode item, const QByteArray &utf8,
                            TopLevelBlock &b)
{
    // Walk children to find: the marker child, and the content range.
    const uint32_t n = ts_node_named_child_count(item);

    int contentStart = -1;
    int contentEnd   = -1;

    for (uint32_t i = 0; i < n; ++i) {
        TSNode child = ts_node_named_child(item, i);
        const char *ctype = ts_node_type(child);

        // Marker children
        if      (strcmp(ctype, "list_marker_dot") == 0)         b.markerStyle = QStringLiteral("dot");
        else if (strcmp(ctype, "list_marker_parenthesis") == 0) b.markerStyle = QStringLiteral("paren");
        else if (strcmp(ctype, "list_marker_minus") == 0)       b.markerStyle = QStringLiteral("minus");
        else if (strcmp(ctype, "list_marker_plus") == 0)        b.markerStyle = QStringLiteral("plus");
        else if (strcmp(ctype, "list_marker_star") == 0)        b.markerStyle = QStringLiteral("star");
        else if (strcmp(ctype, "task_list_marker_unchecked") == 0) {
            b.markerStyle = QStringLiteral("task");
            b.checked = false;
        }
        else if (strcmp(ctype, "task_list_marker_checked") == 0
              || strcmp(ctype, "task_list_marker_extended") == 0) {
            b.markerStyle = QStringLiteral("task");
            b.checked = true;
        }
        else if (strcmp(ctype, "block_continuation") == 0) {
            // Internal — not content
        }
        else if (strcmp(ctype, "list") == 0) {
            // Nested list — handled by caller's recursion. Not part of
            // this item's content range.
        }
        else {
            // Real content child (paragraph, fenced_code_block, etc.).
            const int s = static_cast<int>(ts_node_start_byte(child));
            const int e = static_cast<int>(ts_node_end_byte(child));
            if (contentStart < 0) contentStart = s;
            contentEnd = e;
        }

        // Extract markerNumber from text for ordered markers
        if (b.markerStyle == QStringLiteral("dot")
         || b.markerStyle == QStringLiteral("paren")) {
            const int ms = static_cast<int>(ts_node_start_byte(child));
            const int me = static_cast<int>(ts_node_end_byte(child));
            const QByteArray markerText = utf8.mid(ms, me - ms);
            // Parse digits
            QByteArray digits;
            for (char c : markerText) {
                if (c >= '0' && c <= '9') digits += c;
                else break;
            }
            if (!digits.isEmpty()) b.markerNumber = digits.toInt();
        }
    }

    if (contentStart < 0) {
        // Empty item. Use the item node's end byte minus any trailing '\n'.
        contentStart = ts_node_end_byte(item);
        contentEnd   = contentStart;
    }
    // Strip trailing '\n' from content
    while (contentEnd > contentStart && utf8[contentEnd - 1] == '\n')
        --contentEnd;

    b.byteStart = contentStart;
    b.byteEnd   = contentEnd;
}
```

- [ ] **Step 4: Implement isListLoose helper**

```cpp
static bool isListLoose(TSNode listNode, const QByteArray &utf8)
{
    // tree-sitter-markdown: loose list iff any list_item contains a
    // block_continuation child (which is emitted only when there's a
    // blank line). Conservative detection.
    const uint32_t n = ts_node_named_child_count(listNode);
    for (uint32_t i = 0; i < n; ++i) {
        TSNode item = ts_node_named_child(listNode, i);
        if (strcmp(ts_node_type(item), "list_item") != 0) continue;
        const uint32_t m = ts_node_named_child_count(item);
        for (uint32_t j = 0; j < m; ++j) {
            TSNode child = ts_node_named_child(item, j);
            if (strcmp(ts_node_type(child), "block_continuation") == 0)
                return true;
        }
    }
    return false;
}
```

- [ ] **Step 5: Run the parser test**

Run: `cmake --build build-dev --target tst_parser_list_items -j 8 && ctest --test-dir build-dev -R '^tst_parser_list_items$' --output-on-failure`

Expected: All 7 assertions pass. If `loose_list_marks_all_items_loose`
fails, refine `isListLoose` against the actual tree-sitter-markdown
loose-list emission (run the test app on a loose-list fixture and dump
the tree to see what node types appear).

- [ ] **Step 6: Update existing top-level-blocks tests**

Run: `ctest --test-dir build-dev -R '^tst_document_top_level_blocks$' --output-on-failure`

Look for failures around `Kind::ListTight` / `Kind::ListLoose`. Update
those specific assertions to expect `Kind::ListItem` (with one block per
item). Example: a test that asserted "1. a\n2. b" produced one ListTight
block now expects two ListItem blocks with `markerNumber = 1, 2`.

- [ ] **Step 7: Retire Kind::ListTight and Kind::ListLoose**

In `Document.h`, remove the `ListTight` and `ListLoose` enum values.
In `TreeSitterParser.cpp::classifyTopLevelKind`, remove the `"list"` →
`Kind::ListTight` line (the new code never calls classifier for `list`
nodes).

- [ ] **Step 8: Build full project and run all parser tests**

Run: `cmake --build build-dev -j 8 && ctest --test-dir build-dev -R '^tst_parser' --output-on-failure -j 8`

Expected: all parser tests pass. Foundation tests will fail at this point
because `mapTopLevelKind` switches over `ListTight`/`ListLoose` — that's
addressed in Task 6.

- [ ] **Step 9: Commit**

```bash
git add libs/markoff-parser/
git commit -m "$(cat <<'EOF'
parser: emit one TopLevelBlock per list_item node, retire ListTight/Loose

collectTopLevelBlocks now recurses into list nodes and emits one
ListItem TopLevelBlock per list_item child, with marker style/number,
indent depth, checked/looseRun attributes harvested from the tree.
Byte range is content-only (marker not included).

ListTight and ListLoose enum values retired — their only consumer was
foundation's mapTopLevelKind, which is updated in the next foundation
task to switch on the new ListItem kind.

Test fixtures updated: tst_parser_list_items asserts the new shape;
tst_document_top_level_blocks updated for per-item assertions.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Phase 2: Foundation side

### Task 4: AttrNames additions

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/AttrNames.h`

- [ ] **Step 1: Add MarkerNumber and LooseRun**

In `AttrNames.h`:

```cpp
namespace Markoff::AttrNames {
    inline const AttrName Level        = "level";        // Heading: int 1–6
    inline const AttrName InfoString   = "infoString";   // CodeBlock: QString
    inline const AttrName MarkerStyle  = "markerStyle";  // ListItem: QString ("dot"|"paren"|"minus"|"plus"|"star"|"task")
    inline const AttrName MarkerNumber = "markerNumber"; // ListItem ordered: int 1+
    inline const AttrName IndentLevel  = "indentLevel";  // ListItem: int 0-based
    inline const AttrName Checked      = "checked";      // ListItem task: bool
    inline const AttrName LooseRun     = "looseRun";     // ListItem: bool — parent list was loose
    inline const AttrName Alt          = "alt";          // Image: QString
    inline const AttrName DisplayMode  = "displayMode";  // Math: bool
    // (preserve any other entries already present — diff against current file)
}  // namespace Markoff::AttrNames
```

- [ ] **Step 2: Build + commit**

Run: `cmake --build build-dev --target markoff-foundation -j 8`
Expected: builds.

```bash
git add libs/markoff-core/include/markoff-foundation/AttrNames.h
git commit -m "foundation: add AttrNames::MarkerNumber and ::LooseRun for per-item ListItem

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 5: tst_d2_list_roundtrip — failing test for per-item materialization

**Files:**
- Create: `libs/markoff-core/tests/d2/tst_d2_list_roundtrip.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/AttrNames.h>

using namespace Markoff;

class TstD2ListRoundtrip : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void tight_ordered_one_block_per_item() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "1. one\n2. two\n3. three\n";
        doc.loadFromMarkdown(src);

        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 3);

        for (int i = 0; i < 3; ++i) {
            QCOMPARE(doc.blockKind(ids[i]), BlockKind::ListItem);
            const auto attrs = doc.blockAttrs(ids[i]);
            QCOMPARE(std::get<QString>(attrs.value(AttrNames::MarkerStyle)),
                     QStringLiteral("dot"));
            QCOMPARE(std::get<int>(attrs.value(AttrNames::MarkerNumber)),
                     i + 1);
            QCOMPARE(std::get<int>(attrs.value(AttrNames::IndentLevel)), 0);
            QCOMPARE(std::get<bool>(attrs.value(AttrNames::LooseRun)), false);
        }

        // Buffer text is content-only (no marker, no leading whitespace)
        QCOMPARE(doc.blockText(ids[0]), QByteArrayLiteral("one"));
        QCOMPARE(doc.blockText(ids[1]), QByteArrayLiteral("two"));
        QCOMPARE(doc.blockText(ids[2]), QByteArrayLiteral("three"));
    }

    void roundtrip_tight_ordered() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "1. one\n2. two\n3. three\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }

    void roundtrip_unordered_minus() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "- a\n- b\n- c\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }

    void roundtrip_nested() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src =
            "1. one\n"
            "   - sub a\n"
            "   - sub b\n"
            "2. two\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }

    void roundtrip_task_list() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "- [ ] one\n- [x] two\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }

    void roundtrip_loose_ordered() {
        MarkoffDocument doc(/*replicaId=*/1);
        const QByteArray src = "1. one\n\n2. two\n\n3. three\n";
        doc.loadFromMarkdown(src);
        QCOMPARE(doc.serializeForSave(), src);
    }
};

QTEST_GUILESS_MAIN(TstD2ListRoundtrip)
#include "tst_d2_list_roundtrip.moc"
```

- [ ] **Step 2: Register in CMake**

Find the `add_executable` lines for other `tst_d2_*` tests in
`libs/markoff-core/tests/CMakeLists.txt` and follow the same pattern:

```cmake
add_executable(tst_d2_list_roundtrip d2/tst_d2_list_roundtrip.cpp)
target_link_libraries(tst_d2_list_roundtrip PRIVATE markoff-foundation Qt6::Test)
add_test(NAME tst_d2_list_roundtrip COMMAND tst_d2_list_roundtrip)
```

- [ ] **Step 3: Build and run; expect failures**

Run: `cmake --build build-dev --target tst_d2_list_roundtrip -j 8 && ctest --test-dir build-dev -R '^tst_d2_list_roundtrip$' --output-on-failure`

Expected: test compiles. May fail at runtime — at this point the
foundation might also fail to build because `mapTopLevelKind` references
the retired `Kind::ListTight`/`ListLoose`. If that's the case, fix the
build by stubbing out `mapTopLevelKind` first (Task 6 will do it
properly):

```cpp
// In MarkoffDocument.cpp, mapTopLevelKind:
case Kind::ListItem: return BlockKind::ListItem;
// REMOVE the ListTight/ListLoose cases
```

Then rebuild. The test runs but fails because
`materializeBlocksFromParsedDoc` still does the whole-list-as-one-block
behavior (or has been broken by parser changes; either way, asserts fail).

- [ ] **Step 4: Commit (red state)**

```bash
git add libs/markoff-core/tests/d2/tst_d2_list_roundtrip.cpp libs/markoff-core/tests/CMakeLists.txt libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
test(foundation): tst_d2_list_roundtrip — failing baseline for Tasks 6+7

Asserts per-item BlockKind::ListItem materialization with attrs and
content-only buffer + byte-equal source roundtrip for tight/unordered/
nested/task-list/loose. Currently fails because materializeBlocksFromParsedDoc
still does whole-list-as-one-block. Tasks 6 and 7 make it pass.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: mapTopLevelKind + materializeBlocksFromParsedDoc

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`

- [ ] **Step 1: Update mapTopLevelKind**

Around line 796 of `MarkoffDocument.cpp`:

```cpp
static BlockKind mapTopLevelKind(TopLevelBlock::Kind k)
{
    using Kind = TopLevelBlock::Kind;
    switch (k) {
    case Kind::Paragraph:             return BlockKind::Paragraph;
    case Kind::AtxHeading:
    case Kind::SetextHeading:         return BlockKind::Heading;
    case Kind::FencedCodeBlock:
    case Kind::IndentedCodeBlock:     return BlockKind::CodeBlock;
    case Kind::BlockQuote:            return BlockKind::Blockquote;
    case Kind::ListItem:              return BlockKind::ListItem;  // NEW
    case Kind::ThematicBreak:         return BlockKind::HorizontalRule;
    case Kind::HtmlBlock:             return BlockKind::Paragraph;  // fall-through (see Phase 8)
    case Kind::Table:                 return BlockKind::Paragraph;  // unchanged
    case Kind::LinkReferenceDefinition: return BlockKind::Paragraph;  // routed elsewhere
    case Kind::Other:                 return BlockKind::Paragraph;
    }
    return BlockKind::Paragraph;
}
```

(Confirm against the existing function — preserve any cases not shown above.)

- [ ] **Step 2: Populate ListItem attrs in materializeBlocksFromParsedDoc**

Around line 838 of the same file, after `BlockKind kind = mapTopLevelKind(tb.kind);`,
extend the kind-specific attr block:

```cpp
// Set kind-specific attrs
if (kind == BlockKind::Heading && tb.headingLevel > 0) {
    d->blockAttrsMap.setWithNextStamp(
        BlockAttrKey{newId, AttrNames::Level}, AttrValue{tb.headingLevel});
}
if (kind == BlockKind::CodeBlock && !tb.codeLanguage.isEmpty()) {
    d->blockAttrsMap.setWithNextStamp(
        BlockAttrKey{newId, AttrNames::InfoString}, AttrValue{tb.codeLanguage});
}
if (kind == BlockKind::ListItem) {
    d->blockAttrsMap.setWithNextStamp(
        BlockAttrKey{newId, AttrNames::IndentLevel}, AttrValue{tb.indentDepth});
    d->blockAttrsMap.setWithNextStamp(
        BlockAttrKey{newId, AttrNames::MarkerStyle}, AttrValue{tb.markerStyle});
    if (tb.markerStyle == QStringLiteral("dot")
     || tb.markerStyle == QStringLiteral("paren")) {
        d->blockAttrsMap.setWithNextStamp(
            BlockAttrKey{newId, AttrNames::MarkerNumber}, AttrValue{tb.markerNumber});
    }
    if (tb.markerStyle == QStringLiteral("task")) {
        d->blockAttrsMap.setWithNextStamp(
            BlockAttrKey{newId, AttrNames::Checked}, AttrValue{tb.checked});
    }
    d->blockAttrsMap.setWithNextStamp(
        BlockAttrKey{newId, AttrNames::LooseRun}, AttrValue{tb.looseRun});
}
```

- [ ] **Step 3: Delete the deferred-unwrapping comment**

Remove (or replace with the new behavior comment) the existing comment
near line 853:

```
// For ListTight/ListLoose, full list source is stored; item-level unwrapping
// is deferred (the v1 parser doesn't expose item byte ranges).
```

Replace with:

```
// For ListItem (one per parser list_item node), the buffer holds the
// item's content only — no marker, no leading indent whitespace, no
// trailing newlines. Marker and indent are reconstructed from attrs at
// serialize time.
```

- [ ] **Step 4: Run the load+iterate parts of the roundtrip test**

Run: `cmake --build build-dev --target tst_d2_list_roundtrip -j 8 && ctest --test-dir build-dev -R 'tight_ordered_one_block_per_item' --output-on-failure`

Expected: `tight_ordered_one_block_per_item` passes. The
`roundtrip_*` tests still fail because `serializeForSave` doesn't yet
reconstruct list source — that's Task 7.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
foundation: per-item ListItem materialization with attrs

mapTopLevelKind: Kind::ListItem → BlockKind::ListItem.
materializeBlocksFromParsedDoc: when materializing a ListItem TLB,
populate IndentLevel + MarkerStyle + MarkerNumber (ordered only) +
Checked (task only) + LooseRun attrs. Buffer holds content-only.

The 'item-level unwrapping is deferred' comment is gone.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: serializeForSave list reconstruction

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`

- [ ] **Step 1: Locate serializeForSave**

```bash
grep -n "serializeForSave\|serializeForSave\b" libs/markoff-core/src/MarkoffDocument.cpp
```

The existing implementation iterates blocks and asks the serializer for
each one's source representation, joined with appropriate separators.
Read the current code to understand the join logic.

- [ ] **Step 2: Implement list-aware reconstruction**

The serializer needs to:
- For a ListItem block, emit `"<indent_spaces><marker> <buffer>\n"`
- For a contiguous run of ListItem blocks with `LooseRun=true`, insert
  an extra `\n` between items
- For a transition out of a ListItem run, no extra blank line
  (CommonMark handles paragraph following list)

Add a helper:

```cpp
namespace {
QByteArray markerForListItem(const QHash<AttrName, AttrValue> &attrs)
{
    const QString style = std::get<QString>(attrs.value(AttrNames::MarkerStyle));
    if (style == QStringLiteral("dot")) {
        const int n = std::get<int>(attrs.value(AttrNames::MarkerNumber));
        return QByteArray::number(n) + ".";
    }
    if (style == QStringLiteral("paren")) {
        const int n = std::get<int>(attrs.value(AttrNames::MarkerNumber));
        return QByteArray::number(n) + ")";
    }
    if (style == QStringLiteral("minus")) return "-";
    if (style == QStringLiteral("plus"))  return "+";
    if (style == QStringLiteral("star"))  return "*";
    if (style == QStringLiteral("task")) {
        const bool c = std::get<bool>(attrs.value(AttrNames::Checked));
        return c ? "- [x]" : "- [ ]";
    }
    return "-";  // fallback
}
}  // namespace
```

In `serializeForSave`, when iterating blocks, for ListItem blocks:

```cpp
const auto attrs = blockAttrs(id);
const int indent = std::get<int>(attrs.value(AttrNames::IndentLevel));
const QByteArray indentBytes(indent * 3, ' ');  // 3 spaces per indent (CommonMark default)
const QByteArray marker = markerForListItem(attrs);
const QByteArray content = blockText(id);

result += indentBytes + marker + " " + content + "\n";

// If both this block and the NEXT block are ListItems and either is
// in a LooseRun, append an extra '\n' between them.
const bool thisLoose = std::get<bool>(attrs.value(AttrNames::LooseRun));
if (nextBlockIsListItem && (thisLoose || nextLoose)) {
    result += "\n";
}
```

- [ ] **Step 3: Run the full roundtrip test**

Run: `cmake --build build-dev --target tst_d2_list_roundtrip -j 8 && ctest --test-dir build-dev -R '^tst_d2_list_roundtrip$' --output-on-failure`

Expected: all 6 tests pass. If `roundtrip_nested` fails, double-check
the indent-spaces multiplier (CommonMark allows variable widths; the
parser observed 3 spaces in the test fixture).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "$(cat <<'EOF'
foundation: serializeForSave reconstructs list items from attrs

For each ListItem block, emit '<indent_spaces><marker> <buffer>\n'.
For contiguous ListItem runs with LooseRun=true, insert a blank line
between items. Marker shape comes from MarkerStyle + MarkerNumber/Checked.

tst_d2_list_roundtrip 6/6 passing.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Cmd helpers — insertListItemAfter, insertListItemBefore, renumberRunStartingAt

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/Cmd/D2.h`
- Modify: `libs/markoff-core/src/Cmd/D2.cpp`

- [ ] **Step 1: Add declarations to D2.h**

In `Cmd/D2.h`, after `changeKind` and before `pasteMarkdown`:

```cpp
/// Insert a new ListItem block after `currentItem`. Copies IndentLevel,
/// MarkerStyle, LooseRun from current; sets MarkerNumber = current+1
/// for ordered styles; sets Checked=false for task. Returns the new
/// block's BlockId. Caller usually follows up with renumberRunStartingAt.
MARKOFF_FOUNDATION_EXPORT BlockId insertListItemAfter(
    MarkoffDocument &doc, BlockId currentItem, UndoLog::Transaction &t);

/// Insert a new ListItem block before `currentItem`. Sets
/// MarkerNumber = current's number (caller renumbers afterward). Same
/// attribute copy semantics as insertListItemAfter.
MARKOFF_FOUNDATION_EXPORT BlockId insertListItemBefore(
    MarkoffDocument &doc, BlockId currentItem, UndoLog::Transaction &t);

/// Renumber the contiguous ordered-list run that contains `anyItemInRun`.
/// A run = consecutive ListItem blocks at the same IndentLevel with the
/// same MarkerStyle in {"dot","paren"}. The first item's MarkerNumber
/// is preserved as the seed; subsequent items get MarkerNumber =
/// seed + offset. No-op for non-ordered styles or single-item runs.
MARKOFF_FOUNDATION_EXPORT void renumberRunStartingAt(
    MarkoffDocument &doc, BlockId anyItemInRun, UndoLog::Transaction &t);
```

- [ ] **Step 2: Implement in D2.cpp**

```cpp
namespace {
QHash<AttrName, AttrValue> copyListItemAttrs(MarkoffDocument &doc, BlockId from)
{
    QHash<AttrName, AttrValue> out;
    const auto src = doc.blockAttrs(from);
    if (src.contains(AttrNames::IndentLevel))
        out[AttrNames::IndentLevel] = src.value(AttrNames::IndentLevel);
    if (src.contains(AttrNames::MarkerStyle))
        out[AttrNames::MarkerStyle] = src.value(AttrNames::MarkerStyle);
    if (src.contains(AttrNames::LooseRun))
        out[AttrNames::LooseRun] = src.value(AttrNames::LooseRun);
    return out;
}
}  // namespace

BlockId insertListItemAfter(MarkoffDocument &doc, BlockId currentItem,
                            UndoLog::Transaction &t)
{
    const auto srcAttrs = copyListItemAttrs(doc, currentItem);
    const BlockId newId = doc.d2InsertBlock(currentItem, BlockKind::ListItem, t);
    for (auto it = srcAttrs.constBegin(); it != srcAttrs.constEnd(); ++it)
        doc.d2SetBlockAttr(newId, it.key(), it.value(), t);

    // For ordered, seed MarkerNumber = current + 1 (renumber pass corrects
    // later items in the run).
    const auto curAttrs = doc.blockAttrs(currentItem);
    const QString style = std::get_if<QString>(&curAttrs[AttrNames::MarkerStyle])
        ? std::get<QString>(curAttrs[AttrNames::MarkerStyle]) : QString();
    if (style == QStringLiteral("dot") || style == QStringLiteral("paren")) {
        const int n = std::get<int>(curAttrs[AttrNames::MarkerNumber]);
        doc.d2SetBlockAttr(newId, AttrNames::MarkerNumber, n + 1, t);
    } else if (style == QStringLiteral("task")) {
        doc.d2SetBlockAttr(newId, AttrNames::Checked, false, t);
    }
    return newId;
}

BlockId insertListItemBefore(MarkoffDocument &doc, BlockId currentItem,
                             UndoLog::Transaction &t)
{
    // Find previous block; insert after it.
    const auto blocks = doc.iterateBlocks();
    BlockId prev{};
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i] == currentItem) {
            if (i > 0) prev = blocks[i - 1];
            break;
        }
    }

    const auto srcAttrs = copyListItemAttrs(doc, currentItem);
    const BlockId newId = doc.d2InsertBlock(prev, BlockKind::ListItem, t);
    for (auto it = srcAttrs.constBegin(); it != srcAttrs.constEnd(); ++it)
        doc.d2SetBlockAttr(newId, it.key(), it.value(), t);

    const auto curAttrs = doc.blockAttrs(currentItem);
    const QString style = std::get_if<QString>(&curAttrs[AttrNames::MarkerStyle])
        ? std::get<QString>(curAttrs[AttrNames::MarkerStyle]) : QString();
    if (style == QStringLiteral("dot") || style == QStringLiteral("paren")) {
        // Same number as current — caller renumbers afterward.
        doc.d2SetBlockAttr(newId, AttrNames::MarkerNumber,
                           std::get<int>(curAttrs[AttrNames::MarkerNumber]), t);
    } else if (style == QStringLiteral("task")) {
        doc.d2SetBlockAttr(newId, AttrNames::Checked, false, t);
    }
    return newId;
}

void renumberRunStartingAt(MarkoffDocument &doc, BlockId anyItemInRun,
                           UndoLog::Transaction &t)
{
    // Confirm the seed item is an ordered ListItem.
    if (doc.blockKind(anyItemInRun) != BlockKind::ListItem) return;
    const auto seedAttrs = doc.blockAttrs(anyItemInRun);
    const QString seedStyle = std::get_if<QString>(&seedAttrs[AttrNames::MarkerStyle])
        ? std::get<QString>(seedAttrs[AttrNames::MarkerStyle]) : QString();
    if (seedStyle != QStringLiteral("dot")
     && seedStyle != QStringLiteral("paren")) return;

    const int seedIndent = std::get<int>(seedAttrs[AttrNames::IndentLevel]);

    const auto blocks = doc.iterateBlocks();
    int seedIdx = -1;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i] == anyItemInRun) { seedIdx = static_cast<int>(i); break; }
    }
    if (seedIdx < 0) return;

    // Walk backward to find the run's first item.
    int firstIdx = seedIdx;
    while (firstIdx > 0) {
        const auto prevAttrs = doc.blockAttrs(blocks[firstIdx - 1]);
        const QString prevStyle = std::get_if<QString>(&prevAttrs[AttrNames::MarkerStyle])
            ? std::get<QString>(prevAttrs[AttrNames::MarkerStyle]) : QString();
        const bool prevIsListItem = (doc.blockKind(blocks[firstIdx - 1]) == BlockKind::ListItem);
        if (!prevIsListItem) break;
        if (prevStyle != seedStyle) break;
        const int prevIndent = std::get<int>(prevAttrs[AttrNames::IndentLevel]);
        if (prevIndent != seedIndent) break;
        --firstIdx;
    }

    // First item's number is the seed.
    const auto firstAttrs = doc.blockAttrs(blocks[firstIdx]);
    const int seedNumber = std::get<int>(firstAttrs[AttrNames::MarkerNumber]);

    // Walk forward, fixing numbers.
    int expected = seedNumber;
    for (size_t i = firstIdx; i < blocks.size(); ++i) {
        const auto attrs = doc.blockAttrs(blocks[i]);
        const QString style = std::get_if<QString>(&attrs[AttrNames::MarkerStyle])
            ? std::get<QString>(attrs[AttrNames::MarkerStyle]) : QString();
        const bool isListItem = (doc.blockKind(blocks[i]) == BlockKind::ListItem);
        if (!isListItem || style != seedStyle) break;
        const int indent = std::get<int>(attrs[AttrNames::IndentLevel]);
        if (indent != seedIndent) break;
        const int actual = std::get<int>(attrs[AttrNames::MarkerNumber]);
        if (actual != expected) {
            doc.d2SetBlockAttr(blocks[i], AttrNames::MarkerNumber, expected, t);
        }
        ++expected;
    }
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build-dev --target markoff-foundation -j 8`
Expected: builds.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Cmd/D2.h libs/markoff-core/src/Cmd/D2.cpp
git commit -m "$(cat <<'EOF'
foundation: Cmd::insertListItemAfter/Before + renumberRunStartingAt

Caller-driven helpers for the per-item ListItem flow. insertListItem*
copies indent/style/looseRun from current and seeds the new item's
marker number. renumberRunStartingAt walks the contiguous same-indent
same-style run from any seed item, finds the first item's number as
the run's seed, and emits MarkerNumber attr edits for any later
mismatches. All operations register on the caller-supplied transaction
for undo coherence (one user action = one UndoEntry).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: tst_d2_list_renumber

**Files:**
- Create: `libs/markoff-core/tests/d2/tst_d2_list_renumber.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/Cmd/D2.h>

using namespace Markoff;

class TstD2ListRenumber : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void no_op_on_already_sequential() {
        MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown("1. one\n2. two\n3. three\n");
        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 3);

        UndoLog::Transaction t(doc.d2UndoLog());
        Cmd::renumberRunStartingAt(doc, ids[1], t);
        // No edits expected; numbers stay 1,2,3
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[0])[AttrNames::MarkerNumber]), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[1])[AttrNames::MarkerNumber]), 2);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[2])[AttrNames::MarkerNumber]), 3);
    }

    void renumber_after_mid_insert() {
        MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown("1. one\n2. two\n3. three\n");
        const auto idsBefore = doc.iterateBlocks();

        UndoLog::Transaction t(doc.d2UndoLog());
        const BlockId newId = Cmd::insertListItemAfter(doc, idsBefore[0], t);
        Cmd::renumberRunStartingAt(doc, newId, t);
        t.~Transaction();  // commit

        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 4);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[0])[AttrNames::MarkerNumber]), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[1])[AttrNames::MarkerNumber]), 2);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[2])[AttrNames::MarkerNumber]), 3);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[3])[AttrNames::MarkerNumber]), 4);
    }

    void renumber_does_not_cross_indent_boundary() {
        MarkoffDocument doc(/*replicaId=*/1);
        // Outer 1, 2 with nested sub-list under 1.
        doc.loadFromMarkdown(
            "1. one\n"
            "   1. sub a\n"
            "   2. sub b\n"
            "2. two\n");
        const auto ids = doc.iterateBlocks();
        QCOMPARE(static_cast<int>(ids.size()), 4);

        // Force outer items to be wrong, sub-list correct
        UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2SetBlockAttr(ids[3], AttrNames::MarkerNumber, 99, t);
        Cmd::renumberRunStartingAt(doc, ids[0], t);
        t.~Transaction();

        // Outer renumbered to 1, 2; inner unchanged 1, 2
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[0])[AttrNames::MarkerNumber]), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[1])[AttrNames::MarkerNumber]), 1);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[2])[AttrNames::MarkerNumber]), 2);
        QCOMPARE(std::get<int>(doc.blockAttrs(ids[3])[AttrNames::MarkerNumber]), 2);
    }

    void renumber_ignores_non_ordered_runs() {
        MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown("- a\n- b\n- c\n");
        const auto ids = doc.iterateBlocks();

        UndoLog::Transaction t(doc.d2UndoLog());
        Cmd::renumberRunStartingAt(doc, ids[0], t);
        // No-op: unordered. Should not crash; should not edit anything.
        for (const auto &id : ids)
            QVERIFY(!doc.blockAttrs(id).contains(AttrNames::MarkerNumber));
    }
};

QTEST_GUILESS_MAIN(TstD2ListRenumber)
#include "tst_d2_list_renumber.moc"
```

(Note: the `t.~Transaction()` calls force commit before the test reads
attrs. If `UndoLog::Transaction` only commits on RAII-destruction, those
explicit-destructor calls are equivalent to letting `t` go out of scope.
Restructure tests with scoped blocks if the explicit destructor call is
non-portable.)

- [ ] **Step 2: Register in CMake** (same pattern as Task 5).

- [ ] **Step 3: Build and run**

Run: `cmake --build build-dev --target tst_d2_list_renumber -j 8 && ctest --test-dir build-dev -R '^tst_d2_list_renumber$' --output-on-failure`

Expected: 4/4 pass.

- [ ] **Step 4: Run all foundation tests to confirm no regressions**

Run: `ctest --test-dir build-dev -R '^tst_d2_' --output-on-failure -j 8`

Expected: all `tst_d2_*` tests pass. Existing tests that probed
whole-list-as-one-block behavior (if any) need updating to the new
shape — fix those as you find them.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/tests/d2/tst_d2_list_renumber.cpp libs/markoff-core/tests/CMakeLists.txt libs/markoff-core/tests/d2/
git commit -m "$(cat <<'EOF'
test(foundation): tst_d2_list_renumber — exercise renumberRunStartingAt

Four tests: no-op on sequential, mid-insert renumber, indent boundary
isolation, no-op on unordered. Foundation-level tests for per-item
ListItem now complete (roundtrip + renumber).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Phase 3: Live-render side

### Task 10: LiveBlockModel new roles

**Files:**
- Modify: `libs/markoff-live/include/markoff/live-render/LiveBlockModel.h`
- Modify: `libs/markoff-live/src/LiveBlockModel.cpp`

- [ ] **Step 1: Add role enum values**

In `LiveBlockModel.h`, in the `Role` enum:

```cpp
enum Role {
    KindRole         = Qt::UserRole + 1,
    TextRole,
    BlockAnchorRole,
    InlineSpansRole,
    BlockAttrsRole,
    HeadingLevelRole,
    CodeLanguageRole,
    // NEW for per-item ListItem
    MarkerStyleRole,
    MarkerNumberRole,
    IndentLevelRole,
    CheckedRole,
    LooseRunRole,
};
```

- [ ] **Step 2: Update roleNames + data() in cpp**

In `LiveBlockModel.cpp`, in `roleNames()`:

```cpp
QHash<int, QByteArray> LiveBlockModel::roleNames() const
{
    return {
        // ... existing entries ...
        { MarkerStyleRole,  "markerStyle"  },
        { MarkerNumberRole, "markerNumber" },
        { IndentLevelRole,  "indentLevel"  },
        { CheckedRole,      "checked"      },
        { LooseRunRole,     "looseRun"     },
    };
}
```

In `data()`, add cases that read from `m_rows[row].attrs`:

```cpp
case MarkerStyleRole: {
    auto it = rec.attrs.find(Markoff::AttrNames::MarkerStyle);
    if (it != rec.attrs.end())
        if (const QString *v = std::get_if<QString>(&it.value())) return *v;
    return QString();
}
case MarkerNumberRole: {
    auto it = rec.attrs.find(Markoff::AttrNames::MarkerNumber);
    if (it != rec.attrs.end())
        if (const int *v = std::get_if<int>(&it.value())) return *v;
    return 0;
}
case IndentLevelRole: {
    auto it = rec.attrs.find(Markoff::AttrNames::IndentLevel);
    if (it != rec.attrs.end())
        if (const int *v = std::get_if<int>(&it.value())) return *v;
    return 0;
}
case CheckedRole: {
    auto it = rec.attrs.find(Markoff::AttrNames::Checked);
    if (it != rec.attrs.end())
        if (const bool *v = std::get_if<bool>(&it.value())) return *v;
    return false;
}
case LooseRunRole: {
    auto it = rec.attrs.find(Markoff::AttrNames::LooseRun);
    if (it != rec.attrs.end())
        if (const bool *v = std::get_if<bool>(&it.value())) return *v;
    return false;
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build-dev --target markoff_live_render -j 8`
Expected: builds.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/include/markoff/live-render/LiveBlockModel.h libs/markoff-live/src/LiveBlockModel.cpp
git commit -m "live-render: LiveBlockModel roles for ListItem marker/indent/checked/looseRun

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 11: LiveListModelBinding cleanup — revert chop loop, simplify kind transition

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

- [ ] **Step 1: Revert the multi-trailing-`\n` strip**

In `LiveListModelBinding.cpp` around line 140:

```cpp
QByteArray raw = doc->blockText(id);
// One trailing '\n' is the block delimiter convention. Per-item ListItem
// blocks no longer need the multi-strip workaround that the whole-list-as-
// block model required.
if (raw.endsWith('\n'))
    raw.chop(1);
r.text = QString::fromUtf8(raw);
```

- [ ] **Step 2: Remove ListItem detection from kind-transition Equal-op path**

In the kind-transition loop, when `inferred == BlockKind::ListItem` and
`rec.kind == BlockKind::ListItem`, skip — the parser already set the
kind, no transition needed:

```cpp
if (inferred == rec.kind) {
    // ... existing heading-level check ...
    continue;
}
```

(This part was already correct — verify no regression.)

The Paragraph→ListItem promotion case is handled in Task 13.

- [ ] **Step 3: Build**

Run: `cmake --build build-dev --target markoff_live_render -j 8`
Expected: builds.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "$(cat <<'EOF'
live-render: revert multi-trailing-\\n strip — per-item blocks don't need it

The 'while raw.endsWith('\n')' loop was a band-aid for whole-list-as-one-
block where the parser's byte range included multiple trailing newlines.
Per-item ListItem blocks have content-only buffers; one trailing-\\n
is the block-delimiter convention.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: LiveStructuralKeyHandler ListItem rewrite

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`

- [ ] **Step 1: Delete the existing ListItem section**

In `LiveStructuralKeyHandler.cpp`, find and delete the entire ListItem
handler block (currently ~150 LOC including the marker-prefix regex,
multi-line scan, atLineStart branch, manual renumber loop). Keep the
Backspace, Delete, Tab handlers' shape but rewrite their bodies in
Step 2.

- [ ] **Step 2: Add the new ListItem handlers**

```cpp
// ---- ListItem handlers (per-item blocks per docs/specs/2026-05-06) ----

// Enter: insert new ListItem within the same run, renumber, position cursor.
m_handlers[BlockKind::ListItem][Qt::Key_Return] =
m_handlers[BlockKind::ListItem][Qt::Key_Enter]  = [](const Ctx &c) -> HR {
    const Markoff::BlockId id(c.blockAnchor);
    const QString &content = c.blockText;  // model.text — content only

    const auto attrs = c.document->blockAttrs(id);
    const int indent = attrs.contains(Markoff::AttrNames::IndentLevel)
        ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;
    const bool isEmpty = content.isEmpty();

    UndoLog::Transaction t(c.document->d2UndoLog());

    if (isEmpty && indent > 0) {
        // Outdent — item joins outer run.
        c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                    indent - 1, t);
        Markoff::Cmd::renumberRunStartingAt(*c.document, id, t);
        c.cursorState->requestTextCaretAtRow(c.blockIndex, 0);
        return HR::Handled;
    }
    if (isEmpty && indent == 0) {
        // Exit list — demote to Paragraph.
        c.document->d2SetBlockKind(id, Markoff::BlockKind::Paragraph, t);
        // Clear ListItem-specific attrs
        c.document->d2SetBlockAttr(id, Markoff::AttrNames::MarkerStyle, QString(), t);
        // (MarkerNumber/IndentLevel/Checked/LooseRun read by no Paragraph code;
        //  leaving them is harmless. Could be cleared with d2RemoveAttr if
        //  added later.)
        c.cursorState->requestTextCaretAtRow(c.blockIndex, 0);
        return HR::Handled;
    }

    // Non-empty cases: split or append-after.
    const int qtPos = c.qtPos;
    if (qtPos == 0) {
        // Insert empty item BEFORE current; cursor stays in current row.
        const Markoff::BlockId newId = Markoff::Cmd::insertListItemBefore(
            *c.document, id, t);
        Q_UNUSED(newId)
        Markoff::Cmd::renumberRunStartingAt(*c.document, id, t);
        c.cursorState->requestTextCaretAtRow(c.blockIndex + 1, 0);
        return HR::Handled;
    }
    if (qtPos == content.length()) {
        // Append empty item AFTER current.
        const Markoff::BlockId newId = Markoff::Cmd::insertListItemAfter(
            *c.document, id, t);
        Markoff::Cmd::renumberRunStartingAt(*c.document, newId, t);
        c.cursorState->requestTextCaretAtNewRow(c.blockIndex + 1, 0);
        return HR::Handled;
    }
    // Mid-content split: tail moves to new item below.
    const QByteArray contentUtf8 = content.toUtf8();
    const QByteArray prefixUtf8 = content.left(qtPos).toUtf8();
    const QByteArray suffixUtf8 = content.mid(qtPos).toUtf8();
    const uint32_t byteOff = static_cast<uint32_t>(prefixUtf8.size());
    const uint32_t tailBytes = static_cast<uint32_t>(
        contentUtf8.size() - prefixUtf8.size());

    // Truncate current
    c.document->d2ApplyBufferEdit(id, byteOff, tailBytes, QByteArray{}, t);
    // Insert new after with suffix as content
    const Markoff::BlockId newId = Markoff::Cmd::insertListItemAfter(
        *c.document, id, t);
    if (!suffixUtf8.isEmpty())
        c.document->d2ApplyBufferEdit(newId, 0, 0, suffixUtf8, t);
    Markoff::Cmd::renumberRunStartingAt(*c.document, newId, t);
    c.cursorState->requestTextCaretAtNewRow(c.blockIndex + 1, 0);
    return HR::Handled;
};

// Tab: increase indent. Renumber both old and new runs.
m_handlers[BlockKind::ListItem][Qt::Key_Tab] = [](const Ctx &c) -> HR {
    const Markoff::BlockId id(c.blockAnchor);
    const auto attrs = c.document->blockAttrs(id);
    const int indent = attrs.contains(Markoff::AttrNames::IndentLevel)
        ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;
    UndoLog::Transaction t(c.document->d2UndoLog());
    const bool shift = (c.modifiers & Qt::ShiftModifier) != 0;
    const int newIndent = shift ? std::max(0, indent - 1)
                                 : std::min(6, indent + 1);
    if (newIndent == indent) return HR::Handled;
    c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel, newIndent, t);
    Markoff::Cmd::renumberRunStartingAt(*c.document, id, t);
    // Also renumber the previous-run-this-item-just-left, by finding any
    // sibling at the old indent in the same vicinity. Simplest: pick the
    // item at row blockIndex-1 if it's a ListItem at the old indent.
    if (c.blockIndex > 0) {
        const auto prevId = c.model->recordAt(c.blockIndex - 1).blockAnchor;
        Markoff::Cmd::renumberRunStartingAt(*c.document,
            Markoff::BlockId(prevId), t);
    }
    c.cursorState->requestTextCaretAtRow(c.blockIndex, c.qtPos);
    return HR::Handled;
};

// Backspace at qtPos=0: indent>0 → outdent; else merge with previous block.
m_handlers[BlockKind::ListItem][Qt::Key_Backspace] = [](const Ctx &c) -> HR {
    if (c.qtPos != 0) return HR::NotHandled;  // TextEdit handles
    const Markoff::BlockId id(c.blockAnchor);
    const auto attrs = c.document->blockAttrs(id);
    const int indent = attrs.contains(Markoff::AttrNames::IndentLevel)
        ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;

    UndoLog::Transaction t(c.document->d2UndoLog());
    if (indent > 0) {
        c.document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                    indent - 1, t);
        Markoff::Cmd::renumberRunStartingAt(*c.document, id, t);
        c.cursorState->requestTextCaretAtRow(c.blockIndex, 0);
        return HR::Handled;
    }
    if (c.blockIndex == 0) return HR::NotHandled;
    const int joinQtPos = c.model->recordAt(c.blockIndex - 1).text.length();
    auto result = Markoff::Cmd::backspaceMerge(*c.document, c.blockAnchor);
    if (result.mergedInto.isNull()) return HR::NotHandled;
    Markoff::Cmd::renumberRunStartingAt(*c.document,
        Markoff::BlockId(result.mergedInto), t);
    c.cursorState->requestTextCaretAtAnchor(result.mergedInto, joinQtPos);
    return HR::Handled;
};

// Delete at qtPos=length: merge next block in.
m_handlers[BlockKind::ListItem][Qt::Key_Delete] = [](const Ctx &c) -> HR {
    if (c.qtPos < c.blockText.length()) return HR::NotHandled;
    if (c.blockIndex >= c.model->rowCount() - 1) return HR::NotHandled;
    UndoLog::Transaction t(c.document->d2UndoLog());
    Markoff::Cmd::deleteMerge(*c.document, c.blockAnchor);
    Markoff::Cmd::renumberRunStartingAt(*c.document, c.blockAnchor, t);
    c.cursorState->requestTextCaretAtAnchor(c.blockAnchor, c.qtPos);
    return HR::Handled;
};
```

- [ ] **Step 3: Verify no regex/scan remains**

```bash
grep -n "kMarker\|kOrd\|markerPrefix" libs/markoff-live/src/LiveStructuralKeyHandler.cpp
```
Expected: no matches in the ListItem section. (The Heading section may still
have its own regex; that's unrelated.)

- [ ] **Step 4: Build**

Run: `cmake --build build-dev --target markoff_live_render -j 8`
Expected: builds.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/LiveStructuralKeyHandler.cpp
git commit -m "$(cat <<'EOF'
live-render: rewrite ListItem structural handlers for per-item blocks

Enter / Backspace / Delete / Tab now use Cmd::insertListItemAfter/Before,
Cmd::backspaceMerge, Cmd::deleteMerge, Cmd::renumberRunStartingAt, with
all ops on a single transaction (one user action = one undo step).

DELETED: marker-prefix regex (kMarker), ordered-marker regex (kOrd),
multi-line lineStart scan, atLineStart branch, manual renumber loop,
multi-trailing-\\n reconstruction. ~150 LOC removed.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 13: Paragraph→ListItem promotion path

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`

- [ ] **Step 1: Find the kind-transition site**

In `onD2Changed`, locate the `Cmd::changeKind` call that fires when
`inferred != stored`. Currently it just calls `changeKind` and returns.
Per-item promotion needs to additionally extract the marker from the
buffer text and set list-specific attrs.

- [ ] **Step 2: Replace the changeKind call for ListItem promotion**

When `fk == Markoff::BlockKind::ListItem` (and the previous kind was not
ListItem), we need to:
1. Parse the marker out of `rec.text` (e.g., `"1. one"` → marker `"1.",
   number 1, content `"one"`).
2. Truncate the buffer to content-only (remove the marker prefix).
3. Set MarkerStyle, MarkerNumber, IndentLevel, LooseRun (default false).
4. Change kind.
5. Renumber the run.

Code:

```cpp
if (fk == Markoff::BlockKind::ListItem) {
    static const QRegularExpression kPromoteMarker(
        QStringLiteral(R"(^([ \t]{0,3})(\d{1,9})([.)]) (.*)$|^([ \t]{0,3})([-*+]) (.*)$)"));
    auto pm = kPromoteMarker.match(rec.text);
    if (!pm.hasMatch()) {
        // Shouldn't happen — inferBlockKind already matched the same regex.
        // Fall through to default changeKind without attrs.
        Markoff::Cmd::changeKind(*doc, Markoff::BlockId(rec.blockAnchor),
                                  fk, {}, {});
        return;
    }

    QString style;
    int number = 0;
    QString content;
    int leadingSpaces = 0;
    if (!pm.captured(2).isEmpty()) {
        // Ordered: 1. or 1)
        leadingSpaces = pm.captured(1).size();
        number = pm.captured(2).toInt();
        style = (pm.captured(3) == QStringLiteral(".")) ? QStringLiteral("dot")
                                                         : QStringLiteral("paren");
        content = pm.captured(4);
    } else {
        // Unordered: -, *, +
        leadingSpaces = pm.captured(5).size();
        const QString c = pm.captured(6);
        style = (c == QStringLiteral("-")) ? QStringLiteral("minus")
              : (c == QStringLiteral("*")) ? QStringLiteral("star")
                                            : QStringLiteral("plus");
        content = pm.captured(7);
    }
    const int indent = leadingSpaces / 2;  // CommonMark allows 0-3 leading; 2 is the typical step

    UndoLog::Transaction t(doc->d2UndoLog());

    // Replace buffer content with content-only (parsed marker stripped)
    const QByteArray oldBuf = doc->blockText(Markoff::BlockId(rec.blockAnchor));
    doc->d2ApplyBufferEdit(Markoff::BlockId(rec.blockAnchor),
                            0, static_cast<uint32_t>(oldBuf.size()),
                            content.toUtf8(), t);
    // Set kind
    doc->d2SetBlockKind(Markoff::BlockId(rec.blockAnchor),
                         Markoff::BlockKind::ListItem, t);
    // Set attrs
    doc->d2SetBlockAttr(Markoff::BlockId(rec.blockAnchor),
                         Markoff::AttrNames::IndentLevel, indent, t);
    doc->d2SetBlockAttr(Markoff::BlockId(rec.blockAnchor),
                         Markoff::AttrNames::MarkerStyle, style, t);
    if (style == QStringLiteral("dot") || style == QStringLiteral("paren"))
        doc->d2SetBlockAttr(Markoff::BlockId(rec.blockAnchor),
                             Markoff::AttrNames::MarkerNumber, number, t);
    doc->d2SetBlockAttr(Markoff::BlockId(rec.blockAnchor),
                         Markoff::AttrNames::LooseRun, false, t);
    // Renumber the (newly-formed) run
    Markoff::Cmd::renumberRunStartingAt(*doc, Markoff::BlockId(rec.blockAnchor), t);

    return;  // re-fire onD2Changed will see the new kind and skip transition
}
```

- [ ] **Step 3: Build and dogfood-test the promotion**

Run the test app with a paragraph file. Type `1. ` then text. Verify
the block flips to ListItem with marker `1.` rendered as label and
buffer holding just the typed content.

(Manual verification — no automated test added here. The behavior is
covered in tst_live_render_structural's promotion test added in Task 16.)

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "$(cat <<'EOF'
live-render: Paragraph→ListItem promotion strips marker, sets attrs

When kind-transition detects a Paragraph block whose text matches a list
marker prefix, promote: parse the marker (style + number + indent),
truncate the buffer to content-only, set ListItem attrs, change kind,
and renumber the resulting run. All in one transaction.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 14: LiveCursorState collapse

**Files:**
- Modify: `libs/markoff-live/src/LiveCursorState.cpp`
- Modify: `libs/markoff-live/include/markoff/live-render/LiveCursorState.h`

- [ ] **Step 1: Find all callers of requestTextCaretAtRow**

```bash
grep -rn "requestTextCaretAtRow" libs/markoff-live/
```

After Task 12, the surviving callers are likely only the heading
level-change gesture and a couple of HR/Image edge cases. Audit each:
if the caller is fine waiting for a structural signal, switch to
`requestTextCaretAtNewRow` or `requestTextCaretAtAnchor`. If the caller
genuinely needs immediate-resolve, document why and keep it.

- [ ] **Step 2: If all callers can be collapsed, delete the API**

If audit confirms no remaining callers need immediate-resolve:
- Remove `requestTextCaretAtRow` declaration from `LiveCursorState.h`
- Remove its implementation in `LiveCursorState.cpp`
- Remove `resolvePendingForRow` if it has no other callers

If callers still need it: leave the API in place but add a comment
noting that the only legitimate use is for "row exists with current
text" (i.e., not after a CRDT edit that changes text).

- [ ] **Step 3: Build + run live-render tests**

Run: `cmake --build build-dev --target tst_live_render_cursor -j 8 && ctest --test-dir build-dev -R '^tst_live_render_cursor$' --output-on-failure`

Expected: all cursor tests pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/src/LiveCursorState.cpp libs/markoff-live/include/markoff/live-render/LiveCursorState.h
git commit -m "$(cat <<'EOF'
live-render: LiveCursorState collapse — remove requestTextCaretAtRow

requestTextCaretAtRow's immediate-resolve semantics were a band-aid for
in-block multi-line edits in the whole-list-as-block model. Per-item
ListItem blocks don't generate that race; structural signals fire on
Enter (new row) and merges (row removed). Two APIs survive:
requestTextCaretAtNewRow (resolves on structuralRowsInserted) and
requestTextCaretAtAnchor (resolves on either signal).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

(If audit shows callers must keep the API, replace this commit with one
that documents the residual valid uses instead.)

---

### Task 15: ListItemDelegate.qml — marker label, indent, task checkbox

**Files:**
- Modify: `libs/markoff-live/qml/delegates/ListItemDelegate.qml`

- [ ] **Step 1: Replace the delegate body**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

/// Per-item ListItem delegate. Marker is rendered as a non-editable
/// label (or task-list checkbox) to the left of the TextEdit, populated
/// from model.markerStyle / model.markerNumber / model.checked. Indent
/// is rendered as left padding (3 spaces of width per indent level).
/// Buffer holds content only; the marker is reconstructed at serialize.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding:
        ListView.view ? ListView.view.binding : null
    readonly property var selectionView:
        liveBinding ? liveBinding.selectionView : null

    readonly property int indentLevel: model.indentLevel || 0
    readonly property string markerStyle: model.markerStyle || ""
    readonly property int markerNumber: model.markerNumber || 0
    readonly property bool checked: model.checked || false

    readonly property string markerText: {
        if (markerStyle === "dot")   return markerNumber + "."
        if (markerStyle === "paren") return markerNumber + ")"
        if (markerStyle === "minus") return "-"
        if (markerStyle === "plus")  return "+"
        if (markerStyle === "star")  return "*"
        if (markerStyle === "task")  return checked ? "[x]" : "[ ]"
        return "•"  // fallback
    }

    readonly property int indentPx: 8 + indentLevel * 24

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
        text: model.text
    }

    // Marker label / checkbox to the left
    Text {
        id: markerLabel
        anchors {
            left: parent.left
            top: parent.top
        }
        leftPadding: root.indentPx
        topPadding: 2
        text: root.markerText
        font.family: "monospace"
        font.pixelSize: 14
        color: palette.text

        MouseArea {
            visible: root.markerStyle === "task"
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (!root.liveBinding || !root.liveBinding.document) return
                root.liveBinding.document.toggleListItemChecked(model.blockAnchor)
            }
        }
    }

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: markerLabel.implicitWidth + 12
        rightPadding: 8
        topPadding: 2
        bottomPadding: 2
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        font.pixelSize: 14
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (!handler) { event.accepted = false; return }
            const k = event.key
            if (k !== Qt.Key_Return && k !== Qt.Key_Enter
                    && k !== Qt.Key_Backspace && k !== Qt.Key_Delete
                    && k !== Qt.Key_Tab) {
                return
            }
            const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
                edit.cursorPosition, edit.selectionStart === edit.selectionEnd,
                model.text)
            event.accepted = handled
        }

        function applySelection() {
            const sv = root.selectionView
            if (!sv) { deselect(); return }
            const r = sv.rangeForBlock(model.index)
            if (!r || r.x < 0) { deselect(); return }
            select(r.x, Math.min(r.y, length))
        }

        Connections {
            target: root.selectionView
            function onSelectionChanged() { edit.applySelection() }
        }

        Connections {
            target: root.liveBinding ? root.liveBinding.cursorState : null
            function onCursorChanged() {
                const cs = root.liveBinding ? root.liveBinding.cursorState : null
                if (cs && cs.focusedAnchorRow === root.modelIndex && cs.focusedQtPos >= 0)
                    edit.cursorPosition = cs.focusedQtPos
            }
        }
    }

    function positionAt(x, y) {
        return edit.positionAt(x - edit.leftPadding, y - edit.topPadding)
    }

    function focusEditAt(qtPos) {
        edit.forceActiveFocus()
        if (qtPos >= 0 && qtPos <= edit.length)
            edit.cursorPosition = qtPos
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex)
            Qt.callLater(function() { focusEditAt(cs.focusedQtPos >= 0 ? cs.focusedQtPos : 0) })
    }
}
```

- [ ] **Step 2: Add `MarkoffDocument::toggleListItemChecked`**

In `MarkoffDocument.h`, declare:
```cpp
Q_INVOKABLE void toggleListItemChecked(Markoff::BlockAnchor anchor);
```

In `MarkoffDocument.cpp`:
```cpp
void MarkoffDocument::toggleListItemChecked(BlockAnchor anchor)
{
    const BlockId id(anchor);
    if (blockKind(id) != BlockKind::ListItem) return;
    const auto attrs = blockAttrs(id);
    if (!attrs.contains(AttrNames::Checked)) return;
    const bool oldValue = std::get<bool>(attrs.value(AttrNames::Checked));
    UndoLog::Transaction t(d2UndoLog());
    d2SetBlockAttr(id, AttrNames::Checked, !oldValue, t);
}
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build-dev -j 8 && ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8`

Expected: live-render tests pass except `tst_live_render_structural`'s
ListItem cases (those need rewriting in Task 16).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/qml/delegates/ListItemDelegate.qml libs/markoff-core/src/MarkoffDocument.cpp libs/markoff-core/include/markoff-foundation/MarkoffDocument.h
git commit -m "$(cat <<'EOF'
live-render: ListItemDelegate renders marker label + indent + task checkbox

The QML delegate now reads model.markerStyle / .markerNumber / .checked /
.indentLevel and renders the marker as a non-editable Text to the left
of the TextEdit. Task-list items render as [x] / [ ] and toggle on click
via the new MarkoffDocument::toggleListItemChecked Q_INVOKABLE.

The TextEdit shows model.text (content only). Cursor positions are
relative to content — no marker in the editing buffer.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task 16: Rewrite tst_live_render_structural ListItem tests

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_structural.cpp`

- [ ] **Step 1: Delete obsolete tests**

Remove these tests (they probed whole-list-as-block behavior):
- `list_item_enter_creates_new_list_item`
- `list_item_enter_six_item_ordered_list_at_end_appends_in_block`
- `list_item_enter_compound_six_items_then_three_enters_no_new_blocks`
- `list_item_enter_renumbers_subsequent_ordered_items`
- `list_item_enter_at_line_start_renumbers_too`
- `list_item_load_with_double_trailing_newline_has_no_phantom_line`
- `list_item_probe_trailing_newline_in_buffer`

Keep but rewrite:
- `list_item_enter_on_empty_exits_list`
- `list_item_tab_indents`

- [ ] **Step 2: Add new per-item-block tests**

```cpp
void list_item_loads_one_block_per_item() {
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n3. three\n", 3));
    QCOMPARE(binding.model()->rowCount(), 3);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(binding.model()->data(binding.model()->index(i, 0),
                 LiveBlockModel::KindRole).toString(),
                 QStringLiteral("list-item"));
}

void list_item_enter_at_end_creates_next_item_and_renumbers() {
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n", 2));

    // Enter at end of "two" (qtPos=3, content is "two")
    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, 1, 3, true, QStringLiteral("two"));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    QTRY_COMPARE(binding.model()->rowCount(), 3);
    QCOMPARE(binding.model()->data(binding.model()->index(2, 0),
             LiveBlockModel::MarkerNumberRole).toInt(), 3);
}

void list_item_enter_at_end_of_middle_item_renumbers_below() {
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n3. three\n", 3));

    // Enter at end of "one" (block 0)
    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, 0, 3, true, QStringLiteral("one"));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    QTRY_COMPARE(binding.model()->rowCount(), 4);
    // Now: "1. one", "2. (new empty)", "3. two", "4. three"
    QCOMPARE(binding.model()->data(binding.model()->index(0, 0),
             LiveBlockModel::MarkerNumberRole).toInt(), 1);
    QCOMPARE(binding.model()->data(binding.model()->index(1, 0),
             LiveBlockModel::MarkerNumberRole).toInt(), 2);
    QCOMPARE(binding.model()->data(binding.model()->index(2, 0),
             LiveBlockModel::MarkerNumberRole).toInt(), 3);
    QCOMPARE(binding.model()->data(binding.model()->index(3, 0),
             LiveBlockModel::MarkerNumberRole).toInt(), 4);
}

void list_item_enter_on_empty_exits_to_paragraph() {
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "1. one\n2. \n", 2));

    // Enter on empty item (block 1, content is "")
    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, 1, 0, true, QString());
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    QTRY_COMPARE(binding.model()->data(binding.model()->index(1, 0),
                 LiveBlockModel::KindRole).toString(),
                 QStringLiteral("paragraph"));
}

void list_item_tab_indents_and_renumbers() {
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n", 2));

    // Tab on block 1 (now indent 1)
    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Tab, Qt::NoModifier, 1, 0, true, QStringLiteral("two"));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    QCOMPARE(binding.model()->data(binding.model()->index(1, 0),
             LiveBlockModel::IndentLevelRole).toInt(), 1);
    // Sub-list seeded; if "two" was the only ordered item at indent 1,
    // its number stays 2 (the seed) since the renumber walks just this run.
}
```

- [ ] **Step 3: Build and run**

Run: `cmake --build build-dev --target tst_live_render_structural -j 8 && ctest --test-dir build-dev -R '^tst_live_render_structural$' --output-on-failure`

Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_structural.cpp
git commit -m "$(cat <<'EOF'
test(live-render): rewrite ListItem structural tests for per-item blocks

Drop the band-aid-era multi-line list tests. Add:
- loads one block per item
- Enter at end creates next item + renumbers
- Enter mid-list renumbers below
- Enter on empty exits to Paragraph
- Tab indents + renumbers

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Phase 4: Dogfood + status

### Task 17: Full build, test suite, dogfood, status update

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/d-arc/d-arc-status.md`

- [ ] **Step 1: Full build + test suite**

Run: `cmake --build build-dev -j 8 && ctest --test-dir build-dev -E "tst_realistic|tst_benchmark" --output-on-failure -j 8`

Expected: all tests pass. The new tests added in this plan are
`tst_parser_list_items`, `tst_d2_list_roundtrip`, `tst_d2_list_renumber`
(plus the rewritten `tst_live_render_structural` cases).

- [ ] **Step 2: Manual dogfood**

```bash
./build-dev/bin/markoff-live-app docs/specs/2026-05-06-per-item-listitem-blocks-design.md
```

Test each scenario:
- Click into an existing 6-item ordered list. Press Enter at end of last
  item → verify "7." appears as a new item below, cursor at end of "7. ".
- Click in middle of item 3 ("3. three"). Press Enter → verify item splits;
  items 4, 5, 6 renumbered to 5, 6, 7 (the new item being 4).
- Enter on an empty item → verify exit to paragraph.
- Tab on a non-first item → verify indent + sub-list rendering.
- Click a task-list checkbox → verify toggle.
- Save and reload (if save is wired) → verify byte-equal source.

If any scenario fails, write down the bug, fix it (small bandage in the
relevant handler), re-test, commit fix.

- [ ] **Step 3: Update CLAUDE.md banner — D3 corrective work complete**

Replace the worktree `CLAUDE.md` banner with:

```markdown
> **D3 complete (corrective spec landed and dogfooded).** D3-correction
> per `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` is
> implemented and passes dogfood. ListItem blocks are per-item; renumbering
> is caller-driven; marker rendering is delegate-driven from attrs.
>
> **Fresh agent context — read in order:**
>
> 1. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation
> 2. `docs/d-arc/d-arc-status.md` — live status board (D3 complete)
> 3. `docs/d-arc/collabtext-scope-line.md` — six "won't do" items
> 4. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` — D3 spec
> 5. `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` — corrective spec (now landed)
>
> Next phase: D4 (parser scope reduction). Stub at
> `docs/specs/2026-05-04-d4-parser-scope-reduction-STUB.md`.
```

- [ ] **Step 4: Update d-arc-status.md**

```markdown
**Last updated:** YYYY-MM-DD (D3 corrective spec landed; D3 complete.)
**Active phase:** **D4** (parser scope reduction) — substantive design
pending.
```

In the phase board, mark D3 → `complete`. In the recent-changes log,
add an entry for the corrective spec implementation.

- [ ] **Step 5: Final commit**

```bash
git add CLAUDE.md docs/d-arc/d-arc-status.md
git commit -m "$(cat <<'EOF'
docs: mark D3 complete after per-item ListItem corrective spec dogfood

All scenarios from the corrective spec (per-item materialization, marker
attrs, renumber on insert/delete, task-list toggle, indent via attr,
roundtrip preservation) verified locally. CLAUDE.md banner updated to
point to D4 as the next phase.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Acceptance criteria

The plan is done when:

1. `tst_parser_list_items` passes (all 7 cases).
2. `tst_d2_list_roundtrip` passes (all 6 cases).
3. `tst_d2_list_renumber` passes (all 4 cases).
4. `tst_live_render_structural` passes with the rewritten ListItem tests.
5. The full test suite (`-E "tst_realistic|tst_benchmark"`) is 100% green.
6. Manual dogfood of the user's 6-item-list scenario produces correct
   output: in-block renumbering on Enter, no phantom empty lines, cursor
   lands at end of new item, task-list toggle works, indent via Tab works.
7. `LiveStructuralKeyHandler.cpp` ListItem section has zero regex matches
   for `kMarker` / `kOrd` / `markerPrefix`.
8. `CLAUDE.md` banner reflects D3-complete and points at D4 as next.

## Rollback

Each task commits separately; if a task introduces regressions that
can't be quickly resolved, `git revert <sha>` of that task's commit
returns to the prior state. The corrective spec stays — it's the source
of truth for what to retry.
