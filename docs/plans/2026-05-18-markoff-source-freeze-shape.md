# markoff-source freeze-shape — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Apply the five §4.6 freeze-shape decisions from `docs/specs/2026-05-18-markoff-source-freeze-shape-design.md` to `libs/markoff-source`: flatten the namespace, move internal types to `Detail::`, remove the `QPlainTextEdit` forwarder methods, and turn the stubbed `showFindBar`/`hideFindBar` contract into a real implementation.

**Architecture:** Three commits, each independently green. Commit A is a mechanical namespace sweep (no behaviour change). Commit B moves the two internal types (`Gutter`, `InnerEditor`) into a `Markoff::Source::Detail::` sub-namespace. Commit C drops the six forwarder methods and implements `showFindBar`/`hideFindBar` via lazy-construction of the existing `FindBar` widget inside the `Editor`'s `QVBoxLayout`.

**Tech Stack:** C++20, Qt 6.8+, CMake 3.19+, KSyntaxHighlighting, the existing `scripts/run-tests.sh` (defaults to `QT_QPA_PLATFORM=offscreen`).

---

## File structure

Files touched by this plan, grouped by responsibility:

```
libs/markoff-source/
├── include/markoff/source/
│   ├── Editor.h                                    # Task 1 (namespace), Task 2 (friend), Task 3 (drop forwarders, contract impls)
│   └── FindBar.h                                   # Task 1 (namespace)
├── src/
│   ├── Editor.cpp                                  # Task 1, 2, 3 (m_findBar member, showFindBar impl)
│   ├── FindBar.cpp                                 # Task 1, 3 (forwarder callsite updates)
│   ├── Gutter.h                                    # Task 1, 2 (move to Detail)
│   ├── Gutter.cpp                                  # Task 1, 2
│   └── InnerEditor.h                               # Task 1, 2 (move to Detail)
├── tests/
│   ├── tst_source_widget_editor.cpp                # Task 1, 3 (namespace, new showFindBar slots)
│   ├── tst_source_widget_findbar.cpp               # Task 1, 3 (forwarder callsites)
│   ├── tst_source_widget_binding_roundtrip.cpp     # Task 1, 3 (forwarder callsites)
│   └── tst_v10_source_editor_view_contract.cpp     # Task 1 (namespace only)
├── app/main.cpp                                    # Task 1 (namespace)
└── CLAUDE.md                                       # Task 1 (doc namespace update)
```

The test-file prefix `tst_source_widget_*` stays — it's a project test-prefix convention documented in `libs/markoff-source/CLAUDE.md`, not the C++ namespace. CMake target names (`markoff_source`, `tst_source_widget_*`) are namespace-free; no `CMakeLists.txt` changes needed.

---

## Task 1: Namespace flatten — `Markoff::Source::Widget` → `Markoff::Source`

**Files:**
- Modify: `libs/markoff-source/include/markoff/source/Editor.h`, `FindBar.h`
- Modify: `libs/markoff-source/src/Editor.cpp`, `FindBar.cpp`, `Gutter.h`, `Gutter.cpp`, `InnerEditor.h`
- Modify: `libs/markoff-source/tests/tst_source_widget_editor.cpp`, `tst_source_widget_findbar.cpp`, `tst_source_widget_binding_roundtrip.cpp`, `tst_v10_source_editor_view_contract.cpp`
- Modify: `libs/markoff-source/app/main.cpp`
- Modify: `libs/markoff-source/CLAUDE.md`

This is a mechanical refactor with no behaviour change. Each file gets `namespace Markoff::Source::Widget` → `namespace Markoff::Source` and any qualified uses of `Markoff::Source::Widget::` collapse to `Markoff::Source::`.

- [ ] **Step 1: Update public headers**

Edit `libs/markoff-source/include/markoff/source/Editor.h`:

```cpp
// line 14: was: namespace Markoff::Source::Widget {
namespace Markoff::Source {

// line 76: was: } // namespace Markoff::Source::Widget
} // namespace Markoff::Source
```

Edit `libs/markoff-source/include/markoff/source/FindBar.h`:

```cpp
// line 12: was: namespace Markoff::Source::Widget {
namespace Markoff::Source {

// line 49: was: } // namespace Markoff::Source::Widget
} // namespace Markoff::Source
```

- [ ] **Step 2: Update implementation files**

Edit `libs/markoff-source/src/Editor.cpp`:

```cpp
// line 20: was: namespace Markoff::Source::Widget {
namespace Markoff::Source {

// line 192: was: } // namespace
} // namespace Markoff::Source
```

Edit `libs/markoff-source/src/FindBar.cpp` (open + close namespace lines — open is currently `namespace Markoff::Source::Widget {` near top, close is `} // namespace Markoff::Source::Widget` at bottom). Replace with `namespace Markoff::Source {` / `} // namespace Markoff::Source`.

Edit `libs/markoff-source/src/Gutter.h`:

```cpp
// open namespace: was: namespace Markoff::Source::Widget {
namespace Markoff::Source {

// close: was: } // namespace Markoff::Source::Widget
} // namespace Markoff::Source
```

Edit `libs/markoff-source/src/Gutter.cpp`: same `Widget` → `Source` change on open/close.

Edit `libs/markoff-source/src/InnerEditor.h`:

```cpp
// line 6: was: namespace Markoff::Source::Widget {
namespace Markoff::Source {

// line 21: was: } // namespace Markoff::Source::Widget
} // namespace Markoff::Source
```

- [ ] **Step 3: Update tests**

Each test file currently has `namespace Markoff::Source::Widget;` aliases or qualifies types as `Markoff::Source::Widget::Editor` / `Markoff::Source::Widget::FindBar`. Sweep each test file:

- `libs/markoff-source/tests/tst_source_widget_editor.cpp` — replace `Markoff::Source::Widget::Editor` with `Markoff::Source::Editor`.
- `libs/markoff-source/tests/tst_source_widget_findbar.cpp` — replace `Markoff::Source::Widget::Editor` / `Markoff::Source::Widget::FindBar` with `Markoff::Source::Editor` / `Markoff::Source::FindBar`.
- `libs/markoff-source/tests/tst_source_widget_binding_roundtrip.cpp` — replace `Markoff::Source::Widget::Editor` with `Markoff::Source::Editor`.
- `libs/markoff-source/tests/tst_v10_source_editor_view_contract.cpp` — replace `Markoff::Source::Widget::Editor` with `Markoff::Source::Editor`.

- [ ] **Step 4: Update app/main.cpp**

Edit `libs/markoff-source/app/main.cpp`: any `Markoff::Source::Widget::` references become `Markoff::Source::`.

- [ ] **Step 5: Update CLAUDE.md**

Edit `libs/markoff-source/CLAUDE.md`: in the "Public surface" section, change `Markoff::Source::Widget::Editor` to `Markoff::Source::Editor` and `Markoff::Source::Widget::FindBar` to `Markoff::Source::FindBar`. In the "Internal" line, leave `Markoff::Source::Widget::Gutter` alone for now — Task 2 changes it to `Markoff::Source::Detail::Gutter`.

- [ ] **Step 6: Verify no `::Widget::` references remain**

Run:

```bash
grep -rn 'Markoff::Source::Widget' libs/ apps/
```

Expected: zero output.

- [ ] **Step 7: Build + run tests**

Run:

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R 'source|v10_source'
```

Expected: build succeeds; `tst_source_widget_editor`, `tst_source_widget_findbar`, `tst_source_widget_binding_roundtrip`, `tst_v10_source_editor_view_contract` all pass (4/4).

- [ ] **Step 8: Full fast suite green**

Run:

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 216/216 pass.

- [ ] **Step 9: Commit**

```bash
git add libs/markoff-source/
git commit -m "$(cat <<'EOF'
refactor(source): flatten namespace Markoff::Source::Widget → Markoff::Source

The `Widget` namespace level was reserved for a hypothetical
`Markoff::Source::Headless` partition that never materialized.
Two-level namespace is symmetric with `Markoff::Live::*` and is
the project-wide pattern.

Mechanical sweep across Editor.h, FindBar.h, Editor.cpp,
FindBar.cpp, Gutter.h/cpp, InnerEditor.h, all
libs/markoff-source/tests/*.cpp, app/main.cpp, CLAUDE.md. No
behaviour change.

Decision D3 of docs/specs/2026-05-18-markoff-source-freeze-shape-design.md.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Move internal types `Gutter` + `InnerEditor` to `Markoff::Source::Detail::`

**Files:**
- Modify: `libs/markoff-source/src/Gutter.h`, `Gutter.cpp`
- Modify: `libs/markoff-source/src/InnerEditor.h`
- Modify: `libs/markoff-source/src/Editor.cpp` (qualified references update)
- Modify: `libs/markoff-source/include/markoff/source/Editor.h` (forward-decl + friend update)
- Modify: `libs/markoff-source/CLAUDE.md` (Internal section)

The spec only names `Gutter` for the move (D4), but `InnerEditor` is the same kind of internal-only `src/` type and benefits from the same `Detail::` signal. Including it keeps the project convention consistent.

- [ ] **Step 1: Wrap Gutter in Detail sub-namespace**

Edit `libs/markoff-source/src/Gutter.h`:

```cpp
// was: namespace Markoff::Source {
namespace Markoff::Source::Detail {

// at close: was: } // namespace Markoff::Source
} // namespace Markoff::Source::Detail
```

Edit `libs/markoff-source/src/Gutter.cpp`: same `namespace Markoff::Source` → `namespace Markoff::Source::Detail` change at open and close.

- [ ] **Step 2: Wrap InnerEditor in Detail sub-namespace**

Edit `libs/markoff-source/src/InnerEditor.h`:

```cpp
// line 6: was: namespace Markoff::Source {
namespace Markoff::Source::Detail {

// line 21: was: } // namespace Markoff::Source
} // namespace Markoff::Source::Detail
```

- [ ] **Step 3: Update forward-decl + friend in Editor.h**

Edit `libs/markoff-source/include/markoff/source/Editor.h`. The current state (after Task 1) is:

```cpp
namespace Markoff::Source {

class Gutter;

class Editor : public Markoff::MarkdownView {
    // ...
    friend class Gutter;
};

} // namespace Markoff::Source
```

Change to:

```cpp
namespace Markoff::Source {

namespace Detail { class Gutter; }

class Editor : public Markoff::MarkdownView {
    // ...
    friend class Markoff::Source::Detail::Gutter;
};

} // namespace Markoff::Source
```

- [ ] **Step 4: Update Editor.cpp references**

Edit `libs/markoff-source/src/Editor.cpp`. After Task 1 the source uses unqualified `Gutter` and `InnerEditor` (since the file's namespace is `Markoff::Source` and the types lived there). After this task they're under `Detail::`. Two options for each usage:
- Qualify: `Detail::Gutter`, `Detail::InnerEditor`.
- Add a using-decl after the namespace open: `using Detail::Gutter; using Detail::InnerEditor;` and keep the call sites unqualified.

Use the using-decl form (less callsite churn). Inside `namespace Markoff::Source { ... }` add near the top, just after the existing anonymous namespace block:

```cpp
using Detail::Gutter;
using Detail::InnerEditor;
```

Verify lines 54, 178, 189 (the `Gutter` / `InnerEditor` references in the constructor, `resizeEvent`, and `recomputeGutterWidth`) compile unchanged.

- [ ] **Step 5: Update CLAUDE.md Internal section**

Edit `libs/markoff-source/CLAUDE.md`. In the "Internal" section, change:

```markdown
- `Markoff::Source::Widget::Gutter` — line-number gutter, child of the editor.
```

to:

```markdown
- `Markoff::Source::Detail::Gutter` — line-number gutter, child of the editor.
- `Markoff::Source::Detail::InnerEditor` — thin QPlainTextEdit subclass promoting protected geometry accessors to public.
```

- [ ] **Step 6: Build + run tests**

Run:

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R 'source|v10_source'
```

Expected: build succeeds; 4/4 source tests pass.

- [ ] **Step 7: Full fast suite green**

Run:

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 216/216 pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-source/
git commit -m "$(cat <<'EOF'
refactor(source): move Gutter + InnerEditor to Markoff::Source::Detail

The two internal `src/` types are now in a `Detail::` sub-namespace,
matching the project-wide convention (markoff-core CLAUDE.md:
"Foundation-internal helpers go in `Markoff::Detail` namespace").
Forward-decl and friend declaration in the public Editor.h
header update accordingly.

InnerEditor was included alongside Gutter for the same consistency
reason — both are src/-only types that benefit from the `Detail::`
intent signal.

Decision D4 of docs/specs/2026-05-18-markoff-source-freeze-shape-design.md.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Drop forwarders + implement `showFindBar`/`hideFindBar` (TDD)

**Files:**
- Modify: `libs/markoff-source/include/markoff/source/Editor.h` (remove forwarder method declarations + docs)
- Modify: `libs/markoff-source/src/Editor.cpp` (remove forwarder definitions, add `m_findBar` member + impls)
- Modify: `libs/markoff-source/src/FindBar.cpp` (callsite updates for setExtraSelections/setTextCursor/ensureCursorVisible)
- Modify: `libs/markoff-source/tests/tst_source_widget_editor.cpp` (callsite update + four new slots)
- Modify: `libs/markoff-source/tests/tst_source_widget_findbar.cpp` (callsite updates)
- Modify: `libs/markoff-source/tests/tst_source_widget_binding_roundtrip.cpp` (callsite update)

This task ships behaviour change: forwarders out, `showFindBar`/`hideFindBar` in. TDD: write failing tests first, watch them fail, implement, watch pass. Then mechanically migrate callsites and remove forwarders.

- [ ] **Step 1: Update callsites in FindBar.cpp to go through plainTextEdit()**

Edit `libs/markoff-source/src/FindBar.cpp`:

```cpp
// line 63: was: if (m_editor) m_editor->setExtraSelections({});
    if (m_editor) m_editor->plainTextEdit()->setExtraSelections({});

// line 94: was: if (m_editor) m_editor->setExtraSelections(m_matches);
    if (m_editor) m_editor->plainTextEdit()->setExtraSelections(m_matches);

// line 100: was: m_editor->setTextCursor(m_matches[matchIndex].cursor);
    m_editor->plainTextEdit()->setTextCursor(m_matches[matchIndex].cursor);

// line 101: was: m_editor->ensureCursorVisible();
    m_editor->plainTextEdit()->ensureCursorVisible();
```

Editor's forwarders still exist at this point, so callers compile two ways — we're just migrating to the canonical form before deletion.

- [ ] **Step 2: Update callsites in tests**

Edit `libs/markoff-source/tests/tst_source_widget_binding_roundtrip.cpp` line 48:

```cpp
// was: QTRY_COMPARE(e.toPlainText(), QStringLiteral("hello"));
QTRY_COMPARE(e.plainTextEdit()->toPlainText(), QStringLiteral("hello"));
```

Edit `libs/markoff-source/tests/tst_source_widget_editor.cpp` line 27:

```cpp
// was: QCOMPARE(e.toPlainText(), QStringLiteral("hello world"));
QCOMPARE(e.plainTextEdit()->toPlainText(), QStringLiteral("hello world"));
```

Edit `libs/markoff-source/tests/tst_source_widget_findbar.cpp` — lines 30, 31, 43, 45, 47, 49, 61, 63 each call a forwarder on `e`. Migrate them:

```cpp
// line 30-31: were: QTRY_VERIFY(!e.extraSelections().isEmpty());
//                  QCOMPARE(e.extraSelections().size(), 2);
QTRY_VERIFY(!e.plainTextEdit()->extraSelections().isEmpty());
QCOMPARE(e.plainTextEdit()->extraSelections().size(), 2);

// line 43: was: QTRY_COMPARE(e.extraSelections().size(), 3);
QTRY_COMPARE(e.plainTextEdit()->extraSelections().size(), 3);

// line 45: was: const int firstPos = e.textCursor().position();
const int firstPos = e.plainTextEdit()->textCursor().position();

// line 47: was: QVERIFY(e.textCursor().position() != firstPos);
QVERIFY(e.plainTextEdit()->textCursor().position() != firstPos);

// line 49: was: QCOMPARE(e.textCursor().position(), firstPos);
QCOMPARE(e.plainTextEdit()->textCursor().position(), firstPos);

// line 61: was: QTRY_VERIFY(!e.extraSelections().isEmpty());
QTRY_VERIFY(!e.plainTextEdit()->extraSelections().isEmpty());

// line 63: was: QVERIFY(e.extraSelections().isEmpty());
QVERIFY(e.plainTextEdit()->extraSelections().isEmpty());
```

- [ ] **Step 3: Verify tests still green (forwarders not yet removed)**

Run:

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R 'source|v10_source'
```

Expected: 4/4 pass. (Migration is no-op behaviourally because forwarders still delegate to the same inner QPlainTextEdit.)

- [ ] **Step 4: Write the four new failing tests for showFindBar/hideFindBar**

Append these slots to `libs/markoff-source/tests/tst_source_widget_editor.cpp` before the `QTEST_MAIN` macro, inside the `TstSourceWidgetEditor` class's `private Q_SLOTS:` section. The full additions:

```cpp
    void show_findbar_creates_visible_bar() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello world\n"));
        e.setDocument(&doc);
        e.show();

        // Pre-condition: no FindBar visible.
        QVERIFY(e.findChild<Markoff::Source::FindBar *>() == nullptr);

        e.showFindBar();

        // Post-condition: a FindBar child exists and is visible.
        auto *bar = e.findChild<Markoff::Source::FindBar *>();
        QVERIFY(bar != nullptr);
        QVERIFY(bar->isVisible());
    }

    void hide_findbar_clears_highlights() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello hello hello\n"));
        e.setDocument(&doc);
        e.show();
        e.showFindBar();

        auto *bar = e.findChild<Markoff::Source::FindBar *>();
        QVERIFY(bar != nullptr);

        // Type a needle into the input to populate matches/highlights.
        auto *input = bar->findChild<QLineEdit *>();
        QVERIFY(input != nullptr);
        QTest::keyClicks(input, QStringLiteral("hello"));
        QTRY_VERIFY(!e.plainTextEdit()->extraSelections().isEmpty());

        e.hideFindBar();

        QVERIFY(!bar->isVisible());
        QVERIFY(e.plainTextEdit()->extraSelections().isEmpty());
    }

    void showFindBar_is_idempotent() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello\n"));
        e.setDocument(&doc);
        e.show();

        e.showFindBar();
        auto *firstBar = e.findChild<Markoff::Source::FindBar *>();
        QVERIFY(firstBar != nullptr);

        e.showFindBar();
        const auto bars = e.findChildren<Markoff::Source::FindBar *>();
        QCOMPARE(bars.size(), 1);
        QCOMPARE(bars.front(), firstBar);
    }

    void findbar_close_signal_hides() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello\n"));
        e.setDocument(&doc);
        e.show();
        e.showFindBar();
        auto *bar = e.findChild<Markoff::Source::FindBar *>();
        QVERIFY(bar != nullptr);
        QVERIFY(bar->isVisible());

        emit bar->closed();

        QVERIFY(!bar->isVisible());
    }
```

Add the corresponding `#include <markoff/source/FindBar.h>` and `#include <QLineEdit>` at the top of the file if not already present.

- [ ] **Step 5: Build + run new tests; verify FAIL**

Run:

```bash
cmake --build build-dev --target tst_source_widget_editor -j 8
QT_QPA_PLATFORM=offscreen build-dev/bin/tst_source_widget_editor show_findbar_creates_visible_bar hide_findbar_clears_highlights showFindBar_is_idempotent findbar_close_signal_hides
```

Expected: all four new slots FAIL. Specifically `show_findbar_creates_visible_bar` fails at the `bar != nullptr` assertion because `showFindBar` is still the `/* find bar integration: v1.1 */` stub. The other three fail similarly.

- [ ] **Step 6: Add `m_findBar` member to Editor**

Edit `libs/markoff-source/include/markoff/source/Editor.h`. After Task 1+2 the relevant block is around lines 66-74. Add a forward declaration for `FindBar` and a `m_findBar` member:

```cpp
// At top of file, after existing forward declarations:
namespace Markoff::Source {
namespace Detail { class Gutter; }
class FindBar;

class Editor : public Markoff::MarkdownView {
    // ... existing members ...
private:
    // ... existing members ...
    FindBar                                *m_findBar      = nullptr;
};
```

Add a `#include <QPointer>` only if not already present (it is, per the existing m_session field).

- [ ] **Step 7: Implement showFindBar / hideFindBar in Editor.cpp**

Edit `libs/markoff-source/src/Editor.cpp`. Add `#include <markoff/source/FindBar.h>` at the top with the other `markoff/source/*` includes (none exist yet from the source side; add it just below the `markoff/core/SourceTextDocumentBinding.h` include).

Replace the three stub bodies (currently lines 129-131):

```cpp
// was:
void Editor::showFindBar()    { /* find bar integration: v1.1 */ }
void Editor::showReplaceBar() { /* replace bar integration: v1.1 */ }
void Editor::hideFindBar()    { /* find bar integration: v1.1 */ }
```

with:

```cpp
void Editor::showFindBar()
{
    if (!m_findBar) {
        m_findBar = new FindBar(this, this);
        // The Editor's QVBoxLayout was set up in the constructor; append the
        // FindBar after the inner editor. When hidden, the layout collapses
        // the bar's space; when shown, the inner editor shrinks to make room.
        if (auto *l = qobject_cast<QVBoxLayout *>(layout())) {
            l->addWidget(m_findBar);
        }
        connect(m_findBar, &FindBar::closed, this, &Editor::hideFindBar);
        m_findBar->hide();  // start hidden; activate() below shows it.
    }
    m_findBar->activate();
}

void Editor::showReplaceBar()
{
    // v1: no-op. Reserved for a future ReplaceBar widget (see
    // docs/specs/2026-05-18-markoff-source-freeze-shape-design.md §"Out of scope").
}

void Editor::hideFindBar()
{
    if (m_findBar) m_findBar->deactivate();
}
```

`FindBar::activate()` already calls `show()` + `setFocus()` + `recomputeMatches()` per its existing public-slots implementation; `deactivate()` already calls `hide()` + clears highlights via the existing `m_editor->plainTextEdit()->setExtraSelections({})` (after Step 1's callsite migration).

- [ ] **Step 8: Build + run the new tests; verify PASS**

Run:

```bash
cmake --build build-dev --target tst_source_widget_editor -j 8
QT_QPA_PLATFORM=offscreen build-dev/bin/tst_source_widget_editor show_findbar_creates_visible_bar hide_findbar_clears_highlights showFindBar_is_idempotent findbar_close_signal_hides
```

Expected: all four slots PASS.

- [ ] **Step 9: Remove forwarder methods from Editor.h**

Edit `libs/markoff-source/include/markoff/source/Editor.h`. Currently lines 47-53 declare:

```cpp
    // Forwarding methods for QPlainTextEdit API used by Gutter, FindBar, tests
    QString toPlainText() const { return m_editor->toPlainText(); }
    QList<QTextEdit::ExtraSelection> extraSelections() const { return m_editor->extraSelections(); }
    void setExtraSelections(const QList<QTextEdit::ExtraSelection> &sels) { m_editor->setExtraSelections(sels); }
    QTextCursor textCursor() const { return m_editor->textCursor(); }
    void setTextCursor(const QTextCursor &c) { m_editor->setTextCursor(c); }
    void ensureCursorVisible() { m_editor->ensureCursorVisible(); }
```

Delete these six lines. Replace the comment on the previous `plainTextEdit()` accessor (line 44-45) with the documented contract:

```cpp
    // Escape hatch for raw QPlainTextEdit access. Consumers may use any
    // method on the returned pointer EXCEPT setPlainText, setDocument, or
    // other mutators that bypass SourceTextDocumentBinding. The polymorphic
    // MarkdownView contract (setDocument, cursorPosition, etc.) is the
    // curated, safe surface.
    QPlainTextEdit *plainTextEdit() const { return m_editor; }
```

- [ ] **Step 10: Build + full source suite + full fast suite**

Run:

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R 'source|v10_source'
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected:
- Build succeeds (any missed forwarder callsite would fail to compile here; if any compile error, locate and migrate it to `plainTextEdit()->X()` per Step 1's pattern).
- 8/8 source tests pass (4 existing + 4 new).
- 220/220 fast suite pass (was 216/216 before Step 4 added 4 slots).

- [ ] **Step 11: Commit**

```bash
git add libs/markoff-source/
git commit -m "$(cat <<'EOF'
feat(source): implement showFindBar/hideFindBar; drop QPlainTextEdit forwarders

D1: The six forwarder methods on Editor (toPlainText, extraSelections,
setExtraSelections, textCursor, setTextCursor, ensureCursorVisible)
duplicated the plainTextEdit() escape-hatch accessor — two ways to
do the same thing, none canonical. Forwarders removed; consumers
go through plainTextEdit() directly. plainTextEdit() gets a
docstring describing the consumer contract (allowed and disallowed
operations).

D2: showFindBar / hideFindBar were /* v1.1 */ stubs on the
MarkdownView contract. Now Editor lazy-instantiates a FindBar as a
child widget on first showFindBar(), appends it to the existing
QVBoxLayout, and connects FindBar::closed → hideFindBar. The
host-managed path (FindBar(Editor*) constructor) stays public as
the advanced/custom-placement option. showReplaceBar remains a
documented no-op pending a future ReplaceBar widget.

Four new test slots (show_findbar_creates_visible_bar,
hide_findbar_clears_highlights, showFindBar_is_idempotent,
findbar_close_signal_hides) lock the behaviour.

Decisions D1 + D2 of docs/specs/2026-05-18-markoff-source-freeze-shape-design.md.
Cross-leaf companion (symmetric showFindBar impl on markoff-live)
tracked separately per the spec's Open Questions section.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Post-implementation verification

After Task 3's commit, confirm the full state:

```bash
# 1. No Widget namespace remnants.
grep -rn 'Markoff::Source::Widget' libs/ apps/
# Expected: zero output.

# 2. No forwarder method left in the public header.
grep -nE 'toPlainText\(\) const|extraSelections\(\) const|setExtraSelections\(|textCursor\(\) const|setTextCursor\(const QTextCursor|ensureCursorVisible\(\)' libs/markoff-source/include/markoff/source/Editor.h
# Expected: zero output.

# 3. showFindBar / hideFindBar are real (not /* v1.1 */ stubs).
grep -nE 'showFindBar\(\)\s*\{|hideFindBar\(\)\s*\{' libs/markoff-source/src/Editor.cpp
# Expected: two matches with bodies, not the v1.1 comment stubs.

# 4. Full fast suite green.
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
# Expected: 220/220 pass.

# 5. Slow suite (optional).
scripts/run-tests.sh -R 'tst_realistic|tst_benchmark'
# Expected: 2/2 pass.
