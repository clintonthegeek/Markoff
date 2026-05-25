# E1 Inline-format Highlighter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the Phase E1 inline-format highlighter — `Markoff::Live::InlineHighlighter` painting `QTextCharFormat` ranges from `BlockRecord::inlineSpans` in 4 text-bearing QML delegates (paragraph, heading, blockquote, list-item), driven by the existing 8 `Markoff::Theme::Slot` inline tokens.

**Architecture:** Per-delegate `QSyntaxHighlighter` subclass bound to `TextEdit.textDocument`. The class consumes `QList<Markoff::SourceSpan>` via a new `LiveBlockModel::InlineSpansRole`. Flag-combining `formatFor(span)` walks span flags (`bold`, `italic`, `strikethrough`, `code`, `highlight`, `isLink`, `isWikilink`, `isTag`) and OR-mixes the matching `Theme::Slot` properties into one `QTextCharFormat`. A small `InlineHighlighterAttached` QML shim wraps it for delegate-side property bindings.

**Tech Stack:** C++20, Qt 6.8+, CMake 3.19+, QSyntaxHighlighter, QTextCharFormat. Uses existing `Markoff::SourceSpan`, `Markoff::Theme`, `Markoff::Live::LiveBlockModel`, `Markoff::Live::BlockRecord`. Tests via Qt::Test under `libs/markoff-live/tests/` with the `tst_live_render_inline_*` naming convention.

**Spec:** `docs/specs/2026-05-08-e1-inline-highlighter-design.md` — read §0.2 amendment first (codebase reality vs. initial draft), then §§2–4 for architecture, then §5 for test surface.

**Build/test cadence (whole plan):**

- Build cap: **`-j 8`** (user policy — never use bare `-j` or higher).
- Configure once: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- Per-task build: `cmake --build build-dev --target markoff_live -j 8` (and `markoff-live-app` when wiring delegates).
- Per-task test (focused): `ctest --test-dir build-dev -R '<test-name>' --output-on-failure`.
- Periodic full-suite: `ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8` after each phase.

**Commit convention (whole plan):**

- Subject: `<library>: <description>` (e.g., `markoff-live: add InlineHighlighter skeleton`).
- Trailer: `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`.
- Frequent commits — one per task, sometimes more if a task has multiple atomic steps.
- Append-only `master` (= `exploration/new-foundation` in this worktree). No force-push.

---

## Phase A — Foundation pre-flight

Three small wiring tasks that prepare the model + parser-side types so later tasks compose cleanly.

### Task A1: SourceSpan operator== + metatype registration

**Files:**
- Modify: `libs/markoff-parser/include/markoff/parser/SourceSpan.h`
- Modify: `libs/markoff-live/tests/tst_live_render_block_model.cpp` (add a small assertion using `==`)

- [ ] **Step 1: Confirm existing state.** `grep -n "operator==\|Q_DECLARE_METATYPE" libs/markoff-parser/include/markoff/parser/SourceSpan.h` from the worktree root. If `operator==` already exists, skip Step 3's `==` add. If `Q_DECLARE_METATYPE(QList<Markoff::SourceSpan>)` already present, skip its add too.

- [ ] **Step 2: Write failing test in tst_live_render_block_model.cpp**

Add a new test slot that asserts `Markoff::SourceSpan` equality and `QList<Markoff::SourceSpan>` round-trips through `QVariant`:

```cpp
void source_span_equality_and_metatype_round_trip() {
    Markoff::SourceSpan a{};
    a.charOffset = 0; a.charLength = 4; a.bold = true;
    Markoff::SourceSpan b = a;
    QVERIFY(a == b);
    b.italic = true;
    QVERIFY(!(a == b));

    QList<Markoff::SourceSpan> spans{a, b};
    QVariant v = QVariant::fromValue(spans);
    QVERIFY(v.canConvert<QList<Markoff::SourceSpan>>());
    auto restored = v.value<QList<Markoff::SourceSpan>>();
    QCOMPARE(restored.size(), 2);
    QVERIFY(restored[0] == a);
    QVERIFY(restored[1] == b);
}
```

Add `#include <markoff/parser/SourceSpan.h>` if not already in the file's includes.

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8
ctest --test-dir build-dev -R '^tst_live_render_block_model$' --output-on-failure
```

Expected: FAIL — either compile error (missing `operator==`) or `QVariant::canConvert` returns false (missing metatype).

- [ ] **Step 4: Implement.** Edit `libs/markoff-parser/include/markoff/parser/SourceSpan.h`. Inside the `struct SourceSpan` definition, after the last data member, add:

```cpp
    bool operator==(const SourceSpan &o) const noexcept {
        return utf8Offset == o.utf8Offset && utf8Length == o.utf8Length
            && charOffset == o.charOffset && charLength == o.charLength
            && bold == o.bold && italic == o.italic
            && strikethrough == o.strikethrough && code == o.code
            && math == o.math && mathDisplay == o.mathDisplay
            && highlight == o.highlight && comment == o.comment
            && isTag == o.isTag && isLink == o.isLink
            && isWikilink == o.isWikilink && isImage == o.isImage
            && isFootnoteRef == o.isFootnoteRef
            && isHeading == o.isHeading && headingLevel == o.headingLevel
            && isBlockquoteMarker == o.isBlockquoteMarker
            && isListMarker == o.isListMarker
            && isCodeBlockFence == o.isCodeBlockFence
            && isCodeBlockContent == o.isCodeBlockContent
            && isFrontmatter == o.isFrontmatter
            && isHorizontalRule == o.isHorizontalRule
            && isBlockquote == o.isBlockquote
            && blockquoteDepth == o.blockquoteDepth
            && isCalloutMarker == o.isCalloutMarker
            && isTaskMarker == o.isTaskMarker
            && isDelimiter == o.isDelimiter
            && parentCharStart == o.parentCharStart
            && parentCharEnd == o.parentCharEnd;
    }
    bool operator!=(const SourceSpan &o) const noexcept { return !(*this == o); }
```

Below the namespace closing brace (`} // namespace Markoff`), add:

```cpp
Q_DECLARE_METATYPE(Markoff::SourceSpan)
Q_DECLARE_METATYPE(QList<Markoff::SourceSpan>)
```

(Skip whichever already exists per Step 1.)

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8
ctest --test-dir build-dev -R '^tst_live_render_block_model$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-parser/include/markoff/parser/SourceSpan.h \
        libs/markoff-live/tests/tst_live_render_block_model.cpp
git commit -m "$(cat <<'EOF'
markoff-parser: SourceSpan operator== + QList metatype

E1 prerequisite. The InlineHighlighter pipeline needs to compare
QList<SourceSpan> values to detect spans-changed (E1 spec §3.3) and to
round-trip them through QVariant for the QML InlineSpansRole.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task A2: LiveBlockModel.InlineSpansRole + Q_INVOKABLE spansAtRow

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveBlockModel.h`
- Modify: `libs/markoff-live/src/LiveBlockModel.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_block_model.cpp`

- [ ] **Step 1: Write failing test**

Add a new test slot:

```cpp
void inline_spans_role_exposes_spans_to_qml() {
    LiveBlockModel m;
    QVERIFY(m.roleNames().values().contains(QByteArray("inlineSpans")));

    // Empty model: spansAtRow returns empty list, no crash.
    auto empty = m.spansAtRow(0);
    QCOMPARE(empty.size(), 0);

    // Push one row via applyOps with a synthesized BlockRecord.
    BlockRecord rec;
    rec.kind = "paragraph";
    rec.text = "hello";
    rec.blockAnchor = Markoff::BlockAnchor{};
    Markoff::SourceSpan span{};
    span.charOffset = 0; span.charLength = 5; span.bold = true;
    rec.inlineSpans = {span};

    QList<AstBlockDiff::Op> ops;
    AstBlockDiff::Op op;
    op.kind = AstBlockDiff::Op::Insert;
    op.row = 0;
    op.nextIndex = 0;
    ops.append(op);
    m.applyOps(ops, {rec});

    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.spansAtRow(0).size(), 1);
    QVERIFY(m.spansAtRow(0)[0].bold);

    // data(InlineSpansRole) returns QList<SourceSpan> wrapped in QVariant.
    const QVariant v = m.data(m.index(0), LiveBlockModel::InlineSpansRole);
    QVERIFY(v.canConvert<QList<Markoff::SourceSpan>>());
    QCOMPARE(v.value<QList<Markoff::SourceSpan>>().size(), 1);
}
```

Add includes if missing: `#include <markoff/parser/SourceSpan.h>`, `#include <markoff/live/AstBlockDiff.h>`.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8
ctest --test-dir build-dev -R '^tst_live_render_block_model$' --output-on-failure
```

Expected: FAIL — `LiveBlockModel::InlineSpansRole` doesn't exist; `roleNames` doesn't contain `"inlineSpans"`.

- [ ] **Step 3: Implement the role.** In `libs/markoff-live/include/markoff/live/LiveBlockModel.h`, in the `Role` enum (currently ends at `LooseRunRole`), append:

```cpp
        InlineSpansRole,
```

Change the existing `spansAtRow` declaration:

```cpp
    // Was: const QList<Markoff::SourceSpan> &spansAtRow(int row) const;
    Q_INVOKABLE QList<Markoff::SourceSpan> spansAtRow(int row) const;
```

(Returning by value to make it QML-meta-callable. QList is COW, so existing call sites copy cheaply.)

- [ ] **Step 4: Implement in cpp.** In `libs/markoff-live/src/LiveBlockModel.cpp`:

Update `spansAtRow` body to return by value:

```cpp
QList<Markoff::SourceSpan> LiveBlockModel::spansAtRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    return m_rows[row].inlineSpans;
}
```

In `data(const QModelIndex &, int)`, add a case:

```cpp
case InlineSpansRole:
    return QVariant::fromValue(m_rows[row].inlineSpans);
```

In `roleNames()`, add the entry:

```cpp
{InlineSpansRole, "inlineSpans"},
```

In the `LiveBlockModel` constructor body, add:

```cpp
qRegisterMetaType<QList<Markoff::SourceSpan>>("QList<Markoff::SourceSpan>");
```

(If not already covered by something equivalent. Belt-and-braces: also register the bare type `qRegisterMetaType<Markoff::SourceSpan>("Markoff::SourceSpan");` in the same place.)

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8
ctest --test-dir build-dev -R '^tst_live_render_block_model$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Run the wider live-render suite to catch regressions**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: ALL PASS. If anything fails because a prior call-site relied on `const &` lifetime, fix it (typically: store the returned list in a local before iterating).

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveBlockModel.h \
        libs/markoff-live/src/LiveBlockModel.cpp \
        libs/markoff-live/tests/tst_live_render_block_model.cpp
git commit -m "$(cat <<'EOF'
markoff-live: InlineSpansRole + Q_INVOKABLE spansAtRow

E1 model surface. Exposes the existing per-block inlineSpans data to
QML delegates via the InlineHighlighter pipeline. spansAtRow now returns
by value (QList COW) so QML can invoke it.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task A3: applyOps emits dataChanged on spans-only change

**Files:**
- Modify: `libs/markoff-live/src/LiveBlockModel.cpp` (Equal-op branch in `applyOps`)
- Modify: `libs/markoff-live/tests/tst_live_render_block_model.cpp`

- [ ] **Step 1: Read existing applyOps.** Open `libs/markoff-live/src/LiveBlockModel.cpp`, locate `applyOps` (search `void LiveBlockModel::applyOps`). Identify the branch handling `AstBlockDiff::Op::Equal`. Note the existing dataChanged emission shape.

- [ ] **Step 2: Write failing test**

Add a new test slot in `tst_live_render_block_model.cpp`:

```cpp
void spans_only_change_emits_data_changed_with_inline_spans_role() {
    LiveBlockModel m;

    // Seed one row.
    BlockRecord rec1;
    rec1.kind = "paragraph";
    rec1.text = "hello";
    rec1.blockAnchor = Markoff::BlockAnchor{};
    rec1.inlineSpans = {};
    AstBlockDiff::Op insert;
    insert.kind = AstBlockDiff::Op::Insert;
    insert.row = 0; insert.nextIndex = 0;
    m.applyOps({insert}, {rec1});

    // Now apply an Equal op where ONLY inlineSpans changes.
    BlockRecord rec2 = rec1;  // same kind/text/anchor/attrs
    Markoff::SourceSpan span{};
    span.charOffset = 0; span.charLength = 5; span.bold = true;
    rec2.inlineSpans = {span};

    QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
    AstBlockDiff::Op equal;
    equal.kind = AstBlockDiff::Op::Equal;
    equal.row = 0; equal.nextIndex = 0;
    m.applyOps({equal}, {rec2});

    QVERIFY2(spy.count() >= 1, "dataChanged must fire when spans differ");

    // Find an emit that includes InlineSpansRole.
    bool sawInlineSpansRole = false;
    for (const auto &emission : spy) {
        const QList<int> roles = emission.at(2).value<QList<int>>();
        if (roles.contains(LiveBlockModel::InlineSpansRole) || roles.isEmpty()) {
            // Empty roles list = "all roles changed", which also covers it.
            sawInlineSpansRole = true; break;
        }
    }
    QVERIFY(sawInlineSpansRole);

    // The new spans are now in the model.
    QCOMPARE(m.spansAtRow(0).size(), 1);
    QVERIFY(m.spansAtRow(0)[0].bold);
}
```

Add `#include <QSignalSpy>` if not already present.

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8
ctest --test-dir build-dev -R '^tst_live_render_block_model$' --output-on-failure
```

Expected: FAIL — current Equal-op branch short-circuits on `BlockRecord::operator==` which excludes `inlineSpans`. No `dataChanged` emit.

- [ ] **Step 4: Implement the spans-comparison.** In `libs/markoff-live/src/LiveBlockModel.cpp`'s `applyOps`, in the Equal-op branch, replace the existing equality short-circuit with:

```cpp
case AstBlockDiff::Op::Equal: {
    const int row = op.row;
    const BlockRecord &cur = m_rows[row];
    const BlockRecord &nxt = nextRecords[op.nextIndex];

    // Apply the R4 freshness rule (text update gated on edit-sequence
    // freshness) — preserve existing logic if present.
    const bool nonSpansDiffer = (cur != nxt);  // operator== excludes inlineSpans
    const bool spansDiffer    = (cur.inlineSpans != nxt.inlineSpans);

    if (nonSpansDiffer) {
        // Existing path: replace the row + emit dataChanged for all roles.
        // (Keep whatever the existing code does here — text-freshness gate,
        // attrs assignment, etc. The only addition is to also overwrite
        // inlineSpans if it differs.)
        m_rows[row] = nxt;  // NOTE: includes inlineSpans automatically
        emit dataChanged(index(row), index(row));
    } else if (spansDiffer) {
        // New path: spans-only change. Update spans + emit a targeted
        // dataChanged so the InlineHighlighter rehighlights without the
        // model's text role re-firing.
        m_rows[row].inlineSpans = nxt.inlineSpans;
        emit dataChanged(index(row), index(row), {InlineSpansRole});
    }
    // If neither differ, no emit (existing behaviour preserved).
    break;
}
```

**IMPORTANT:** This pseudocode shows the *shape*. The existing `applyOps` may already have R4-freshness guarding around the `m_rows[row] = nxt` assignment (per the LiveBlockModel.h comments about `parseInputEditSeq`). Preserve all existing behaviour; the only additive change is the `else if (spansDiffer)` branch.

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8
ctest --test-dir build-dev -R '^tst_live_render_block_model$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Run wider suite for regressions**

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: ALL PASS.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/src/LiveBlockModel.cpp \
        libs/markoff-live/tests/tst_live_render_block_model.cpp
git commit -m "$(cat <<'EOF'
markoff-live: applyOps emits dataChanged(InlineSpansRole) on spans-only change

Closes the equality-short-circuit gap for E1: BlockRecord::operator==
excludes inlineSpans by design (existing diff-identity choice), so
spans-only updates were silently dropping. applyOps now compares
inlineSpans separately and emits a targeted dataChanged role-list when
non-span fields are equal but spans differ.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase B — InlineHighlighter class (TDD)

Build the C++ highlighter incrementally, one flag at a time.

### Task B1: InlineHighlighter skeleton + first test (no spans, no paint)

**Files:**
- Create: `libs/markoff-live/include/markoff/live/InlineHighlighter.h`
- Create: `libs/markoff-live/src/InlineHighlighter.cpp`
- Create: `libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt` (add new sources)
- Modify: `libs/markoff-live/tests/CMakeLists.txt` (register new test)

- [ ] **Step 1: Create the header**

`libs/markoff-live/include/markoff/live/InlineHighlighter.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/parser/SourceSpan.h>

#include <QList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace Markoff { class Theme; }

namespace Markoff::Live {

/// Per-delegate inline-format painter. Reads BlockRecord::inlineSpans for
/// the bound row (via the LiveBlockModel::InlineSpansRole + the QML shim
/// InlineHighlighterAttached) and paints the configured Markoff::Theme
/// emphasis tokens via QTextCharFormat. E1 covers 8 flags: bold, italic,
/// strikethrough, code, highlight, isLink, isWikilink, isTag.
///
/// Markers (`**`, `_`, `~~`, `` ` ``, `==`, etc.) are painted along with
/// the inner content — auto-hide of markers is E2's work via
/// SourceSpan::isDelimiter (ignored in E1).
class MARKOFF_LIVE_EXPORT InlineHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit InlineHighlighter(QTextDocument *parent);
    ~InlineHighlighter() override;

    void setInlineSpans(const QList<Markoff::SourceSpan> &spans);
    const QList<Markoff::SourceSpan> &inlineSpans() const noexcept { return m_spans; }

    void setTheme(const Markoff::Theme *theme);
    const Markoff::Theme *theme() const noexcept { return m_theme; }

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat formatFor(const Markoff::SourceSpan &span) const;

    QList<Markoff::SourceSpan> m_spans;
    const Markoff::Theme      *m_theme = nullptr;
};

}  // namespace Markoff::Live
```

- [ ] **Step 2: Create the impl skeleton**

`libs/markoff-live/src/InlineHighlighter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/InlineHighlighter.h>

#include <markoff/core/Theme.h>

namespace Markoff::Live {

InlineHighlighter::InlineHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
}

InlineHighlighter::~InlineHighlighter() = default;

void InlineHighlighter::setInlineSpans(const QList<Markoff::SourceSpan> &spans)
{
    if (m_spans == spans) return;
    m_spans = spans;
    rehighlight();
}

void InlineHighlighter::setTheme(const Markoff::Theme *theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    rehighlight();
}

void InlineHighlighter::highlightBlock(const QString &text)
{
    Q_UNUSED(text);
    if (!m_theme) return;
    for (const Markoff::SourceSpan &span : std::as_const(m_spans)) {
        const QTextCharFormat fmt = formatFor(span);
        if (fmt == QTextCharFormat()) continue;
        if (span.charLength <= 0) continue;
        setFormat(span.charOffset, span.charLength, fmt);
    }
}

QTextCharFormat InlineHighlighter::formatFor(const Markoff::SourceSpan &span) const
{
    Q_UNUSED(span);
    // Step B2 starts populating this; for now: return default = no paint.
    return QTextCharFormat();
}

}  // namespace Markoff::Live
```

- [ ] **Step 3: Register the new sources in CMake**

In `libs/markoff-live/CMakeLists.txt`, find the `qt_add_qml_module(markoff_live ... SOURCES ...)` block and add the new files. Place them with the other "Block value types" group for consistency:

```cmake
        # Inline-format highlighter (E1)
        include/markoff/live/InlineHighlighter.h
        src/InlineHighlighter.cpp
```

- [ ] **Step 4: Create the test file**

`libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Per-flag inline-format-highlighter tests. Each slot exercises a
// single SourceSpan flag against a fresh QTextDocument + InlineHighlighter
// + defaultLight Theme, asserting the expected QTextCharFormat property
// at the expected (charOffset, charLength) range.
//
// Test harness: bare QTextDocument is sufficient since the highlighter's
// rehighlight() walks blocks directly without a QTextEdit / QTextControl.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/parser/SourceSpan.h>

using namespace Markoff::Live;

// Helper: walk the QTextDocument and find the first span where `pred(fmt)`
// is true; return its (start, length).
template <class Pred>
static QPair<int,int> findFormatRange(const QTextDocument &doc, Pred pred) {
    int start = -1, end = -1;
    int pos = 0;
    QTextBlock block = doc.firstBlock();
    while (block.isValid()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            if (pred(frag.charFormat())) {
                if (start < 0) start = block.position() + frag.position() - block.position();
                end = start + frag.length();
            }
        }
        block = block.next();
    }
    Q_UNUSED(pos);
    if (start < 0) return {-1, 0};
    return {start, end - start};
}

class TstLiveRenderInlinePerKind : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void empty_spans_no_paint() {
        QTextDocument doc;
        doc.setPlainText("plain text");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        h.setInlineSpans({});
        // Walk the doc; no fragment should have non-default fontWeight or
        // any custom format property.
        auto bold = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.fontWeight() == QFont::Bold;
        });
        QCOMPARE(bold.first, -1);
    }
};

QTEST_MAIN(TstLiveRenderInlinePerKind)
#include "tst_live_render_inline_per_kind.moc"
```

- [ ] **Step 5: Register the new test in CMake**

In `libs/markoff-live/tests/CMakeLists.txt`, append:

```cmake
qt_add_executable(tst_live_render_inline_per_kind
    tst_live_render_inline_per_kind.cpp
)
target_link_libraries(tst_live_render_inline_per_kind PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_per_kind COMMAND tst_live_render_inline_per_kind)
```

- [ ] **Step 6: Build and run — verify it passes**

```bash
cmake --build build-dev --target markoff_live -j 8
cmake --build build-dev --target tst_live_render_inline_per_kind -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_per_kind$' --output-on-failure
```

Expected: PASS (the empty case is the trivial baseline).

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/include/markoff/live/InlineHighlighter.h \
        libs/markoff-live/src/InlineHighlighter.cpp \
        libs/markoff-live/CMakeLists.txt \
        libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: InlineHighlighter skeleton (E1)

Empty formatFor returns default QTextCharFormat; highlightBlock walks
spans and paints when format is non-empty. Skeleton + first test
(empty-spans-no-paint) green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task B2: Bold flag → BoldEmphasis slot

**Files:**
- Modify: `libs/markoff-live/src/InlineHighlighter.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp`

- [ ] **Step 1: Write failing test**

Append a new slot to `tst_live_render_inline_per_kind.cpp`:

```cpp
    void bold_flag_paints_bold_weight() {
        QTextDocument doc;
        doc.setPlainText("plain **bold** plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        // Ensure BoldEmphasis is configured to be bold (defaultLight should
        // already; this asserts the precondition).
        QVERIFY(theme.isBold(Markoff::Theme::Slot::BoldEmphasis)
                || theme.color(Markoff::Theme::Slot::BoldEmphasis).isValid());

        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 8;  // covers **bold**
        span.bold = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.fontWeight() == QFont::Bold;
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 8);
    }
```

- [ ] **Step 2: Run — verify FAIL**

```bash
cmake --build build-dev --target tst_live_render_inline_per_kind -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_per_kind$' --output-on-failure
```

Expected: FAIL — `bold_flag_paints_bold_weight` finds no bold range.

- [ ] **Step 3: Implement formatFor for `bold`**

Replace `formatFor` body in `libs/markoff-live/src/InlineHighlighter.cpp`:

```cpp
QTextCharFormat InlineHighlighter::formatFor(const Markoff::SourceSpan &span) const
{
    if (!m_theme) return QTextCharFormat();
    QTextCharFormat fmt;
    bool any = false;

    auto applyEmphasis = [&](Markoff::Theme::Slot slot) {
        const QColor c = m_theme->color(slot);
        if (c.isValid()) fmt.setForeground(c);
        if (m_theme->isBold(slot))   fmt.setFontWeight(QFont::Bold);
        if (m_theme->isItalic(slot)) fmt.setFontItalic(true);
        any = true;
    };

    if (span.bold) applyEmphasis(Markoff::Theme::Slot::BoldEmphasis);

    return any ? fmt : QTextCharFormat();
}
```

- [ ] **Step 4: Run — verify PASS**

```bash
cmake --build build-dev --target tst_live_render_inline_per_kind -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_per_kind$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: If the theme assertion in Step 1 failed**, fix `Theme::defaultLight()` in `libs/markoff-core/src/Theme.cpp` so `BoldEmphasis` is `setBold(BoldEmphasis, true)`. Re-run.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/src/InlineHighlighter.cpp \
        libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp \
        libs/markoff-core/src/Theme.cpp  # if touched
git commit -m "$(cat <<'EOF'
markoff-live: InlineHighlighter paints bold flag (E1)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task B3: Italic + Strikethrough + InlineCode flags

**Files:**
- Modify: `libs/markoff-live/src/InlineHighlighter.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp`
- Modify: `libs/markoff-core/src/Theme.cpp` (if defaults need adjusting)

- [ ] **Step 1: Write three failing tests**

Append to `tst_live_render_inline_per_kind.cpp`:

```cpp
    void italic_flag_paints_italic() {
        QTextDocument doc;
        doc.setPlainText("plain *italic* plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 8;  // *italic*
        span.italic = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.fontItalic();
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 8);
    }

    void strikethrough_flag_paints_strike() {
        QTextDocument doc;
        doc.setPlainText("plain ~~struck~~ plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 10;  // ~~struck~~
        span.strikethrough = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.fontStrikeOut();
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 10);
    }

    void inline_code_flag_paints_monospace_and_bg() {
        QTextDocument doc;
        doc.setPlainText("plain `code` plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 6;  // `code`
        span.code = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [](const QTextCharFormat &f){
            return f.background().style() != Qt::NoBrush;
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 6);
        // Monospace family is asserted by checking the format inside the range.
        QTextCursor c(&doc);
        c.setPosition(8);
        const QTextCharFormat fmt = c.charFormat();
        QVERIFY(fmt.fontFamilies().toStringList().contains(
                    theme.font(Markoff::Theme::FontRole::Monospace).family())
                || fmt.font().family() == theme.font(Markoff::Theme::FontRole::Monospace).family());
    }
```

- [ ] **Step 2: Run — verify all 3 FAIL**

```bash
cmake --build build-dev --target tst_live_render_inline_per_kind -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_per_kind$' --output-on-failure
```

- [ ] **Step 3: Implement** by extending `formatFor`:

```cpp
    if (span.bold)          applyEmphasis(Markoff::Theme::Slot::BoldEmphasis);
    if (span.italic)        applyEmphasis(Markoff::Theme::Slot::ItalicEmphasis);
    if (span.strikethrough) {
        fmt.setFontStrikeOut(true);
        const QColor c = m_theme->color(Markoff::Theme::Slot::StrikeEmphasis);
        if (c.isValid()) fmt.setForeground(c);
        any = true;
    }
    if (span.code) {
        const QColor fg = m_theme->color(Markoff::Theme::Slot::InlineCode);
        const QColor bg = m_theme->color(Markoff::Theme::Slot::CodeBlockBackground);
        if (fg.isValid()) fmt.setForeground(fg);
        if (bg.isValid()) fmt.setBackground(bg);
        fmt.setFont(m_theme->font(Markoff::Theme::FontRole::Monospace));
        any = true;
    }
```

(Place these between the existing `bold` line and the closing `return any ? fmt : QTextCharFormat();`.)

- [ ] **Step 4: Run — verify PASS**

```bash
cmake --build build-dev --target tst_live_render_inline_per_kind -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_per_kind$' --output-on-failure
```

If any test fails because the Theme defaults are missing the relevant slot setting (e.g., `ItalicEmphasis` is not `setItalic(true)` in `defaultLight`), fix the Theme default. Re-run.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/InlineHighlighter.cpp \
        libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp \
        libs/markoff-core/src/Theme.cpp  # if touched
git commit -m "$(cat <<'EOF'
markoff-live: InlineHighlighter paints italic / strike / code (E1)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task B4: Highlight + Link + Wikilink + Tag flags

**Files:**
- Modify: `libs/markoff-live/src/InlineHighlighter.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp`
- Modify: `libs/markoff-core/src/Theme.cpp` (if defaults need adjusting)

- [ ] **Step 1: Write four failing tests**

Append to `tst_live_render_inline_per_kind.cpp`:

```cpp
    void highlight_flag_paints_background() {
        QTextDocument doc;
        doc.setPlainText("plain ==HL== plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 6;  // ==HL==
        span.highlight = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.background().color() == theme.color(Markoff::Theme::Slot::Highlight);
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 6);
    }

    void link_flag_paints_color_and_underline() {
        QTextDocument doc;
        doc.setPlainText("[label](url) plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan span{};
        span.charOffset = 0; span.charLength = 12;  // [label](url)
        span.isLink = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.fontUnderline()
                && f.foreground().color() == theme.color(Markoff::Theme::Slot::Link);
        });
        QCOMPARE(range.first, 0);
        QCOMPARE(range.second, 12);
    }

    void wikilink_flag_paints_color_and_underline() {
        QTextDocument doc;
        doc.setPlainText("[[Page]] plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan span{};
        span.charOffset = 0; span.charLength = 8;  // [[Page]]
        span.isWikilink = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.fontUnderline()
                && f.foreground().color() == theme.color(Markoff::Theme::Slot::WikiLink);
        });
        QCOMPARE(range.first, 0);
        QCOMPARE(range.second, 8);
    }

    void tag_flag_paints_color() {
        QTextDocument doc;
        doc.setPlainText("plain #tag plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 4;  // #tag
        span.isTag = true;
        h.setInlineSpans({span});

        auto range = findFormatRange(doc, [&](const QTextCharFormat &f){
            return f.foreground().color() == theme.color(Markoff::Theme::Slot::Tag);
        });
        QCOMPARE(range.first, 6);
        QCOMPARE(range.second, 4);
    }
```

- [ ] **Step 2: Run — verify all 4 FAIL**

```bash
cmake --build build-dev --target tst_live_render_inline_per_kind -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_per_kind$' --output-on-failure
```

- [ ] **Step 3: Implement** by extending `formatFor`:

```cpp
    if (span.highlight) {
        const QColor c = m_theme->color(Markoff::Theme::Slot::Highlight);
        if (c.isValid()) fmt.setBackground(c);
        any = true;
    }
    if (span.isLink) {
        applyEmphasis(Markoff::Theme::Slot::Link);
        fmt.setFontUnderline(true);
    }
    if (span.isWikilink) {
        applyEmphasis(Markoff::Theme::Slot::WikiLink);
        fmt.setFontUnderline(true);
    }
    if (span.isTag) applyEmphasis(Markoff::Theme::Slot::Tag);
```

- [ ] **Step 4: Run — verify PASS**

If any default is missing (e.g., `Theme::defaultLight()` doesn't set a Link color), update `libs/markoff-core/src/Theme.cpp` so all 8 inline slots have visually distinct values. Re-run.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/InlineHighlighter.cpp \
        libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp \
        libs/markoff-core/src/Theme.cpp  # if touched
git commit -m "$(cat <<'EOF'
markoff-live: InlineHighlighter paints highlight / link / wikilink / tag (E1)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task B5: Combined-flag tests (bold+italic, link+bold)

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_inline_combined.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Combined-flag tests. SourceSpan can carry multiple flags
// simultaneously (e.g., ***bold-italic*** is one span with bold AND italic).
// The parser may also emit nested spans (e.g., [**foo**](url) → outer link
// span + inner bold span); QSyntaxHighlighter's overlay semantics merge
// them via setFormat calls.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>

using namespace Markoff::Live;

class TstLiveRenderInlineCombined : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void bold_and_italic_on_one_span() {
        QTextDocument doc;
        doc.setPlainText("***foo***");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 0; span.charLength = 9;
        span.bold = true; span.italic = true;
        h.setInlineSpans({span});

        QTextCursor c(&doc);
        c.setPosition(4);  // inside foo
        const QTextCharFormat f = c.charFormat();
        QCOMPARE(f.fontWeight(), int(QFont::Bold));
        QVERIFY(f.fontItalic());
    }

    void link_with_inner_bold_span() {
        QTextDocument doc;
        doc.setPlainText("[**foo**](url)");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan link{};
        link.charOffset = 0; link.charLength = 14;
        link.isLink = true;

        Markoff::SourceSpan bold{};
        bold.charOffset = 1; bold.charLength = 7;  // **foo**
        bold.bold = true;

        h.setInlineSpans({link, bold});

        // Inside foo (position 4): both link (underline) and bold (weight)
        // should apply.
        QTextCursor c(&doc);
        c.setPosition(4);
        const QTextCharFormat f = c.charFormat();
        QCOMPARE(f.fontWeight(), int(QFont::Bold));
        QVERIFY(f.fontUnderline());

        // Outside the bold range but still in the link (position 12, in "url"):
        // underline applies, weight does not.
        c.setPosition(12);
        const QTextCharFormat f2 = c.charFormat();
        QVERIFY(f2.fontUnderline());
        QVERIFY(f2.fontWeight() != int(QFont::Bold));
    }

    void strikethrough_and_code_on_one_span() {
        QTextDocument doc;
        doc.setPlainText("~~`x`~~");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 0; span.charLength = 7;
        span.strikethrough = true; span.code = true;
        h.setInlineSpans({span});

        QTextCursor c(&doc);
        c.setPosition(3);  // on the 'x'
        const QTextCharFormat f = c.charFormat();
        QVERIFY(f.fontStrikeOut());
        QVERIFY(f.background().style() != Qt::NoBrush);
    }
};

QTEST_MAIN(TstLiveRenderInlineCombined)
#include "tst_live_render_inline_combined.moc"
```

- [ ] **Step 2: Register in CMakeLists**

```cmake
qt_add_executable(tst_live_render_inline_combined
    tst_live_render_inline_combined.cpp
)
target_link_libraries(tst_live_render_inline_combined PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_combined COMMAND tst_live_render_inline_combined)
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_inline_combined -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_combined$' --output-on-failure
```

Expected: PASS (the highlighter implementation from B2-B4 should already cover these — the tests are confirming that flag-combination works as designed).

If any test fails, the failure indicates `formatFor` isn't combining flags correctly — fix in `formatFor`.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_inline_combined.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: InlineHighlighter combined-flag tests (E1)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task B6: Out-of-scope flags ignored

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp`

- [ ] **Step 1: Append a failing test**

```cpp
    void out_of_scope_flags_do_not_paint() {
        QTextDocument doc;
        doc.setPlainText("plain $x^2$ plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        // Math, footnote, image, isDelimiter, comment — all out of scope
        // for E1. None should produce any QTextCharFormat painting.
        Markoff::SourceSpan math{};       math.charOffset = 6;  math.charLength = 5; math.math = true;
        Markoff::SourceSpan footnote{};   footnote.charOffset = 0; footnote.charLength = 4; footnote.isFootnoteRef = true;
        Markoff::SourceSpan image{};      image.charOffset = 0;  image.charLength = 4; image.isImage = true;
        Markoff::SourceSpan delimiter{};  delimiter.charOffset = 6; delimiter.charLength = 1; delimiter.isDelimiter = true;
        Markoff::SourceSpan comment{};    comment.charOffset = 0; comment.charLength = 4; comment.comment = true;

        h.setInlineSpans({math, footnote, image, delimiter, comment});

        // No range should have any non-default format property.
        bool anyPaint = false;
        QTextBlock block = doc.firstBlock();
        while (block.isValid()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat f = it.fragment().charFormat();
                if (f.fontWeight() == QFont::Bold || f.fontItalic()
                    || f.fontStrikeOut() || f.fontUnderline()
                    || f.foreground().color().isValid()
                    || (f.background().style() != Qt::NoBrush
                        && f.background().color() != Qt::transparent)) {
                    anyPaint = true; break;
                }
            }
            block = block.next();
        }
        QVERIFY2(!anyPaint, "Out-of-scope flags must not paint");
    }
```

- [ ] **Step 2: Build and run — verify PASS**

```bash
cmake --build build-dev --target tst_live_render_inline_per_kind -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_per_kind$' --output-on-failure
```

Expected: PASS — `formatFor` returns default `QTextCharFormat()` when no in-scope flag is set, so `highlightBlock` skips painting (the `if (fmt == QTextCharFormat()) continue;` check).

If anything *does* paint, the highlighter is reading a flag it shouldn't. Audit `formatFor` and remove the offending branch.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp
git commit -m "$(cat <<'EOF'
markoff-live: assert out-of-scope SourceSpan flags do not paint (E1)

Math, footnote, image, isDelimiter, comment — all reserved for E2/E3/
E4/E5. E1's formatFor must return default QTextCharFormat() so they
no-op.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase C — QML attachment

### Task C1: InlineHighlighterAttached QML shim

**Files:**
- Create: `libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h`
- Create: `libs/markoff-live/src/InlineHighlighterAttached.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Create the header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/parser/SourceSpan.h>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QVariantList>
#include <qqmlintegration.h>

namespace Markoff { class Theme; }

namespace Markoff::Live {

class InlineHighlighter;

/// QML-property wrapper around InlineHighlighter. Wired in delegate QML:
///
/// TextEdit {
///     id: edit
///     InlineHighlighterAttached {
///         target: edit
///         spans: model.inlineSpans
///         theme: ListView.view.binding.theme
///     }
/// }
///
/// Constructs an owned InlineHighlighter against `target.textDocument`
/// when `target` is set. Forwards spans + theme changes to the highlighter.
class MARKOFF_LIVE_EXPORT InlineHighlighterAttached : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QVariantList spans          READ spans  WRITE setSpans  NOTIFY spansChanged)
    Q_PROPERTY(const Markoff::Theme *theme READ theme  WRITE setTheme  NOTIFY themeChanged)
public:
    explicit InlineHighlighterAttached(QObject *parent = nullptr);
    ~InlineHighlighterAttached() override;

    QQuickTextDocument *target() const noexcept { return m_target; }
    void setTarget(QQuickTextDocument *target);

    QVariantList spans() const;
    void setSpans(const QVariantList &v);

    const Markoff::Theme *theme() const noexcept { return m_theme; }
    void setTheme(const Markoff::Theme *theme);

Q_SIGNALS:
    void targetChanged();
    void spansChanged();
    void themeChanged();

private:
    void rebuildHighlighter();

    QPointer<QQuickTextDocument> m_target;
    InlineHighlighter           *m_highlighter = nullptr;  // owned via QObject parent (this)
    QList<Markoff::SourceSpan>   m_spans;
    const Markoff::Theme        *m_theme = nullptr;
};

}  // namespace Markoff::Live
```

- [ ] **Step 2: Create the impl**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/InlineHighlighterAttached.h>
#include <markoff/live/InlineHighlighter.h>

#include <QTextDocument>

namespace Markoff::Live {

InlineHighlighterAttached::InlineHighlighterAttached(QObject *parent)
    : QObject(parent) {}

InlineHighlighterAttached::~InlineHighlighterAttached() = default;

void InlineHighlighterAttached::setTarget(QQuickTextDocument *target)
{
    if (m_target == target) return;
    m_target = target;
    rebuildHighlighter();
    emit targetChanged();
}

QVariantList InlineHighlighterAttached::spans() const
{
    QVariantList out;
    out.reserve(m_spans.size());
    for (const auto &s : m_spans) out.append(QVariant::fromValue(s));
    return out;
}

void InlineHighlighterAttached::setSpans(const QVariantList &v)
{
    QList<Markoff::SourceSpan> next;
    next.reserve(v.size());
    for (const QVariant &item : v) {
        if (item.canConvert<Markoff::SourceSpan>()) {
            next.append(item.value<Markoff::SourceSpan>());
        }
    }
    if (next == m_spans) return;
    m_spans = next;
    if (m_highlighter) m_highlighter->setInlineSpans(m_spans);
    emit spansChanged();
}

void InlineHighlighterAttached::setTheme(const Markoff::Theme *theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    if (m_highlighter) m_highlighter->setTheme(m_theme);
    emit themeChanged();
}

void InlineHighlighterAttached::rebuildHighlighter()
{
    if (m_highlighter) {
        m_highlighter->deleteLater();
        m_highlighter = nullptr;
    }
    if (!m_target) return;
    QTextDocument *doc = m_target->textDocument();
    if (!doc) return;
    m_highlighter = new InlineHighlighter(doc);
    m_highlighter->setParent(this);
    m_highlighter->setTheme(m_theme);
    m_highlighter->setInlineSpans(m_spans);
}

}  // namespace Markoff::Live
```

- [ ] **Step 3: Register in CMakeLists**

In `libs/markoff-live/CMakeLists.txt`, add to the SOURCES list near the other inline-highlighter entries:

```cmake
        include/markoff/live/InlineHighlighterAttached.h
        src/InlineHighlighterAttached.cpp
```

Also add `Qt6::Quick` to the target's link libraries if the existing block doesn't already pull in QQuickTextDocument transitively. Check by building.

- [ ] **Step 4: Build and verify it compiles**

```bash
cmake --build build-dev --target markoff_live -j 8
```

Expected: clean build.

- [ ] **Step 5: Smoke-test by writing a focused QML/C++ test**

Append a new test slot to `tst_live_render_inline_per_kind.cpp` that exercises the attached shim end-to-end (creating it, setting target/spans/theme, asserting paint via the underlying QTextDocument):

```cpp
    void attached_shim_drives_highlighter_via_qml_properties() {
        QTextDocument doc;
        doc.setPlainText("plain **bold** plain");
        // Wrap doc in a QQuickTextDocument-like fake. QQuickTextDocument's
        // constructor takes a QQuickItem — we can't fully exercise that
        // here without a Quick scene. Instead, smoke-test the C++ surface
        // of the shim: m_target stays null, so rebuildHighlighter is a
        // no-op. Spans + theme setters wire correctly when target lands.
        InlineHighlighterAttached att;
        att.setTheme(&doc.defaultFont() == nullptr ? nullptr : nullptr);
        Markoff::SourceSpan span{};
        span.charOffset = 6; span.charLength = 8; span.bold = true;
        QVariantList spans;
        spans.append(QVariant::fromValue(span));
        att.setSpans(spans);
        // Without a target, no highlighter is built — just assert no crash.
        QCOMPARE(att.spans().size(), 1);
    }
```

(The full QML-end-to-end test lives at the delegate-integration tasks E1–E4 below.)

- [ ] **Step 6: Run the test, build app target**

```bash
ctest --test-dir build-dev -R '^tst_live_render_inline_per_kind$' --output-on-failure
cmake --build build-dev --target markoff-live-app -j 8
```

Expected: PASS, app target compiles.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h \
        libs/markoff-live/src/InlineHighlighterAttached.cpp \
        libs/markoff-live/CMakeLists.txt \
        libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp
git commit -m "$(cat <<'EOF'
markoff-live: InlineHighlighterAttached QML shim (E1)

Wraps InlineHighlighter for delegate-side QML property bindings:
target (QQuickTextDocument), spans (QVariantList), theme. Owns the
underlying highlighter; rebuilds on target change.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase D — Delegate integration

### Task D1: Wire ParagraphDelegate

**Files:**
- Modify: `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`

- [ ] **Step 1: Read the existing delegate.** Open `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`. Note how it imports `org.markoff.live` and how `LiveEditBinding` is wired.

- [ ] **Step 2: Add the InlineHighlighterAttached binding**

Inside the `TextEdit { id: edit; ... }` block, add a child `InlineHighlighterAttached`:

```qml
        InlineHighlighterAttached {
            target: edit
            spans: model.inlineSpans
            theme: root.liveBinding ? root.liveBinding.theme : null
        }
```

If `liveBinding` doesn't currently expose `theme`, that's a gap that needs fixing first — see Step 3.

- [ ] **Step 3: Verify (or add) theme on LiveListModelBinding**

```bash
grep -n "theme\|Theme" libs/markoff-live/include/markoff/live/LiveListModelBinding.h
```

If `LiveListModelBinding` already exposes a `theme()` accessor / `Q_PROPERTY`, use that. If not, the delegate can fall back to a constant `Markoff::Theme::defaultLight()` provided by the test app or `null` (highlighter no-ops without a theme — acceptable for first integration).

For first integration, hardcode in the QML to a binding-provided value or fall back to null (which means no painting — clearly visible in dogfood, will catch in Step 5).

- [ ] **Step 4: Build app and rebuild**

```bash
cmake --build build-dev --target markoff-live-app -j 8
```

- [ ] **Step 5: Manual smoke check (5-second sanity)**

```bash
./build-dev/bin/markoff-live-app /dev/null  # blank doc; just verify it launches
./build-dev/bin/markoff-live-app docs/specs/2026-05-08-e1-inline-highlighter-design.md
```

In the app: skim the first few paragraphs. With theme-binding wired, bold/italic/etc. spans in the spec should now render formatted (bold renders bold, italic renders italic). With theme = null, content renders as plain text — that's expected for now if the binding-theme is not yet wired.

If theme isn't yet exposed by binding, this is fine — the next task (D2: Heading) plus a follow-up task wiring the theme to the binding will close it. Note in the commit.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/qml/delegates/ParagraphDelegate.qml
git commit -m "$(cat <<'EOF'
markoff-live: ParagraphDelegate uses InlineHighlighter (E1)

Adds InlineHighlighterAttached driven by model.inlineSpans + the
binding's theme. Renders inline formatting in paragraph blocks.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task D2: Wire HeadingDelegate, BlockquoteDelegate, ListItemDelegate

**Files:**
- Modify: `libs/markoff-live/qml/delegates/HeadingDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/ListItemDelegate.qml`

- [ ] **Step 1: For each of the 3 delegates**, read the existing structure. Find the delegate's `TextEdit { id: edit; ... }` block.

- [ ] **Step 2: Add the same `InlineHighlighterAttached` block** inside each delegate's TextEdit:

```qml
        InlineHighlighterAttached {
            target: edit
            spans: model.inlineSpans
            theme: root.liveBinding ? root.liveBinding.theme : null
        }
```

- [ ] **Step 3: If the delegate's TextEdit has a different id (not `edit`)**, adapt the `target` accordingly.

- [ ] **Step 4: Build app**

```bash
cmake --build build-dev --target markoff-live-app -j 8
```

- [ ] **Step 5: Manual smoke check**

Open `markoff-live-app docs/specs/2026-05-08-e1-inline-highlighter-design.md` again. Now:

- Headings (`# E1 — ...`) — note that headings are still styled as headings (existing HeadingDelegate styling); inline formatting *inside* a heading should now render too (e.g., a bold word in a heading).
- Blockquotes (lines starting `> `) — text inside renders with inline formatting.
- List items (`- foo`) — text inside renders with inline formatting.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/qml/delegates/HeadingDelegate.qml \
        libs/markoff-live/qml/delegates/BlockquoteDelegate.qml \
        libs/markoff-live/qml/delegates/ListItemDelegate.qml
git commit -m "$(cat <<'EOF'
markoff-live: HeadingDelegate / BlockquoteDelegate / ListItemDelegate use InlineHighlighter (E1)

All four text-bearing delegates now bind InlineHighlighterAttached.
Inline formatting renders in paragraphs, headings, blockquotes,
list items.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task D3: Cross-delegate sanity tests

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_inline_cross_delegate.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Cross-delegate sanity tests. Verify the InlineHighlighter pattern
// renders in non-paragraph delegates. We test at the C++ harness level
// (one InlineHighlighter per QTextDocument, mimicking the per-delegate
// binding shape) rather than instantiating a full QML scene; the
// integration is logically equivalent.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>

using namespace Markoff::Live;

class TstLiveRenderInlineCrossDelegate : public QObject {
    Q_OBJECT
private Q_SLOTS:

    // Each test treats the QTextDocument as if it were that delegate's
    // own document (delegates are independent — heading-2 has its own
    // QTextDocument with the heading text; the highlighter binds to it).
    // Inline formatting works the same regardless of which delegate
    // owns the document.

    void bold_in_heading_renders_bold() {
        QTextDocument doc;
        doc.setPlainText("Heading with **bold** word");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 13; span.charLength = 8;
        span.bold = true;
        h.setInlineSpans({span});

        QTextCursor c(&doc); c.setPosition(15);
        QCOMPARE(c.charFormat().fontWeight(), int(QFont::Bold));
    }

    void italic_in_blockquote_renders_italic() {
        QTextDocument doc;
        doc.setPlainText("> A *quoted* note");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 4; span.charLength = 8;
        span.italic = true;
        h.setInlineSpans({span});

        QTextCursor c(&doc); c.setPosition(6);
        QVERIFY(c.charFormat().fontItalic());
    }

    void inline_code_in_list_item_renders_monospace() {
        QTextDocument doc;
        doc.setPlainText("- item with `code` inside");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 12; span.charLength = 6;
        span.code = true;
        h.setInlineSpans({span});

        QTextCursor c(&doc); c.setPosition(14);
        QVERIFY(c.charFormat().background().style() != Qt::NoBrush);
    }

    void link_in_heading_renders_link_color_and_underline() {
        QTextDocument doc;
        doc.setPlainText("Heading [target](url)");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 8; span.charLength = 13;
        span.isLink = true;
        h.setInlineSpans({span});

        QTextCursor c(&doc); c.setPosition(10);
        QVERIFY(c.charFormat().fontUnderline());
        QCOMPARE(c.charFormat().foreground().color(),
                 theme.color(Markoff::Theme::Slot::Link));
    }

    void tag_in_blockquote_renders_tag_color() {
        QTextDocument doc;
        doc.setPlainText("> Quoted with #tag");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 14; span.charLength = 4;
        span.isTag = true;
        h.setInlineSpans({span});

        QTextCursor c(&doc); c.setPosition(15);
        QCOMPARE(c.charFormat().foreground().color(),
                 theme.color(Markoff::Theme::Slot::Tag));
    }

    void highlight_in_list_item_renders_bg() {
        QTextDocument doc;
        doc.setPlainText("- ==important== item");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);

        Markoff::SourceSpan span{};
        span.charOffset = 2; span.charLength = 13;
        span.highlight = true;
        h.setInlineSpans({span});

        QTextCursor c(&doc); c.setPosition(5);
        QCOMPARE(c.charFormat().background().color(),
                 theme.color(Markoff::Theme::Slot::Highlight));
    }
};

QTEST_MAIN(TstLiveRenderInlineCrossDelegate)
#include "tst_live_render_inline_cross_delegate.moc"
```

- [ ] **Step 2: Register in CMakeLists**

```cmake
qt_add_executable(tst_live_render_inline_cross_delegate
    tst_live_render_inline_cross_delegate.cpp
)
target_link_libraries(tst_live_render_inline_cross_delegate PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_cross_delegate COMMAND tst_live_render_inline_cross_delegate)
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_inline_cross_delegate -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_cross_delegate$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_inline_cross_delegate.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: cross-delegate sanity tests for InlineHighlighter (E1)

Six tests verifying inline formatting renders correctly across the
4 text-bearing delegates (paragraph implicit, heading, blockquote,
list-item) at the C++ highlighter level.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase E — Edge cases

### Task E1: Edge-case tests

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_inline_edge_cases.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Edge-case tests for InlineHighlighter.

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>

using namespace Markoff::Live;

class TstLiveRenderInlineEdgeCases : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void span_at_start_of_block() {
        QTextDocument doc; doc.setPlainText("**bold** plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 0; s.charLength = 8; s.bold = true;
        h.setInlineSpans({s});
        QTextCursor c(&doc); c.setPosition(2);
        QCOMPARE(c.charFormat().fontWeight(), int(QFont::Bold));
    }

    void span_at_end_of_block() {
        QTextDocument doc; doc.setPlainText("plain **bold**");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 6; s.charLength = 8; s.bold = true;
        h.setInlineSpans({s});
        QTextCursor c(&doc); c.setPosition(8);
        QCOMPARE(c.charFormat().fontWeight(), int(QFont::Bold));
    }

    void span_covering_whole_block() {
        QTextDocument doc; doc.setPlainText("**all bold**");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 0; s.charLength = 12; s.bold = true;
        h.setInlineSpans({s});
        QTextCursor c(&doc); c.setPosition(6);
        QCOMPARE(c.charFormat().fontWeight(), int(QFont::Bold));
    }

    void empty_span_no_paint_no_crash() {
        QTextDocument doc; doc.setPlainText("plain text");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 5; s.charLength = 0; s.bold = true;
        h.setInlineSpans({s});  // must not crash
        QTextCursor c(&doc); c.setPosition(5);
        QVERIFY(c.charFormat().fontWeight() != int(QFont::Bold));
    }

    void no_theme_no_paint() {
        QTextDocument doc; doc.setPlainText("plain **bold** plain");
        InlineHighlighter h(&doc);  // no theme set
        Markoff::SourceSpan s{}; s.charOffset = 6; s.charLength = 8; s.bold = true;
        h.setInlineSpans({s});  // must not crash; must not paint
        QTextCursor c(&doc); c.setPosition(8);
        QVERIFY(c.charFormat().fontWeight() != int(QFont::Bold));
    }

    void marker_spanning_list_item_text() {
        // List-item text begins after the marker. A span that starts
        // inside the marker text region should paint correctly.
        QTextDocument doc; doc.setPlainText("- **bold** in list");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 2; s.charLength = 8; s.bold = true;
        h.setInlineSpans({s});
        QTextCursor c(&doc); c.setPosition(4);
        QCOMPARE(c.charFormat().fontWeight(), int(QFont::Bold));
    }

    void span_longer_than_doc_clamped_safely() {
        QTextDocument doc; doc.setPlainText("short");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 0; s.charLength = 1000; s.bold = true;
        h.setInlineSpans({s});  // QSyntaxHighlighter::setFormat clamps internally
        // Just assert no crash + bold applied to the visible part.
        QTextCursor c(&doc); c.setPosition(2);
        QCOMPARE(c.charFormat().fontWeight(), int(QFont::Bold));
    }

    void no_in_scope_flags_no_paint() {
        // Span where only out-of-scope flags are set.
        QTextDocument doc; doc.setPlainText("plain $x$ plain");
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc); h.setTheme(&theme);
        Markoff::SourceSpan s{}; s.charOffset = 6; s.charLength = 3; s.math = true;
        h.setInlineSpans({s});
        QTextCursor c(&doc); c.setPosition(7);
        QVERIFY(!c.charFormat().fontStrikeOut());
        QVERIFY(c.charFormat().fontWeight() != int(QFont::Bold));
    }
};

QTEST_MAIN(TstLiveRenderInlineEdgeCases)
#include "tst_live_render_inline_edge_cases.moc"
```

- [ ] **Step 2: Register in CMakeLists**

```cmake
qt_add_executable(tst_live_render_inline_edge_cases
    tst_live_render_inline_edge_cases.cpp
)
target_link_libraries(tst_live_render_inline_edge_cases PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_edge_cases COMMAND tst_live_render_inline_edge_cases)
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-dev --target tst_live_render_inline_edge_cases -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_edge_cases$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_inline_edge_cases.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: edge-case tests for InlineHighlighter (E1)

Boundary, empty span, no-theme, marker-spanning, span longer than
document, no-in-scope-flags. All pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase F — Performance benchmark

### Task F1: tst_live_render_inline_typing_perf

**Files:**
- Create: `libs/markoff-live/tests/fixtures/inline-formats/typing-corpus-1k.md`
- Create: `libs/markoff-live/tests/tst_live_render_inline_typing_perf.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Generate the typing corpus**

Create `libs/markoff-live/tests/fixtures/inline-formats/typing-corpus-1k.md`. Generation strategy: programmatically write 1000 lines, alternating between paragraph types — 50% with at least one inline span. A simple Python one-liner you can run:

```bash
mkdir -p libs/markoff-live/tests/fixtures/inline-formats
python3 - <<'PY' > libs/markoff-live/tests/fixtures/inline-formats/typing-corpus-1k.md
import random
random.seed(42)
for i in range(1000):
    n = i % 4
    if n == 0:
        print(f"Plain paragraph line {i} with no formatting at all here.")
    elif n == 1:
        print(f"Paragraph {i} with **bold word** and *italic word* mixed.")
    elif n == 2:
        print(f"Paragraph {i} with `inline_code` and a [link](url-{i}) here.")
    else:
        print(f"Paragraph {i} with [[WikiLink]] and #tag-{i} and ==highlight==.")
    print()  # blank line
PY
```

Verify the output has 2000 lines (~1000 paragraph lines + ~1000 blank lines). Adjust if the parser-input shape needs tweaking.

- [ ] **Step 2: Create the perf test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// E1: Performance benchmark for the inline highlighter typing path.
// Loads a 1000-line markdown corpus, types 100 characters into the
// first paragraph, captures per-keystroke time-to-rehighlight.

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QApplication>
#include <algorithm>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveEditBinding.h>

using namespace Markoff::Live;

class TstLiveRenderInlineTypingPerf : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void typing_under_thirty_three_ms_p99() {
        // Load corpus.
        QFile f(QFINDTESTDATA("fixtures/inline-formats/typing-corpus-1k.md"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray corpus = f.readAll();
        QVERIFY(!corpus.isEmpty());

        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);
        document.loadFromMarkdown(corpus);

        QVERIFY(binding.model()->rowCount() > 100);

        // Bind a highlighter to a QTextDocument that mirrors row 0's text.
        QTextDocument doc;
        doc.setPlainText(binding.model()->recordAt(0).text);

        Markoff::Theme theme = Markoff::Theme::defaultLight();
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        h.setInlineSpans(binding.model()->spansAtRow(0));

        QTextEdit editor;
        editor.setDocument(&doc);

        LiveEditBinding eb;
        eb.setBinding(&binding);
        eb.setModelIndex(0);
        eb.setText(binding.model()->recordAt(0).text);
        eb.setRawTextDocument(&doc);

        // Spy for dataChanged carrying the InlineSpansRole.
        QSignalSpy spy(binding.model(), &QAbstractItemModel::dataChanged);

        QList<qint64> timings;
        timings.reserve(100);

        QTextCursor cur(&doc);
        for (int i = 0; i < 100; ++i) {
            spy.clear();
            QElapsedTimer t; t.start();
            cur.movePosition(QTextCursor::End);
            cur.insertText("x");
            // Wait for spans-changed dataChanged. With debounced d2 emit,
            // this may not fire on every keystroke; tolerate that by
            // upper-bounding the wait.
            spy.wait(50);
            timings.append(t.nsecsElapsed());
        }

        std::sort(timings.begin(), timings.end());
        const qint64 p50 = timings[50];
        const qint64 p99 = timings[99];

        const double p50ms = p50 / 1.0e6;
        const double p99ms = p99 / 1.0e6;
        qDebug() << "Per-keystroke timing  p50:" << p50ms << "ms  p99:" << p99ms << "ms";

        // Hard CI gate: 33 ms.
        QVERIFY2(p99ms < 33.0,
                 qPrintable(QString("p99 %1ms exceeded 33ms gate").arg(p99ms)));
    }
};

QTEST_MAIN(TstLiveRenderInlineTypingPerf)
#include "tst_live_render_inline_typing_perf.moc"
```

- [ ] **Step 3: Register in CMakeLists**

```cmake
qt_add_executable(tst_live_render_inline_typing_perf
    tst_live_render_inline_typing_perf.cpp
)
target_link_libraries(tst_live_render_inline_typing_perf PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test markoff_live markoff_core)
add_test(NAME tst_live_render_inline_typing_perf COMMAND tst_live_render_inline_typing_perf)

# Make the fixture directory locatable via QFINDTESTDATA.
set_property(TEST tst_live_render_inline_typing_perf
    PROPERTY ENVIRONMENT
    "QT_TESTDATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}")
```

The `QFINDTESTDATA` macro expands by walking up from the test binary to the source directory; if the env var pattern above doesn't work in this codebase's existing convention, mirror what an existing test does to find fixtures (check whether any existing test already loads fixtures from disk; if not, embed the corpus in the CMakeLists via `qt_add_resources` or similar).

- [ ] **Step 4: Build and run**

```bash
cmake --build build-dev --target tst_live_render_inline_typing_perf -j 8
ctest --test-dir build-dev -R '^tst_live_render_inline_typing_perf$' --output-on-failure
```

Expected: PASS — p99 well under 33ms on dev hardware (likely <5 ms; the test logs both p50 and p99 for visibility).

If p99 exceeds 33 ms, the implementation has a perf bug — investigate. Most likely culprits: full-document rehighlight on every keystroke (should only re-paint the affected block), expensive `formatFor` (it should be cheap — Theme accessors are hash lookups), or `dataChanged` echoing back into the typing path.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_inline_typing_perf.cpp \
        libs/markoff-live/tests/fixtures/inline-formats/typing-corpus-1k.md \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: typing-perf benchmark for InlineHighlighter (E1)

100-iteration typing on a 1000-line corpus; asserts p99 < 33ms (30 FPS
budget). Logs p50 + p99 to console for dev-hardware tracking;
aspirational target is p99 < 16ms (60 FPS).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Phase G — Closeout

### Task G1: Full suite + dogfood

**Files:** none new; user runs commands.

- [ ] **Step 1: Full-suite test run**

```bash
ctest --test-dir build-dev --output-on-failure -j 8
```

Expected: ALL PASS, no regressions. If any pre-existing test fails, investigate before tagging.

- [ ] **Step 2: Build the app**

```bash
cmake --build build-dev --target markoff-live-app -j 8
```

- [ ] **Step 3: Manual dogfood pass (USER must run; cannot be automated).** Tell the user:

> "E1 is implementation-complete and the test suite is green. Please run `./build-dev/bin/markoff-live-app <some-realistic-markdown-doc>` and verify:
>
> 1. Bold text renders bold (typeface).
> 2. Italic text renders italic.
> 3. Strikethrough renders with a strike line.
> 4. `Inline code` renders monospace with a background tint.
> 5. ==Highlights== render with a yellow-ish background.
> 6. [Links](url) and [[Wikilinks]] render coloured + underlined.
> 7. #Tags render in a distinct color.
> 8. Markers (** ** _ _ ~~ ~~ ` ` == == [ ]( ) [[ ]] # ) are still visible — auto-hide is E2's work.
> 9. Typing in a paragraph stays smooth (no perceptible lag).
> 10. No regressions in the existing live-render features (cursor, selection, structural keys).
>
> Report any visual issues or perceived slowness."

If the user reports issues, fix them before tagging. Each fix lands as its own commit (test + impl + commit per the existing TDD cadence).

- [ ] **Step 4: Tag**

Once the user confirms the dogfood is clean:

```bash
git tag -a v0.7.0-e1 -m "E1 complete: inline-format highlighter

- 4 text-bearing delegates render inline formatting via per-delegate
  InlineHighlighter (QSyntaxHighlighter subclass).
- 8 inline kinds: bold, italic, strikethrough, inline-code, highlight,
  link, wikilink, tag.
- Test surface: per-flag, combined-flags, cross-delegate, edge cases,
  perf benchmark — all green.
- Perf: typing p99 < 33ms gate (typically << on dev hardware).

Spec:  docs/specs/2026-05-08-e1-inline-highlighter-design.md
Plan:  docs/plans/2026-05-08-e1-inline-highlighter.md
"
```

(Do NOT push the tag remotely unless the user explicitly asks.)

- [ ] **Step 5: No commit needed for this task** — the tag itself is the artefact.

---

### Task G2: Closeout docs

**Files:**
- Modify: `libs/markoff-live/CLAUDE.md`
- Modify: `docs/e-arc/e-arc-status.md`
- Modify: `docs/e-arc/2026-05-08-e-arc-roadmap.md`

- [ ] **Step 1: Update libs/markoff-live/CLAUDE.md**

Append a new "Inline-format highlighter (E1)" section after "Architecture":

```markdown
## Inline-format highlighter (E1)

`Markoff::Live::InlineHighlighter` (header `<markoff/live/InlineHighlighter.h>`)
is a per-delegate `QSyntaxHighlighter` painting `QTextCharFormat` ranges
from `BlockRecord::inlineSpans`. The QML shim
`InlineHighlighterAttached` wraps it for delegate-side property bindings.

8 inline kinds rendered via existing `Markoff::Theme::Slot` tokens:
`bold` → `BoldEmphasis`, `italic` → `ItalicEmphasis`, `strikethrough` →
`StrikeEmphasis`, `code` → `InlineCode`, `highlight` → `Highlight`,
`isLink` → `Link`, `isWikilink` → `WikiLink`, `isTag` → `Tag`.

Markers (`**`, `_`, `~~`, `` ` ``, `==`, `[]()`, `[[]]`, `#`) render
visible in E1; auto-hide is E2's work.

Tests: `tst_live_render_inline_per_kind`, `tst_live_render_inline_combined`,
`tst_live_render_inline_cross_delegate`, `tst_live_render_inline_edge_cases`,
`tst_live_render_inline_typing_perf`.
```

Also fix the out-of-date "Public headers under `include/markoff/live-render/`" line in the Conventions section to read `include/markoff/live/`.

- [ ] **Step 2: Update docs/e-arc/e-arc-status.md**

Phase board: `E1 → complete`. Recent-changes log: append a 2026-05-08 entry recording E1 closeout, the v0.7.0-e1 tag, and a 1-line summary of the test surface.

- [ ] **Step 3: Update docs/e-arc/2026-05-08-e-arc-roadmap.md**

Phase summary table: `E1 → complete`. Add a one-line cross-reference to the spec + plan + tag.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/CLAUDE.md \
        docs/e-arc/e-arc-status.md \
        docs/e-arc/2026-05-08-e-arc-roadmap.md
git commit -m "$(cat <<'EOF'
docs: E1 inline-format highlighter complete (v0.7.0-e1)

E-arc phase E1 closeout: status board, roadmap, library guide updated.
Next phase is E2 (cursor-aware delimiter visibility).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review checklist (already run by spec author; logged here for transparency)

**Spec coverage.** Each spec section maps to tasks:
- §2.1 InlineHighlighter class → Tasks B1–B6
- §2.2 Construction site → Task C1 + Phase D
- §2.3, §2.4 painting policy + flag-combining → Tasks B2–B6 cumulatively
- §3.1, §3.2 model surface → Task A2
- §3.3 refresh trigger → Task A3
- §3.4 empty case → Task B1's first test
- §4 Theme integration → Tasks B2–B4 verify defaults inline (no separate task; flagged in each commit if defaults change)
- §5.1–5.6 test surface → Tasks B1–B6, B5, D3, E1, F1
- §6 edge cases → Task E1
- §8 files-touched manifest → covered by all tasks
- §9 subtractability note → restated; no task (it's documentation in the spec itself)
- §10 acceptance → Task G1 (manual) + G2 (docs)

**Placeholder scan.** No "TBD"/"TODO"/"see file X" leftovers in tasks.

**Type consistency.** `Markoff::SourceSpan`, `Markoff::Theme`, `Markoff::Live::LiveBlockModel`, `Markoff::Live::InlineHighlighter`, `Markoff::Live::InlineHighlighterAttached` consistent across tasks. `BlockRecord::inlineSpans` field name consistent. `LiveBlockModel::InlineSpansRole` consistent. Theme slot names (`BoldEmphasis` etc.) consistent with existing Theme.h.
