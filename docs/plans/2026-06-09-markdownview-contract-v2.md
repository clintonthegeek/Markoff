# MarkdownView Contract v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **PROGRESS — 2026-06-09 session handoff. Tasks 1–8 COMPLETE (implemented,
> spec-reviewed, quality-reviewed, committed). Tasks 9–13 remain.**
> Read `docs/handoff/2026-06-09-contract-v2-arc-handoff.md` before resuming —
> it carries the per-task commit SHAs, the review dispositions, the accumulated
> spec deviations Task 13 must reconcile, and the prepared context for Task 9
> (whose RED-phase test slots are saved as
> `docs/handoff/2026-06-09-task9-red-slots.patch`, ready to apply).
>
> | Task | Commit(s) | Note |
> |---|---|---|
> | 1 base contract | `9156224c` | |
> | 2 contract suites | `e0910d1d` | found+fixed real bug: source verbs ignored read-only |
> | 3 FormatOps hoist | `548f6793` | ops return `std::optional<QtRange>` (deviation, reviewed OK) |
> | 4 BlockPositionWalk | `da8bfc1f` | WalkEntry carries kind/text/frame/qtBlocks |
> | 5 StyledFindAdapter | `cf80da4a` + `f2505748` | in-frame nav scrolls to frame |
> | 6 styled verbs | `0fd75082` | conservative after-frame guard |
> | 7 live cursor mapping | `c2223a0f` (+proof pair `cf601e62`/`bcbeb683`) | seam work, falsifiability in history |
> | 8 read-only gates | `1c0a4d46` (+proof pair `bffcc874`/`bab51a6d`) | +KeyDispatch.js deleteSelection gate the plan missed |
>
> Baseline at handoff: **266/269** via `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
> (the 3 failures are the documented queue-#10 binaries, pre-existing).

**Goal:** Make `Markoff::MarkdownView` the honest common contract of all
three view leaves (find, undo/redo, theme, fontScale, format verbs,
cursor, read-only, editor context) so Corbomite swaps views with zero
`qobject_cast` switches and zero escape-hatch calls.

**Spec:** `docs/specs/2026-06-09-markdownview-contract-v2-design.md`
(read it first — §0 decisions, §3 contract, §4 live honesty, §5
FormatOps, §6 styled find, §7 context feed).

**Architecture:** Grow the existing `MarkdownView` base with virtuals
(safe defaults; undo/redo base-implemented over `undoD2`). Lift the
source leaf's format-op logic into widget-free `Markoff::FormatOps`.
Live leaf gets a real `CursorPos` mapping over the canonical
`LiveCursorState` and read-only via mutation-ingress gates on a single
binding flag. Styled gets a frame-aware find adapter sharing
FormatPass's walk discipline.

**Tech stack:** Qt 6.8+, C++20, QtTest. Build
`cmake --build build-dev -j 8 --target <t>`; run tests
`QT_QPA_PLATFORM=offscreen ./build-dev/bin/<t>`; full baseline
`scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` (expected
**260/263** before this plan; the 3 failures are queue #10, untouched
here).

**Discipline:** INVARIANTS apply — the live-cursor and read-only tasks
are seam work (falsifiability proofs required, stub-then-revert,
committed in history). When a pre-existing test fails, classify
drift-vs-bug before touching anything.

---

## File structure

| File | Role |
|---|---|
| `libs/markoff-core/include/markoff/core/MarkdownView.h` + `src/MarkdownView.cpp` | the contract (Task 1) |
| `libs/markoff-core/include/markoff/core/FormatOps.h` + `src/FormatOps.cpp` (new) | hoisted format-op logic (Task 3) |
| `libs/markoff-core/tests/tst_markdown_view_base.cpp` (new) | base-contract unit tests (Task 1) |
| `libs/markoff-core/tests/tst_format_ops.cpp` (new) | headless FormatOps tests (Task 3) |
| `libs/markoff-source/src/Editor.cpp` / `.h` | format ops → FormatOps wrappers; fontScale; clamp; context feed (Tasks 3, 8, 9, 10) |
| `libs/markoff-styled/src/BlockPositionWalk.{h,cpp}` (new) | frame-aware model-block → QTextBlock walk, extracted from FormatPass (Task 4) |
| `libs/markoff-styled/src/Detail/StyledFindAdapter.{h,cpp}` (new) | styled find (Task 5) |
| `libs/markoff-styled/src/Editor.cpp` / `.h` | find override, format verbs, context feed (Tasks 5, 6, 10) |
| `libs/markoff-live/src/EditorWidget.cpp` + `include/.../EditorWidget.h` | cursor mapping, readOnly push, theme/fontScale forward, verb delegation, context feed (Tasks 7, 8, 11) |
| `libs/markoff-live/src/{LiveEditBinding,LiveStructuralKeyHandler,LiveClipboardController,TableEditBinding,LiveActionController,LiveListModelBinding}.cpp` | read-only ingress gates (Task 8) |
| `libs/*/tests/tst_view_contract_{source,styled,live}.cpp` (new) + `libs/markoff-core/tests/ViewContractChecks.h` (new) | shared contract suites (Tasks 2, 6, 9, 12) |
| `docs/handoff/2026-06-09-corbomite-api-adoption-brief.md` (new) | adoption brief (Task 13) |

Phases land in dependency order: base (1–2) → FormatOps (3) → styled
(4–6) → live (7–8) → context/scale/signals (9–11) → consolidation
(12) → docs (13).

---

### Task 1: Base contract on MarkdownView (TDD)

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/MarkdownView.h`
- Modify: `libs/markoff-core/src/MarkdownView.cpp`
- Create: `libs/markoff-core/tests/tst_markdown_view_base.cpp`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test.** Create
  `libs/markoff-core/tests/tst_markdown_view_base.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Contract tests for the MarkdownView base: default behaviors every
// leaf inherits. Spec: docs/specs/2026-06-09-markdownview-contract-v2-design.md §3.
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/EditorContext.h>
#include <markoff/core/FindController.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

using namespace Markoff;

class TstMarkdownViewBase : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undo_routes_to_undoD2_and_respects_readOnly() {
        MarkdownView v;
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        v.setDocument(&doc);

        // Make one undoable d2 edit through the flat entry point.
        doc.applyFlatEdit(5, 5, QByteArray(" world"), Origin::UserEdit);
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world\n"));

        v.undo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello\n"));
        v.redo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world\n"));

        v.setReadOnly(true);
        v.undo();   // must NOT mutate while read-only
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world\n"));
        v.setReadOnly(false);
        v.undo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello\n"));
    }

    void theme_and_fontScale_store_and_signal() {
        MarkdownView v;
        QSignalSpy themeSpy(&v, &MarkdownView::themeChanged);
        QSignalSpy scaleSpy(&v, &MarkdownView::fontScaleChanged);

        Theme t = Theme::defaultDark();
        v.setTheme(t);
        QCOMPARE(themeSpy.count(), 1);

        QCOMPARE(v.fontScale(), 1.0);
        v.setFontScale(1.5);
        QCOMPARE(v.fontScale(), 1.5);
        QCOMPARE(scaleSpy.count(), 1);
        v.setFontScale(1.5);             // no-op → no second signal
        QCOMPARE(scaleSpy.count(), 1);
        v.setFontScale(99.0);            // clamps to 4.0
        QCOMPARE(v.fontScale(), 4.0);
        v.setFontScale(0.0);             // clamps to 0.25
        QCOMPARE(v.fontScale(), 0.25);
    }

    void find_and_format_defaults_are_safe_noops() {
        MarkdownView v;
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        v.setDocument(&doc);
        FindController fc(&doc);
        v.attachFindController(&fc);   // qWarning + no-op; must not crash
        v.detachFindController();
        v.toggleBold();
        v.toggleItalic();
        v.toggleStrikethrough();
        v.toggleInlineCode();
        v.insertLink();
        v.setHeadingLevel(2);
        QCOMPARE(doc.serializeForSave(), QByteArray("hello\n"));
    }
};

QTEST_MAIN(TstMarkdownViewBase)
#include "tst_markdown_view_base.moc"
```

- [ ] **Step 2: Register the test.** In
  `libs/markoff-core/tests/CMakeLists.txt`, copy the registration
  pattern of an adjacent foundation test:

```cmake
add_executable(tst_markdown_view_base tst_markdown_view_base.cpp)
add_test(NAME tst_markdown_view_base COMMAND tst_markdown_view_base)
target_link_libraries(tst_markdown_view_base PRIVATE Qt6::Test Qt6::Widgets markoff_core)
set_tests_properties(tst_markdown_view_base PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Verify it fails to compile** (the new virtuals don't
  exist yet):
  `cmake -S . -B build-dev && cmake --build build-dev -j 8 --target tst_markdown_view_base`
  → expected: compile errors on `themeChanged`, `undo`, etc.

- [ ] **Step 4: Implement the contract.** In `MarkdownView.h`, add
  includes `<markoff/core/EditorContext.h>`, `<markoff/core/Theme.h>`;
  forward-declare `class FindController;` inside `namespace Markoff`.
  Extend the class:

```cpp
    // --- Find (spec §3). Default: loud no-op. ---
    virtual void attachFindController(FindController *fc);
    virtual void detachFindController();

    // --- Undo/redo: base-implemented over undoD2; no-op while read-only. ---
    virtual void undo();
    virtual void redo();

    // --- Theme / font scale: base stores + signals; leaves override to
    //     apply (call the base first to keep the store coherent). ---
    virtual Theme theme() const;
    virtual void setTheme(const Theme &t);
    virtual qreal fontScale() const;
    virtual void  setFontScale(qreal s);

    // --- Format verbs. Default no-op; hasEditing() advertises support. ---
    virtual void toggleBold() {}
    virtual void toggleItalic() {}
    virtual void toggleStrikethrough() {}
    virtual void toggleInlineCode() {}
    virtual void insertLink() {}
    virtual void setHeadingLevel(int level) { Q_UNUSED(level); }

signals:   // appended to the existing signals block
    void themeChanged();
    void fontScaleChanged(qreal scale);
    void contextChanged(const Markoff::EditorContext &ctx);

private:   // appended members
    Theme m_theme;
    qreal m_fontScale = 1.0;
```

  In `MarkdownView.cpp`:

```cpp
void MarkdownView::attachFindController(FindController *)
{
    qWarning() << metaObject()->className()
               << "does not implement attachFindController(); find is unavailable in this view";
}
void MarkdownView::detachFindController() {}

void MarkdownView::undo()
{
    if (auto *doc = document(); doc && !isReadOnly()) doc->undoD2();
}
void MarkdownView::redo()
{
    if (auto *doc = document(); doc && !isReadOnly()) doc->redoD2();
}

Theme MarkdownView::theme() const { return m_theme; }
void MarkdownView::setTheme(const Theme &t)
{
    m_theme = t;
    emit themeChanged();
}

qreal MarkdownView::fontScale() const { return m_fontScale; }
void MarkdownView::setFontScale(qreal s)
{
    s = std::clamp(s, 0.25, 4.0);
    if (qFuzzyCompare(s, m_fontScale)) return;
    m_fontScale = s;
    emit fontScaleChanged(s);
}
```

  Add `#include <algorithm>` and `#include <QDebug>` and
  `#include <markoff/core/MarkoffDocument.h>` to the .cpp.

- [ ] **Step 5: Resolve derived-class name collisions.** The leaves
  already declare some of these names; make them overrides, not
  shadows:
  - `libs/markoff-source/include/markoff/source/Editor.h`: mark
    `setTheme` `override`; **delete** the leaf's `themeChanged()`
    signal declaration (the base now provides it; `Q_PROPERTY ...
    NOTIFY themeChanged` keeps working with the inherited signal).
    Mark the six format ops `override`. `theme()` const: mark
    `override`; its return type already matches (`Markoff::Theme`).
    Mark `attachFindController`/`detachFindController` `override`.
  - `libs/markoff-styled/include/markoff/styled/Editor.h`: mark
    `setTheme`/`theme`/`setFontScale`/`fontScale` `override`;
    **delete** the leaf's `themeChanged()` and `fontScaleChanged()`
    signal declarations; change every `emit fontScaleChanged()` in
    `libs/markoff-styled/src/Editor.cpp` to
    `emit fontScaleChanged(m_fontScale)`.
  - In both leaves' `setTheme`/`setFontScale` bodies, call the base
    first (`MarkdownView::setTheme(t);`) and delete any now-duplicate
    local `emit`.
  - `libs/markoff-live/include/markoff/live/EditorWidget.h`: mark
    `attachFindController`/`detachFindController` `override`.

- [ ] **Step 6: Build the affected leaves and run the new test.**
  `cmake --build build-dev -j 8 --target tst_markdown_view_base markoff_source markoff_styled markoff_live`
  then `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_markdown_view_base`
  → expected: all slots PASS.

- [ ] **Step 7: No-regression check on the leaves' own suites.**
  `scripts/run-tests.sh -R 'tst_source_|tst_styled_|tst_v10'` →
  expected: all pass (classify-before-fixing if not).

- [ ] **Step 8: Commit.**
  `git add -A && git commit -m "feat(core): MarkdownView contract v2 — find/undo/theme/fontScale/format virtuals (spec §3)"`

---

### Task 2: Source + styled contract suites (shared checks header)

The shared header keeps the three leaf suites from drifting apart.
Live's suite arrives in Task 12 (it needs Tasks 7–8 first).

**Files:**
- Create: `libs/markoff-core/tests/ViewContractChecks.h`
- Create: `libs/markoff-source/tests/tst_view_contract_source.cpp`
- Create: `libs/markoff-styled/tests/tst_view_contract_styled.cpp`
- Modify: both tests/CMakeLists.txt

- [ ] **Step 1: Write the shared checks header**
  `libs/markoff-core/tests/ViewContractChecks.h` (installed nowhere;
  included by relative path from the leaf test dirs):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Shared MarkdownView-contract assertions. Each leaf's contract test
// instantiates its concrete editor, loads FIXTURE, then calls these
// against the BASE pointer — the point is that the contract works
// polymorphically. Spec §10.
#pragma once
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>

namespace ViewContract {

// 3 blocks; block 1 is a 3-line code block, so the flat-line model is:
// line 1 = "alpha one", lines 2-4 = code fence, line 5 = "omega end".
inline QByteArray fixture() {
    return QByteArray("alpha one\n\n```\ncode line\n```\n\nomega end");
}

inline void checkCursorRoundTrip(Markoff::MarkdownView *v) {
    QVERIFY(v->hasCursor());
    v->setCursorPosition({5, 3});
    const auto p = v->cursorPosition();
    QCOMPARE(p.line, 5);
    QCOMPARE(p.column, 3);
    v->setCursorPosition({9999, 1});            // clamps, never no-ops
    QVERIFY(v->cursorPosition().line >= 1);
}

inline void checkReadOnlyBlocksUndoAndKeepsBytes(Markoff::MarkdownView *v,
                                                 Markoff::MarkoffDocument *doc) {
    const QByteArray before = doc->serializeForSave();
    v->setReadOnly(true);
    QVERIFY(v->isReadOnly());
    QVERIFY(!v->hasEditing());
    v->undo();
    v->toggleBold();
    QCOMPARE(doc->serializeForSave(), before);
    v->setReadOnly(false);
}

inline void checkUndoRedoViaBase(Markoff::MarkdownView *v,
                                 Markoff::MarkoffDocument *doc) {
    const QByteArray before = doc->serializeForSave();
    doc->applyFlatEdit(0, 0, QByteArray("X"), Markoff::Origin::UserEdit);
    const QByteArray after = doc->serializeForSave();
    QVERIFY(after != before);
    v->undo();
    QCOMPARE(doc->serializeForSave(), before);
    v->redo();
    QCOMPARE(doc->serializeForSave(), after);
    v->undo();   // restore fixture for subsequent checks
}

inline void checkFontScaleSignal(Markoff::MarkdownView *v) {
    QSignalSpy spy(v, &Markoff::MarkdownView::fontScaleChanged);
    v->setFontScale(1.25);
    QCOMPARE(v->fontScale(), 1.25);
    QVERIFY(spy.count() >= 1);
    v->setFontScale(1.0);
}

}  // namespace ViewContract
```

- [ ] **Step 2: Write the source suite**
  `libs/markoff-source/tests/tst_view_contract_source.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

#include "../../markoff-core/tests/ViewContractChecks.h"

class TstViewContractSource : public QObject {
    Q_OBJECT
    Markoff::MarkoffDocument *m_doc = nullptr;
    Markoff::Source::Editor  *m_ed  = nullptr;
private Q_SLOTS:
    void init() {
        m_doc = new Markoff::MarkoffDocument(1);
        m_doc->loadFromMarkdown(ViewContract::fixture());
        m_ed  = new Markoff::Source::Editor;
        m_ed->setDocument(m_doc);
        QTest::qWait(50);
    }
    void cleanup() { delete m_ed; delete m_doc; }

    void cursor_round_trip()   { ViewContract::checkCursorRoundTrip(m_ed); }
    void read_only_blocks()    { ViewContract::checkReadOnlyBlocksUndoAndKeepsBytes(m_ed, m_doc); }
    void undo_redo_via_base()  { ViewContract::checkUndoRedoViaBase(m_ed, m_doc); }
    void font_scale_signal()   { ViewContract::checkFontScaleSignal(m_ed); }
};

QTEST_MAIN(TstViewContractSource)
#include "tst_view_contract_source.moc"
```

  The styled suite is identical except `markoff/styled/Editor.h`,
  class names, and the file/binary name `tst_view_contract_styled`.

- [ ] **Step 3: Register both binaries** (same CMake pattern as
  Task 1 Step 2, in each leaf's tests/CMakeLists.txt, linking
  `markoff_source`/`markoff_styled` respectively + `markoff_core`).

- [ ] **Step 4: Run; classify failures.** Expected NOW: cursor
  clamping fails (both leaves currently no-op on invalid line) and
  source `font_scale_signal` fails (source has no fontScale yet —
  base store passes the scale check, signal passes; actually the base
  makes this pass — if it passes, fine). Fix the clamp in both leaves
  (`setCursorPosition`): replace `if (!blk.isValid()) return;` with

```cpp
    if (!blk.isValid())
        blk = m_editor->document()->lastBlock();
```

  (source: `block`/`cursor` naming per `Editor.cpp:134-139`.)

- [ ] **Step 5: Verify both suites pass**, then full
  `scripts/run-tests.sh -R 'tst_view_contract'` → PASS.

- [ ] **Step 6: Commit.**
  `git commit -am "test: MarkdownView contract suites for source+styled; cursor clamping per spec §4.1"`

---

### Task 3: FormatOps hoist (core ← source) — no behavior change

**Files:**
- Create: `libs/markoff-core/include/markoff/core/FormatOps.h`
- Create: `libs/markoff-core/src/FormatOps.cpp`
- Create: `libs/markoff-core/tests/tst_format_ops.cpp`
- Modify: `libs/markoff-source/src/Editor.cpp` (lines ~240–563)
- Modify: `libs/markoff-core/CMakeLists.txt`, tests CMakeLists

- [ ] **Step 1: Read the donor code** —
  `libs/markoff-source/src/Editor.cpp:240-563`: file-local free
  functions `wrapToggle(QPlainTextEdit*, SourceTextDocumentBinding*,
  delim)`, `insertLink`, and `Editor::setHeadingLevel`. They already
  mutate the model via `qtPosToByteOffset` → `findBlockAtSepByte` →
  `d2ApplyBufferEdit`/`applyFlatEdit`; the QPlainTextEdit is used only
  for `toPlainText()`, `textCursor()` positions, and re-applying the
  resulting caret.

- [ ] **Step 2: Write the failing headless test**
  `libs/markoff-core/tests/tst_format_ops.cpp` — drives FormatOps with
  NO widget:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Headless tests for Markoff::FormatOps (spec §5). The flat text passed
// in is widgetFlatView() — exactly what a widget's toPlainText() holds.
#include <QTest>

#include <markoff/core/FormatOps.h>
#include <markoff/core/MarkoffDocument.h>

using namespace Markoff;

class TstFormatOps : public QObject {
    Q_OBJECT
    static QString flat(MarkoffDocument &d) {
        return QString::fromUtf8(d.widgetFlatView());
    }
private Q_SLOTS:
    void wrapToggle_wraps_and_unwraps_selection() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello world\n\nsecond block");
        // select "world" (qt positions 6..11 in flat line 1)
        auto r = FormatOps::wrapToggle(&doc, flat(doc), {6, 11}, "**");
        QCOMPARE(doc.serializeForSave(),
                 QByteArray("hello **world**\n\nsecond block\n"));
        // toggle back using the returned range
        r = FormatOps::wrapToggle(&doc, flat(doc), r, "**");
        QCOMPARE(doc.serializeForSave(),
                 QByteArray("hello world\n\nsecond block\n"));
    }

    void wrapToggle_in_second_block_is_block_aware() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("first\n\nsecond block");
        // "second" = qt 6..12 in the flat view "first\nsecond block"
        FormatOps::wrapToggle(&doc, flat(doc), {6, 12}, "_");
        QCOMPARE(doc.serializeForSave(),
                 QByteArray("first\n\n_second_ block\n"));
    }

    void setHeadingLevel_on_non_first_block() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("first\n\nsecond");
        FormatOps::setHeadingLevel(&doc, flat(doc), /*caretQtPos=*/8, 2);
        QCOMPARE(doc.serializeForSave(), QByteArray("first\n\n## second\n"));
        FormatOps::setHeadingLevel(&doc, flat(doc), 8, 0);
        QCOMPARE(doc.serializeForSave(), QByteArray("first\n\nsecond\n"));
    }

    void insertLink_wraps_selection() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("see docs here");
        FormatOps::insertLink(&doc, flat(doc), {4, 8});
        QCOMPARE(doc.serializeForSave(), QByteArray("see [docs]() here\n"));
    }
};

QTEST_MAIN(TstFormatOps)
#include "tst_format_ops.moc"
```

  (If a donor-behavior detail differs — e.g. `insertLink` produces
  `[docs](url)` placeholder text — run the existing
  `tst_source_widget_format_ops` to read the pinned contract and match
  THAT, not this sketch; the donor behavior is normative.)

- [ ] **Step 3: Register + verify it fails to compile** (FormatOps.h
  doesn't exist).

- [ ] **Step 4: Create FormatOps.** `FormatOps.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {
class MarkoffDocument;

/// Widget-free markdown format operations over the single-`\n`
/// widgetFlatView coordinate space (spec §5). Lifted from
/// markoff-source's Editor (queue #8.6-hardened, block-aware via
/// Detail::findBlockAtSepByte). Each op mutates the model through d2
/// primitives and returns the UTF-16 caret/selection the caller
/// should re-apply after its binding's reverse sync.
namespace FormatOps {

struct QtRange { int start = 0; int end = 0; };

MARKOFF_CORE_EXPORT QtRange wrapToggle(MarkoffDocument *doc,
                                       const QString &flatText,
                                       QtRange sel,
                                       const QByteArray &delim);
MARKOFF_CORE_EXPORT QtRange insertLink(MarkoffDocument *doc,
                                       const QString &flatText,
                                       QtRange sel);
MARKOFF_CORE_EXPORT void    setHeadingLevel(MarkoffDocument *doc,
                                            const QString &flatText,
                                            int caretQtPos, int level);
}  // namespace FormatOps
}  // namespace Markoff
```

  `FormatOps.cpp`: **move** the bodies from `Editor.cpp` verbatim,
  with the mechanical substitutions: `te->toPlainText()` → the
  `flatText` parameter; `te->textCursor()` position/anchor → `sel`;
  `binding->markoffDocument()` → `doc`; the final
  "re-apply caret to the widget" tail of each function becomes the
  returned `QtRange`. Keep every comment. Add `FormatOps.cpp` to
  `libs/markoff-core/CMakeLists.txt`'s source list.

- [ ] **Step 5: Make `tst_format_ops` pass.**

- [ ] **Step 6: Rewire the source Editor.** Replace each op body with
  the wrapper pattern (selection extraction + call + caret re-apply),
  e.g.:

```cpp
void Editor::toggleBold() {
    QTextCursor c = m_editor->textCursor();
    const auto r = Markoff::FormatOps::wrapToggle(
        Markoff::MarkdownView::document(), m_editor->toPlainText(),
        {qMin(c.anchor(), c.position()), qMax(c.anchor(), c.position())},
        "**");
    Markoff::MarkoffDocument *doc = Markoff::MarkdownView::document();
    if (doc) doc->flushPendingD2Changed();
    QTextCursor c2 = m_editor->textCursor();
    c2.setPosition(r.start);
    c2.setPosition(r.end, QTextCursor::KeepAnchor);
    m_editor->setTextCursor(c2);
}
```

  (Match the donor's existing flush/caret-re-apply tail exactly — read
  it; if the donor flushed inside the free function, keep the flush
  inside FormatOps instead of the wrapper.) Delete the now-unused
  file-local free functions.

- [ ] **Step 7: The donor's contract is the guard.**
  `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_source_widget_format_ops`
  → expected **16/16 PASS, zero test edits**. If any slot fails, the
  lift changed behavior — fix the lift, never the test.

- [ ] **Step 8: Commit.**
  `git commit -am "refactor(core): hoist format ops into Markoff::FormatOps; source Editor wraps it (spec §5)"`

---

### Task 4: Extract the frame-aware block walk from FormatPass

**Files:**
- Create: `libs/markoff-styled/src/BlockPositionWalk.h` + `.cpp`
- Modify: `libs/markoff-styled/src/FormatPass.cpp` (walk section,
  ~lines 445–590)
- Modify: `libs/markoff-styled/CMakeLists.txt`

- [ ] **Step 1:** Read `FormatPass.cpp:445-590` — the
  `QTextFrame::iterator` lockstep walk (model block ↔ document
  top-level element, frames consumed whole, artifact blocks skipped
  via `skipArtifactBlocks`).

- [ ] **Step 2:** Extract a reusable iterator into
  `BlockPositionWalk.h` whose shape mirrors what the walk already
  computes per model block — target API:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QTextBlock>
#include <markoff/core/BlockId.h>

class QTextDocument;
namespace Markoff { class MarkoffDocument; }

namespace Markoff::Styled {

/// Frame-aware lockstep walk: visits each model block with its
/// corresponding QTextDocument element. A Table block maps to its
/// QTextTable frame (firstQtBlock invalid, isFrame=true); a text block
/// maps to its first QTextBlock. Extracted from FormatPass so find /
/// future consumers cannot drift from the rendering walk (the
/// 2026-05-31 SIGSEGV class). MUST stay behavior-identical to the
/// FormatPass walk — FormatPass consumes this same iterator.
struct WalkEntry {
    Markoff::BlockId blockId;
    bool       isFrame = false;
    QTextBlock firstQtBlock;     // valid when !isFrame
    int        qtLineCount = 0;  // QTextBlocks this model block spans
};

/// Calls visit(entry) for each model block in order. Returns false if
/// the document structure desynced (defensive; matches FormatPass).
bool walkBlocks(const Markoff::MarkoffDocument *doc, QTextDocument *qdoc,
                const std::function<void(const WalkEntry &)> &visit);

}  // namespace Markoff::Styled
```

  Implement by MOVING the loop skeleton from FormatPass (frame
  consumption, artifact-block skip, `text.count('\n')+1` line
  consumption) and have `FormatPass::apply` consume `walkBlocks`,
  keeping all its per-block formatting logic in the visitor.

- [ ] **Step 3: Behavior guard.** Run the styled suite:
  `scripts/run-tests.sh -R 'tst_styled'` → expected: ALL pass
  unchanged (especially `tst_styled_table_render::list_after_table_does_not_crash_and_renders`).

- [ ] **Step 4: Commit.**
  `git commit -am "refactor(styled): extract frame-aware BlockPositionWalk from FormatPass (spec §6)"`

---

### Task 5: StyledFindAdapter (TDD, frame-aware)

**Files:**
- Create: `libs/markoff-styled/src/Detail/StyledFindAdapter.h` + `.cpp`
- Modify: `libs/markoff-styled/include/markoff/styled/Editor.h`,
  `src/Editor.cpp`
- Create: `libs/markoff-styled/tests/tst_styled_find_adapter.cpp`

- [ ] **Step 1: Write the failing test** (model it on
  `libs/markoff-source/tests/tst_source_find_adapter.cpp`, which this
  session added — same fixture discipline, plus the frame case):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Find highlights in the styled view must be frame-aware: a match
// AFTER a rendered QTextTable lands at the visible-text position, not
// at flat-byte arithmetic positions (spec §6; 2026-05-31 SIGSEGV class).
#include <QPlainTextEdit>
#include <QTest>
#include <QTextEdit>

#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/styled/Editor.h>

class TstStyledFindAdapter : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void highlights_align_without_tables() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("caf\xC3\xA9 target alpha\n\nbravo target middle");
        e.setDocument(&doc);
        QTest::qWait(50);

        Markoff::FindController fc(&doc);
        fc.activate();
        fc.setNeedle("target");
        e.attachFindController(&fc);

        auto *te = e.findChild<QTextEdit *>();
        QVERIFY(te);
        const QString plain = te->toPlainText();
        const auto sels = te->extraSelections();
        QCOMPARE(sels.size(), 2);
        QCOMPARE(sels[0].cursor.selectionStart(), int(plain.indexOf("target")));
        QCOMPARE(sels[1].cursor.selectionStart(),
                 int(plain.indexOf("target", sels[0].cursor.selectionStart() + 1)));
    }

    void match_after_table_lands_at_visible_position() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("| A | B |\n| --- | --- |\n| x | y |\n\nafter target end");
        e.setDocument(&doc);
        QTest::qWait(50);

        Markoff::FindController fc(&doc);
        fc.activate();
        fc.setNeedle("target");
        e.attachFindController(&fc);

        auto *te = e.findChild<QTextEdit *>();
        QVERIFY(te);
        // The match is in the paragraph after the rendered table frame.
        const auto sels = te->extraSelections();
        QCOMPARE(sels.size(), 1);
        QTextCursor cur = sels[0].cursor;
        QCOMPARE(cur.selectedText(), QStringLiteral("target"));
        // And it selects the actual glyphs in the visible document:
        QCOMPARE(cur.block().text().mid(cur.selectionStart() - cur.block().position(), 6),
                 QStringLiteral("target"));
    }
};

QTEST_MAIN(TstStyledFindAdapter)
#include "tst_styled_find_adapter.moc"
```

  (If `Styled::Editor` exposes its QTextEdit other than as a child —
  check `Editor.h` — adapt the accessor; add a test-visible accessor
  only if none exists.) Register in tests/CMakeLists.txt per the
  established pattern.

- [ ] **Step 2: Verify it fails** (attachFindController hits the base
  qWarning no-op → zero extraSelections).

- [ ] **Step 3: Implement** `Detail::StyledFindAdapter` — copy the
  *structure* of `SourceFindAdapter` (attach/detach/onMatchesChanged/
  onNavigationRequested/renderHighlights), but compute positions via
  `BlockPositionWalk::walkBlocks`: walk to the entry whose `blockId ==
  m.block`; if `isFrame`, skip (documented degradation — count but no
  highlight); else position = `entry.firstQtBlock.position()` + UTF-16
  offset of `m.byteOffset` within the block text (use
  `QString::fromUtf8(blockText.left(m.byteOffset)).size()`, handling
  internal `\n`s by walking `qtLineCount` blocks when offset exceeds a
  line). Editor overrides:

```cpp
void Editor::attachFindController(Markoff::FindController *fc) {
    if (!m_findAdapter)
        m_findAdapter = new Detail::StyledFindAdapter(this, this);
    m_findAdapter->attach(fc);
}
void Editor::detachFindController() {
    if (m_findAdapter) m_findAdapter->detach();
}
```

- [ ] **Step 4: Verify both slots pass; run `scripts/run-tests.sh -R
  'tst_styled'` clean.**

- [ ] **Step 5: Commit.**
  `git commit -am "feat(styled): FindController integration via frame-aware StyledFindAdapter (spec §6)"`

---

### Task 6: Styled format verbs via FormatOps (TDD)

**Files:**
- Modify: `libs/markoff-styled/include/markoff/styled/Editor.h`,
  `src/Editor.cpp`
- Modify: `libs/markoff-styled/tests/tst_view_contract_styled.cpp`

- [ ] **Step 1: Failing test** — add to the styled contract suite:

```cpp
    void format_verbs_match_source_semantics() {
        // Select "one" in block 0 ("alpha one") and bold it via the BASE.
        auto *te = m_ed->findChild<QTextEdit *>();
        QVERIFY(te);
        QTextCursor c = te->textCursor();
        c.setPosition(6); c.setPosition(9, QTextCursor::KeepAnchor);
        te->setTextCursor(c);
        static_cast<Markoff::MarkdownView *>(m_ed)->toggleBold();
        QVERIFY(m_doc->serializeForSave().startsWith("alpha **one**"));
        static_cast<Markoff::MarkdownView *>(m_ed)->toggleBold();
        QVERIFY(m_doc->serializeForSave().startsWith("alpha one"));
    }
    void format_verbs_noop_inside_table_frame() {
        // covered with a table fixture: caret inside the frame, verb is
        // a loud no-op, bytes unchanged (spec §5).
    }
```

  (Write the table-frame slot fully: load a table doc, place the caret
  in the frame via `firstTable()` from
  `tests/support/TableTestHelpers.h`, call `toggleBold()`, compare
  `serializeForSave()` before/after.)

- [ ] **Step 2: Verify both fail** (base no-ops).

- [ ] **Step 3: Implement the six verbs on `Styled::Editor`** as
  overrides delegating to FormatOps, mirroring the source wrappers of
  Task 3 Step 6 (QTextEdit instead of QPlainTextEdit), with the
  table-frame guard first:

```cpp
bool Editor::caretInsideFrame() const {
    QTextCursor c = m_editor->textCursor();
    return c.currentFrame() != m_editor->document()->rootFrame();
}
void Editor::toggleBold() {
    if (isReadOnly()) return;
    if (caretInsideFrame()) { qWarning("format ops unavailable inside a table"); return; }
    /* wrapper as in Task 3 Step 6, with "**" */
}
```

- [ ] **Step 4: Verify pass; styled suite clean; commit.**
  `git commit -am "feat(styled): format verbs over FormatOps with table-frame guard (spec §5)"`

---

### Task 7: Live cursorPosition mapping (seam work — falsifiability required)

Invariant-1 citation: developmental history §A;
`docs/specs/2026-05-22-cursor-authority-decision.md`. The mapping READS
`LiveCursorState` (canonical) and WRITES through
`requestTextCaretAtRow` — no new store, no cache.

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/EditorWidget.h`,
  `src/EditorWidget.cpp`
- Create: `libs/markoff-live/tests/tst_view_contract_live.cpp`
  (cursor slots now; readOnly slots in Task 8)

- [ ] **Step 1: Failing test** — new binary on the QML integration
  fixture (copy the harness boilerplate from
  `tst_live_render_qml_integration.cpp`'s fixture setup; drive the
  REAL `EditorWidget`, not the binding directly — invariant 5):

```cpp
    // Fixture: ViewContract::fixture() (3 model blocks; block 1 is a
    // 3-line code block → flat lines 1..5).
    void cursorPosition_maps_flat_lines() {
        // place caret in "omega end" (model row 2, qtPos 6) through the
        // chokepoint, then read through the BASE pointer:
        binding->cursorState()->requestTextCaretAtRow(2, 6);
        QTRY_COMPARE(widget->cursorPosition().line, 5);
        QCOMPARE(widget->cursorPosition().column, 7);
    }
    void setCursorPosition_routes_through_chokepoint() {
        static_cast<Markoff::MarkdownView *>(widget)->setCursorPosition({5, 7});
        QTRY_COMPARE(binding->cursorState()->currentTextCaret()->cachedQtPos, 6);
        // line 2 = first line of the code block (model row 1, qtPos 0):
        static_cast<Markoff::MarkdownView *>(widget)->setCursorPosition({2, 1});
        QTRY_VERIFY(binding->cursorState()->currentTextCaret().has_value());
    }
    void cursorPositionChanged_signal_fires() {
        QSignalSpy spy(widget, &Markoff::MarkdownView::cursorPositionChanged);
        binding->cursorState()->requestTextCaretAtRow(0, 2);
        QTRY_VERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toInt(), 1);
        QCOMPARE(spy.last().at(1).toInt(), 3);
    }
```

- [ ] **Step 2: Verify failing** (`cursorPosition()` returns `{0,0}`).

- [ ] **Step 3: Implement** in `EditorWidget.cpp` — pure helpers + the
  overrides:

```cpp
namespace {
// Flat-line model per spec §3: each block contributes 1 + internal-\n
// lines. Returns {line,col} (1-based) for (blockRow, qtPosInBlock).
Markoff::CursorPos toCursorPos(const Markoff::MarkoffDocument *doc,
                               int blockRow, int qtPos)
{
    int line = 1;
    const auto ids = doc->iterateBlocks();
    for (int row = 0; row < ids.size(); ++row) {
        const QString text = QString::fromUtf8(doc->blockText(ids[row]));
        if (row == blockRow) {
            const int lastNl = text.lastIndexOf(QLatin1Char('\n'), qMax(0, qtPos - 1));
            const int lineStart = (lastNl < 0 || qtPos == 0) ? 0 : lastNl + 1;
            const int innerLine = (qtPos == 0) ? 0
                : int(QStringView(text).left(qtPos).count(QLatin1Char('\n')));
            return { line + innerLine, qtPos - lineStart + 1 };
        }
        line += 1 + int(text.count(QLatin1Char('\n')));
    }
    return {1, 1};
}
// Inverse: flat (line,col) → (blockRow, qtPos), clamping to the last block.
std::pair<int,int> fromCursorPos(const Markoff::MarkoffDocument *doc,
                                 Markoff::CursorPos p)
{
    int line = 1;
    const auto ids = doc->iterateBlocks();
    for (int row = 0; row < ids.size(); ++row) {
        const QString text = QString::fromUtf8(doc->blockText(ids[row]));
        const int span = 1 + int(text.count(QLatin1Char('\n')));
        if (p.line < line + span) {
            // qtPos = start of the (p.line - line)-th inner line + col-1
            int pos = 0;
            for (int i = 0; i < p.line - line; ++i)
                pos = int(text.indexOf(QLatin1Char('\n'), pos)) + 1;
            const int lineEnd = [&]{ const auto nl = text.indexOf(QLatin1Char('\n'), pos);
                                     return nl < 0 ? int(text.size()) : int(nl); }();
            return { row, qMin(pos + qMax(0, p.column - 1), lineEnd) };
        }
        line += span;
    }
    if (ids.isEmpty()) return {0, 0};
    const QString last = QString::fromUtf8(doc->blockText(ids.last()));
    return { int(ids.size()) - 1, int(last.size()) };   // clamp
}
}  // namespace
```

  Overrides (declare in `EditorWidget.h`, replacing the "Known
  degradations" doc block for cursorPosition):

```cpp
Markoff::CursorPos EditorWidget::cursorPosition() const
{
    auto *doc = document();
    auto *cs  = d->binding ? d->binding->cursorState() : nullptr;
    if (!doc || !cs) return {1, 1};
    const auto caret = cs->currentTextCaret();
    if (!caret) return {1, 1};
    const int row = /* row of caret->block via iterateBlocks() index scan,
                       same id-equality pattern SourceFindAdapter uses */;
    return toCursorPos(doc, row, caret->cachedQtPos);
}
void EditorWidget::setCursorPosition(Markoff::CursorPos pos)
{
    auto *doc = document();
    auto *cs  = d->binding ? d->binding->cursorState() : nullptr;
    if (!doc || !cs) return;
    const auto [row, qtPos] = fromCursorPos(doc, pos);
    cs->requestTextCaretAtRow(row, qtPos);
}
```

  Wire the signal in the constructor / `setDocument`:
  `connect(cursorState, &LiveCursorState::cursorChanged, this, [this]{ const auto p = cursorPosition(); emit cursorPositionChanged(p.line, p.column); });`

- [ ] **Step 4: Verify the three slots pass.**

- [ ] **Step 5: Falsifiability proof (invariant 4).** In a throwaway
  commit, change `line += 1 + ...` to `line += 1;` (drop internal
  lines) — `cursorPosition_maps_flat_lines` must FAIL. Revert. Both
  commits stay in history:
  `git commit -am "proof: break live line summation — contract test fails"` /
  `git revert --no-edit HEAD`.

- [ ] **Step 6: Commit.**
  `git commit -am "feat(live): honest CursorPos mapping over LiveCursorState (spec §4.1)"`

---

### Task 8: Live read-only ingress gates (seam work — falsifiability required)

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h`,
  `src/LiveListModelBinding.cpp` (the flag)
- Modify: `src/LiveEditBinding.cpp`, `src/LiveStructuralKeyHandler.cpp`,
  `src/LiveClipboardController.cpp`, `src/TableEditBinding.cpp`,
  `src/LiveActionController.cpp` (the gates)
- Modify: `src/EditorWidget.cpp` (push the flag)
- Modify: `libs/markoff-live/tests/tst_view_contract_live.cpp`

- [ ] **Step 1: Failing test slots** (QML fixture; real key/paste
  paths):

```cpp
    void readOnly_blocks_typing_structural_paste_but_not_copy() {
        const QByteArray before = doc->serializeForSave();
        widget->setReadOnly(true);

        focusRowAndType(0, "XYZ");                       // harness helper
        QTest::keyClick(window, Qt::Key_Return);
        QGuiApplication::clipboard()->setText("PASTE");
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(doc->serializeForSave(), before);       // nothing landed

        // copy still works:
        binding->cursorState()->selectAll();
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(QGuiApplication::clipboard()->text().contains("alpha"));

        widget->setReadOnly(false);
        focusRowAndType(0, "Z");
        QTRY_VERIFY(doc->serializeForSave() != before);  // editing restored
    }
```

- [ ] **Step 2: Verify failing** (typing mutates despite readOnly).

- [ ] **Step 3: Implement.**
  - `LiveListModelBinding`: `Q_PROPERTY(bool readOnly READ readOnly
    WRITE setReadOnly NOTIFY readOnlyChanged)`, member + accessor +
    signal; setter emits only on change.
  - `EditorWidget::setReadOnly(bool ro)`: `MarkdownView::setReadOnly(ro);
    if (d->binding) d->binding->setReadOnly(ro);` (also push current
    value in `setDocument` wiring).
  - Gates — first line of each mutation ingress, citing the spec:
    - `LiveEditBinding::onContentsChange`: if read-only, do NOT apply
      the d2 edit; re-push the canonical block text to the TextEdit
      (the existing `m_applyingTextUpdate`-guarded push path) so the
      view can't drift from the model.
    - `LiveStructuralKeyHandler::tryHandle`: for mutating keys
      (Enter/Backspace/Delete/Tab/Backtab/typed-char paths) return
      handled-without-mutation when read-only; navigation keys fall
      through untouched.
    - `LiveClipboardController::{cut,paste,pastePrimary,pasteText,pasteFrom}`:
      early-return when read-only (`copy()` untouched).
    - `LiveCursorState::deleteSelection` callers are covered by the
      key/clipboard gates — do NOT gate inside LiveCursorState (it's
      the cursor authority, not a mutation ingress).
    - `TableEditBinding::applyCellEdit`: early-return.
    - `LiveActionController`: on `readOnlyChanged`, `setEnabled(false)`
      on cut/paste/delete/undo/redo/bold/italic/strike/inlineCode/
      link/heading0..6 actions; copy/selectAll/zoom/dark stay enabled.
    Each gate reads the binding's flag (pass the binding pointer or a
    `std::function<bool()>` where the class doesn't already hold it —
    follow each class's existing wiring pattern; no new global state).

- [ ] **Step 4: Verify the slot passes; run
  `scripts/run-tests.sh -R 'tst_live_render_qml_integration|tst_view_contract_live'` clean.**

- [ ] **Step 5: Falsifiability proof.** Throwaway-disable the
  structural-key gate only → the read-only slot must FAIL on the
  Enter sub-assertion. Commit + revert (in-history proof pair).

- [ ] **Step 6: Commit.**
  `git commit -am "feat(live): setReadOnly via mutation-ingress gates on a single binding flag (spec §4.2)"`

---

### Task 9: Live theme/fontScale forwarding + verb delegation; live find override

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/EditorWidget.h`,
  `src/EditorWidget.cpp`
- Modify: `libs/markoff-live/tests/tst_view_contract_live.cpp`

- [ ] **Step 1: Failing slots:** via the BASE pointer —
  `setFontScale(1.5)` → `QTRY_COMPARE(binding->fontScale(), 1.5)`;
  `setTheme(defaultDark())` → binding theme pointer non-null and
  `themeChanged` spy fired; `toggleBold()` with a selection mutates
  the model (reuse the contract fixture; assert via
  `serializeForSave()` containing `**`).

- [ ] **Step 2: Implement overrides:**

```cpp
void EditorWidget::setTheme(const Markoff::Theme &t)
{
    Markoff::MarkdownView::setTheme(t);          // store + signal
    if (d->binding) {
        d->themeCopy = t;                        // binding takes a pointer
        d->binding->setTheme(&d->themeCopy);
    }
}
void EditorWidget::setFontScale(qreal s)
{
    Markoff::MarkdownView::setFontScale(s);      // clamp + store + signal
    if (d->binding) d->binding->setFontScale(fontScale());
}
void EditorWidget::toggleBold()
{ if (auto *ac = d->binding ? d->binding->actionController() : nullptr)
      ac->boldAction()->trigger(); }
// ... same one-liners for italic/strike/inlineCode/link;
// setHeadingLevel(level) triggers heading0..6Action per level.
```

- [ ] **Step 3: Verify pass; commit.**
  `git commit -am "feat(live): theme/fontScale/format verbs honor the base contract (spec §4.3-4.4)"`

---

### Task 10: EditorContext feed — source + styled

**Files:**
- Modify: both Editors (+ headers)
- Modify: both contract suites
- Modify: `libs/markoff-core/include/markoff/core/EditorContext.h`
  (add `constexpr auto Table = "table";` to `BlockKindNames`)

- [ ] **Step 1: Failing slots** (each suite):

```cpp
    void contextChanged_reports_kind_and_is_change_gated() {
        QSignalSpy spy(m_ed, &Markoff::MarkdownView::contextChanged);
        m_ed->setCursorPosition({5, 1});                 // "omega end" paragraph
        QTRY_VERIFY(spy.count() >= 1);
        auto ctx = spy.last().at(0).value<Markoff::EditorContext>();
        QCOMPARE(ctx.blockKind, QString(Markoff::BlockKindNames::Paragraph));
        const int n = spy.count();
        m_ed->setCursorPosition({5, 2});                 // same block, same kind
        QTest::qWait(20);
        QCOMPARE(spy.count(), n);                        // change-gated
        m_ed->setCursorPosition({2, 1});                 // code block
        QTRY_VERIFY(spy.count() > n);
        QCOMPARE(spy.last().at(0).value<Markoff::EditorContext>().blockKind,
                 QString(Markoff::BlockKindNames::CodeBlock));
    }
```

  Add `Q_DECLARE_METATYPE(Markoff::EditorContext)` in
  `EditorContext.h` + `qRegisterMetaType` where the suites need it.

- [ ] **Step 2: Implement** a private `recomputeContext()` on each
  Editor, connected to the inner edit widget's
  `cursorPositionChanged` and the document's `d2DocumentChanged`:
  caret → `qtPosToByteOffset(toPlainText(), pos)` →
  `Detail::findBlockAtSepByte` → `doc->blockKind(hit->blockId)` (check
  the exact kind accessor on MarkoffDocument — `iterateBlocks` +
  per-block kind read is the established pattern); heading level from
  the buffer's leading `#` count; styled sets
  `inTable = (kind == Table)`. Compare against `m_lastContext`; emit
  only on change.

- [ ] **Step 3: Verify pass both suites; commit.**
  `git commit -am "feat(widgets): contextChanged EditorContext feed, change-gated (spec §7)"`

---

### Task 11: EditorContext feed + signal consistency — live; scroll signals all leaves

**Files:**
- Modify: `libs/markoff-live/src/EditorWidget.cpp`
- Modify: source/styled Editors (scrollPositionChanged emission audit)
- Modify: live contract suite

- [ ] **Step 1: Failing slot** (live suite): same shape as Task 10's,
  driving the caret through `requestTextCaretAtRow`, plus an in-table
  case using a table fixture asserting `inTable == true` and
  row/col ≥ 0.

- [ ] **Step 2: Implement:** `recomputeContext()` on EditorWidget,
  connected to `LiveCursorState::cursorChanged` + the model's
  `dataChanged` (kind transitions): kind from the model record /
  `doc` kind accessor; table fields from
  `d->binding->tableEditBinding()`-equivalent (check the binding's
  accessor; if cell coordinates aren't reachable without QML, set
  row/col only when the table binding exposes them — `inTable` is the
  contract minimum). Change-gated like Task 10.

- [ ] **Step 3: Scroll-signal audit.** grep each leaf for
  `scrollPositionChanged` emission; wire missing ones to the native
  scrollbar/Flickable change signal mapping to
  `scrollPositionVisualLine()`. Add one slot per contract suite:
  scroll programmatically → spy fires.

- [ ] **Step 4: Verify; commit.**
  `git commit -am "feat(live): EditorContext feed; scroll-signal consistency across leaves (spec §7, §9)"`

---

### Task 12: Source fontScale + contract-suite consolidation

**Files:**
- Modify: `libs/markoff-source/src/Editor.{h,cpp}`
- Modify: all three contract suites (fill any check not yet wired)

- [ ] **Step 1: Failing slot** (source suite): `setFontScale(2.0)` →
  inner editor `font().pointSizeF()` doubles from its captured base;
  gutter width recomputed; `setFontScale(1.0)` restores.

- [ ] **Step 2: Implement** `Source::Editor::setFontScale` override:
  capture `m_baseFontPt` on first call (or in ctor from
  `m_editor->font().pointSizeF()`), then

```cpp
void Editor::setFontScale(qreal s)
{
    Markoff::MarkdownView::setFontScale(s);
    QFont f = m_editor->font();
    f.setPointSizeF(m_baseFontPt * fontScale());
    m_editor->setFont(f);
    if (m_gutter) { m_gutter->setFont(f); recomputeGutterWidth(); }
    applyParagraphMargins();
}
```

- [ ] **Step 3:** Sweep the §10 checklist against the three suites;
  add any missing check (cursor clamp on live, undo-while-readOnly on
  styled, find-highlight check on source via the existing
  `tst_source_find_adapter` — referencing is fine, duplicating is not).

- [ ] **Step 4: Full baseline.**
  `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` → expected:
  previous 260 passes + every new binary green; the same 3 queue-#10
  binaries are the only failures.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(source): fontScale; contract suites consolidated (spec §8, §10)"`

---

### Task 13: Corbomite adoption brief + docs

**Files:**
- Create: `docs/handoff/2026-06-09-corbomite-api-adoption-brief.md`
- Modify: `docs/STATUS.md`, `docs/queue.md`, `CLAUDE.md` (status
  block), `libs/markoff-{live,styled,source}/CLAUDE.md` (new surface)

- [ ] **Step 1: Write the brief.** Contents (all call sites verified
  during the 2026-06-09 audit; re-verify line numbers against
  Corbomite HEAD when executing):
  - Re-pin guidance: jump to the commit containing this arc (never
    into `8c13c5d..079ac1f`).
  - Migration table: `NoteEditorWidget.cpp:489-505` find-attach
    qobject_cast switch → `activeLeaf()->attachFindController(fc)`;
    `MainWindow.cpp:1275` `plainTextEdit()->undo()` → `view->undo()`
    (kills the Qt-widget-undo dual authority); `NoteEditorWidget.cpp:132`
    `binding()->setTheme()` → `view->setTheme(t)`; the 3-way undo/redo
    switch → base calls; format-action wiring → base verbs;
    ephemeral-state capture/restore + goToLine + line/col statusbar →
    now-honest `cursorPosition`/`setCursorPosition` +
    `cursorPositionChanged`; toolbar state → `contextChanged`;
    Reading mode read-only → unchanged (`Styled::Editor::setReadOnly`
    already real) but now gains find.
  - Behavior notes: live `setReadOnly` blocks mutation but keeps
    caret/selection/copy; styled find skips matches inside rendered
    tables; `CursorPos` line model = flat visual lines (code blocks
    span multiple).
- [ ] **Step 2: Update docs:** STATUS.md workfront + open-items
  (API-finalization items → done, link spec/brief); queue.md: nothing
  closes (queue #10–#13 untouched); per-lib CLAUDE.md "Public surface"
  sections gain the new overrides; root CLAUDE.md status block:
  workfront advances to "Corbomite adoption (brief in hand)".
- [ ] **Step 3: Full baseline once more; commit; push.**
  `git commit -am "docs: Corbomite API-adoption brief; status board advances past API finalization" && git push`

---

## Self-review (performed at write time)

- **Spec coverage:** §3→Task 1; §4.1→Task 7; §4.2→Task 8; §4.3/4.4→
  Task 9; §5→Tasks 3, 6; §6→Tasks 4, 5; §7→Tasks 10, 11; §8→Task 12;
  §9→Task 11; §10→Tasks 2, 7, 8, 12; §11 phases map 1:1; Corbomite
  brief→Task 13. No gaps.
- **Known unknowns called out in-task** (donor `insertLink` exact
  placeholder, styled QTextEdit accessor, MarkoffDocument kind
  accessor, table-binding accessor) — each task says what to check
  and where, with the donor/neighbor as normative.
- **Type consistency:** `FormatOps::QtRange{start,end}` used in Tasks
  3/6; `WalkEntry{blockId,isFrame,firstQtBlock,qtLineCount}` in Tasks
  4/5; `cursorState()`/`currentTextCaret()`/`cachedQtPos`/
  `requestTextCaretAtRow(row,qtPos)` verified against
  `LiveCursorState.h` before writing.
