# Cross-Boundary Selection Implementation Plan

> **Status: IMPLEMENTED** — SelectionManager with mouse and keyboard
> selection, Ctrl+C/Ctrl+A/Escape handling all shipped.

**Goal:** Implement unified selection across heterogeneous QGraphicsScene items (text, tables, code blocks, images) with markdown clipboard serialization.

**Architecture:** A `SelectionManager` QObject owned by a QGraphicsScene subclass intercepts mouse events when a drag crosses item boundaries. It breaks Qt's implicit mouse grab via `ungrabMouse()`, then manages selection state across items through a `SelectableItem` interface. Text items use programmatic `setTextCursor()` for native selection highlights; non-text items paint a blue overlay.

**Tech Stack:** C++20, Qt6 Widgets (QGraphicsView, QGraphicsScene, QGraphicsItem), our forked TextControl, QTest.

**Spec:** `docs/specs/2026-04-02-cross-boundary-selection-design.md`

**Parent spec:** `docs/specs/2026-04-02-graphicsview-editor-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/SelectableItem.h` | Create | Pure virtual interface — `isTextItem`, `hitTest`, `setSelection`, `clearSelection`, `selectedMarkdown`, `allMarkdown`, `setFullySelected`, `isFullySelected`, `toMarkdown`, `asGraphicsItem` |
| `src/SelectionManager.h` | Create | Public API — `handleMousePress`, `handleMouseMove`, `handleMouseRelease`, `handleKeyPress`, `createMimeData`, `clearSelection`, `hasSelection` |
| `src/SelectionManager.cpp` | Create | State machine (None/WithinItem/CrossBoundary), `applySelection()` algorithm, clipboard serialization |
| `src/MarkdownTextItem.h` | Create | QGraphicsObject + SelectableItem. Wraps TextControl + QTextDocument for an editable text region |
| `src/MarkdownTextItem.cpp` | Create | Implements SelectableItem text operations: `hitTest`, `setSelection`, `clearSelection`, `selectedMarkdown`, `allMarkdown`, `toMarkdown`. Delegates painting to TextControl::drawContents. |
| `src/BlockItem.h` | Create | QGraphicsObject + SelectableItem base for non-text items. `setFullySelected`, overlay painting, pure virtual `toMarkdown` |
| `src/BlockItem.cpp` | Create | Overlay painting (`QColor(51, 153, 255, 80)`) when fully selected |
| `src/StubBlockItem.h` | Create | Minimal concrete BlockItem for testing (fixed size, stores markdown string) |
| `src/StubBlockItem.cpp` | Create | Implementation of StubBlockItem |
| `tests/tst_selection.cpp` | Create | All selection tests — state machine, cross-boundary algorithm, clipboard, edge cases |
| `tests/CMakeLists.txt` | Modify | Add `tst_markoff_selection` executable |
| `CMakeLists.txt` | Modify | Add new source files to markoff library |

## Build & Test Commands

All commands run from the repo root `/home/clinton/dev/Corbomite`.

```bash
# Configure (once)
cmake -B build -DCORBOMITE_DEV_BUILD=ON

# Build markoff library + tests
cmake --build build --target tst_markoff_selection

# Run selection tests
cd build && ctest -R tst_markoff_selection --output-on-failure

# Run all markoff tests (after all tasks complete)
cd build && ctest -R tst_markoff --output-on-failure
```

---

## Task 1: SelectableItem Interface

**Files:**
- Create: `libs/markoff/src/SelectableItem.h`
- Modify: `libs/markoff/CMakeLists.txt` (header-only, just add to sources for IDE visibility)

- [ ] **Step 1: Create the interface header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SELECTABLEITEM_H
#define MARKOFF_SELECTABLEITEM_H

#include <QString>
#include <QPointF>

class QGraphicsItem;

namespace Markoff {

/// Interface for items that participate in cross-boundary selection.
/// Text items implement hitTest/setSelection/selectedMarkdown.
/// Non-text items implement setFullySelected/toMarkdown.
class SelectableItem {
public:
    virtual ~SelectableItem() = default;

    virtual QGraphicsItem *asGraphicsItem() = 0;
    virtual bool isTextItem() const = 0;

    // --- Text item operations (no-op defaults for non-text) ---
    virtual int hitTest(const QPointF &scenePos) const { Q_UNUSED(scenePos); return -1; }
    virtual void setSelection(int anchorPos, int cursorPos) { Q_UNUSED(anchorPos); Q_UNUSED(cursorPos); }
    virtual void clearSelection() {}
    virtual QString selectedMarkdown() const { return {}; }
    virtual QString allMarkdown() const { return {}; }

    // --- Non-text item operations (no-op defaults for text) ---
    virtual void setFullySelected(bool selected) { Q_UNUSED(selected); }
    virtual bool isFullySelected() const { return false; }

    // --- Common ---
    virtual QString toMarkdown() const = 0;
};

} // namespace Markoff

#endif // MARKOFF_SELECTABLEITEM_H
```

- [ ] **Step 2: Add to CMakeLists.txt**

In `libs/markoff/CMakeLists.txt`, add `src/SelectableItem.h` to the `add_library(markoff STATIC ...)` source list, after the existing headers:

```cmake
add_library(markoff STATIC
    include/markoff/Editor.h
    include/markoff/ReadingView.h
    src/SelectableItem.h
    src/Document.cpp
    # ... rest unchanged
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build --target markoff`
Expected: Builds successfully. Header-only, no link changes.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff/src/SelectableItem.h libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): add SelectableItem interface for cross-boundary selection"
```

---

## Task 2: BlockItem Base Class

**Files:**
- Create: `libs/markoff/src/BlockItem.h`
- Create: `libs/markoff/src/BlockItem.cpp`
- Modify: `libs/markoff/CMakeLists.txt`

- [ ] **Step 1: Create BlockItem header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_BLOCKITEM_H
#define MARKOFF_BLOCKITEM_H

#include "SelectableItem.h"
#include <QGraphicsObject>

namespace Markoff {

/// Base class for non-text scene items (tables, code blocks, images).
/// Provides fully-selected overlay painting and the SelectableItem
/// interface with non-text defaults.
class BlockItem : public QGraphicsObject, public SelectableItem {
    Q_OBJECT
public:
    explicit BlockItem(QGraphicsItem *parent = nullptr);

    // SelectableItem
    QGraphicsItem *asGraphicsItem() override { return this; }
    bool isTextItem() const override { return false; }
    void setFullySelected(bool selected) override;
    bool isFullySelected() const override { return m_fullySelected; }

protected:
    /// Call from subclass paint() to draw selection overlay on top.
    void paintSelectionOverlay(QPainter *painter, const QRectF &rect);

private:
    bool m_fullySelected = false;
};

} // namespace Markoff

#endif // MARKOFF_BLOCKITEM_H
```

- [ ] **Step 2: Create BlockItem implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockItem.h"
#include <QPainter>

namespace Markoff {

BlockItem::BlockItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
}

void BlockItem::setFullySelected(bool selected)
{
    if (m_fullySelected == selected)
        return;
    m_fullySelected = selected;
    update();
}

void BlockItem::paintSelectionOverlay(QPainter *painter, const QRectF &rect)
{
    if (!m_fullySelected)
        return;
    painter->fillRect(rect, QColor(51, 153, 255, 80));
}

} // namespace Markoff
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `src/BlockItem.h` and `src/BlockItem.cpp` to the `add_library(markoff STATIC ...)` source list.

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build --target markoff`
Expected: Builds successfully.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff/src/BlockItem.h libs/markoff/src/BlockItem.cpp libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): add BlockItem base class with selection overlay"
```

---

## Task 3: StubBlockItem for Testing

**Files:**
- Create: `libs/markoff/src/StubBlockItem.h`
- Create: `libs/markoff/src/StubBlockItem.cpp`
- Modify: `libs/markoff/CMakeLists.txt`

This is a minimal concrete BlockItem used only for testing. It has a fixed bounding rect and stores a markdown string.

- [ ] **Step 1: Create StubBlockItem header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_STUBBLOCKITEM_H
#define MARKOFF_STUBBLOCKITEM_H

#include "BlockItem.h"

namespace Markoff {

/// Minimal BlockItem for testing. Fixed size, stores a markdown string.
class StubBlockItem : public BlockItem {
    Q_OBJECT
public:
    explicit StubBlockItem(const QString &markdown, qreal width, qreal height,
                           QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    QString toMarkdown() const override;

private:
    QString m_markdown;
    qreal m_width;
    qreal m_height;
};

} // namespace Markoff

#endif // MARKOFF_STUBBLOCKITEM_H
```

- [ ] **Step 2: Create StubBlockItem implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StubBlockItem.h"
#include <QPainter>

namespace Markoff {

StubBlockItem::StubBlockItem(const QString &markdown, qreal width, qreal height,
                             QGraphicsItem *parent)
    : BlockItem(parent)
    , m_markdown(markdown)
    , m_width(width)
    , m_height(height)
{
}

QRectF StubBlockItem::boundingRect() const
{
    return {0, 0, m_width, m_height};
}

void StubBlockItem::paint(QPainter *painter,
                          const QStyleOptionGraphicsItem * /*option*/,
                          QWidget * /*widget*/)
{
    painter->fillRect(boundingRect(), Qt::lightGray);
    paintSelectionOverlay(painter, boundingRect());
}

QString StubBlockItem::toMarkdown() const
{
    return m_markdown;
}

} // namespace Markoff
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `src/StubBlockItem.h` and `src/StubBlockItem.cpp` to the library sources.

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build --target markoff`
Expected: Builds successfully.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff/src/StubBlockItem.h libs/markoff/src/StubBlockItem.cpp libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): add StubBlockItem for selection testing"
```

---

## Task 4: MarkdownTextItem

**Files:**
- Create: `libs/markoff/src/MarkdownTextItem.h`
- Create: `libs/markoff/src/MarkdownTextItem.cpp`
- Modify: `libs/markoff/CMakeLists.txt`

This wraps our existing TextControl + QTextDocument into a QGraphicsObject that implements SelectableItem for text operations.

- [ ] **Step 1: Create MarkdownTextItem header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_MARKDOWNTEXTITEM_H
#define MARKOFF_MARKDOWNTEXTITEM_H

#include "SelectableItem.h"
#include <QGraphicsObject>

class QTextDocument;

namespace Markoff {

class TextControl;

/// Editable markdown text region in the graphics scene.
/// Wraps TextControl + QTextDocument. Implements SelectableItem
/// for text-level selection operations.
class MarkdownTextItem : public QGraphicsObject, public SelectableItem {
    Q_OBJECT
public:
    explicit MarkdownTextItem(QGraphicsItem *parent = nullptr);
    ~MarkdownTextItem() override;

    /// Set the raw markdown text content.
    void setPlainText(const QString &text);

    /// Access the underlying document and control.
    QTextDocument *document() const;
    TextControl *textControl() const { return m_control; }

    // QGraphicsItem
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    // SelectableItem
    QGraphicsItem *asGraphicsItem() override { return this; }
    bool isTextItem() const override { return true; }
    int hitTest(const QPointF &scenePos) const override;
    void setSelection(int anchorPos, int cursorPos) override;
    void clearSelection() override;
    QString selectedMarkdown() const override;
    QString allMarkdown() const override;
    QString toMarkdown() const override;

Q_SIGNALS:
    /// Emitted when Shift+Arrow can't move further.
    void cursorAtBoundary(Qt::Edge edge);

private:
    void updateGeometry();

    TextControl *m_control = nullptr;
    QTextDocument *m_document = nullptr;
    qreal m_width = 600.0;
};

} // namespace Markoff

#endif // MARKOFF_MARKDOWNTEXTITEM_H
```

- [ ] **Step 2: Create MarkdownTextItem implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MarkdownTextItem.h"
#include "TextControl.h"

#include <QPainter>
#include <QTextDocument>
#include <QTextCursor>
#include <QAbstractTextDocumentLayout>
#include <QStyleOptionGraphicsItem>

namespace Markoff {

MarkdownTextItem::MarkdownTextItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_document(new QTextDocument(this))
    , m_control(new TextControl(this))
{
    m_control->setDocument(m_document);
    m_document->setDocumentMargin(0);

    connect(m_document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, &MarkdownTextItem::updateGeometry);
    connect(m_control, &TextControl::updateRequest,
            this, [this]() { update(); });
}

MarkdownTextItem::~MarkdownTextItem() = default;

void MarkdownTextItem::setPlainText(const QString &text)
{
    m_document->setPlainText(text);
}

QTextDocument *MarkdownTextItem::document() const
{
    return m_document;
}

QRectF MarkdownTextItem::boundingRect() const
{
    return {0, 0, m_width, m_document->size().height()};
}

void MarkdownTextItem::paint(QPainter *painter,
                             const QStyleOptionGraphicsItem * /*option*/,
                             QWidget *widget)
{
    painter->save();
    m_control->drawContents(painter, boundingRect(), widget);
    painter->restore();
}

int MarkdownTextItem::hitTest(const QPointF &scenePos) const
{
    QPointF localPos = mapFromScene(scenePos);
    return m_document->documentLayout()->hitTest(localPos, Qt::FuzzyHit);
}

void MarkdownTextItem::setSelection(int anchorPos, int cursorPos)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(anchorPos);
    cursor.setPosition(cursorPos, QTextCursor::KeepAnchor);
    m_control->setTextCursor(cursor);
}

void MarkdownTextItem::clearSelection()
{
    QTextCursor cursor = m_control->textCursor();
    cursor.clearSelection();
    m_control->setTextCursor(cursor);
}

QString MarkdownTextItem::selectedMarkdown() const
{
    QTextCursor cursor = m_control->textCursor();
    return cursor.selectedText();
}

QString MarkdownTextItem::allMarkdown() const
{
    return m_document->toPlainText();
}

QString MarkdownTextItem::toMarkdown() const
{
    return allMarkdown();
}

void MarkdownTextItem::updateGeometry()
{
    prepareGeometryChange();
}

} // namespace Markoff
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `src/MarkdownTextItem.h` and `src/MarkdownTextItem.cpp` to the library sources.

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build --target markoff`
Expected: Builds successfully.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff/src/MarkdownTextItem.h libs/markoff/src/MarkdownTextItem.cpp libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): add MarkdownTextItem wrapping TextControl for scene"
```

---

## Task 5: SelectionManager Core + Test Scaffold

**Files:**
- Create: `libs/markoff/src/SelectionManager.h`
- Create: `libs/markoff/src/SelectionManager.cpp`
- Create: `libs/markoff/tests/tst_selection.cpp`
- Modify: `libs/markoff/CMakeLists.txt`
- Modify: `libs/markoff/tests/CMakeLists.txt`

- [ ] **Step 1: Create SelectionManager header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_SELECTIONMANAGER_H
#define MARKOFF_SELECTIONMANAGER_H

#include <QObject>
#include <QList>
#include <QPointF>

class QGraphicsSceneMouseEvent;
class QKeyEvent;
class QMimeData;

namespace Markoff {

class SelectableItem;

enum class SelectionMode { None, WithinItem, CrossBoundary };

class SelectionManager : public QObject {
    Q_OBJECT
public:
    explicit SelectionManager(QObject *parent = nullptr);

    /// Set the ordered list of items in the scene (top to bottom by Y).
    /// Called by the scene coordinator whenever items change.
    void setItems(const QList<SelectableItem *> &items);

    /// Mouse event handlers. Return true if the event was consumed
    /// (caller should NOT call the base class).
    bool handleMousePress(const QPointF &scenePos, Qt::KeyboardModifiers modifiers);
    bool handleMouseMove(const QPointF &scenePos);
    bool handleMouseRelease(const QPointF &scenePos);

    /// Key event handler for Ctrl+C, Ctrl+A, Escape.
    /// Returns true if consumed.
    bool handleKeyPress(QKeyEvent *event);

    /// Create MIME data from current selection for clipboard.
    QMimeData *createMimeData() const;

    /// Clear all selection state across all items.
    void clearSelection();

    /// Whether there is any active cross-boundary selection.
    bool hasSelection() const;

    /// Current mode (for testing).
    SelectionMode mode() const { return m_mode; }

private:
    void applySelection();
    SelectableItem *itemAt(const QPointF &scenePos) const;
    QString serializeAsMarkdown() const;

    SelectionMode m_mode = SelectionMode::None;

    SelectableItem *m_anchorItem = nullptr;
    int m_anchorTextPos = -1;

    SelectableItem *m_currentItem = nullptr;
    int m_currentTextPos = -1;

    QList<SelectableItem *> m_items;
};

} // namespace Markoff

#endif // MARKOFF_SELECTIONMANAGER_H
```

- [ ] **Step 2: Create minimal SelectionManager implementation (compiles, methods stubbed)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SelectionManager.h"
#include "SelectableItem.h"

#include <QKeyEvent>
#include <QMimeData>
#include <QGraphicsItem>

namespace Markoff {

SelectionManager::SelectionManager(QObject *parent)
    : QObject(parent)
{
}

void SelectionManager::setItems(const QList<SelectableItem *> &items)
{
    m_items = items;
}

bool SelectionManager::handleMousePress(const QPointF &scenePos,
                                        Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);
    m_anchorItem = itemAt(scenePos);
    if (!m_anchorItem) {
        m_mode = SelectionMode::None;
        return false;
    }
    m_anchorTextPos = m_anchorItem->hitTest(scenePos);
    m_mode = SelectionMode::WithinItem;
    return false; // let Qt handle the press normally
}

bool SelectionManager::handleMouseMove(const QPointF &scenePos)
{
    if (m_mode == SelectionMode::None)
        return false;

    SelectableItem *hoverItem = itemAt(scenePos);

    if (m_mode == SelectionMode::WithinItem) {
        if (!m_anchorItem)
            return false;
        // Check if we've left the anchor item
        QGraphicsItem *gi = m_anchorItem->asGraphicsItem();
        if (gi->boundingRect().contains(gi->mapFromScene(scenePos)))
            return false; // still within item, let Qt handle
        // Transition to CrossBoundary
        m_mode = SelectionMode::CrossBoundary;
    }

    // CrossBoundary mode
    m_currentItem = hoverItem ? hoverItem : m_anchorItem;
    m_currentTextPos = m_currentItem->hitTest(scenePos);
    applySelection();
    return true; // consumed — don't call base class
}

bool SelectionManager::handleMouseRelease(const QPointF &scenePos)
{
    Q_UNUSED(scenePos);
    if (m_mode == SelectionMode::CrossBoundary) {
        m_mode = SelectionMode::None;
        return true; // consumed
    }
    m_mode = SelectionMode::None;
    return false;
}

bool SelectionManager::handleKeyPress(QKeyEvent *event)
{
    Q_UNUSED(event);
    return false; // stubbed — implemented in Task 7
}

QMimeData *SelectionManager::createMimeData() const
{
    auto *data = new QMimeData;
    data->setText(serializeAsMarkdown());
    return data;
}

void SelectionManager::clearSelection()
{
    for (auto *item : m_items) {
        if (item->isTextItem())
            item->clearSelection();
        else
            item->setFullySelected(false);
    }
    m_anchorItem = nullptr;
    m_currentItem = nullptr;
    m_anchorTextPos = -1;
    m_currentTextPos = -1;
    m_mode = SelectionMode::None;
}

bool SelectionManager::hasSelection() const
{
    return m_mode == SelectionMode::CrossBoundary && m_anchorItem && m_currentItem;
}

void SelectionManager::applySelection()
{
    // Stubbed — implemented in Task 6
}

SelectableItem *SelectionManager::itemAt(const QPointF &scenePos) const
{
    for (auto *item : m_items) {
        QGraphicsItem *gi = item->asGraphicsItem();
        if (gi->sceneBoundingRect().contains(scenePos))
            return item;
    }
    // Fallback: find nearest item by Y
    if (m_items.isEmpty())
        return nullptr;
    if (scenePos.y() <= m_items.first()->asGraphicsItem()->sceneBoundingRect().top())
        return m_items.first();
    return m_items.last();
}

QString SelectionManager::serializeAsMarkdown() const
{
    return {}; // stubbed — implemented in Task 7
}

} // namespace Markoff
```

- [ ] **Step 3: Create test scaffold with first test (state machine transitions)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QGraphicsScene>
#include "SelectionManager.h"
#include "MarkdownTextItem.h"
#include "StubBlockItem.h"

using namespace Markoff;

class TestSelection : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testInitialState();
    void testPressTransitionsToWithinItem();
    void testMoveWithinItemStaysWithinItem();
    void testMoveCrossBoundaryTransitions();
    void testReleaseFromCrossBoundaryResetsToNone();
    void testReleaseFromWithinItemResetsToNone();
};

void TestSelection::testInitialState()
{
    SelectionManager mgr;
    QCOMPARE(mgr.mode(), SelectionMode::None);
    QVERIFY(!mgr.hasSelection());
}

void TestSelection::testPressTransitionsToWithinItem()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Hello world"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    SelectionManager mgr;
    mgr.setItems({text1});

    // Press inside the text item
    QPointF pressPos(10, 5);
    mgr.handleMousePress(pressPos, Qt::NoModifier);
    QCOMPARE(mgr.mode(), SelectionMode::WithinItem);
}

void TestSelection::testMoveWithinItemStaysWithinItem()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Hello world"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    SelectionManager mgr;
    mgr.setItems({text1});

    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    // Move within the same item's bounding rect
    bool consumed = mgr.handleMouseMove(QPointF(50, 5));
    QVERIFY(!consumed); // not consumed, Qt handles within-item
    QCOMPARE(mgr.mode(), SelectionMode::WithinItem);
}

void TestSelection::testMoveCrossBoundaryTransitions()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("First region"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *block = new StubBlockItem(QStringLiteral("| A |\n|---|\n| 1 |"), 600, 60);
    scene.addItem(block);
    block->setPos(0, 30); // below text1

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("Second region"));
    scene.addItem(text2);
    text2->setPos(0, 100);

    SelectionManager mgr;
    mgr.setItems({text1, block, text2});

    // Press in text1
    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    QCOMPARE(mgr.mode(), SelectionMode::WithinItem);

    // Move into the block item (below text1's bounding rect)
    bool consumed = mgr.handleMouseMove(QPointF(10, 50));
    QVERIFY(consumed);
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);
}

void TestSelection::testReleaseFromCrossBoundaryResetsToNone()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Hello"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("World"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, text2});

    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 35)); // cross into text2
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);

    bool consumed = mgr.handleMouseRelease(QPointF(10, 35));
    QVERIFY(consumed);
    QCOMPARE(mgr.mode(), SelectionMode::None);
}

void TestSelection::testReleaseFromWithinItemResetsToNone()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Hello"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    SelectionManager mgr;
    mgr.setItems({text1});

    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    bool consumed = mgr.handleMouseRelease(QPointF(50, 5));
    QVERIFY(!consumed); // Qt handles within-item release
    QCOMPARE(mgr.mode(), SelectionMode::None);
}

QTEST_MAIN(TestSelection)
#include "tst_selection.moc"
```

- [ ] **Step 4: Update tests/CMakeLists.txt**

Append after the existing test executables:

```cmake
add_executable(tst_markoff_selection tst_selection.cpp)
add_test(NAME tst_markoff_selection COMMAND tst_markoff_selection)
target_link_libraries(tst_markoff_selection PRIVATE Qt6::Test markoff)
target_include_directories(tst_markoff_selection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 5: Add SelectionManager to library CMakeLists.txt**

Add `src/SelectionManager.h` and `src/SelectionManager.cpp` to the `add_library(markoff STATIC ...)` source list.

- [ ] **Step 6: Build and run tests**

Run: `cmake --build build --target tst_markoff_selection && cd build && ctest -R tst_markoff_selection --output-on-failure`
Expected: All 6 tests pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff/src/SelectionManager.h libs/markoff/src/SelectionManager.cpp \
       libs/markoff/tests/tst_selection.cpp libs/markoff/tests/CMakeLists.txt \
       libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): add SelectionManager core with state machine tests"
```

---

## Task 6: applySelection Algorithm

**Files:**
- Modify: `libs/markoff/src/SelectionManager.cpp`
- Modify: `libs/markoff/tests/tst_selection.cpp`

- [ ] **Step 1: Write failing tests for applySelection behavior**

Add these test methods to the `TestSelection` class declaration and implementation:

```cpp
// Add to private Q_SLOTS:
    void testApplySelectionForward();
    void testApplySelectionBackward();
    void testApplySelectionMiddleItemFullySelected();
    void testApplySelectionClearsOutOfRange();
    void testApplySelectionDragReversal();

// Implementations:

void TestSelection::testApplySelectionForward()
{
    // Drag from text1 into text2 — text1 partial (anchor to end),
    // text2 partial (start to cursor)
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("AAABBB"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("CCCDDD"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, text2});

    // Press at char position 3 in text1 ("BBB" starts)
    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    // Force cross-boundary by moving into text2
    mgr.handleMouseMove(QPointF(10, 35));

    // text1 should have a selection from its anchor to the end
    QTextCursor c1 = text1->textControl()->textCursor();
    QVERIFY(c1.hasSelection());

    // text2 should have a selection from start to some position
    QTextCursor c2 = text2->textControl()->textCursor();
    QVERIFY(c2.hasSelection());
    QCOMPARE(c2.anchor(), 0); // starts from beginning
}

void TestSelection::testApplySelectionBackward()
{
    // Drag from text2 back up into text1
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("AAABBB"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("CCCDDD"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, text2});

    // Press in text2
    mgr.handleMousePress(QPointF(10, 35), Qt::NoModifier);
    // Drag up into text1
    mgr.handleMouseMove(QPointF(10, 5));
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);

    // text2 (anchor) should select from anchor to start (backward)
    QTextCursor c2 = text2->textControl()->textCursor();
    QVERIFY(c2.hasSelection());

    // text1 (current) should select from end to cursor position
    QTextCursor c1 = text1->textControl()->textCursor();
    QVERIFY(c1.hasSelection());
}

void TestSelection::testApplySelectionMiddleItemFullySelected()
{
    // text1 → block → text2: block in the middle should be fully selected
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("First"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *block = new StubBlockItem(QStringLiteral("| A |\n|---|\n| 1 |"), 600, 60);
    scene.addItem(block);
    block->setPos(0, 30);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("Second"));
    scene.addItem(text2);
    text2->setPos(0, 100);

    SelectionManager mgr;
    mgr.setItems({text1, block, text2});

    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 105));
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);
    QVERIFY(block->isFullySelected());
}

void TestSelection::testApplySelectionClearsOutOfRange()
{
    // After selecting text1→block→text2, drag back so only text1→block
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("First"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *block = new StubBlockItem(QStringLiteral("| A |"), 600, 60);
    scene.addItem(block);
    block->setPos(0, 30);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("Second"));
    scene.addItem(text2);
    text2->setPos(0, 100);

    SelectionManager mgr;
    mgr.setItems({text1, block, text2});

    // Drag all the way to text2
    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 105));

    // Now drag back to block (text2 should be cleared)
    mgr.handleMouseMove(QPointF(10, 50));
    QTextCursor c2 = text2->textControl()->textCursor();
    QVERIFY(!c2.hasSelection());
}

void TestSelection::testApplySelectionDragReversal()
{
    // Drag down past anchor, then drag back above anchor
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Above"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("Middle"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    auto *text3 = new MarkdownTextItem;
    text3->setPlainText(QStringLiteral("Below"));
    scene.addItem(text3);
    text3->setPos(0, 60);

    SelectionManager mgr;
    mgr.setItems({text1, text2, text3});

    // Press in text2, drag down to text3
    mgr.handleMousePress(QPointF(10, 35), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 65));
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);

    // Now reverse: drag up to text1
    mgr.handleMouseMove(QPointF(10, 5));
    // text3 should be cleared (no longer in range)
    QTextCursor c3 = text3->textControl()->textCursor();
    QVERIFY(!c3.hasSelection());
    // text1 should now have a selection
    QTextCursor c1 = text1->textControl()->textCursor();
    QVERIFY(c1.hasSelection());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target tst_markoff_selection && cd build && ctest -R tst_markoff_selection --output-on-failure`
Expected: The 5 new tests fail (applySelection is stubbed).

- [ ] **Step 3: Implement applySelection()**

Replace the stubbed `applySelection()` in `SelectionManager.cpp`:

```cpp
void SelectionManager::applySelection()
{
    if (!m_anchorItem || !m_currentItem)
        return;

    int anchorIdx = m_items.indexOf(m_anchorItem);
    int currentIdx = m_items.indexOf(m_currentItem);
    if (anchorIdx < 0 || currentIdx < 0)
        return;

    bool forward = currentIdx >= anchorIdx;
    int lo = qMin(anchorIdx, currentIdx);
    int hi = qMax(anchorIdx, currentIdx);

    for (int i = 0; i < m_items.size(); ++i) {
        SelectableItem *item = m_items[i];

        if (i < lo || i > hi) {
            // Outside selection range
            if (item->isTextItem())
                item->clearSelection();
            else
                item->setFullySelected(false);
        } else if (i == anchorIdx && i == currentIdx) {
            // Same item (drag returned to anchor)
            if (item->isTextItem())
                item->setSelection(m_anchorTextPos, m_currentTextPos);
            else
                item->setFullySelected(true);
        } else if (i == anchorIdx) {
            // Anchor item — partial from anchor to edge
            if (item->isTextItem()) {
                int end = item->allMarkdown().length();
                if (forward)
                    item->setSelection(m_anchorTextPos, end);
                else
                    item->setSelection(m_anchorTextPos, 0);
            } else {
                item->setFullySelected(true);
            }
        } else if (i == currentIdx) {
            // Current item — partial from edge to cursor
            if (item->isTextItem()) {
                int end = item->allMarkdown().length();
                if (forward)
                    item->setSelection(0, m_currentTextPos);
                else
                    item->setSelection(end, m_currentTextPos);
            } else {
                item->setFullySelected(true);
            }
        } else {
            // Middle item — fully selected
            if (item->isTextItem()) {
                int end = item->allMarkdown().length();
                item->setSelection(0, end);
            } else {
                item->setFullySelected(true);
            }
        }
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target tst_markoff_selection && cd build && ctest -R tst_markoff_selection --output-on-failure`
Expected: All 11 tests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff/src/SelectionManager.cpp libs/markoff/tests/tst_selection.cpp
git commit -m "feat(markoff): implement applySelection algorithm for cross-boundary selection"
```

---

## Task 7: Clipboard Serialization + Key Handling

**Files:**
- Modify: `libs/markoff/src/SelectionManager.cpp`
- Modify: `libs/markoff/tests/tst_selection.cpp`

- [ ] **Step 1: Write failing tests for clipboard and key handling**

Add to the `TestSelection` class:

```cpp
// Add to private Q_SLOTS:
    void testSerializeMarkdownForward();
    void testSerializeMarkdownWithBlockItem();
    void testCtrlCCreatesCorrectMimeData();
    void testCtrlAClearsAndSelectsAll();
    void testEscapeClearsSelection();
    void testClearSelectionResetsAll();

// Implementations:

void TestSelection::testSerializeMarkdownForward()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Hello World"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("Goodbye Moon"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, text2});

    // Press at start of text1, drag to end of text2
    mgr.handleMousePress(QPointF(0, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(200, 35));

    std::unique_ptr<QMimeData> data(mgr.createMimeData());
    QString md = data->text();
    // Should contain text from both items
    QVERIFY(md.contains(QStringLiteral("Hello")));
    QVERIFY(md.contains(QStringLiteral("Goodbye")));
}

void TestSelection::testSerializeMarkdownWithBlockItem()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Before table"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    QString tableMd = QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |");
    auto *block = new StubBlockItem(tableMd, 600, 60);
    scene.addItem(block);
    block->setPos(0, 30);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("After table"));
    scene.addItem(text2);
    text2->setPos(0, 100);

    SelectionManager mgr;
    mgr.setItems({text1, block, text2});

    // Select all three items
    mgr.handleMousePress(QPointF(0, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 105));

    std::unique_ptr<QMimeData> data(mgr.createMimeData());
    QString md = data->text();
    QVERIFY(md.contains(QStringLiteral("Before table")));
    QVERIFY(md.contains(QStringLiteral("| A | B |")));
    QVERIFY(md.contains(QStringLiteral("After table")));
}

void TestSelection::testCtrlCCreatesCorrectMimeData()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Copy me"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("And me"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, text2});

    mgr.handleMousePress(QPointF(0, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 35));
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);

    QKeyEvent copyEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    bool consumed = mgr.handleKeyPress(&copyEvent);
    QVERIFY(consumed);
}

void TestSelection::testCtrlAClearsAndSelectsAll()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("First"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *block = new StubBlockItem(QStringLiteral("| T |"), 600, 60);
    scene.addItem(block);
    block->setPos(0, 30);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("Second"));
    scene.addItem(text2);
    text2->setPos(0, 100);

    SelectionManager mgr;
    mgr.setItems({text1, block, text2});

    QKeyEvent selectAllEvent(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    bool consumed = mgr.handleKeyPress(&selectAllEvent);
    QVERIFY(consumed);
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);
    QVERIFY(block->isFullySelected());

    // Both text items should be fully selected
    QTextCursor c1 = text1->textControl()->textCursor();
    QVERIFY(c1.hasSelection());
    QCOMPARE(c1.selectedText(), QStringLiteral("First"));

    QTextCursor c2 = text2->textControl()->textCursor();
    QVERIFY(c2.hasSelection());
    QCOMPARE(c2.selectedText(), QStringLiteral("Second"));
}

void TestSelection::testEscapeClearsSelection()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Hello"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("World"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, text2});

    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 35));
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);

    QKeyEvent escEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    bool consumed = mgr.handleKeyPress(&escEvent);
    QVERIFY(consumed);
    QCOMPARE(mgr.mode(), SelectionMode::None);
    QVERIFY(!mgr.hasSelection());
}

void TestSelection::testClearSelectionResetsAll()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("Hello"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *block = new StubBlockItem(QStringLiteral("| A |"), 600, 60);
    scene.addItem(block);
    block->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, block});

    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 50));
    QVERIFY(block->isFullySelected());

    mgr.clearSelection();
    QVERIFY(!block->isFullySelected());
    QTextCursor c1 = text1->textControl()->textCursor();
    QVERIFY(!c1.hasSelection());
    QCOMPARE(mgr.mode(), SelectionMode::None);
}
```

- [ ] **Step 2: Run tests to verify the new ones fail**

Run: `cmake --build build --target tst_markoff_selection && cd build && ctest -R tst_markoff_selection --output-on-failure`
Expected: The 6 new tests fail (handleKeyPress and serializeAsMarkdown are stubbed).

- [ ] **Step 3: Implement serializeAsMarkdown()**

Replace the stub in `SelectionManager.cpp`:

```cpp
QString SelectionManager::serializeAsMarkdown() const
{
    if (!m_anchorItem || !m_currentItem)
        return {};

    int anchorIdx = m_items.indexOf(m_anchorItem);
    int currentIdx = m_items.indexOf(m_currentItem);
    if (anchorIdx < 0 || currentIdx < 0)
        return {};

    int lo = qMin(anchorIdx, currentIdx);
    int hi = qMax(anchorIdx, currentIdx);
    QString result;

    for (int i = lo; i <= hi; ++i) {
        SelectableItem *item = m_items[i];
        if (i == anchorIdx || i == currentIdx) {
            if (item->isTextItem())
                result += item->selectedMarkdown();
            else
                result += item->toMarkdown();
        } else {
            if (item->isTextItem())
                result += item->allMarkdown();
            else
                result += item->toMarkdown();
        }
    }
    return result;
}
```

- [ ] **Step 4: Implement handleKeyPress()**

Replace the stub in `SelectionManager.cpp`:

```cpp
bool SelectionManager::handleKeyPress(QKeyEvent *event)
{
    // Ctrl+A: select all
    if (event->key() == Qt::Key_A && event->modifiers() == Qt::ControlModifier) {
        if (m_items.isEmpty())
            return false;
        m_anchorItem = m_items.first();
        m_anchorTextPos = 0;
        m_currentItem = m_items.last();
        m_currentTextPos = m_currentItem->isTextItem()
            ? m_currentItem->allMarkdown().length()
            : -1;
        m_mode = SelectionMode::CrossBoundary;
        applySelection();
        return true;
    }

    // Escape: clear selection
    if (event->key() == Qt::Key_Escape && m_mode == SelectionMode::CrossBoundary) {
        clearSelection();
        return true;
    }

    // Ctrl+C: copy
    if (event->key() == Qt::Key_C && event->modifiers() == Qt::ControlModifier
        && m_mode == SelectionMode::CrossBoundary) {
        QMimeData *data = createMimeData();
        QGuiApplication::clipboard()->setMimeData(data);
        return true;
    }

    return false;
}
```

Add `#include <QGuiApplication>` and `#include <QClipboard>` to the includes in `SelectionManager.cpp`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target tst_markoff_selection && cd build && ctest -R tst_markoff_selection --output-on-failure`
Expected: All 17 tests pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff/src/SelectionManager.cpp libs/markoff/tests/tst_selection.cpp
git commit -m "feat(markoff): implement clipboard serialization and key handling for selection"
```

---

## Task 8: Shift+Click and hasSelection Robustness

**Files:**
- Modify: `libs/markoff/src/SelectionManager.h`
- Modify: `libs/markoff/src/SelectionManager.cpp`
- Modify: `libs/markoff/tests/tst_selection.cpp`

- [ ] **Step 1: Write failing tests for Shift+Click**

Add to the `TestSelection` class:

```cpp
// Add to private Q_SLOTS:
    void testShiftClickExtendsCrossBoundary();
    void testClickWithoutShiftClearsSelection();

// Implementations:

void TestSelection::testShiftClickExtendsCrossBoundary()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("First"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("Second"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, text2});

    // Normal press in text1 to set anchor
    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    mgr.handleMouseRelease(QPointF(10, 5));
    QCOMPARE(mgr.mode(), SelectionMode::None);

    // Shift+click in text2 should jump to CrossBoundary
    mgr.handleMousePress(QPointF(10, 35), Qt::ShiftModifier);
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);
    QVERIFY(mgr.hasSelection());

    // text1 should have selection (anchor item)
    QTextCursor c1 = text1->textControl()->textCursor();
    QVERIFY(c1.hasSelection());
}

void TestSelection::testClickWithoutShiftClearsSelection()
{
    QGraphicsScene scene;
    auto *text1 = new MarkdownTextItem;
    text1->setPlainText(QStringLiteral("First"));
    scene.addItem(text1);
    text1->setPos(0, 0);

    auto *text2 = new MarkdownTextItem;
    text2->setPlainText(QStringLiteral("Second"));
    scene.addItem(text2);
    text2->setPos(0, 30);

    SelectionManager mgr;
    mgr.setItems({text1, text2});

    // Create a cross-boundary selection
    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    mgr.handleMouseMove(QPointF(10, 35));
    QCOMPARE(mgr.mode(), SelectionMode::CrossBoundary);
    mgr.handleMouseRelease(QPointF(10, 35));

    // Click without Shift should clear everything
    mgr.handleMousePress(QPointF(10, 5), Qt::NoModifier);
    QTextCursor c2 = text2->textControl()->textCursor();
    QVERIFY(!c2.hasSelection());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target tst_markoff_selection && cd build && ctest -R tst_markoff_selection --output-on-failure`
Expected: `testShiftClickExtendsCrossBoundary` fails (Shift handling not implemented).

- [ ] **Step 3: Update handleMousePress for Shift+Click and click-to-clear**

Replace `handleMousePress` in `SelectionManager.cpp`:

```cpp
bool SelectionManager::handleMousePress(const QPointF &scenePos,
                                        Qt::KeyboardModifiers modifiers)
{
    SelectableItem *pressedItem = itemAt(scenePos);

    // Shift+Click: extend from existing anchor to new position
    if (modifiers & Qt::ShiftModifier && m_anchorItem) {
        m_currentItem = pressedItem ? pressedItem : m_anchorItem;
        m_currentTextPos = m_currentItem->hitTest(scenePos);
        m_mode = SelectionMode::CrossBoundary;
        applySelection();
        return true; // consumed — we handle this entirely
    }

    // Click without Shift: clear any existing cross-boundary selection
    if (m_mode == SelectionMode::CrossBoundary || hasSelection()) {
        clearSelection();
    }

    m_anchorItem = pressedItem;
    if (!m_anchorItem) {
        m_mode = SelectionMode::None;
        return false;
    }
    m_anchorTextPos = m_anchorItem->hitTest(scenePos);
    m_mode = SelectionMode::WithinItem;
    return false; // let Qt handle the press normally
}
```

Also update `hasSelection()` to be more robust — it should check if any item actually has a selection, not just the mode flag:

```cpp
bool SelectionManager::hasSelection() const
{
    if (m_mode == SelectionMode::CrossBoundary && m_anchorItem && m_currentItem)
        return true;
    // Also check if we have a stale anchor from a completed drag
    for (auto *item : m_items) {
        if (item->isTextItem() && !item->selectedMarkdown().isEmpty())
            return true;
        if (!item->isTextItem() && item->isFullySelected())
            return true;
    }
    return false;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target tst_markoff_selection && cd build && ctest -R tst_markoff_selection --output-on-failure`
Expected: All 19 tests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff/src/SelectionManager.h libs/markoff/src/SelectionManager.cpp \
       libs/markoff/tests/tst_selection.cpp
git commit -m "feat(markoff): add Shift+Click cross-boundary extension and click-to-clear"
```

---

## Task 9: Run All Markoff Tests + Final Verification

**Files:** None modified — verification only.

- [ ] **Step 1: Build and run all markoff tests**

Run: `cmake --build build && cd build && ctest -R tst_markoff --output-on-failure`
Expected: All markoff tests pass (document, renderer, table, selection).

- [ ] **Step 2: Verify no existing tests are broken**

Run: `cd build && ctest --output-on-failure`
Expected: All project tests pass.

- [ ] **Step 3: Verify clean compile with no warnings**

Run: `cmake --build build --target markoff 2>&1 | grep -i warning`
Expected: No new warnings from SelectionManager, BlockItem, MarkdownTextItem, or SelectableItem files.

- [ ] **Step 4: Final commit if any fixups were needed**

Only if Steps 1-3 revealed issues that required fixes.
