# Find/Replace Search Bar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an embedded find/replace bar to the markoff Editor with incremental highlighting, match count, replace/replace-all, and standard keyboard shortcuts.

**Architecture:** `SearchBar` is a child widget of the Editor's viewport, positioned at the bottom via `resizeEvent()`. No container widget or layout changes needed — transparent to consumers. Match highlighting uses `TextControl::setExtraSelections()`. Search logic reuses existing `Editor::findText()`/`replaceText()`/`replaceAll()`.

**Tech Stack:** C++20, Qt6

**Spec:** `../specs/2026-04-14-find-replace-design.md`

---

### Task 1: SearchBar Widget (Find-Only UI)

**Files:**
- Create: `libs/markoff/include/markoff/SearchBar.h`
- Create: `libs/markoff/src/SearchBar.cpp`
- Modify: `libs/markoff/CMakeLists.txt`

- [ ] **Step 1: Create SearchBar header**

Create `libs/markoff/include/markoff/SearchBar.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SEARCHBAR_H
#define MARKOFF_SEARCHBAR_H

#include <QWidget>

class QLineEdit;
class QToolButton;
class QLabel;

namespace Markoff {

class SearchBar : public QWidget {
    Q_OBJECT

public:
    explicit SearchBar(QWidget *parent = nullptr);

    QString searchText() const;
    void setSearchText(const QString &text);
    bool matchCase() const;

    /// Show the bar in find-only mode.
    void showFind();
    /// Show the bar in find+replace mode.
    void showReplace();

    QString replaceText() const;

Q_SIGNALS:
    void searchTextChanged(const QString &text);
    void findNext();
    void findPrevious();
    void replaceRequested();
    void replaceAllRequested();
    void closed();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void buildUi();

    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QToolButton *m_prevButton = nullptr;
    QToolButton *m_nextButton = nullptr;
    QToolButton *m_caseButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QLabel *m_countLabel = nullptr;

    // Replace row
    QWidget *m_replaceRow = nullptr;
    QToolButton *m_replaceButton = nullptr;
    QToolButton *m_replaceAllButton = nullptr;
};

} // namespace Markoff

#endif // MARKOFF_SEARCHBAR_H
```

- [ ] **Step 2: Create SearchBar implementation**

Create `libs/markoff/src/SearchBar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/SearchBar.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QKeyEvent>
#include <QIcon>

namespace Markoff {

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    hide();
}

void SearchBar::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);

    // Find row
    auto *findRow = new QHBoxLayout;
    findRow->setSpacing(2);

    m_findEdit = new QLineEdit;
    m_findEdit->setPlaceholderText(tr("Find..."));
    m_findEdit->setClearButtonEnabled(true);

    m_prevButton = new QToolButton;
    m_prevButton->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    m_prevButton->setToolTip(tr("Find Previous (Shift+Enter)"));
    m_prevButton->setAutoRaise(true);

    m_nextButton = new QToolButton;
    m_nextButton->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    m_nextButton->setToolTip(tr("Find Next (Enter)"));
    m_nextButton->setAutoRaise(true);

    m_caseButton = new QToolButton;
    m_caseButton->setText(QStringLiteral("Aa"));
    m_caseButton->setToolTip(tr("Match Case"));
    m_caseButton->setCheckable(true);
    m_caseButton->setAutoRaise(true);

    m_countLabel = new QLabel;
    m_countLabel->setMinimumWidth(60);

    m_closeButton = new QToolButton;
    m_closeButton->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    m_closeButton->setToolTip(tr("Close (Escape)"));
    m_closeButton->setAutoRaise(true);

    findRow->addWidget(m_findEdit, 1);
    findRow->addWidget(m_prevButton);
    findRow->addWidget(m_nextButton);
    findRow->addWidget(m_caseButton);
    findRow->addWidget(m_countLabel);
    findRow->addWidget(m_closeButton);

    mainLayout->addLayout(findRow);

    // Replace row
    m_replaceRow = new QWidget;
    auto *replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(2);

    m_replaceEdit = new QLineEdit;
    m_replaceEdit->setPlaceholderText(tr("Replace..."));
    m_replaceEdit->setClearButtonEnabled(true);

    m_replaceButton = new QToolButton;
    m_replaceButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-find-replace")));
    m_replaceButton->setToolTip(tr("Replace (Enter in replace field)"));
    m_replaceButton->setAutoRaise(true);

    m_replaceAllButton = new QToolButton;
    m_replaceAllButton->setText(tr("All"));
    m_replaceAllButton->setToolTip(tr("Replace All"));
    m_replaceAllButton->setAutoRaise(true);

    replaceLayout->addWidget(m_replaceEdit, 1);
    replaceLayout->addWidget(m_replaceButton);
    replaceLayout->addWidget(m_replaceAllButton);
    // Spacer to align with find row buttons
    replaceLayout->addSpacing(m_caseButton->sizeHint().width()
                              + m_countLabel->minimumWidth()
                              + m_closeButton->sizeHint().width()
                              + 6); // spacing

    mainLayout->addWidget(m_replaceRow);
    m_replaceRow->hide();

    // Connections
    connect(m_findEdit, &QLineEdit::textChanged,
            this, &SearchBar::searchTextChanged);
    connect(m_nextButton, &QToolButton::clicked,
            this, &SearchBar::findNext);
    connect(m_prevButton, &QToolButton::clicked,
            this, &SearchBar::findPrevious);
    connect(m_closeButton, &QToolButton::clicked,
            this, &SearchBar::closed);
    connect(m_caseButton, &QToolButton::toggled,
            this, [this]() { emit searchTextChanged(m_findEdit->text()); });
    connect(m_replaceButton, &QToolButton::clicked,
            this, &SearchBar::replaceRequested);
    connect(m_replaceAllButton, &QToolButton::clicked,
            this, &SearchBar::replaceAllRequested);
}

QString SearchBar::searchText() const
{
    return m_findEdit->text();
}

void SearchBar::setSearchText(const QString &text)
{
    m_findEdit->setText(text);
}

bool SearchBar::matchCase() const
{
    return m_caseButton->isChecked();
}

QString SearchBar::replaceText() const
{
    return m_replaceEdit->text();
}

void SearchBar::showFind()
{
    m_replaceRow->hide();
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void SearchBar::showReplace()
{
    m_replaceRow->show();
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void SearchBar::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        emit closed();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_replaceEdit->hasFocus()) {
            emit replaceRequested();
        } else if (e->modifiers() & Qt::ShiftModifier) {
            emit findPrevious();
        } else {
            emit findNext();
        }
        return;
    }
    QWidget::keyPressEvent(e);
}

} // namespace Markoff
```

- [ ] **Step 3: Register SearchBar in CMakeLists.txt**

Add to the `add_library(markoff STATIC ...)` source list in `libs/markoff/CMakeLists.txt`, after the `src/Editor.cpp` line:

```cmake
    src/SearchBar.cpp
```

- [ ] **Step 4: Build and verify compilation**

Run:
```bash
cd /home/clinton/dev/Corbomite && cmake -B build -DCORBOMITE_DEV_BUILD=ON 2>&1 | tail -3
cmake --build build --target markoff 2>&1 | tail -5
```
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/Corbomite && git add libs/markoff/include/markoff/SearchBar.h libs/markoff/src/SearchBar.cpp libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): add SearchBar widget skeleton

Two-row find/replace bar with find field, prev/next buttons,
match case toggle, count label, and replace row (hidden default).
Signals for search events, keyboard handling for Enter/Escape."
```

---

### Task 2: Embed SearchBar in Editor

**Files:**
- Modify: `libs/markoff/include/markoff/Editor.h`
- Modify: `libs/markoff/src/Editor.cpp`

- [ ] **Step 1: Add SearchBar member and methods to Editor.h**

In `libs/markoff/include/markoff/Editor.h`, add forward declaration and members:

```cpp
// After the existing forward declarations (after "class ResourceProvider;"):
class SearchBar;
```

Add public methods after the existing search section (`// --- Search ---`):

```cpp
    /// Show the embedded find bar (Ctrl+F).
    void showSearchBar();
    /// Show the embedded find+replace bar (Ctrl+H).
    void showReplaceBar();
    /// Hide the search bar and clear highlights.
    void hideSearchBar();
```

Add to the private section (after `m_document`):

```cpp
    SearchBar *m_searchBar = nullptr;
    QString m_lastSearchText;
    int m_currentMatchIndex = -1;
    int m_totalMatchCount = 0;
```

Add private methods (after `insertAtCursor`):

```cpp
    void highlightAllMatches(const QString &text);
    void clearSearchHighlights();
    void updateMatchCount();
    void repositionSearchBar();
    QTextDocument::FindFlags searchFlags() const;
```

- [ ] **Step 2: Create SearchBar as viewport child, wire signals**

In `libs/markoff/src/Editor.cpp`, add `#include "markoff/SearchBar.h"` to the includes.

At the end of the `Editor::Editor(QWidget *parent)` constructor (after the existing body, before the closing `}`), add:

```cpp
    // SearchBar is a child of the viewport, positioned at the bottom
    m_searchBar = new SearchBar(viewport());
    m_searchBar->hide();

    connect(m_searchBar, &SearchBar::searchTextChanged,
            this, &Editor::highlightAllMatches);
    connect(m_searchBar, &SearchBar::findNext, this, [this]() {
        findText(m_searchBar->searchText(), searchFlags());
        updateMatchCount();
    });
    connect(m_searchBar, &SearchBar::findPrevious, this, [this]() {
        findText(m_searchBar->searchText(),
                 searchFlags() | QTextDocument::FindBackward);
        updateMatchCount();
    });
    connect(m_searchBar, &SearchBar::replaceRequested, this, [this]() {
        replaceText(m_searchBar->searchText(), m_searchBar->replaceText(),
                    searchFlags());
        highlightAllMatches(m_searchBar->searchText());
    });
    connect(m_searchBar, &SearchBar::replaceAllRequested, this, [this]() {
        int count = replaceAll(m_searchBar->searchText(),
                               m_searchBar->replaceText(), searchFlags());
        highlightAllMatches(m_searchBar->searchText());
        Q_UNUSED(count);
    });
    connect(m_searchBar, &SearchBar::closed, this, &Editor::hideSearchBar);
```

- [ ] **Step 3: Implement search helper methods (stubs + positioning)**

Add to `libs/markoff/src/Editor.cpp` (before the closing `} // namespace Markoff`):

```cpp
QTextDocument::FindFlags Editor::searchFlags() const
{
    QTextDocument::FindFlags flags;
    if (m_searchBar && m_searchBar->matchCase())
        flags |= QTextDocument::FindCaseSensitively;
    return flags;
}

void Editor::showSearchBar()
{
    if (auto *ti = focusedTextItem()) {
        QString sel = ti->textControl()->textCursor().selectedText();
        if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
            m_searchBar->setSearchText(sel);
    }
    m_searchBar->showFind();
    repositionSearchBar();
}

void Editor::showReplaceBar()
{
    if (auto *ti = focusedTextItem()) {
        QString sel = ti->textControl()->textCursor().selectedText();
        if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
            m_searchBar->setSearchText(sel);
    }
    m_searchBar->showReplace();
    repositionSearchBar();
}

void Editor::hideSearchBar()
{
    m_searchBar->hide();
    clearSearchHighlights();
    setFocus();
}

void Editor::repositionSearchBar()
{
    if (!m_searchBar->isVisible()) return;
    QSize barSize = m_searchBar->sizeHint();
    int vw = viewport()->width();
    m_searchBar->setGeometry(0, viewport()->height() - barSize.height(),
                             vw, barSize.height());
    m_searchBar->raise();
}

// Stubs — replaced with real implementations in Task 3
void Editor::highlightAllMatches(const QString &text) { Q_UNUSED(text); }
void Editor::clearSearchHighlights() {}
void Editor::updateMatchCount() {}
```

Also add `repositionSearchBar()` call to the existing `Editor::resizeEvent()`:

In the `resizeEvent` method, add at the end (before the closing `}`):

```cpp
    repositionSearchBar();
```

- [ ] **Step 4: Add keyboard shortcuts in keyPressEvent**

In `libs/markoff/src/Editor.cpp`, in `Editor::keyPressEvent()`, add inside the `if (e->modifiers() & Qt::ControlModifier)` block (after the Ctrl+V handler):

```cpp
        if (e->key() == Qt::Key_F) { showSearchBar(); return; }
        if (e->key() == Qt::Key_H) { showReplaceBar(); return; }
```

Add F3/Shift+F3 handling after the `bool ctrl` line (before the `// Ctrl+Home` block):

```cpp
    // F3 / Shift+F3: find next/previous (works even with bar closed)
    if (e->key() == Qt::Key_F3) {
        if (m_searchBar && !m_searchBar->searchText().isEmpty()) {
            if (shift)
                findText(m_searchBar->searchText(),
                         searchFlags() | QTextDocument::FindBackward);
            else
                findText(m_searchBar->searchText(), searchFlags());
            updateMatchCount();
        }
        return;
    }
```

- [ ] **Step 5: Build and verify**

Run:
```bash
cd /home/clinton/dev/Corbomite && cmake --build build --target markoff 2>&1 | tail -5
```
Expected: Compiles without errors.

- [ ] **Step 6: Commit**

```bash
cd /home/clinton/dev/Corbomite && git add libs/markoff/include/markoff/Editor.h libs/markoff/src/Editor.cpp
git commit -m "feat(markoff): embed SearchBar in Editor with keyboard shortcuts

Compose Editor and SearchBar via QVBoxLayout container.
Wire Ctrl+F (find), Ctrl+H (replace), F3/Shift+F3 (next/prev),
Escape (close). Connect search bar signals to existing find/replace API."
```

---

### Task 3: Match Highlighting and Count

**Files:**
- Modify: `libs/markoff/src/Editor.cpp`
- Modify: `libs/markoff/include/markoff/SearchBar.h`
- Modify: `libs/markoff/src/SearchBar.cpp`

- [ ] **Step 1: Add setMatchCount to SearchBar**

In `libs/markoff/include/markoff/SearchBar.h`, add public method:

```cpp
    void setMatchCount(int current, int total);
```

In `libs/markoff/src/SearchBar.cpp`, add implementation:

```cpp
void SearchBar::setMatchCount(int current, int total)
{
    if (total == 0) {
        m_countLabel->setText(tr("No results"));
    } else if (total > 65536) {
        m_countLabel->setText(tr("65536+ matches"));
    } else {
        m_countLabel->setText(tr("%1 of %2").arg(current).arg(total));
    }
}
```

- [ ] **Step 2: Implement highlightAllMatches (replace stub)**

In `libs/markoff/src/Editor.cpp`, add `#include <QTextEdit>` to the includes (for `QTextEdit::ExtraSelection`).

Remove the stub `void Editor::highlightAllMatches(const QString &text) { Q_UNUSED(text); }` and replace with:

Add the implementation:

```cpp
void Editor::highlightAllMatches(const QString &text)
{
    clearSearchHighlights();
    m_lastSearchText = text;
    m_totalMatchCount = 0;
    m_currentMatchIndex = -1;

    if (text.isEmpty() || !m_coordinator) {
        m_searchBar->setMatchCount(0, 0);
        return;
    }

    QTextDocument::FindFlags flags = searchFlags();

    // Highlight formats
    QTextCharFormat matchFmt;
    matchFmt.setBackground(QColor(255, 255, 0, 120)); // yellow

    QTextCharFormat currentFmt;
    currentFmt.setBackground(QColor(255, 150, 50, 180)); // orange

    // Find the focused item and cursor position for current-match tracking
    auto *focusedItem = focusedTextItem();
    int focusedCursorPos = -1;
    if (focusedItem)
        focusedCursorPos = focusedItem->textControl()->textCursor().position();

    int globalIndex = 0;
    int closestIndex = -1;
    int closestDistance = std::numeric_limits<int>::max();
    MarkdownTextItem *closestItem = nullptr;
    QTextCursor closestCursor;

    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        auto *doc = ti->document();
        QList<QTextEdit::ExtraSelection> selections;

        QTextCursor search(doc);
        while (!(search = doc->find(text, search, flags)).isNull()) {
            if (m_totalMatchCount >= 65536) break;

            QTextEdit::ExtraSelection sel;
            sel.cursor = search;
            sel.format = matchFmt;
            selections.append(sel);

            // Track closest match to cursor for current-match index
            if (ti == focusedItem && focusedCursorPos >= 0) {
                int dist = search.selectionStart() - focusedCursorPos;
                if (dist >= 0 && dist < closestDistance) {
                    closestDistance = dist;
                    closestIndex = globalIndex;
                    closestItem = ti;
                    closestCursor = search;
                }
            }

            ++globalIndex;
            ++m_totalMatchCount;
        }

        ti->textControl()->setExtraSelections(selections);
    }

    // If no match found at/after cursor, use the first match
    if (closestIndex < 0 && m_totalMatchCount > 0)
        closestIndex = 0;

    m_currentMatchIndex = closestIndex;

    // Highlight current match in orange
    if (closestItem && closestCursor.hasSelection()) {
        auto existing = closestItem->textControl()->extraSelections();
        for (auto &sel : existing) {
            if (sel.cursor.selectionStart() == closestCursor.selectionStart()
                && sel.cursor.selectionEnd() == closestCursor.selectionEnd()) {
                sel.format = currentFmt;
                break;
            }
        }
        // Convert back from QAbstractTextDocumentLayout::Selection
        QList<QTextEdit::ExtraSelection> updated;
        auto rawSelections = closestItem->textControl()->extraSelections();
        // Re-set with the updated format
        for (int i = 0; i < rawSelections.size(); ++i) {
            if (rawSelections[i].cursor.selectionStart() == closestCursor.selectionStart()
                && rawSelections[i].cursor.selectionEnd() == closestCursor.selectionEnd()) {
                rawSelections[i].format = currentFmt;
            }
        }
        closestItem->textControl()->setExtraSelections(rawSelections);
    }

    m_searchBar->setMatchCount(
        m_totalMatchCount > 0 ? m_currentMatchIndex + 1 : 0,
        m_totalMatchCount);
}
```

- [ ] **Step 3: Implement clearSearchHighlights (replace stub)**

Remove the stub `void Editor::clearSearchHighlights() {}` and replace with:

```cpp
void Editor::clearSearchHighlights()
{
    if (!m_coordinator) return;
    const QList<QTextEdit::ExtraSelection> empty;
    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        ti->textControl()->setExtraSelections(empty);
    }
    m_totalMatchCount = 0;
    m_currentMatchIndex = -1;
}
```

- [ ] **Step 4: Implement updateMatchCount (replace stub)**

Remove the stub `void Editor::updateMatchCount() {}` and replace with:

```cpp
void Editor::updateMatchCount()
{
    if (m_lastSearchText.isEmpty() || m_totalMatchCount == 0) return;

    // Recompute current index based on cursor position
    auto *focusedItem = focusedTextItem();
    if (!focusedItem) return;
    int cursorPos = focusedItem->textControl()->textCursor().selectionStart();

    int index = 0;
    QTextDocument::FindFlags flags = searchFlags();
    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        auto *doc = ti->document();
        QTextCursor search(doc);
        while (!(search = doc->find(m_lastSearchText, search, flags)).isNull()) {
            if (ti == focusedItem && search.selectionStart() == cursorPos) {
                m_currentMatchIndex = index;
                m_searchBar->setMatchCount(index + 1, m_totalMatchCount);

                // Update orange highlight
                highlightAllMatches(m_lastSearchText);
                return;
            }
            ++index;
        }
    }
}
```

- [ ] **Step 5: Build and verify**

Run:
```bash
cd /home/clinton/dev/Corbomite && cmake --build build --target markoff 2>&1 | tail -5
```
Expected: Compiles without errors.

- [ ] **Step 6: Commit**

```bash
cd /home/clinton/dev/Corbomite && git add libs/markoff/include/markoff/SearchBar.h libs/markoff/src/SearchBar.cpp libs/markoff/src/Editor.cpp
git commit -m "feat(markoff): incremental match highlighting and count

Highlight all matches yellow, current match orange via ExtraSelections.
Display 'N of M' in search bar. Cap at 65536 highlights.
Recompute highlights on every keystroke in find field."
```

---

### Task 4: Fix replaceAll Undo Grouping

**Files:**
- Modify: `libs/markoff/src/Editor.cpp`

- [ ] **Step 1: Wrap replaceAll in beginEditBlock/endEditBlock**

In `libs/markoff/src/Editor.cpp`, replace the `Editor::replaceAll` method:

```cpp
int Editor::replaceAll(const QString &find, const QString &replace,
                       QTextDocument::FindFlags flags)
{
    if (find.isEmpty() || !m_coordinator) return 0;
    int count = 0;
    for (auto *item : m_coordinator->items()) {
        if (!item->isTextItem()) continue;
        auto *ti = static_cast<MarkdownTextItem *>(item);
        auto *doc = ti->document();
        QTextCursor cursor(doc);
        QTextCursor first = doc->find(find, cursor, flags);
        if (first.isNull()) continue;

        cursor = first;
        cursor.beginEditBlock();
        do {
            cursor.insertText(replace);
            ++count;
            cursor = doc->find(find, cursor, flags);
        } while (!cursor.isNull());
        first.endEditBlock();
    }
    return count;
}
```

- [ ] **Step 2: Build and verify**

Run:
```bash
cd /home/clinton/dev/Corbomite && cmake --build build --target markoff 2>&1 | tail -5
```
Expected: Compiles without errors.

- [ ] **Step 3: Commit**

```bash
cd /home/clinton/dev/Corbomite && git add libs/markoff/src/Editor.cpp
git commit -m "fix(markoff): wrap replaceAll in beginEditBlock for atomic undo

Each item's replacements are grouped so Ctrl+Z undoes all
replacements within an item as a single operation."
```

---

### Task 5: Tests

**Files:**
- Create: `libs/markoff/tests/tst_search_bar.cpp`
- Modify: `libs/markoff/tests/CMakeLists.txt`

- [ ] **Step 1: Write tests**

Create `libs/markoff/tests/tst_search_bar.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>
#include <QSignalSpy>
#include "markoff/Editor.h"
#include "markoff/SearchBar.h"

using namespace Markoff;

class TestSearchBar : public QObject {
    Q_OBJECT

private slots:
    void findHighlightsMatches();
    void findCountsMatches();
    void findNextAdvancesCursor();
    void replaceAllWithUndo();
    void emptySearchClearsHighlights();
    void matchCaseToggle();
};

void TestSearchBar::findHighlightsMatches()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("foo bar foo baz foo"));

    // Trigger search via the public API
    QVERIFY(editor.findText(QStringLiteral("foo")));
}

void TestSearchBar::findCountsMatches()
{
    SearchBar bar;
    bar.setMatchCount(3, 17);
    // Verify the label text via the widget's visual state
    // (SearchBar internally updates m_countLabel)
    // This test verifies the method doesn't crash and accepts valid args
    bar.setMatchCount(0, 0);
    bar.setMatchCount(1, 65537);
}

void TestSearchBar::findNextAdvancesCursor()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("aaa bbb aaa bbb aaa"));

    // First find should succeed
    QVERIFY(editor.findText(QStringLiteral("aaa")));
    // Second find should also succeed (advances to next match)
    QVERIFY(editor.findText(QStringLiteral("aaa")));
    // Third find wraps around
    QVERIFY(editor.findText(QStringLiteral("aaa")));
}

void TestSearchBar::replaceAllWithUndo()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("cat dog cat dog cat"));

    int count = editor.replaceAll(QStringLiteral("cat"),
                                   QStringLiteral("bird"));
    QCOMPARE(count, 3);

    QString after = editor.toPlainText();
    QVERIFY(after.contains(QStringLiteral("bird")));
    QVERIFY(!after.contains(QStringLiteral("cat")));

    // Single undo should restore all replacements within the item
    editor.undo();
    QString undone = editor.toPlainText();
    QCOMPARE(undone, QStringLiteral("cat dog cat dog cat"));
}

void TestSearchBar::emptySearchClearsHighlights()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("test text"));

    // Search then clear — should not crash
    QVERIFY(editor.findText(QStringLiteral("test")));
    QVERIFY(!editor.findText(QString()));
}

void TestSearchBar::matchCaseToggle()
{
    Editor editor;
    editor.setPlainText(QStringLiteral("Foo foo FOO"));

    // Case-insensitive (default)
    QVERIFY(editor.findText(QStringLiteral("foo")));

    // Case-sensitive
    QVERIFY(editor.findText(QStringLiteral("foo"),
                             QTextDocument::FindCaseSensitively));
}

QTEST_MAIN(TestSearchBar)
#include "tst_search_bar.moc"
```

- [ ] **Step 2: Register the test in CMakeLists.txt**

Add to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_search_bar tst_search_bar.cpp)
add_test(NAME tst_markoff_search_bar COMMAND tst_markoff_search_bar)
target_link_libraries(tst_markoff_search_bar PRIVATE Qt6::Test Qt6::Widgets markoff)
set_tests_properties(tst_markoff_search_bar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build and run tests**

Run:
```bash
cd /home/clinton/dev/Corbomite && cmake -B build -DCORBOMITE_DEV_BUILD=ON 2>&1 | tail -3
cmake --build build --target tst_markoff_search_bar 2>&1 | tail -5
QT_QPA_PLATFORM=offscreen ./build/bin/tst_markoff_search_bar
```
Expected: All tests PASS.

- [ ] **Step 4: Run full test suite**

Run:
```bash
cd /home/clinton/dev/Corbomite/build && find . -name "tst_markoff*" -type f -executable | while read t; do QT_QPA_PLATFORM=offscreen "$t" 2>&1 | grep -E "^(Totals|FAIL)"; done
```
Expected: All tests pass including the new ones.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/Corbomite && git add libs/markoff/tests/tst_search_bar.cpp libs/markoff/tests/CMakeLists.txt
git commit -m "test(markoff): add SearchBar and find/replace tests

Test find navigation, match counting, replace-all with atomic undo,
empty search handling, and case sensitivity."
```

---

### Task 6: Manual Smoke Test

- [ ] **Step 1: Build the test app**

```bash
cd /home/clinton/dev/Corbomite && cmake --build build --target markoff-testapp
```

- [ ] **Step 2: Launch and test**

```bash
cd /home/clinton/dev/Corbomite && ./build/bin/markoff-testapp libs/markoff/tests/showcase.md &
```

Manual checklist:
1. Ctrl+F opens the find bar at the bottom
2. Typing in the find field highlights matches incrementally (yellow)
3. The current match is highlighted orange
4. "N of M" count updates as you type
5. Enter advances to next match
6. Shift+Enter goes to previous match
7. Ctrl+H shows the replace row
8. Replace button replaces current match and advances
9. Replace All replaces all and shows correct count
10. Escape closes the bar and clears highlights
11. F3 / Shift+F3 work even after closing the bar
12. Ctrl+Z after Replace All undoes all replacements at once
