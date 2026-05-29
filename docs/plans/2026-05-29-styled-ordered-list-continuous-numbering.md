# Ordered-list continuous numbering — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Consecutive same-(markerStyle, depth) ListItem blocks in `markoff-styled` share one `QTextList` so ordered numbering is continuous (`1, 2, 3` instead of `1, 1, 1`). Nested-list transitions resume the outer list per CommonMark.

**Architecture:** New helper `manageListMembership(qblk, kind, attrs, listStack)` runs once per block iteration in `StyleApplier::applyFormats`, OUTSIDE the hash gate. State is a depth-stack of `{depth, markerStyle, QTextList*}`. `applyListItem` becomes format-only (drops `cursor.createList(lf)`); list membership is now the walk's responsibility.

**Tech Stack:** C++20, Qt6 (Core/Gui/Widgets/Test), CMake.

**Authoritative spec:** [`../specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md`](../specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md).

**Build/test:**
- Build: `cmake --build build-dev --target tst_styled_dogfood_invariants -j 8`
- Run: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants`
- Fast suite: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
- 2026-05-29 baseline: 249/254. Five pre-existing failures unchanged.

---

## Task 1: Tests (TDD) + production change (single commit)

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp` — refactor `applyListItem`, add `manageListMembership` + `ListStackEntry`, wire into the block walk.
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` — three new slots.

- [ ] **Step 1: Write the three failing test slots**

Append these slots before the closing `};` of `TstStyledDogfoodInvariants` (the file already has `attr_toggle_re_renders_task_marker` as the last slot from the just-landed queue #8.5 work — add after it).

```cpp
    // Ordered-list continuous numbering (queue #8.4; spec
    // 2026-05-29-styled-ordered-list-continuous-numbering-design.md).
    // Consecutive same-(markerStyle, depth) ListItems must share one
    // QTextList so ListDecimal numbers them 1, 2, 3 rather than 1, 1, 1.
    void ordered_list_items_share_one_list_with_continuous_numbering() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("1. one\n2. two\n3. three\n"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextDocument *qdoc = e.textEdit()->document();
        const QTextBlock b0 = qdoc->findBlockByNumber(0);
        const QTextBlock b1 = qdoc->findBlockByNumber(1);
        const QTextBlock b2 = qdoc->findBlockByNumber(2);

        QVERIFY(b0.isValid());
        QVERIFY(b1.isValid());
        QVERIFY(b2.isValid());
        QVERIFY(b0.textList() != nullptr);
        QCOMPARE(b1.textList(), b0.textList());
        QCOMPARE(b2.textList(), b0.textList());
    }

    void paragraph_between_items_breaks_list() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("1. one\n\nbreak\n\n2. two\n"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextDocument *qdoc = e.textEdit()->document();
        const QTextBlock b0 = qdoc->findBlockByNumber(0);  // "1. one"
        const QTextBlock b1 = qdoc->findBlockByNumber(1);  // "break" paragraph
        const QTextBlock b2 = qdoc->findBlockByNumber(2);  // "2. two"

        QVERIFY(b0.textList() != nullptr);
        QVERIFY(b2.textList() != nullptr);
        QCOMPARE(b1.textList(), static_cast<QTextList *>(nullptr));
        QVERIFY(b0.textList() != b2.textList());
    }

    void nested_list_then_outer_resumes() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // "1. outer\n   1. nested\n2. outer-two\n"
        // Outer items at depth 0, nested at depth 1, all dot style.
        doc.loadFromMarkdown(QByteArrayLiteral(
            "1. outer\n   1. nested\n2. outer-two\n"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTextDocument *qdoc = e.textEdit()->document();
        const QTextBlock b0 = qdoc->findBlockByNumber(0);
        const QTextBlock b1 = qdoc->findBlockByNumber(1);
        const QTextBlock b2 = qdoc->findBlockByNumber(2);

        QVERIFY(b0.textList() != nullptr);
        QVERIFY(b1.textList() != nullptr);
        QVERIFY(b2.textList() != nullptr);
        // Outer (b0) and outer-two (b2) share one list — the stack pops the
        // nested entry when b2's depth (0) is shallower than nested's (1).
        QCOMPARE(b2.textList(), b0.textList());
        // Nested (b1) is in a different list.
        QVERIFY(b1.textList() != b0.textList());
    }
```

- [ ] **Step 2: Build and run; all three new slots must FAIL**

```bash
cmake --build build-dev --target tst_styled_dogfood_invariants -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants
```

Expected: existing slots pass, the three new ones fail. Failure shape:
- `ordered_list_items_share_one_list_with_continuous_numbering` — `b1.textList() != b0.textList()` because each item gets its own one-item list today.
- `paragraph_between_items_breaks_list` — passes the breaks assertion (each item still in its own list, which happens to satisfy `b0.textList() != b2.textList()`), but paragraph's `textList()` should be null today (it never gets a list). This slot may pass against HEAD! That's fine — its real purpose is to guard against the new code accidentally putting paragraphs in lists. Note this in Step 3 if it passes pre-fix.
- `nested_list_then_outer_resumes` — `b2.textList() != b0.textList()` because each gets its own list.

If `paragraph_between_items_breaks_list` passes pre-fix, that's expected — log it but don't sweat it.

- [ ] **Step 3: Refactor `applyListItem` to format-only**

In `libs/markoff-styled/src/StyleApplier.cpp` find `applyListItem` (line 138) and remove the `QTextList` creation tail (the last 9 lines starting from `if (markerStyle == QStringLiteral("task")) return;`). Replace with a comment:

```cpp
void applyListItem(QTextCursor &cursor, int depth,
                   const QString &markerStyle, bool checked,
                   qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(listItemMarginPt(fontScale));
    bf.setBottomMargin(listItemMarginPt(fontScale));

    // Task-list checkboxes are the only marker type QTextBlockFormat can
    // render natively. Set them on the block format; clear any prior marker
    // when the kind isn't task.
    if (markerStyle == QStringLiteral("task")) {
        bf.setMarker(checked ? QTextBlockFormat::MarkerType::Checked
                             : QTextBlockFormat::MarkerType::Unchecked);
    } else {
        bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
    }
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(emPt(fontScale));
    applyBlockCharFormat(cursor, cf);

    // QTextList membership (bullet / numeral rendering) is handled by
    // manageListMembership in the walk so consecutive same-style items
    // can share one list (continuous numbering). The walk runs after the
    // hash gate so this format-only function isn't responsible for
    // cross-block continuity; see spec
    // docs/specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md.
    // The `depth` parameter is no longer used here; left in the signature
    // because callers pass it and removing complicates the call site.
    Q_UNUSED(depth);
}
```

- [ ] **Step 4: Add `ListStackEntry` + `manageListMembership` in the anonymous namespace**

Insert into the file-local `namespace { ... }` (around line 27) after the `applyHorizontalRule` definition. Right above the closing `} // namespace` is fine.

```cpp
struct ListStackEntry {
    int depth = -1;
    QString markerStyle;
    QTextList *list = nullptr;
};

// Reconcile a model block's QTextList membership against the
// neighbour-aware list stack, so consecutive same-style ListItems share
// one list (continuous numbering) and nested-list transitions resume
// the outer list. Runs OUTSIDE the hash gate so a structural change
// (e.g. a paragraph inserted between two formerly-adjacent ordered
// items) breaks the prior shared list even when neither item's hash
// changed.
//
// Spec: docs/specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md.
void manageListMembership(
    QTextBlock qblk,
    Markoff::BlockKind kind,
    const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs,
    std::vector<ListStackEntry> &listStack)
{
    auto removeFromAnyList = [&](QTextBlock b) {
        if (QTextList *lst = b.textList())
            lst->remove(b);
    };

    if (kind != Markoff::BlockKind::ListItem) {
        // Any non-list block ends the enclosing list under CommonMark.
        listStack.clear();
        removeFromAnyList(qblk);
        return;
    }

    // Read ListItem attrs.
    int depth = 0;
    if (auto it = attrs.find(Markoff::AttrNames::IndentLevel);
        it != attrs.end() && std::holds_alternative<int>(*it))
        depth = std::get<int>(*it);
    QString markerStyle;
    if (auto it = attrs.find(Markoff::AttrNames::MarkerStyle);
        it != attrs.end() && std::holds_alternative<QString>(*it))
        markerStyle = std::get<QString>(*it);

    if (markerStyle == QStringLiteral("task")) {
        // Task items render their checkbox via QTextBlockFormat::Marker,
        // not via QTextList. They interrupt the same-depth list of any
        // marker style — pop deeper-than-task entries, then pop the
        // same-depth entry if present.
        while (!listStack.empty() && listStack.back().depth > depth)
            listStack.pop_back();
        if (!listStack.empty() && listStack.back().depth == depth)
            listStack.pop_back();
        removeFromAnyList(qblk);
        return;
    }

    // Non-task ListItem: depth-stack reconciliation.
    while (!listStack.empty() && listStack.back().depth > depth)
        listStack.pop_back();

    if (!listStack.empty() && listStack.back().depth == depth) {
        if (listStack.back().markerStyle == markerStyle) {
            // Same depth + same style → join the running list.
            listStack.back().list->add(qblk);
            return;
        }
        // Same depth, different style: end the running list, fall
        // through to create a fresh one at this depth.
        listStack.pop_back();
    }

    // Empty stack or top is shallower than this item — create a new list
    // at this depth and push.
    QTextListFormat lf;
    if (markerStyle == QStringLiteral("dot")
        || markerStyle == QStringLiteral("paren"))
        lf.setStyle(QTextListFormat::ListDecimal);
    else  // minus / plus / star / unknown → disc
        lf.setStyle(QTextListFormat::ListDisc);
    lf.setIndent(depth + 1);
    QTextCursor c(qblk);
    QTextList *fresh = c.createList(lf);
    listStack.push_back({depth, markerStyle, fresh});
}
```

- [ ] **Step 5: Wire the helper into `applyFormats`**

In `libs/markoff-styled/src/StyleApplier.cpp` find the block-walk loop in `applyFormats` (around line 437). Before the loop, declare the stack:

```cpp
        // List-membership stack for continuous numbering across the walk.
        // Reset each cascade; reconciliation in manageListMembership runs
        // OUTSIDE the hash gate so a paragraph inserted between two
        // formerly-adjacent items breaks the old shared list even when
        // neither item's hash changed.
        std::vector<ListStackEntry> listStack;
```

Then locate the inner format pass (the `while (qblk.isValid() && qblk.position() <= endQt)` block, the surrounding span pass, and the `bytePos = blockEnd; if (...) bytePos += kSepLen;` advance). The plan: move the `bytePos` advance to a unified spot at the loop bottom, and call `manageListMembership` there too.

Concretely:

(a) In the hash-skip branch, **remove** the `bytePos = blockEnd; if (i + 1 < blocks.size()) bytePos += kSepLen; continue;` block. Replace with a one-line `++m_hashSkipsLastPass;` and let control fall through to the end of the loop.

Wait — the hash-skip branch needs to skip the format pass. Use a boolean flag instead:

```cpp
            const bool hashSkipped = (m_blockHashes.value(id, 0) == h);
            if (hashSkipped) {
                ++m_hashSkipsLastPass;
            } else {
                m_blockHashes[id] = h;

                // Kind transition: ... (existing code unchanged)

                const int startQt = ...; // existing
                const int endQt   = ...; // existing
                cursor.setPosition(startQt);
                QTextBlock qblk = cursor.block();
                while (qblk.isValid() && qblk.position() <= endQt) {
                    // ... existing kind-branch unchanged ...
                }
                // Inline span pass: unchanged.
            }

            // List membership: always, even on hash-skip. Needs qblk
            // resolved for THIS model block; resolve it via the same
            // byteOffsetToQtPos translation.
            const int listStartQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flatBytes, blockStart);
            const QTextBlock listBlk = m_textDocument->findBlock(listStartQt);
            manageListMembership(listBlk, kind, attrs, listStack);

            // Advance byte position.
            bytePos = blockEnd;
            if (i + 1 < blocks.size()) bytePos += kSepLen;
        }
```

The full updated loop body should look approximately like the above pseudocode. Read the existing 437-543 range carefully before editing; preserve every existing piece of logic.

- [ ] **Step 6: Build and re-run; all three new slots must PASS**

```bash
cmake --build build-dev --target tst_styled_dogfood_invariants -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants
```

Expected: 16/16 pass (was 13 before this work + 3 new).

- [ ] **Step 7: Spot-check neighbours**

```bash
cmake --build build-dev --target tst_styled_d2_integration tst_styled_inline_formats tst_styled_block_formats tst_styled_binding_caret -j 8
for t in tst_styled_d2_integration tst_styled_inline_formats tst_styled_block_formats tst_styled_binding_caret; do printf "%-40s " "$t"; QT_QPA_PLATFORM=offscreen ./build-dev/bin/$t 2>&1 | grep "Totals:"; done
```

Expected: same totals as before. `tst_styled_block_formats` keeps its 3 pre-existing failures.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-styled/src/StyleApplier.cpp libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp
git commit -m "$(cat <<'EOF'
feat(styled): ordered-list continuous numbering via depth-stack

Each ListItem previously got its own one-item QTextList — Qt's
ListDecimal renders every ordered item as "1." because numbering
is positional within a list. Consecutive same-style items now
share one QTextList so ordered items number 1, 2, 3 continuously
and unordered items group visually under one disc.

New manageListMembership helper runs once per block in the
applyFormats walk, OUTSIDE the hash gate. State is a depth-stack
of {depth, markerStyle, QTextList*}. Per-block decision:

* Non-ListItem → clear stack (ends all enclosing lists per
  CommonMark); remove qblk from any current list.
* Task ListItem → pop deeper entries, then pop same-depth entry
  if present (task interrupts same-depth list of any style);
  qblk doesn't push (renders via native checkbox marker).
* Non-task ListItem → pop deeper entries; if top matches depth
  + markerStyle, join its list; else create new list, push.

Nested-list-then-outer resumes correctly: "2. b" after a
nested "1. nested" at depth 1 pops the nested entry, finds the
outer entry at depth 0, joins its list. Renders 1, ?, 2.

Hash-gate interaction: manageListMembership runs every cascade
regardless of hash match because list membership depends on
neighbour state, not just per-block content. A paragraph
inserted between two formerly-adjacent items now correctly
breaks the prior shared list even when the items' content
hashes are unchanged.

applyListItem refactored to format-only — drops cursor.createList.
List creation/membership is the walk's concern now.

Three new tst_styled_dogfood_invariants slots pin the contract:
ordered items share one list with continuous numbering, paragraph
break splits the list, nested-then-outer resumes the outer list.

Spec: docs/specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md
Plan: docs/plans/2026-05-29-styled-ordered-list-continuous-numbering.md
Queue: closes #8.4.
EOF
)"
```

---

## Task 2: Docs + queue closeout

**Files:**
- Modify: `libs/markoff-styled/CLAUDE.md` — update the ListItem marker rendering invariant.
- Modify: `docs/queue.md` — close #8.4.

- [ ] **Step 1: Update `libs/markoff-styled/CLAUDE.md`**

Find the bullet starting "ListItem marker rendering (2026-05-29)" and replace the trailing v0.2 caveat (the sentence about ordered items always reading `1.`) with a statement that continuous numbering now works. Concretely, find:

```
- **ListItem marker rendering (2026-05-29).** `applyListItem` reads
  `MarkerStyle`, `IndentLevel`, and (for tasks) `Checked` from the block
  attrs — depth is no longer derived from buffer leading whitespace
  (which is always 0 post-marker anyway). Task-list checkboxes use the
  native `QTextBlockFormat::MarkerType::{Unchecked,Checked}`. Bullets
  and decimals come from a per-item `QTextList` (`ListDisc` for
  minus/plus/star markers; `ListDecimal` for dot/paren). Single-item
  lists render the marker correctly but ordered items always read `1.`;
  sibling-grouping is a v0.2 follow-up (see queue #8).
```

Replace with:

```
- **ListItem marker rendering (2026-05-29).** `applyListItem` reads
  `MarkerStyle`, `IndentLevel`, and (for tasks) `Checked` from the
  block attrs and applies block + char format. Task-list checkboxes
  use the native `QTextBlockFormat::MarkerType::{Unchecked,Checked}`.
  Bullets and decimals come from `QTextList`s assigned by the walk's
  `manageListMembership` helper — consecutive same-(markerStyle,
  depth) ListItems share one list so ordered numbering is continuous
  (1, 2, 3). Nested-then-outer transitions resume the outer list per
  CommonMark via a depth-stack. Marker-style transitions at the same
  depth break the list. Spec
  `docs/specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md`.
```

- [ ] **Step 2: Close queue #8.4**

In `docs/queue.md`, find the #8.4 entry (the "Ordered-list continuous numbering" paragraph) and replace with:

```
4. ~~**Ordered-list continuous numbering** in `markoff-styled`.~~ →
   closed 2026-05-29 in commit `<hash>`. `manageListMembership` helper
   runs once per block in the `applyFormats` walk, OUTSIDE the hash
   gate, maintaining a depth-stack of `{depth, markerStyle, QTextList*}`.
   Consecutive same-style items share one `QTextList`; nested-list
   transitions resume the outer list (1, ?, 2); paragraph between
   items breaks the chain; marker-style change at same depth also
   breaks. `applyListItem` refactored to format-only. Spec:
   `docs/specs/2026-05-29-styled-ordered-list-continuous-numbering-design.md`.
   Plan: `docs/plans/2026-05-29-styled-ordered-list-continuous-numbering.md`.
```

Substitute `<hash>` with the short SHA from Task 1's commit
(`git log --oneline -2`).

- [ ] **Step 3: Commit docs**

```bash
git add libs/markoff-styled/CLAUDE.md docs/queue.md
git commit -m "docs: continuous-numbering invariant + queue #8.4 closeout"
```

---

## Task 3: Full-suite verification

- [ ] **Step 1: Run the fast suite**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 249/254 baseline preserved. The same 5 pre-existing failures
remain (the `tst_styled_block_formats` count of 3 includes
`list_item_has_left_margin`, which fell out of the QTextList migration
in `845fc0f` and is tracked separately under queue #8.7).

If `list_item_has_left_margin` starts passing, that's a bonus — the
QTextList indent provides the left margin Qt-side. Note it in the
report.

If any new test fails, the most likely culprit is:
- Wrong qblk reference in `manageListMembership` (use `findBlock` from
  the model block's startQt; don't reuse `qblk` from the inner span
  loop after it advanced).
- `bytePos` accumulator broken because the hash-skip branch's
  advancement was deleted but the fall-through didn't pick it up —
  verify with a paragraph + list-item mixed document.

- [ ] **Step 2: Report**

Summarise: test count delta, commit SHAs, any list_item_has_left_margin
movement. Plan complete when baseline holds.

---

## Self-review

Spec coverage:
- §2.1 per-block decision → Task 1 Step 4 helper body.
- §2.2 behaviour examples → Task 1 Step 1 test slots (3 of the 6
  examples in the table; the other three would be useful but the
  spec said three suffice for the falsifiable contract).
- §2.3 hash-gate interaction → Task 1 Step 5 (helper called outside
  the hash gate).
- §3 tests → Task 1 Steps 1 + 6.
- §6 def-of-done → Task 3.

Placeholder scan: `<hash>` in Task 2 Step 2 is intentional with an
explicit `git log` instruction.

Type consistency: `ListStackEntry`, `manageListMembership`,
`QTextList`, `QTextListFormat`, `BlockKind::ListItem`,
`AttrNames::{IndentLevel,MarkerStyle,Checked}`,
`SourceTextDocumentBinding::byteOffsetToQtPos`,
`m_textDocument->findBlock(int)` — all match existing code as
inspected during planning.
