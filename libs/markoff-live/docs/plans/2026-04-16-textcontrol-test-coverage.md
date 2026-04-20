# TextControl Test Coverage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a dedicated regression suite for the 2,572-LOC `Markoff::TextControl` fork — six new test files covering cursor movement, selection, editing, input-method, link activation, and MarkdownTextItem integration — and absorb the existing `tst_cjk_autocorrect.cpp` into the new input-method file.

**Architecture:** A header-only test utility (`tests/support/textcontrol_testutil.h`) exposes factories and event-synthesis helpers used by all six test files. Five direct files instantiate a bare `TextControl` + `QTextDocument` + test-only U+FFFC placeholder object and drive it via `processEvent()`. One integration file builds a real `Editor` and reaches through to the first `MarkdownTextItem`'s `TextControl` to verify behaviors that only reproduce with real `MathTextObject` / `CheckboxTextObject` handlers in the loop.

**Tech Stack:** Qt6 (Core, Gui, Widgets, Test), QTest framework, `QT_QPA_PLATFORM=offscreen`, C++20. No new third-party deps. No new CMake helper macros.

**Spec:** `docs/specs/2026-04-16-textcontrol-test-coverage-design.md`

---

## File Structure

New files:

| Path | Responsibility |
|---|---|
| `tests/support/textcontrol_testutil.h` | Header-only test helpers. `TestPlaceholderObject`, `TextControlFixture`, `makeFixture()`, `insertPlaceholder()`, `sendKey()`, `sendMousePress/Move/Release()`, `sendInputMethod()`, `editorFirstTextControl()`. |
| `tests/tst_textcontrol_cursor.cpp` | Arrow/Home/End/Ctrl+word movement, `moveCursor()` parity, traversal past U+FFFC placeholder. |
| `tests/tst_textcontrol_selection.cpp` | shift-modifier extension, shift+click anchoring, double/triple-click, drag-select, `selectAll()`, selection across placeholder. |
| `tests/tst_textcontrol_editing.cpp` | Character insert, Backspace, Delete, `insertPlainText()`, Backspace/Delete adjacent to placeholder, read-only mode suppression. |
| `tests/tst_textcontrol_input.cpp` | Preedit insert/update/commit, `inputMethodQuery()`, plus CJK autocorrect cases migrated from `tst_cjk_autocorrect.cpp`. |
| `tests/tst_textcontrol_links.cpp` | `anchorAt()`, `linkActivated`/`linkHovered` emission. |
| `tests/tst_textcontrol_integration.cpp` | Real `MathTextObject` / `CheckboxTextObject` cursor/selection/backspace interactions through `MarkdownTextItem`. |

Modified files:

| Path | Change |
|---|---|
| `tests/CMakeLists.txt` | Remove `tst_markoff_cjk_autocorrect` block (lines 36-40). Add six new test target blocks, each adding `support/` to include path. |

Deleted files:

| Path | Reason |
|---|---|
| `tests/tst_cjk_autocorrect.cpp` | Cases migrated into `tst_textcontrol_input.cpp`. |

---

## Task 1: Test-utility header

**Files:**
- Create: `tests/support/textcontrol_testutil.h`

- [ ] **Step 1: Create the support directory and write the helper header**

Create `tests/support/textcontrol_testutil.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TEXTCONTROL_TESTUTIL_H
#define MARKOFF_TEXTCONTROL_TESTUTIL_H

#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextObjectInterface>
#include <QWidget>
#include <memory>

#include "TextControl.h"

namespace Markoff::TestUtil {

/// Test-only inline text object. Sentinel TypeId well clear of
/// MathTextObject::TypeId (UserObject+1) and CheckboxTextObject::TypeId
/// (UserObject+2).
class TestPlaceholderObject : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    static constexpr int TypeId = QTextFormat::UserObject + 100;

    explicit TestPlaceholderObject(QObject *parent = nullptr)
        : QObject(parent) {}

    QSizeF intrinsicSize(QTextDocument *, int,
                         const QTextFormat &) override {
        return QSizeF(10.0, 16.0);
    }
    void drawObject(QPainter *, const QRectF &, QTextDocument *,
                    int, const QTextFormat &) override {
        // paint nothing — tests don't render
    }
};

/// RAII fixture. `control` targets `document`; `contextWidget` is needed
/// by processEvent() for IME + clipboard focus affordances. The
/// placeholder handler is pre-registered on the document.
struct TextControlFixture {
    std::unique_ptr<QTextDocument> document;
    std::unique_ptr<QWidget> contextWidget;
    std::unique_ptr<TestPlaceholderObject> placeholder;
    Markoff::TextControl control;

    TextControlFixture()
        : document(std::make_unique<QTextDocument>()),
          contextWidget(std::make_unique<QWidget>()),
          placeholder(std::make_unique<TestPlaceholderObject>())
    {
        document->documentLayout()->registerHandler(
            TestPlaceholderObject::TypeId, placeholder.get());
        control.setDocument(document.get());
    }
};

/// Build a fixture, set plain text, position cursor at `cursorPos`.
inline TextControlFixture makeFixture(const QString &text = {},
                                      int cursorPos = 0)
{
    TextControlFixture fx;
    fx.control.setPlainText(text);
    QTextCursor c(fx.document.get());
    c.setPosition(std::min(cursorPos, text.size()));
    fx.control.setTextCursor(c);
    return fx;
}

/// Insert one U+FFFC run bound to TestPlaceholderObject::TypeId at the
/// cursor's current position. The cursor is advanced past the inserted
/// glyph.
inline void insertPlaceholder(QTextCursor &c)
{
    QTextCharFormat fmt;
    fmt.setObjectType(TestPlaceholderObject::TypeId);
    c.insertText(QString(QChar(QChar::ObjectReplacementCharacter)), fmt);
}

/// Synthesize and dispatch a QKeyEvent (KeyPress) through
/// TextControl::processEvent().
inline void sendKey(Markoff::TextControl &tc, int key,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                    const QString &text = {})
{
    QKeyEvent ev(QEvent::KeyPress, key, modifiers, text);
    tc.processEvent(&ev, QPointF(0, 0));
}

/// Synthesize a mouse press event at document-local position `pos`.
inline void sendMousePress(Markoff::TextControl &tc, const QPointF &pos,
                           Qt::MouseButton button = Qt::LeftButton,
                           Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                           QWidget *context = nullptr)
{
    QMouseEvent ev(QEvent::MouseButtonPress, pos, pos, button, button,
                   modifiers);
    tc.processEvent(&ev, QPointF(0, 0), context);
}

inline void sendMouseMove(Markoff::TextControl &tc, const QPointF &pos,
                          Qt::MouseButtons buttons = Qt::LeftButton,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                          QWidget *context = nullptr)
{
    QMouseEvent ev(QEvent::MouseMove, pos, pos, Qt::NoButton, buttons,
                   modifiers);
    tc.processEvent(&ev, QPointF(0, 0), context);
}

inline void sendMouseRelease(Markoff::TextControl &tc, const QPointF &pos,
                             Qt::MouseButton button = Qt::LeftButton,
                             Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                             QWidget *context = nullptr)
{
    QMouseEvent ev(QEvent::MouseButtonRelease, pos, pos, button,
                   Qt::NoButton, modifiers);
    tc.processEvent(&ev, QPointF(0, 0), context);
}

inline void sendMouseDoubleClick(Markoff::TextControl &tc, const QPointF &pos,
                                 Qt::MouseButton button = Qt::LeftButton,
                                 Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                                 QWidget *context = nullptr)
{
    QMouseEvent ev(QEvent::MouseButtonDblClick, pos, pos, button, button,
                   modifiers);
    tc.processEvent(&ev, QPointF(0, 0), context);
}

/// Synthesize an IME event. `commitString` is text to commit (replaces
/// preedit); `preeditString` is the still-composing preedit to show.
inline void sendInputMethod(Markoff::TextControl &tc,
                            const QString &commitString,
                            const QString &preeditString,
                            const QList<QInputMethodEvent::Attribute> &attrs = {})
{
    QInputMethodEvent ev(preeditString, attrs);
    if (!commitString.isEmpty())
        ev.setCommitString(commitString);
    tc.processEvent(&ev, QPointF(0, 0));
}

} // namespace Markoff::TestUtil

#endif // MARKOFF_TEXTCONTROL_TESTUTIL_H
```

- [ ] **Step 2: Verify header compiles standalone (no test slot yet)**

The header will be include-tested by Task 2's scaffold build. No standalone compile step at this point — the CMake target that will consume it is added in Task 2.

- [ ] **Step 3: Commit**

```bash
git add tests/support/textcontrol_testutil.h
git commit -m "Add textcontrol_testutil.h (header-only test helpers)

Supports the TextControl regression suite. TestPlaceholderObject sits
at UserObject+100 to avoid colliding with MathTextObject (UserObject+1)
and CheckboxTextObject (UserObject+2). TextControlFixture RAII-owns
document+control+context widget; synthesis helpers route through
TextControl::processEvent() so tests exercise the real dispatch path."
```

---

## Task 2: `tst_textcontrol_cursor.cpp`

Covers: arrow-key movement, Home/End, Ctrl+word-left/right, shift-extend,
`moveCursor()` API parity, traversal past U+FFFC placeholder.

**Files:**
- Create: `tests/tst_textcontrol_cursor.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file scaffold with one proof-of-harness slot**

Create `tests/tst_textcontrol_cursor.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlCursor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void arrowRight_advancesByOne();
    void arrowLeft_retreatsByOne();
    void arrowRight_atEnd_stays();
    void arrowLeft_atStart_stays();
    void home_goesToLineStart();
    void end_goesToLineEnd();
    void ctrlRight_jumpsToNextWord();
    void ctrlLeft_jumpsToPrevWord();
    void shiftRight_extendsSelection();
    void moveCursorRight_matchesKeyboard();
    void cursorRight_overPlaceholder_advancesByOne();
    void cursorLeft_overPlaceholder_retreatsByOne();
    void ctrlRight_acrossPlaceholder_skipsIt();
};

void TstTextControlCursor::arrowRight_advancesByOne()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    sendKey(fx.control, Qt::Key_Right);
    QCOMPARE(fx.control.textCursor().position(), 1);
}

QTEST_MAIN(TstTextControlCursor)
#include "tst_textcontrol_cursor.moc"
```

- [ ] **Step 2: Register in `tests/CMakeLists.txt`**

Append after the last existing test block:

```cmake
add_executable(tst_markoff_textcontrol_cursor tst_textcontrol_cursor.cpp)
add_test(NAME tst_markoff_textcontrol_cursor COMMAND tst_markoff_textcontrol_cursor)
target_link_libraries(tst_markoff_textcontrol_cursor PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_textcontrol_cursor
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_cursor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

(Note: the include path adds the tests dir itself so `"support/textcontrol_testutil.h"` resolves; the `../src` entry gives access to `TextControl.h`.)

- [ ] **Step 3: Configure + build the scaffold**

Run from the parent Corbomite tree:

```bash
cmake --build build --target tst_markoff_textcontrol_cursor
```

Expected: builds with declared-but-not-defined slots linking past moc (they won't run, but the scaffold compiles). If compilation fails on the `TextControl.h` include, the `../src` path in CMakeLists is wrong — fix before proceeding.

Because all slots are declared but only `arrowRight_advancesByOne` is defined, linking with QTest will emit a link error on the undefined slots. To avoid that, temporarily comment out the undefined declarations — OR skip directly to Step 4 where they all get bodies. Prefer Step 4 to keep iteration tight.

- [ ] **Step 4: Fill in the remaining cursor slots**

Append these slot bodies above `QTEST_MAIN`:

```cpp
void TstTextControlCursor::arrowLeft_retreatsByOne()
{
    auto fx = makeFixture(QStringLiteral("hello"), 3);
    sendKey(fx.control, Qt::Key_Left);
    QCOMPARE(fx.control.textCursor().position(), 2);
}

void TstTextControlCursor::arrowRight_atEnd_stays()
{
    auto fx = makeFixture(QStringLiteral("hi"), 2);
    sendKey(fx.control, Qt::Key_Right);
    QCOMPARE(fx.control.textCursor().position(), 2);
}

void TstTextControlCursor::arrowLeft_atStart_stays()
{
    auto fx = makeFixture(QStringLiteral("hi"), 0);
    sendKey(fx.control, Qt::Key_Left);
    QCOMPARE(fx.control.textCursor().position(), 0);
}

void TstTextControlCursor::home_goesToLineStart()
{
    auto fx = makeFixture(QStringLiteral("hello"), 4);
    sendKey(fx.control, Qt::Key_Home);
    QCOMPARE(fx.control.textCursor().position(), 0);
}

void TstTextControlCursor::end_goesToLineEnd()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    sendKey(fx.control, Qt::Key_End);
    QCOMPARE(fx.control.textCursor().position(), 5);
}

void TstTextControlCursor::ctrlRight_jumpsToNextWord()
{
    auto fx = makeFixture(QStringLiteral("one two three"), 0);
    sendKey(fx.control, Qt::Key_Right, Qt::ControlModifier);
    // End of "one" or start of "two" depending on Qt boundary rules;
    // both are valid word-right semantics. Assert we moved past "one".
    QVERIFY(fx.control.textCursor().position() >= 3);
    QVERIFY(fx.control.textCursor().position() <= 4);
}

void TstTextControlCursor::ctrlLeft_jumpsToPrevWord()
{
    auto fx = makeFixture(QStringLiteral("one two three"), 13);
    sendKey(fx.control, Qt::Key_Left, Qt::ControlModifier);
    QVERIFY(fx.control.textCursor().position() >= 7);
    QVERIFY(fx.control.textCursor().position() <= 8);
}

void TstTextControlCursor::shiftRight_extendsSelection()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    sendKey(fx.control, Qt::Key_Right, Qt::ShiftModifier);
    sendKey(fx.control, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(fx.control.textCursor().anchor(), 0);
    QCOMPARE(fx.control.textCursor().position(), 2);
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("he"));
}

void TstTextControlCursor::moveCursorRight_matchesKeyboard()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    fx.control.moveCursor(QTextCursor::Right);
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlCursor::cursorRight_overPlaceholder_advancesByOne()
{
    // doc: "a" + U+FFFC + "b", cursor at 0 (before "a").
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    // Now: "a<fffc>b", length 3.
    QTextCursor start(fx.document.get());
    start.setPosition(1);  // between "a" and the placeholder
    fx.control.setTextCursor(start);
    sendKey(fx.control, Qt::Key_Right);
    QCOMPARE(fx.control.textCursor().position(), 2);  // past placeholder
}

void TstTextControlCursor::cursorLeft_overPlaceholder_retreatsByOne()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    QTextCursor start(fx.document.get());
    start.setPosition(2);  // between placeholder and "b"
    fx.control.setTextCursor(start);
    sendKey(fx.control, Qt::Key_Left);
    QCOMPARE(fx.control.textCursor().position(), 1);  // before placeholder
}

void TstTextControlCursor::ctrlRight_acrossPlaceholder_skipsIt()
{
    // doc: "one <fffc> two"; starting at 0, ctrl+right should land
    // somewhere after the placeholder char.
    auto fx = makeFixture(QStringLiteral("one "));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral(" two"));
    QTextCursor start(fx.document.get());
    start.setPosition(0);
    fx.control.setTextCursor(start);
    sendKey(fx.control, Qt::Key_Right, Qt::ControlModifier);
    // At minimum, cursor moved off position 0.
    QVERIFY(fx.control.textCursor().position() > 0);
}
```

- [ ] **Step 5: Build**

```bash
cmake --build build --target tst_markoff_textcontrol_cursor
```

Expected: compiles and links clean.

- [ ] **Step 6: Run**

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_textcontrol_cursor
```

Expected: all 13 slots PASS. If any slot fails, the failure is diagnostic
output — either the test encodes an incorrect expectation (tighten the
assertion) or a real TextControl behavior has drifted (investigate; don't
silence). For Ctrl+word slots in particular, word-boundary semantics vary
with locale — widen the acceptable range rather than pinning a single
position.

- [ ] **Step 7: Commit**

```bash
git add tests/tst_textcontrol_cursor.cpp tests/CMakeLists.txt
git commit -m "Add tst_textcontrol_cursor direct test suite

Covers arrow-key / Home / End / Ctrl+word movement, shift-extend,
moveCursor() API parity, and traversal past U+FFFC placeholder glyphs.
13 slots, direct against bare TextControl + QTextDocument via
textcontrol_testutil.h."
```

---

## Task 3: `tst_textcontrol_selection.cpp`

Covers: shift+arrow extension, shift+click anchoring, double-click word,
triple-click line, drag-select press/move/release, `selectAll()`, selection
spanning placeholder, `visibilityRequest` emission at drag edge.

**Files:**
- Create: `tests/tst_textcontrol_selection.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

Create `tests/tst_textcontrol_selection.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlSelection : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void shiftRight_extends_from_anchor();
    void shiftLeft_retracts_selection();
    void selectAll_selectsEntireDocument();
    void shiftClick_setsAnchor();
    void doubleClick_selectsWord();
    void tripleClick_selectsLine();
    void dragSelect_pressMoveRelease_selectsRange();
    void selection_spanning_placeholder_includes_fffc();
};

void TstTextControlSelection::shiftRight_extends_from_anchor()
{
    auto fx = makeFixture(QStringLiteral("hello world"), 0);
    for (int i = 0; i < 5; ++i)
        sendKey(fx.control, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(fx.control.textCursor().anchor(), 0);
    QCOMPARE(fx.control.textCursor().position(), 5);
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("hello"));
}

void TstTextControlSelection::shiftLeft_retracts_selection()
{
    auto fx = makeFixture(QStringLiteral("hello"), 0);
    for (int i = 0; i < 4; ++i)
        sendKey(fx.control, Qt::Key_Right, Qt::ShiftModifier);
    sendKey(fx.control, Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(fx.control.textCursor().position(), 3);
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("hel"));
}

void TstTextControlSelection::selectAll_selectsEntireDocument()
{
    auto fx = makeFixture(QStringLiteral("line one\nline two"), 0);
    fx.control.selectAll();
    QCOMPARE(fx.control.textCursor().selectedText().length(),
             fx.document->characterCount() - 1);
}

void TstTextControlSelection::shiftClick_setsAnchor()
{
    auto fx = makeFixture(QStringLiteral("hello world"), 2);
    // Simulate shift+click at a position corresponding to ~8 chars in.
    // Use cursorForPosition + setTextCursor with Qt::KeepAnchor to
    // model what TextControl does internally when receiving a shift
    // mouse press.
    QPointF clickAt = fx.control.cursorRect(
        [&] {
            QTextCursor c(fx.document.get());
            c.setPosition(8);
            return c;
        }()).center();
    sendMousePress(fx.control, clickAt, Qt::LeftButton, Qt::ShiftModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, clickAt, Qt::LeftButton, Qt::ShiftModifier,
                     fx.contextWidget.get());
    QCOMPARE(fx.control.textCursor().anchor(), 2);
    QVERIFY(fx.control.textCursor().position() != 2);
}

void TstTextControlSelection::doubleClick_selectsWord()
{
    auto fx = makeFixture(QStringLiteral("hello world"), 0);
    // Click in the middle of "hello" (position ~2).
    QTextCursor probe(fx.document.get());
    probe.setPosition(2);
    QPointF clickAt = fx.control.cursorRect(probe).center();
    sendMouseDoubleClick(fx.control, clickAt, Qt::LeftButton,
                         Qt::NoModifier, fx.contextWidget.get());
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("hello"));
}

void TstTextControlSelection::tripleClick_selectsLine()
{
    auto fx = makeFixture(QStringLiteral("one two\nthree"), 0);
    QTextCursor probe(fx.document.get());
    probe.setPosition(3);  // inside "one two"
    QPointF clickAt = fx.control.cursorRect(probe).center();
    // Triple-click is delivered as press/release/press/release/
    // MouseButtonDblClick pattern in Qt; TextControl detects the
    // rapid-third-press via its internal state. Simplest deterministic
    // path: two double-clicks in quick succession.
    sendMouseDoubleClick(fx.control, clickAt, Qt::LeftButton,
                         Qt::NoModifier, fx.contextWidget.get());
    sendMouseDoubleClick(fx.control, clickAt, Qt::LeftButton,
                         Qt::NoModifier, fx.contextWidget.get());
    // Whole first line selected (the substring on block 0).
    const QString sel = fx.control.textCursor().selectedText();
    QVERIFY2(sel.contains(QStringLiteral("one two")),
             qPrintable(QStringLiteral("expected line selection, got '%1'")
                        .arg(sel)));
}

void TstTextControlSelection::dragSelect_pressMoveRelease_selectsRange()
{
    auto fx = makeFixture(QStringLiteral("hello world"), 0);
    QTextCursor startCur(fx.document.get());
    startCur.setPosition(0);
    QTextCursor endCur(fx.document.get());
    endCur.setPosition(5);
    QPointF startPt = fx.control.cursorRect(startCur).center();
    QPointF endPt = fx.control.cursorRect(endCur).center();
    sendMousePress(fx.control, startPt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseMove(fx.control, endPt, Qt::LeftButton, Qt::NoModifier,
                  fx.contextWidget.get());
    sendMouseRelease(fx.control, endPt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    QCOMPARE(fx.control.textCursor().selectedText(), QStringLiteral("hello"));
}

void TstTextControlSelection::selection_spanning_placeholder_includes_fffc()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    fx.control.selectAll();
    const QString selected = fx.control.textCursor().selectedText();
    QCOMPARE(selected.length(), 3);
    QCOMPARE(selected.at(1), QChar(QChar::ObjectReplacementCharacter));
}
```

`QTEST_MAIN(TstTextControlSelection)` + moc include at end (same pattern as
Task 2).

- [ ] **Step 2: Register in `tests/CMakeLists.txt`**

Append:

```cmake
add_executable(tst_markoff_textcontrol_selection tst_textcontrol_selection.cpp)
add_test(NAME tst_markoff_textcontrol_selection COMMAND tst_markoff_textcontrol_selection)
target_link_libraries(tst_markoff_textcontrol_selection PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_textcontrol_selection
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target tst_markoff_textcontrol_selection
```

- [ ] **Step 4: Run**

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_textcontrol_selection
```

Expected: all 8 slots PASS. `doubleClick_selectsWord` and `tripleClick_selectsLine` depend on Qt's double-click detection timing — if they flake, lean on `QTextCursor::select(QTextCursor::WordUnderCursor)` comparisons rather than tightening timing.

- [ ] **Step 5: Commit**

```bash
git add tests/tst_textcontrol_selection.cpp tests/CMakeLists.txt
git commit -m "Add tst_textcontrol_selection direct test suite

Shift-modifier extend, shift+click anchor, double-click word,
triple-click line, drag-select press/move/release, selectAll, selection
spanning U+FFFC placeholder. 8 slots."
```

---

## Task 4: `tst_textcontrol_editing.cpp`

Covers: char insert via KeyPress, Backspace, Delete, `insertPlainText()`,
Backspace/Delete adjacent to placeholder, read-only mode suppression.

**Files:**
- Create: `tests/tst_textcontrol_editing.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlEditing : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void keyPress_insertsCharacter();
    void backspace_removesPreviousCharacter();
    void deleteKey_removesNextCharacter();
    void insertPlainText_insertsAtCursor();
    void backspace_atStart_isNoop();
    void deleteKey_atEnd_isNoop();
    void backspace_adjacentToPlaceholder_removesPlaceholder();
    void deleteKey_adjacentToPlaceholder_removesPlaceholder();
    void readOnly_keyPressEvent_rejectsEdit();
    void readOnly_insertPlainText_stillInserts();
};

void TstTextControlEditing::keyPress_insertsCharacter()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    sendKey(fx.control, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("axb"));
    QCOMPARE(fx.control.textCursor().position(), 2);
}

void TstTextControlEditing::backspace_removesPreviousCharacter()
{
    auto fx = makeFixture(QStringLiteral("abc"), 2);
    sendKey(fx.control, Qt::Key_Backspace);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ac"));
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlEditing::deleteKey_removesNextCharacter()
{
    auto fx = makeFixture(QStringLiteral("abc"), 1);
    sendKey(fx.control, Qt::Key_Delete);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ac"));
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlEditing::insertPlainText_insertsAtCursor()
{
    auto fx = makeFixture(QStringLiteral("hello"), 5);
    fx.control.insertPlainText(QStringLiteral(" world"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("hello world"));
}

void TstTextControlEditing::backspace_atStart_isNoop()
{
    auto fx = makeFixture(QStringLiteral("abc"), 0);
    sendKey(fx.control, Qt::Key_Backspace);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("abc"));
}

void TstTextControlEditing::deleteKey_atEnd_isNoop()
{
    auto fx = makeFixture(QStringLiteral("abc"), 3);
    sendKey(fx.control, Qt::Key_Delete);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("abc"));
}

void TstTextControlEditing::backspace_adjacentToPlaceholder_removesPlaceholder()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    // doc: "a<fffc>b", position cursor at 2 (between placeholder and "b")
    QTextCursor pc(fx.document.get());
    pc.setPosition(2);
    fx.control.setTextCursor(pc);
    sendKey(fx.control, Qt::Key_Backspace);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlEditing::deleteKey_adjacentToPlaceholder_removesPlaceholder()
{
    auto fx = makeFixture(QStringLiteral("a"));
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    insertPlaceholder(c);
    c.insertText(QStringLiteral("b"));
    QTextCursor pc(fx.document.get());
    pc.setPosition(1);  // between "a" and placeholder
    fx.control.setTextCursor(pc);
    sendKey(fx.control, Qt::Key_Delete);
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
    QCOMPARE(fx.control.textCursor().position(), 1);
}

void TstTextControlEditing::readOnly_keyPressEvent_rejectsEdit()
{
    auto fx = makeFixture(QStringLiteral("abc"), 1);
    fx.control.setTextInteractionFlags(Qt::TextSelectableByMouse
                                       | Qt::TextSelectableByKeyboard);
    sendKey(fx.control, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("abc"));
}

void TstTextControlEditing::readOnly_insertPlainText_stillInserts()
{
    // Documented behavior: insertPlainText() is a direct API call that
    // bypasses TextInteractionFlags. Read-only mode only filters
    // keyboard/mouse events, not programmatic API calls.
    auto fx = makeFixture(QStringLiteral("abc"), 1);
    fx.control.setTextInteractionFlags(Qt::TextSelectableByMouse);
    fx.control.insertPlainText(QStringLiteral("X"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("aXbc"));
}
```

Include `QTEST_MAIN(TstTextControlEditing)` + moc.

- [ ] **Step 2: Register in CMakeLists.txt**

```cmake
add_executable(tst_markoff_textcontrol_editing tst_textcontrol_editing.cpp)
add_test(NAME tst_markoff_textcontrol_editing COMMAND tst_markoff_textcontrol_editing)
target_link_libraries(tst_markoff_textcontrol_editing PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_textcontrol_editing
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_editing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target tst_markoff_textcontrol_editing
```

- [ ] **Step 4: Run**

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_textcontrol_editing
```

Expected: all 10 slots PASS. If `readOnly_insertPlainText_stillInserts` fails with actually-blocked behavior, `insertPlainText` enforces read-only — update the slot expectation to match (not silently suppressed, raised via some other mechanism).

- [ ] **Step 5: Commit**

```bash
git add tests/tst_textcontrol_editing.cpp tests/CMakeLists.txt
git commit -m "Add tst_textcontrol_editing direct test suite

Character insert, Backspace, Delete, insertPlainText, boundary
no-ops, Backspace/Delete adjacent to placeholder, read-only mode
suppression of keyboard edits. 10 slots."
```

---

## Task 5: `tst_textcontrol_input.cpp` — IME + absorb CJK autocorrect

Covers: preedit insertion/update/commit, `inputMethodQuery()`, IME
attributes, and the four CJK autocorrect cases from `tst_cjk_autocorrect.cpp`.

**Files:**
- Create: `tests/tst_textcontrol_input.cpp`
- Modify: `tests/CMakeLists.txt`

Note: CJK autocorrect lives in `MarkdownTextItem::keyPressEvent`, not in
`TextControl` itself. We keep the four absorbed cases exercising the
MarkdownTextItem path (same pattern as the existing file, just renamed into
this TU) so no direct-vs-indirect split is imposed mid-file — all four CJK
slots go through `QApplication::sendEvent(&m_scene, &press)`. Direct IME
slots drive TextControl via `sendInputMethod()`.

- [ ] **Step 1: Create the test file**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include "MarkdownTextItem.h"
#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlInput : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // Direct IME tests — bare TextControl
    void preedit_isDisplayedButNotCommitted();
    void preedit_update_replacesPrior();
    void preedit_commit_clearsPreeditAndInsertsText();
    void inputMethodQuery_returnsCursorPosition();
    void preedit_withStyleAttribute_accepted();

    // CJK autocorrect — migrated from tst_cjk_autocorrect.cpp
    void fullWidthDoubleBracketOpenReplacesWithWikilink();
    void fullWidthDoubleBracketCloseReplacesWithClose();
    void fullWidthExclamationBracketReplacesWithEmbed();
    void singleFullWidthBracketDoesNotReplace();
    void replacementIsUndoable();

private:
    MarkdownTextItem *createItem();
    void typeText(const QString &text);
    QGraphicsScene m_scene;
    QGraphicsView m_view{&m_scene};
};

// --- Direct IME slots ---

void TstTextControlInput::preedit_isDisplayedButNotCommitted()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    sendInputMethod(fx.control, QString(), QStringLiteral("XY"));
    // Preedit text is not in the document plain text.
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
}

void TstTextControlInput::preedit_update_replacesPrior()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    sendInputMethod(fx.control, QString(), QStringLiteral("X"));
    sendInputMethod(fx.control, QString(), QStringLiteral("XY"));
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
    // The preedit-replace semantics mean only the second preedit is live;
    // query the TextControl if it surfaces current preedit.
    QVERIFY(fx.control.textCursor().position() == 1);
}

void TstTextControlInput::preedit_commit_clearsPreeditAndInsertsText()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    sendInputMethod(fx.control, QString(), QStringLiteral("XY"));
    sendInputMethod(fx.control, QStringLiteral("ZZ"), QString());
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("aZZb"));
}

void TstTextControlInput::inputMethodQuery_returnsCursorPosition()
{
    auto fx = makeFixture(QStringLiteral("hello"), 3);
    QVariant v = fx.control.inputMethodQuery(Qt::ImCursorPosition, QVariant());
    QCOMPARE(v.toInt(), 3);
}

void TstTextControlInput::preedit_withStyleAttribute_accepted()
{
    auto fx = makeFixture(QStringLiteral("ab"), 1);
    QList<QInputMethodEvent::Attribute> attrs;
    QTextCharFormat fmt;
    fmt.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    attrs.append(QInputMethodEvent::Attribute(
        QInputMethodEvent::TextFormat, 0, 2, QVariant::fromValue(fmt)));
    sendInputMethod(fx.control, QString(), QStringLiteral("XY"), attrs);
    // Preedit is accepted without mutation to plain text.
    QCOMPARE(fx.document->toPlainText(), QStringLiteral("ab"));
}

// --- Migrated CJK autocorrect slots ---

MarkdownTextItem *TstTextControlInput::createItem()
{
    auto *item = new MarkdownTextItem;
    m_scene.addItem(item);
    item->setFlag(QGraphicsItem::ItemIsFocusable);
    item->setFocus();
    item->setPlainText({});
    return item;
}

void TstTextControlInput::typeText(const QString &text)
{
    for (const QChar &ch : text) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(&m_scene, &press);
        QApplication::sendEvent(&m_scene, &release);
    }
}

void TstTextControlInput::fullWidthDoubleBracketOpenReplacesWithWikilink()
{
    auto *item = createItem();
    typeText(QStringLiteral("\u3010\u3010"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("[["));
}

void TstTextControlInput::fullWidthDoubleBracketCloseReplacesWithClose()
{
    auto *item = createItem();
    typeText(QStringLiteral("\u3011\u3011"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("]]"));
}

void TstTextControlInput::fullWidthExclamationBracketReplacesWithEmbed()
{
    auto *item = createItem();
    typeText(QStringLiteral("\uff01\u3010\u3010"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("![["));
}

void TstTextControlInput::singleFullWidthBracketDoesNotReplace()
{
    auto *item = createItem();
    typeText(QStringLiteral("\u3010"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("\u3010"));
}

void TstTextControlInput::replacementIsUndoable()
{
    auto *item = createItem();
    typeText(QStringLiteral("\u3010\u3010"));
    QCOMPARE(item->document()->toPlainText(), QStringLiteral("[["));
    item->document()->undo();
    QVERIFY(item->document()->toPlainText() != QStringLiteral("[["));
}

QTEST_MAIN(TstTextControlInput)
#include "tst_textcontrol_input.moc"
```

- [ ] **Step 2: Register in CMakeLists.txt**

```cmake
add_executable(tst_markoff_textcontrol_input tst_textcontrol_input.cpp)
add_test(NAME tst_markoff_textcontrol_input COMMAND tst_markoff_textcontrol_input)
target_link_libraries(tst_markoff_textcontrol_input PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_textcontrol_input
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_input PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target tst_markoff_textcontrol_input
```

- [ ] **Step 4: Run**

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_textcontrol_input
```

Expected: all 10 slots PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/tst_textcontrol_input.cpp tests/CMakeLists.txt
git commit -m "Add tst_textcontrol_input direct + migrated CJK suite

Direct IME: preedit display/update/commit, inputMethodQuery cursor
position, preedit style attributes. Plus the four CJK autocorrect
cases migrated verbatim from tst_cjk_autocorrect.cpp. 10 slots."
```

---

## Task 6: `tst_textcontrol_links.cpp`

Covers: `anchorAt()` positive/negative, `linkActivated` emission on click
over anchor, `linkHovered` emission on hover, silence on click outside or
on empty-href anchor.

**Files:**
- Create: `tests/tst_textcontrol_links.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlLinks : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void anchorAt_onAnchor_returnsHref();
    void anchorAt_offAnchor_returnsEmpty();
    void click_onAnchor_emitsLinkActivated();
    void click_offAnchor_doesNotEmit();
    void click_onEmptyHrefAnchor_doesNotEmit();
    void hover_onAnchor_emitsLinkHovered();

private:
    // Helper: inserts `text` as an anchor with `href` at document end
    // and returns the document-local position of the anchor's center.
    static QPointF insertAnchor(TextControlFixture &fx,
                                 const QString &text,
                                 const QString &href);
};

QPointF TstTextControlLinks::insertAnchor(TextControlFixture &fx,
                                          const QString &text,
                                          const QString &href)
{
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    int startPos = c.position();
    QTextCharFormat fmt;
    fmt.setAnchor(true);
    fmt.setAnchorHref(href);
    c.insertText(text, fmt);
    // Center of the inserted range
    QTextCursor mid(fx.document.get());
    mid.setPosition(startPos + text.length() / 2);
    return fx.control.cursorRect(mid).center();
}

void TstTextControlLinks::anchorAt_onAnchor_returnsHref()
{
    auto fx = makeFixture();
    QPointF pt = insertAnchor(fx, QStringLiteral("click me"),
                               QStringLiteral("https://example.org"));
    QCOMPARE(fx.control.anchorAt(pt),
             QStringLiteral("https://example.org"));
}

void TstTextControlLinks::anchorAt_offAnchor_returnsEmpty()
{
    auto fx = makeFixture(QStringLiteral("plain text with no anchor"), 0);
    QTextCursor c(fx.document.get());
    c.setPosition(5);
    QPointF pt = fx.control.cursorRect(c).center();
    QCOMPARE(fx.control.anchorAt(pt), QString());
}

void TstTextControlLinks::click_onAnchor_emitsLinkActivated()
{
    auto fx = makeFixture();
    fx.control.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QPointF pt = insertAnchor(fx, QStringLiteral("click me"),
                               QStringLiteral("https://example.org"));
    QSignalSpy spy(&fx.control, &Markoff::TextControl::linkActivated);
    sendMousePress(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(),
             QStringLiteral("https://example.org"));
}

void TstTextControlLinks::click_offAnchor_doesNotEmit()
{
    auto fx = makeFixture(QStringLiteral("plain text"), 0);
    fx.control.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QTextCursor c(fx.document.get());
    c.setPosition(5);
    QPointF pt = fx.control.cursorRect(c).center();
    QSignalSpy spy(&fx.control, &Markoff::TextControl::linkActivated);
    sendMousePress(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    QCOMPARE(spy.count(), 0);
}

void TstTextControlLinks::click_onEmptyHrefAnchor_doesNotEmit()
{
    auto fx = makeFixture();
    fx.control.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QPointF pt = insertAnchor(fx, QStringLiteral("click"), QString());
    QSignalSpy spy(&fx.control, &Markoff::TextControl::linkActivated);
    sendMousePress(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    QCOMPARE(spy.count(), 0);
}

void TstTextControlLinks::hover_onAnchor_emitsLinkHovered()
{
    auto fx = makeFixture();
    fx.control.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QPointF pt = insertAnchor(fx, QStringLiteral("click me"),
                               QStringLiteral("https://example.org"));
    QSignalSpy spy(&fx.control, &Markoff::TextControl::linkHovered);
    sendMouseMove(fx.control, pt, Qt::NoButton, Qt::NoModifier,
                  fx.contextWidget.get());
    QVERIFY(spy.count() >= 1);
    QCOMPARE(spy.last().first().toString(),
             QStringLiteral("https://example.org"));
}
```

Add `QTEST_MAIN(TstTextControlLinks)` + moc include.

- [ ] **Step 2: Register in CMakeLists.txt**

```cmake
add_executable(tst_markoff_textcontrol_links tst_textcontrol_links.cpp)
add_test(NAME tst_markoff_textcontrol_links COMMAND tst_markoff_textcontrol_links)
target_link_libraries(tst_markoff_textcontrol_links PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_textcontrol_links
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_links PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target tst_markoff_textcontrol_links
```

- [ ] **Step 4: Run**

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_textcontrol_links
```

Expected: all 6 slots PASS. `click_onAnchor_emitsLinkActivated` requires
`Qt::TextBrowserInteraction` (the flag that turns on anchor-click dispatch);
without it, TextControl treats anchors as plain text.

- [ ] **Step 5: Commit**

```bash
git add tests/tst_textcontrol_links.cpp tests/CMakeLists.txt
git commit -m "Add tst_textcontrol_links direct test suite

anchorAt() positive/negative, linkActivated on anchor click,
linkHovered on anchor hover, silence on non-anchor and empty-href
anchor clicks. 6 slots."
```

---

## Task 7: `tst_textcontrol_integration.cpp`

Indirect suite through `MarkdownTextItem` with real `MathTextObject` /
`CheckboxTextObject` handlers attached. Covers U+FFFC interactions that
direct tests can't reproduce because the test-only placeholder doesn't have
the same format properties (SourceProperty, CheckedProperty).

**Files:**
- Create: `tests/tst_textcontrol_integration.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/Editor.h>
#include "MarkdownTextItem.h"
#include "SceneCoordinator.h"
#include "SelectableItem.h"
#include "TextControl.h"

using namespace Markoff;

class TstTextControlIntegration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cursorRight_overRealMathGlyph_advancesByOne();
    void cursorLeft_overRealMathGlyph_retreatsByOne();
    void backspace_adjacentToMathGlyph_removesGlyphAndSource();
    void selection_spanningMathGlyph_yieldsSourceInToPlainText();
    void cursorRight_overRealCheckbox_advancesByOne();
    void arrowKey_traversesMixedMathAndCheckboxLine();

private:
    /// Build an editor, load markdown with inline math, wait for
    /// substitution to land, return the Editor plus the first
    /// MarkdownTextItem.
    struct EditorHandle {
        Editor *editor;
        MarkdownTextItem *item;
    };
    EditorHandle makeEditor(const QString &markdown);
};

TstTextControlIntegration::EditorHandle
TstTextControlIntegration::makeEditor(const QString &markdown)
{
    auto *editor = new Editor;
    editor->resize(600, 400);
    editor->setPlainText(markdown);
    editor->show();
    QApplication::processEvents();
    // Allow the reparse debounce (150 ms) to complete so inline object
    // substitution has applied.
    QTest::qWait(300);
    QApplication::processEvents();
    MarkdownTextItem *ti = nullptr;
    for (auto *item : editor->coordinatorForTesting()->items()) {
        if (item->isTextItem()) {
            ti = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    return {editor, ti};
}

void TstTextControlIntegration::cursorRight_overRealMathGlyph_advancesByOne()
{
    auto h = makeEditor(QStringLiteral("a $x^2$ b"));
    QVERIFY(h.item);
    // After substitution, document reads: "a " + <fffc> + " b"
    // i.e. length 5. Position cursor immediately before the glyph.
    const QString docText = h.item->document()->toPlainText();
    int fffcIdx = docText.indexOf(QChar(QChar::ObjectReplacementCharacter));
    QVERIFY2(fffcIdx != -1,
             qPrintable(QStringLiteral("expected U+FFFC in doc: '%1'")
                        .arg(docText)));
    QTextCursor c(h.item->document());
    c.setPosition(fffcIdx);
    h.item->textControl()->setTextCursor(c);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    QCOMPARE(h.item->textControl()->textCursor().position(), fffcIdx + 1);
    delete h.editor;
}

void TstTextControlIntegration::cursorLeft_overRealMathGlyph_retreatsByOne()
{
    auto h = makeEditor(QStringLiteral("a $x^2$ b"));
    QVERIFY(h.item);
    const QString docText = h.item->document()->toPlainText();
    int fffcIdx = docText.indexOf(QChar(QChar::ObjectReplacementCharacter));
    QVERIFY(fffcIdx != -1);
    QTextCursor c(h.item->document());
    c.setPosition(fffcIdx + 1);
    h.item->textControl()->setTextCursor(c);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    QCOMPARE(h.item->textControl()->textCursor().position(), fffcIdx);
    delete h.editor;
}

void TstTextControlIntegration::backspace_adjacentToMathGlyph_removesGlyphAndSource()
{
    auto h = makeEditor(QStringLiteral("a $x^2$ b"));
    QVERIFY(h.item);
    const QString initialDoc = h.item->document()->toPlainText();
    int fffcIdx = initialDoc.indexOf(QChar(QChar::ObjectReplacementCharacter));
    QVERIFY(fffcIdx != -1);
    QTextCursor c(h.item->document());
    c.setPosition(fffcIdx + 1);  // just past the glyph
    h.item->textControl()->setTextCursor(c);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    QApplication::processEvents();
    QTest::qWait(300);  // let reparse settle
    QApplication::processEvents();
    // After reparse, the math source `$x^2$` should be gone from
    // toMarkdown() too.
    auto *coord = h.editor->coordinatorForTesting();
    const QString serialised = coord->toMarkdown();
    QVERIFY2(!serialised.contains(QStringLiteral("$x^2$")),
             qPrintable(QStringLiteral("expected math source removed, got '%1'")
                        .arg(serialised)));
    delete h.editor;
}

void TstTextControlIntegration::selection_spanningMathGlyph_yieldsSourceInToPlainText()
{
    auto h = makeEditor(QStringLiteral("a $x^2$ b"));
    QVERIFY(h.item);
    h.item->textControl()->selectAll();
    const QString selected = h.item->textControl()
                                 ->textCursor()
                                 .selectedText();
    QVERIFY(selected.contains(QChar(QChar::ObjectReplacementCharacter)));
    delete h.editor;
}

void TstTextControlIntegration::cursorRight_overRealCheckbox_advancesByOne()
{
    auto h = makeEditor(QStringLiteral("- [ ] task"));
    QVERIFY(h.item);
    const QString docText = h.item->document()->toPlainText();
    int fffcIdx = docText.indexOf(QChar(QChar::ObjectReplacementCharacter));
    QVERIFY2(fffcIdx != -1,
             qPrintable(QStringLiteral("expected checkbox FFFC in doc: '%1'")
                        .arg(docText)));
    QTextCursor c(h.item->document());
    c.setPosition(fffcIdx);
    h.item->textControl()->setTextCursor(c);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    QCOMPARE(h.item->textControl()->textCursor().position(), fffcIdx + 1);
    delete h.editor;
}

void TstTextControlIntegration::arrowKey_traversesMixedMathAndCheckboxLine()
{
    auto h = makeEditor(QStringLiteral("- [ ] see $x^2$"));
    QVERIFY(h.item);
    // Walk cursor from 0 to end by repeated Right keys; position must
    // monotonically increase and land past every U+FFFC without getting
    // stuck.
    QTextCursor start(h.item->document());
    start.setPosition(0);
    h.item->textControl()->setTextCursor(start);
    int lastPos = -1;
    const int totalLen = h.item->document()->characterCount() - 1;
    for (int i = 0; i < totalLen + 2; ++i) {
        int curPos = h.item->textControl()->textCursor().position();
        QVERIFY2(curPos >= lastPos,
                 qPrintable(QStringLiteral("cursor retreated at step %1")
                            .arg(i)));
        lastPos = curPos;
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    }
    QCOMPARE(h.item->textControl()->textCursor().position(), totalLen);
    delete h.editor;
}

QTEST_MAIN(TstTextControlIntegration)
#include "tst_textcontrol_integration.moc"
```

- [ ] **Step 2: Register in CMakeLists.txt**

```cmake
add_executable(tst_markoff_textcontrol_integration tst_textcontrol_integration.cpp)
add_test(NAME tst_markoff_textcontrol_integration COMMAND tst_markoff_textcontrol_integration)
target_link_libraries(tst_markoff_textcontrol_integration PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_textcontrol_integration
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_markoff_textcontrol_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target tst_markoff_textcontrol_integration
```

- [ ] **Step 4: Run**

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_textcontrol_integration
```

Expected: all 6 slots PASS. These tests depend on the 150ms reparse
debounce + inline-object substitution fully running; `QTest::qWait(300)`
plus `processEvents()` should be enough margin. If a test fails because
the FFFC doesn't appear in `docText`, substitution hasn't run — increase
the wait. Don't silently guard it with `if(fffcIdx == -1) QSKIP(...)` —
that masks the regression the suite is meant to catch.

- [ ] **Step 5: Commit**

```bash
git add tests/tst_textcontrol_integration.cpp tests/CMakeLists.txt
git commit -m "Add tst_textcontrol_integration suite through MarkdownTextItem

Indirect tests using real MathTextObject / CheckboxTextObject handlers.
Cursor traversal past real glyphs, Backspace removing glyph + underlying
source, selection containing FFFC, mixed math+checkbox line monotonic
traversal. 6 slots."
```

---

## Task 8: Remove `tst_cjk_autocorrect.cpp`

The CJK cases now live in `tst_textcontrol_input.cpp`. The old file and its
CMake block are removed.

**Files:**
- Delete: `tests/tst_cjk_autocorrect.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Delete the file**

```bash
git rm tests/tst_cjk_autocorrect.cpp
```

- [ ] **Step 2: Remove the CMakeLists block**

Delete lines 36-40 of `tests/CMakeLists.txt` (the 5-line block beginning
`add_executable(tst_markoff_cjk_autocorrect ...)`).

- [ ] **Step 3: Re-run the configure + build**

```bash
cmake --build build
```

Expected: entire library + all test targets build clean. The
`tst_markoff_cjk_autocorrect` target no longer exists. If CMake complains
about a stale cache entry, delete `build/CMakeCache.txt` and reconfigure.

- [ ] **Step 4: Verify the migrated CJK cases still pass**

```bash
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_textcontrol_input
```

Expected: all 10 slots pass including the 4 migrated CJK cases + the
1 replacement-undoable case.

- [ ] **Step 5: Commit**

```bash
git add tests/CMakeLists.txt
git commit -m "Remove tst_cjk_autocorrect.cpp (absorbed into tst_textcontrol_input)

CJK autocorrect cases migrated verbatim in Task 5 / the preceding
commit. Removing the duplicate test target; the CJK assertions now
live alongside the IME preedit tests, reflecting that autocorrect
runs in the same MarkdownTextItem::keyPressEvent path."
```

---

## Task 9: Full-suite validation

Run the entire markoff test suite via ctest and confirm all tests pass —
including the six new TextControl targets and every pre-existing test.

**Files:** None modified.

- [ ] **Step 1: Configure + build**

```bash
cmake --build build
```

Expected: full build clean, no warnings about the new targets.

- [ ] **Step 2: Run ctest, filtered to markoff tests**

```bash
cd build && ctest -R markoff --output-on-failure
```

Expected: all tests pass, including:
- All pre-existing `tst_markoff_*` tests from before this plan.
- `tst_markoff_textcontrol_cursor`
- `tst_markoff_textcontrol_selection`
- `tst_markoff_textcontrol_editing`
- `tst_markoff_textcontrol_input`
- `tst_markoff_textcontrol_links`
- `tst_markoff_textcontrol_integration`
- `tst_markoff_cjk_autocorrect` is **gone** from the ctest listing.

If any pre-existing test regresses, investigate: a TextControl-adjacent
change in this plan might have exposed a real bug. Do not mark a test
as broken unless the root cause is understood.

- [ ] **Step 3: Commit test summary as a doc update**

Update `docs/TODO.md` to move the TextControl line out of §Testing:

Before (in §Testing):
```
- [ ] **TextControl has zero direct test coverage.** The 2,572-line Qt
  fork is exercised only indirectly through MarkdownTextItem. No tests
  for cursor movement edge cases, input method handling, drag-and-drop,
  link activation, preedit composition, or triple-click behavior. This
  is the single largest risk in the codebase.
```

Replace with a deferred-items note in §Testing (the remaining gaps —
MarkdownHighlighter, broader SceneCoordinator, CheckboxTextObject
paint/size, cross-item undo, perf harness — stay listed).

Append to §Recently fixed:

```
- **TextControl direct test coverage** (2026-04-16): six new test files —
  cursor, selection, editing, input (absorbs tst_cjk_autocorrect), links,
  integration — plus header-only `tests/support/textcontrol_testutil.h`
  with factories and event-synthesis helpers. ~53 slots covering Tier 1
  (fork-specific / known-risky) + Tier 2 (general correctness). Plan:
  `docs/plans/2026-04-16-textcontrol-test-coverage.md`. Spec:
  `docs/specs/2026-04-16-textcontrol-test-coverage-design.md`.
```

- [ ] **Step 4: Final commit**

```bash
git add docs/TODO.md
git commit -m "Update TODO.md — TextControl direct test coverage landed

Remove the TextControl-zero-tests entry from §Testing; the six-file
regression suite now covers Tier 1 + Tier 2. Append a §Recently fixed
entry pointing at the plan + spec."
```

---

## Notes for the executor

- **Offscreen QPA is mandatory** for every test (set via
  `set_tests_properties`). Running without it may flake on mouse
  synthesis or miss signals.
- **`QTest::qWait(300)`** is the conservative wait for the 150 ms reparse
  debounce in the integration suite. If a test flakes on a slow system,
  raise to 500; don't drop below 300.
- **Word-boundary slots** (Ctrl+Right/Left) use range asserts rather than
  exact position because Qt's word-segmentation may differ by locale. Keep
  the range asserts; don't tighten to exact positions.
- **IME slots** assume `preeditString` is delivered through
  `QInputMethodEvent`. If a platform-specific IME path is exercised via
  `QGuiApplication::inputMethod()->event(...)` and that's what production
  uses, the slot's direct-path assertion may need to switch to that API.
  Fix by updating the helper; tests should still target `TextControl`.
- **The sentinel `QTextFormat::UserObject + 100`** in
  `TestPlaceholderObject::TypeId` is chosen to be well clear of the
  production `UserObject + 1` (math) and `UserObject + 2` (checkbox).
  Don't lower it.
- **If an integration slot fails because the FFFC substitution hasn't
  run yet**, the root cause is timing — don't `QSKIP`, extend the wait.
  The whole point of the integration suite is to catch substitution-
  related regressions.
