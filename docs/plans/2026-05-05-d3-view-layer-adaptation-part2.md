# D3 — View-layer adaptation — Implementation Plan (Part 2 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Read Part 1 first:** `docs/plans/2026-05-05-d3-view-layer-adaptation.md` (Tasks 1–16, Phases 1–4).

**Spec:** `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md`

---

## Phase 5 — L7: ListItem and Blockquote

### Task 17: `ListItemDelegate.qml` + structural key handlers

**Files:**
- Create: `libs/markoff-live/qml/delegates/ListItemDelegate.qml`
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt` (add QML file)

- [ ] **Step 1: Write failing structural test**

In `tst_live_render_structural.cpp`:
```cpp
void list_item_enter_creates_new_list_item() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("- hello");
    QTRY_COMPARE(binding.model()->rowCount(), 1);
    QCOMPARE(binding.model()->data(binding.model()->index(0),
             Markoff::LiveRender::LiveBlockModel::KindRole).toString(),
             QStringLiteral("list-item"));

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, 0, 7, true, "- hello");

    QTRY_COMPARE(binding.model()->rowCount(), 2);
    QCOMPARE(binding.model()->data(binding.model()->index(1),
             Markoff::LiveRender::LiveBlockModel::KindRole).toString(),
             QStringLiteral("list-item"));
}
void list_item_enter_on_empty_exits_list() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("- ");
    QTRY_COMPARE(binding.model()->rowCount(), 1);

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, 0, 2, true, "- ");

    QTRY_COMPARE(binding.model()->data(binding.model()->index(0),
                 Markoff::LiveRender::LiveBlockModel::KindRole).toString(),
                 QStringLiteral("paragraph"));
}
void list_item_tab_indents() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("- item");
    QTRY_COMPARE(binding.model()->rowCount(), 1);

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Tab, Qt::NoModifier, 0, 3, true, "- item");

    QTRY_VERIFY(doc.blockAttrs(doc.iterateBlocks()[0]).contains("indentLevel"));
    QCOMPARE(std::get<int>(doc.blockAttrs(doc.iterateBlocks()[0]).value("indentLevel")), 1);
}
```

- [ ] **Step 2: Run to verify fail**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 && \
ctest --test-dir build-dev -R tst_live_render_structural --output-on-failure
```
Expected: new tests fail (no ListItem handlers registered).

- [ ] **Step 3: Add ListItem handlers to `LiveStructuralKeyHandler.cpp`**

In `tryHandle`, add a block for `kind == BlockKind::ListItem`:
```cpp
if (kind == BlockKind::ListItem) {
    const auto blockIds = d->document->iterateBlocks();
    if (blockIndex >= static_cast<int>(blockIds.size())) return false;
    const BlockId id = blockIds[blockIndex];
    const auto attrs = d->document->blockAttrs(id);
    const int indentLevel = attrs.contains(Markoff::AttrNames::IndentLevel)
        ? std::get<int>(attrs.value(Markoff::AttrNames::IndentLevel)) : 0;
    const QString markerStyle = attrs.contains(Markoff::AttrNames::MarkerStyle)
        ? std::get<QString>(attrs.value(Markoff::AttrNames::MarkerStyle)) : QStringLiteral("-");

    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !selectionEmpty)
        return false;  // Let TextEdit handle selection replacement

    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // Determine if block is "empty" (text is just the marker prefix)
        const QString text = d->model->recordAt(blockIndex).text;
        const bool isEmpty = text.trimmed() == markerStyle
                          || text.trimmed() == markerStyle + QStringLiteral(" ");

        if (isEmpty && indentLevel > 0) {
            // Outdent
            auto t = d->document->d2UndoLog().beginTransaction();
            d->document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                         indentLevel - 1, t);
            t.commit();
            d->cursorState->requestTextCaretAtRow(blockIndex, qtPos);
            return true;
        }
        if (isEmpty && indentLevel == 0) {
            // Exit list: change to Paragraph
            Cmd::changeKind(*d->document, id, Markoff::BlockKind::Paragraph, {}, {});
            d->cursorState->requestTextCaretAtRow(blockIndex, 0);
            return true;
        }
        // Non-empty: insert new ListItem after current
        auto t = d->document->d2UndoLog().beginTransaction();
        const BlockId newId = d->document->d2InsertBlock(id, Markoff::BlockKind::ListItem, t);
        d->document->d2SetBlockAttr(newId, Markoff::AttrNames::MarkerStyle, markerStyle, t);
        d->document->d2SetBlockAttr(newId, Markoff::AttrNames::IndentLevel, indentLevel, t);
        t.commit();
        d->cursorState->requestTextCaretAtAnchor(newId, 0);
        return true;
    }

    if (key == Qt::Key_Backspace && qtPos == 0) {
        if (indentLevel > 0) {
            auto t = d->document->d2UndoLog().beginTransaction();
            d->document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                         indentLevel - 1, t);
            t.commit();
            d->cursorState->requestTextCaretAtRow(blockIndex, 0);
            return true;
        }
        return backspaceMerge(blockIndex, qtPos, text);
    }

    if (key == Qt::Key_Delete && qtPos == text.length())
        return deleteMerge(blockIndex, text);

    if (key == Qt::Key_Tab) {
        auto t = d->document->d2UndoLog().beginTransaction();
        d->document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                     std::min(indentLevel + 1, 6), t);
        t.commit();
        d->cursorState->requestTextCaretAtRow(blockIndex, qtPos);
        return true;
    }

    if (key == Qt::Key_Tab && (modifiers & Qt::ShiftModifier)) {
        if (indentLevel > 0) {
            auto t = d->document->d2UndoLog().beginTransaction();
            d->document->d2SetBlockAttr(id, Markoff::AttrNames::IndentLevel,
                                         indentLevel - 1, t);
            t.commit();
        }
        d->cursorState->requestTextCaretAtRow(blockIndex, qtPos);
        return true;
    }
}
```

Note: `backspaceMerge` and `deleteMerge` are existing helper methods in `LiveStructuralKeyHandler`; reuse them.

- [ ] **Step 4: Create `ListItemDelegate.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null
    readonly property var selectionView: liveBinding ? liveBinding.selectionView : null

    // Marker prefix stripped for editing; marker shown as chrome
    readonly property string markerStyle: {
        const a = model.blockAttrs
        return a ? (a["markerStyle"] || "-") : "-"
    }
    readonly property int indentLevel: {
        const a = model.blockAttrs
        return a ? (a["indentLevel"] || 0) : 0
    }
    readonly property bool isChecked: {
        const a = model.blockAttrs
        return a ? (a["checked"] || false) : false
    }

    // Strip leading marker prefix from text for the editing region
    readonly property string editText: {
        const t = model.text
        const re = /^(\s{0,3})([-*+]|\d+[.)]) /
        const m = t.match(re)
        return m ? t.slice(m[0].length) : t
    }

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
        text: root.editText
    }

    Row {
        id: contentRow
        anchors.fill: parent
        leftPadding: 8 + root.indentLevel * 16
        spacing: 4

        // Marker glyph column
        Item {
            width: 20
            height: parent.height

            CheckBox {
                visible: root.markerStyle === "[ ]" || root.markerStyle === "[x]"
                anchors.centerIn: parent
                checked: root.isChecked
                onToggled: {
                    const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
                    if (handler) handler.toggleListItemChecked(model.blockAnchor, checked)
                }
            }

            Text {
                visible: root.markerStyle !== "[ ]" && root.markerStyle !== "[x]"
                anchors.centerIn: parent
                text: root.markerStyle
                color: palette.mid
                font.pixelSize: 14
            }
        }

        TextEdit {
            id: edit
            width: parent.width - parent.leftPadding - 24
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
                const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
                    edit.cursorPosition, edit.selectionStart === edit.selectionEnd, model.text)
                event.accepted = handled
            }

            onTextChanged: {
                const cs = root.liveBinding ? root.liveBinding.cursorState : null
                if (cs && cs.focusedAnchorRow === root.modelIndex && cs.focusedQtPos >= 0)
                    cursorPosition = cs.focusedQtPos
            }

            Connections {
                target: root.selectionView
                function onSelectionChanged() {
                    const sv = root.selectionView
                    if (!sv) { edit.deselect(); return }
                    const r = sv.rangeForBlock(model.index)
                    if (!r || r.x < 0) { edit.deselect(); return }
                    edit.select(r.x, Math.min(r.y, edit.length))
                }
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
    }

    function positionAt(x, y) {
        return edit.positionAt(x - contentRow.leftPadding - 24, y)
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

- [ ] **Step 5: Run tests**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 && \
ctest --test-dir build-dev -R tst_live_render_structural --output-on-failure
```
Expected: all pass including new list-item tests.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/qml/delegates/ListItemDelegate.qml \
        libs/markoff-live/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live/CMakeLists.txt
git commit -m "feat(live-render): ListItemDelegate + Enter/Backspace/Tab/indent structural handlers

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 18: `BlockquoteDelegate.qml` + structural key handlers

**Files:**
- Create: `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml`
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

```cpp
void blockquote_enter_creates_new_blockquote() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("> quote text");
    QTRY_COMPARE(binding.model()->rowCount(), 1);

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, 0,
        binding.model()->recordAt(0).text.length(), true,
        binding.model()->recordAt(0).text);

    QTRY_COMPARE(binding.model()->rowCount(), 2);
    QCOMPARE(binding.model()->data(binding.model()->index(1),
             Markoff::LiveRender::LiveBlockModel::KindRole).toString(),
             QStringLiteral("blockquote"));
}
void blockquote_enter_on_empty_exits() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("> ");
    QTRY_COMPARE(binding.model()->rowCount(), 1);

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, 0, 2, true, "> ");

    QTRY_COMPARE(binding.model()->data(binding.model()->index(0),
                 Markoff::LiveRender::LiveBlockModel::KindRole).toString(),
                 QStringLiteral("paragraph"));
}
```

- [ ] **Step 2: Add Blockquote handlers to `tryHandle`**

```cpp
if (kind == BlockKind::Blockquote) {
    const auto blockIds = d->document->iterateBlocks();
    if (blockIndex >= static_cast<int>(blockIds.size())) return false;
    const BlockId id = blockIds[blockIndex];

    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        const QString text = d->model->recordAt(blockIndex).text;
        const bool isEmpty = text.trimmed() == QStringLiteral(">")
                          || text.trimmed() == QStringLiteral("> ");
        if (isEmpty) {
            // Exit blockquote
            Cmd::changeKind(*d->document, id, Markoff::BlockKind::Paragraph, {}, {});
            d->cursorState->requestTextCaretAtRow(blockIndex, 0);
            return true;
        }
        // Insert new blockquote block
        auto t = d->document->d2UndoLog().beginTransaction();
        const BlockId newId = d->document->d2InsertBlock(id, Markoff::BlockKind::Blockquote, t);
        t.commit();
        d->cursorState->requestTextCaretAtAnchor(newId, 0);
        return true;
    }

    if (key == Qt::Key_Backspace && qtPos == 0)
        return backspaceMerge(blockIndex, qtPos, text);
    if (key == Qt::Key_Delete && qtPos == text.length())
        return deleteMerge(blockIndex, text);
}
```

- [ ] **Step 3: Create `BlockquoteDelegate.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null
    readonly property var selectionView: liveBinding ? liveBinding.selectionView : null

    // Strip leading "> " for the editing region
    readonly property string editText: {
        const t = model.text
        if (t.startsWith("> ")) return t.slice(2)
        if (t === ">") return ""
        return t
    }

    // Left accent border
    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 3
        color: palette.highlight
        opacity: 0.6
    }

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
        text: root.editText
    }

    TextEdit {
        id: edit
        anchors { fill: parent; leftMargin: 12; rightMargin: 8 }
        topPadding: 4; bottomPadding: 4
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        font.pixelSize: 14
        color: palette.text
        font.italic: true
        selectByMouse: true
        persistentSelection: true

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (!handler) { event.accepted = false; return }
            const k = event.key
            if (k !== Qt.Key_Return && k !== Qt.Key_Enter
                    && k !== Qt.Key_Backspace && k !== Qt.Key_Delete)
                return
            const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
                edit.cursorPosition, edit.selectionStart === edit.selectionEnd, model.text)
            event.accepted = handled
        }

        onTextChanged: {
            const cs = root.liveBinding ? root.liveBinding.cursorState : null
            if (cs && cs.focusedAnchorRow === root.modelIndex && cs.focusedQtPos >= 0)
                cursorPosition = cs.focusedQtPos
        }

        Connections {
            target: root.selectionView
            function onSelectionChanged() {
                const sv = root.selectionView
                if (!sv) { edit.deselect(); return }
                const r = sv.rangeForBlock(model.index)
                if (!r || r.x < 0) { edit.deselect(); return }
                edit.select(r.x, Math.min(r.y, edit.length))
            }
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

    function positionAt(x, y) { return edit.positionAt(x - 12, y) }
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

- [ ] **Step 4: Run tests**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 && \
ctest --test-dir build-dev -R tst_live_render_structural --output-on-failure
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/qml/delegates/BlockquoteDelegate.qml \
        libs/markoff-live/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live/CMakeLists.txt
git commit -m "feat(live-render): BlockquoteDelegate + Enter/exit structural handlers

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 19: Update `LiveView.qml` — add DelegateChoices for new kinds

**Files:**
- Modify: `libs/markoff-live/qml/LiveView.qml`

- [ ] **Step 1: Add DelegateChoices**

In `LiveView.qml`, update the `DelegateChooser` block:
```qml
delegate: DelegateChooser {
    role: "kind"
    DelegateChoice { roleValue: "paragraph";  delegate: ParagraphDelegate  {} }
    DelegateChoice { roleValue: "heading";    delegate: HeadingDelegate    {} }
    DelegateChoice { roleValue: "code-block"; delegate: CodeBlockDelegate  {} }
    DelegateChoice { roleValue: "hr";         delegate: HorizontalRuleDelegate {} }
    DelegateChoice { roleValue: "image";      delegate: ImageDelegate      {} }
    DelegateChoice { roleValue: "list-item";  delegate: ListItemDelegate   {} }
    DelegateChoice { roleValue: "blockquote"; delegate: BlockquoteDelegate {} }
    DelegateChoice { roleValue: "math";       delegate: MathDelegate       {} }
}
```

Also expose `binding` property on the ListView so delegates can access it:
(This should already exist as `required property var binding` — verify it's present. If not, the delegates use `ListView.view.binding` which requires the ListView to expose it.)

- [ ] **Step 2: Build**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```
Expected: clean build. MathDelegate doesn't exist yet (Task 21) — add a stub QML or leave the DelegateChoice for math commented out until Task 21.

Stub `MathDelegate.qml` for now:
```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
Item {
    property int modelIndex: index
    readonly property string blockText: model.text
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: 40
    function positionAt(x, y) { return -1 }
    function focusEditAt(qtPos) {}
    Text { anchors.centerIn: parent; text: "Math: " + model.text; color: "gray" }
}
```

- [ ] **Step 3: Build and test app**

```bash
cmake --build build-dev --target markoff-live-app -j 8 && \
./build-dev/bin/markoff-live-app
```
Load a markdown file with various block types. Verify list items, blockquotes render with their delegates.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/qml/LiveView.qml \
        libs/markoff-live/qml/delegates/MathDelegate.qml \
        libs/markoff-live/CMakeLists.txt
git commit -m "feat(live-render): LiveView DelegateChoices for ListItem/Blockquote/Math

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Phase 6 — L8: Math + BlockInternalEdit

### Task 20: Wire `jkqtmathtext` into the build

**Files:**
- Modify: `CMakeLists.txt` (root)
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Add jkqtmathtext to root CMakeLists**

In `CMakeLists.txt`, before `add_subdirectory(libs/markoff-live)`, add:
```cmake
# jkqtmathtext — LaTeX math rendering. Used by markoff-live-render for Math blocks.
if(NOT TARGET jkqtmathtext)
    add_subdirectory(libs/jkqtmathtext)
endif()
```

- [ ] **Step 2: Link jkqtmathtext in markoff-live-render**

In `libs/markoff-live/CMakeLists.txt`, add to `target_link_libraries`:
```cmake
target_link_libraries(markoff_live_render PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2
    markoff_core
    jkqtmathtext
)
```

- [ ] **Step 3: Build**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && \
cmake --build build-dev --target markoff_live_render -j 8
```
Expected: jkqtmathtext compiles and links cleanly.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt libs/markoff-live/CMakeLists.txt
git commit -m "build: wire jkqtmathtext into markoff-live-render for Math block rendering

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 21: `MathRenderer` C++ class + singleton QML registration

**Files:**
- Create: `libs/markoff-live/src/MathRenderer.h`
- Create: `libs/markoff-live/src/MathRenderer.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Create `MathRenderer.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

/// Wraps JKQTMathText to render LaTeX source to a QPixmap.
/// Registered as a QML singleton so delegates can call render() from JS.
class MARKOFF_LIVE_RENDER_EXPORT MathRenderer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit MathRenderer(QObject *parent = nullptr);

    /// Render `latex` to a pixmap at the given point size.
    /// Returns a null pixmap if parsing fails.
    Q_INVOKABLE QPixmap render(const QString &latex, bool displayMode,
                               qreal pointSize = 14.0) const;

    /// Returns true if the last render() call succeeded.
    Q_INVOKABLE bool lastRenderOk() const { return m_lastOk; }

private:
    mutable bool m_lastOk = false;
};

}  // namespace Markoff::LiveRender
```

- [ ] **Step 2: Create `MathRenderer.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MathRenderer.h"
#include <jkqtmathtext.h>
#include <QPainter>
#include <QImage>

namespace Markoff::LiveRender {

MathRenderer::MathRenderer(QObject *parent) : QObject(parent) {}

QPixmap MathRenderer::render(const QString &latex, bool displayMode, qreal pointSize) const
{
    JKQTMathText mt;
    mt.useXITS();
    mt.setFontSize(pointSize);

    // Strip outer delimiters for JKQTMathText (it doesn't want $$ or \[...\])
    QString inner = latex;
    if (displayMode) {
        if (inner.startsWith(QStringLiteral("$$")) && inner.endsWith(QStringLiteral("$$")))
            inner = inner.mid(2, inner.size() - 4);
        else if (inner.startsWith(QStringLiteral("\\[")) && inner.endsWith(QStringLiteral("\\]")))
            inner = inner.mid(2, inner.size() - 4);
    } else {
        if (inner.startsWith(u'$') && inner.endsWith(u'$'))
            inner = inner.mid(1, inner.size() - 2);
        else if (inner.startsWith(QStringLiteral("\\(")) && inner.endsWith(QStringLiteral("\\)")))
            inner = inner.mid(2, inner.size() - 4);
    }

    m_lastOk = mt.parse(inner);
    if (!m_lastOk)
        return QPixmap{};

    // Render to image
    QSizeF size = mt.getSize(QPainter{});
    if (size.isEmpty()) { m_lastOk = false; return QPixmap{}; }

    const int w = static_cast<int>(std::ceil(size.width())) + 4;
    const int h = static_cast<int>(std::ceil(size.height())) + 4;
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    mt.draw(p, Qt::AlignLeft | Qt::AlignVCenter, QRectF(2, 2, w - 4, h - 4), false);
    p.end();
    return QPixmap::fromImage(img);
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add `src/MathRenderer.h` and `src/MathRenderer.cpp` to the SOURCES list of `qt_add_qml_module(markoff_live_render ...)`.

- [ ] **Step 4: Build**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```
Expected: clean build with MathRenderer compiled.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/MathRenderer.h \
        libs/markoff-live/src/MathRenderer.cpp \
        libs/markoff-live/CMakeLists.txt
git commit -m "feat(live-render): MathRenderer wraps jkqtmathtext for QML math rendering

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 22: Full `MathDelegate.qml` — render + edit modes

**Files:**
- Modify: `libs/markoff-live/qml/delegates/MathDelegate.qml` (replace stub)

- [ ] **Step 1: Replace stub with full delegate**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: isEditing ? editArea.implicitHeight : renderArea.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null

    readonly property bool isSelected:
        liveBinding && liveBinding.cursorState
        && liveBinding.cursorState.cursorKind === "BlockSelected"
        && liveBinding.cursorState.focusedAnchorRow === root.modelIndex

    readonly property bool isEditing:
        liveBinding && liveBinding.cursorState
        && liveBinding.cursorState.cursorKind === "BlockInternalEdit"
        && liveBinding.cursorState.focusedAnchorRow === root.modelIndex

    readonly property bool displayMode: {
        const a = model.blockAttrs
        return a ? (a["displayMode"] || false) : false
    }

    // ---- Render mode ----
    Column {
        id: renderArea
        width: parent.width
        visible: !root.isEditing
        padding: 8

        Image {
            id: mathImage
            anchors.horizontalCenter: parent.horizontalCenter
            fillMode: Image.Pad
            // Render on text change (debounced via Timer)
        }

        Text {
            visible: mathImage.status !== Image.Ready || mathImage.width === 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.blockText
            font.family: "monospace"
            color: palette.mid
            font.pixelSize: 12
        }
    }

    // ---- Edit mode ----
    Column {
        id: editArea
        visible: root.isEditing
        width: parent.width
        padding: 8
        spacing: 4

        LiveEditBinding {
            id: editBinding
            binding: root.liveBinding
            modelIndex: root.modelIndex
            textDocument: latexEdit.textDocument
            composing: latexEdit.inputMethodComposing
            text: model.text
        }

        TextEdit {
            id: latexEdit
            width: parent.width - 16
            height: Math.max(60, implicitHeight)
            readOnly: false
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            font.family: "monospace"
            font.pixelSize: 13
            color: palette.text

            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    root.exitEditMode()
                    event.accepted = true
                }
            }

            onTextChanged: renderTimer.restart()
        }

        // Live preview below the editor
        Image {
            id: previewImage
            width: parent.width - 16
            fillMode: Image.Pad
        }
    }

    // Debounced render timer (250ms)
    Timer {
        id: renderTimer
        interval: 250
        onTriggered: {
            const src = root.isEditing ? latexEdit.text : model.text
            if (src === "") return
            const px = MathRenderer.render(src, root.displayMode, 14)
            if (root.isEditing) {
                previewImage.source = ""  // force refresh
                // QML Image can't take a QPixmap directly; use a provider
                // For simplicity, show raw text when pixmap path isn't set up.
                // Full pixmap→image-provider wiring is a polish task.
            } else {
                // Same: show placeholder until image provider is wired
            }
        }
    }

    // Focus ring
    Rectangle {
        visible: root.isSelected || root.isEditing
        anchors.fill: parent
        anchors.margins: -2
        border.color: palette.highlight
        border.width: 2
        color: "transparent"
        radius: 3
    }

    function positionAt(x, y) { return -1 }

    function focusEditAt(qtPos) {
        root.forceActiveFocus()
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    function enterEditMode() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockInternalEdit",
                              block: model.blockAnchor, mode: "editing-latex" })
        Qt.callLater(function() { latexEdit.forceActiveFocus() })
    }

    function exitEditMode() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (root.isSelected && event.key === Qt.Key_F2) {
            root.enterEditMode(); event.accepted = true; return
        }
        if (root.isSelected
                && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (handler) {
                event.accepted = handler.tryHandle(event.key, event.modifiers,
                    root.modelIndex, -1, true, model.text)
            }
            return
        }
        event.accepted = false
    }

    MouseArea {
        anchors.fill: parent
        onDoubleClicked: if (root.isSelected) root.enterEditMode()
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex)
            Qt.callLater(function() { focusEditAt(-1) })
    }
}
```

Note on pixmap rendering: QML `Image` cannot directly accept a `QPixmap`. The full integration requires either a `QQuickImageProvider` or saving the pixmap to a temp file/in-memory buffer. For D3, the render timer fires but the image display is deferred to a polish pass. The text fallback ensures the raw LaTeX source is always visible. The `MathRenderer.render()` call is the important structural piece — wiring `QPixmap → QML Image` via an image provider is a small follow-up.

- [ ] **Step 2: Build**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```
Expected: clean build.

- [ ] **Step 3: Manual test**

```bash
./build-dev/bin/markoff-live-app
```
Type `$x^2$` in a paragraph block. Verify it transitions to a Math block (kind-transition detection). Press F2 → verify edit mode activates. Press Escape → verify returns to BlockSelected.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/qml/delegates/MathDelegate.qml
git commit -m "feat(live-render): MathDelegate with render/edit modes (F2 entry, Escape exit)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 23: `LiveStructuralKeyHandler` — F2/Escape BlockInternalEdit dispatch

The `tryHandle` method needs to handle `F2` on BlockSelected blocks (enter internal-edit mode) and `Escape` on `BlockInternalEdit` blocks (return to BlockSelected). These are kind-agnostic transitions.

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp`
- Modify: `libs/markoff-live/include/markoff/live-render/LiveStructuralKeyHandler.h`

- [ ] **Step 1: Write test**

```cpp
void f2_on_math_block_transitions_to_internal_edit() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    // Manually insert a Math block
    auto t = doc.d2UndoLog().beginTransaction();
    auto ids = doc.iterateBlocks(); // empty
    // Use loadFromMarkdown with math syntax instead
    doc.loadFromMarkdown("$x^2$");
    QTRY_COMPARE(binding.model()->rowCount(), 1);
    QTRY_COMPARE(binding.model()->data(binding.model()->index(0),
                 Markoff::LiveRender::LiveBlockModel::KindRole).toString(),
                 QStringLiteral("math"));

    // Set cursor to BlockSelected
    Markoff::LiveRender::BlockSelected sel;
    sel.block = binding.model()->recordAt(0).blockAnchor;
    binding.cursorState()->request(sel);
    QCOMPARE(binding.cursorState()->cursorKind(), QStringLiteral("BlockSelected"));

    bool handled = binding.structuralKeyHandler()->tryHandle(
        Qt::Key_F2, Qt::NoModifier, 0, -1, true, "$x^2$");
    QVERIFY(handled);
    QCOMPARE(binding.cursorState()->cursorKind(), QStringLiteral("BlockInternalEdit"));
}
```

- [ ] **Step 2: Add `tryHandleBlockInternalEditTransition` helper in `tryHandle`**

At the top of `tryHandle` (before kind-specific dispatch), add:

```cpp
// F2: enter BlockInternalEdit on kinds that support it
if (key == Qt::Key_F2) {
    const auto *desc = d->registry->find(kind);
    if (desc && desc->supportedCursorVariants.contains(QStringLiteral("BlockInternalEdit"))
             && !desc->internalEditModes.isEmpty()) {
        // Transition cursor to BlockInternalEdit with the first supported mode
        BlockInternalEdit bie;
        bie.block = blockIds[blockIndex];
        bie.mode  = desc->internalEditModes.first();
        d->cursorState->request(bie);
        return true;
    }
    return false;
}

// Escape: exit BlockInternalEdit → BlockSelected
if (key == Qt::Key_Escape) {
    if (d->cursorState->cursorKind() == QStringLiteral("BlockInternalEdit")) {
        BlockSelected sel;
        sel.block = blockIds[blockIndex];
        d->cursorState->request(sel);
        return true;
    }
    return false;
}
```

- [ ] **Step 3: Run test**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 && \
ctest --test-dir build-dev -R tst_live_render_structural --output-on-failure
```
Expected: all pass including F2 transition test.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/src/LiveStructuralKeyHandler.cpp
git commit -m "feat(live-render): F2 enters BlockInternalEdit; Escape exits to BlockSelected

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Phase 7 — Per-block undo UI

### Task 24: `LiveContextMenu.qml` + expose via `LiveListModelBinding`

**Files:**
- Create: `libs/markoff-live/qml/LiveContextMenu.qml`
- Modify: `libs/markoff-live/include/markoff/live-render/LiveListModelBinding.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp`
- Modify: `libs/markoff-live/qml/LiveView.qml`
- Modify: `libs/markoff-live/CMakeLists.txt`

- [ ] **Step 1: Create `LiveContextMenu.qml`**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

/// Right-click context menu for block delegates.
/// Usage: LiveContextMenu { id: ctxMenu; binding: liveBinding }
/// Call: ctxMenu.showForBlock(blockAnchor, mapToGlobal(...))
Menu {
    id: root

    required property var binding  // LiveListModelBinding *

    property var _anchor: null

    function showForBlock(blockAnchor, globalPos) {
        root._anchor = blockAnchor
        root.popup(globalPos.x, globalPos.y)
    }

    MenuItem {
        text: qsTr("Undo in this block")
        enabled: root._anchor !== null
                 && root.binding !== null
                 && root.binding.document !== null
                 && root.binding.document.canUndoForBlock(root._anchor)
        onTriggered: {
            if (root.binding && root.binding.document && root._anchor)
                root.binding.document.undoForBlock(root._anchor)
        }
    }

    MenuSeparator {}

    MenuItem {
        text: qsTr("Undo")
        enabled: root.binding !== null && root.binding.document !== null
        onTriggered: if (root.binding && root.binding.document) root.binding.document.undoD2()
    }
    MenuItem {
        text: qsTr("Redo")
        enabled: root.binding !== null && root.binding.document !== null
        onTriggered: if (root.binding && root.binding.document) root.binding.document.redoD2()
    }

    MenuSeparator {}

    MenuItem {
        text: qsTr("Copy")
        onTriggered: {
            if (!root.binding) return
            const sv = root.binding.selectionView
            if (sv && sv.hasSelection) {
                // Gather text from binding.model
                const texts = []
                const m = root.binding.model
                for (let i = 0; i < m.rowCount(); ++i) {
                    const item = m.data(m.index(i, 0),
                                        Markoff.LiveRender.LiveBlockModel.TextRole)
                    texts.push(item || "")
                }
                sv.copyToClipboard(texts)
            }
        }
    }
}
```

- [ ] **Step 2: Update `LiveView.qml` — right-click shows context menu**

Add to `LiveView.qml`:
```qml
// Lazy context menu (instantiated on first right-click)
property var _contextMenu: null
function _getContextMenu() {
    if (!_contextMenu) {
        const comp = Qt.createComponent("LiveContextMenu.qml")
        _contextMenu = comp.createObject(root, { binding: root.binding })
    }
    return _contextMenu
}
```

In the `MouseArea`, add:
```qml
acceptedButtons: Qt.LeftButton | Qt.RightButton

onClicked: (mouse) => {
    if (mouse.button === Qt.RightButton) {
        const r = root.hit(mouse.x, mouse.y)
        if (r && r.blockIndex >= 0) {
            const item = root.itemAtIndex(r.blockIndex)
            const anchor = item ? item.model.blockAnchor : null
            if (anchor) {
                const menu = root._getContextMenu()
                menu.showForBlock(anchor, mapToGlobal(mouse.x, mouse.y))
            }
        }
    }
}
```

- [ ] **Step 3: Add `LiveContextMenu.qml` to CMakeLists QML_FILES**

In `libs/markoff-live/CMakeLists.txt`, add `qml/LiveContextMenu.qml` to QML_FILES.

- [ ] **Step 4: Build**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```
Expected: clean build.

- [ ] **Step 5: Write test for `canUndoForBlock` wiring**

In `tst_live_render_context_menu.cpp` (new file):
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Cmd/D2.h>

using namespace Markoff::LiveRender;

class TstContextMenu : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void canUndo_false_before_any_edit() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        const auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        Markoff::BlockAnchor anchor = ids[0];
        QVERIFY(!doc.canUndoForBlock(anchor));
    }

    void canUndo_true_after_edit() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        const auto ids = doc.iterateBlocks();
        const Markoff::BlockAnchor anchor = ids[0];

        Cmd::insertCharacter(doc, anchor, 5, "!");
        QVERIFY(doc.canUndoForBlock(anchor));
    }

    void undoForBlock_reverts_edit() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        const auto ids = doc.iterateBlocks();
        const Markoff::BlockAnchor anchor = ids[0];

        Cmd::insertCharacter(doc, anchor, 5, "!");
        QCOMPARE(QString::fromUtf8(doc.blockText(ids[0])), QStringLiteral("hello!"));

        doc.undoForBlock(anchor);
        QCOMPARE(QString::fromUtf8(doc.blockText(ids[0])), QStringLiteral("hello"));
    }
};
QTEST_MAIN(TstContextMenu)
#include "tst_live_render_context_menu.moc"
```

Add to `libs/markoff-live/tests/CMakeLists.txt`:
```cmake
qt_add_executable(tst_live_render_context_menu
    tst_live_render_context_menu.cpp
)
target_link_libraries(tst_live_render_context_menu PRIVATE
    Qt6::Core Qt6::Test markoff_live_render markoff_core)
add_test(NAME tst_live_render_context_menu COMMAND tst_live_render_context_menu)
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build-dev --target tst_live_render_context_menu -j 8 && \
ctest --test-dir build-dev -R tst_live_render_context_menu --output-on-failure
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/qml/LiveContextMenu.qml \
        libs/markoff-live/qml/LiveView.qml \
        libs/markoff-live/tests/tst_live_render_context_menu.cpp \
        libs/markoff-live/tests/CMakeLists.txt \
        libs/markoff-live/CMakeLists.txt
git commit -m "feat(live-render): right-click LiveContextMenu with per-block undo; tst_live_render_context_menu

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Phase 8 — Integration and dogfood

### Task 25: Full build + all tests pass

- [ ] **Step 1: Full build**

```bash
cmake --build build-dev -j 8
```
Expected: zero errors, zero warnings about missing symbols.

- [ ] **Step 2: Run complete test suite**

```bash
ctest --test-dir build-dev -j 8 --output-on-failure \
      -E "tst_realistic|tst_benchmark"
```
Expected: all tests pass. Note any failures.

- [ ] **Step 3: Fix any failures**

If any test fails, diagnose and fix before proceeding. Do not mark this task complete until all tests pass.

- [ ] **Step 4: Run slow tests**

```bash
ctest --test-dir build-dev -j 4 --output-on-failure
```
Expected: 141+ tests pass (D2 had 141; D3 adds ~15–20 new tests).

- [ ] **Step 5: Commit if any fixes were needed**

```bash
git add -u
git commit -m "fix: integration fixes from full test run

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 26: Dogfood pass

This is the manual user-facing test. Run the test app and exercise all D3 features.

- [ ] **Step 1: Launch app**

```bash
./build-dev/bin/markoff-live-app
```

- [ ] **Step 2: Kind-transition detection**

- Type `# ` in an empty paragraph → verify it becomes a Heading with correct font size
- Type `## ` → verify heading level 2
- Type `- ` → verify ListItem with bullet
- Type `> ` → verify Blockquote with left border
- Type `$$` → verify Math block appears
- Type `$x$` → verify Math (inline) block
- Type ` ``` ` → verify CodeBlock
- Type `---` → verify HorizontalRule

- [ ] **Step 3: L7 structural keys**

- In a list-item, press Enter at end → new list-item created
- In an empty list-item, press Enter → exits to paragraph
- In a list-item, press Tab → indent increases
- In a list-item, press Shift+Tab → indent decreases
- In a blockquote, press Enter at end → new blockquote
- In an empty blockquote, press Enter → exits to paragraph

- [ ] **Step 4: L6 full delegates**

- In a heading, press Ctrl+Shift+1 → level 1
- In a heading, press Ctrl+Shift+0 → demotes to paragraph
- In a code block, press Tab → 4 spaces inserted
- Click language tag on code block → editable field appears → type language → Enter → syntax highlighting updates
- Click on HorizontalRule → BlockSelected ring shows
- Press Delete on selected HR → HR removed
- In an image block (load file with `![alt](url)`) → press Enter → alt-edit mode

- [ ] **Step 5: L8 Math**

- In a Math block, press F2 → edit mode with TextEdit
- Edit LaTeX → preview updates after 250ms
- Press Escape → returns to render mode

- [ ] **Step 6: Per-block undo UI**

- Type several chars in a block
- Right-click the block → context menu shows
- "Undo in this block" is enabled → click it → last edit reverted
- "Undo in this block" in a fresh block is grayed out

- [ ] **Step 7: Cursor delivery**

- Type in a paragraph, split with Enter → cursor in new block
- Backspace-merge two blocks → cursor at merge point, correct position
- Delete-merge → cursor at correct position

- [ ] **Step 8: Record any bugs found**

For each bug: describe reproduction steps, expected vs actual behavior. Fix or record in `docs/d-arc/d-arc-status.md` as a known issue.

- [ ] **Step 9: Final commit**

After any dogfood fixes:
```bash
cmake --build build-dev -j 8 && \
ctest --test-dir build-dev -j 8 --output-on-failure -E "tst_realistic|tst_benchmark"
```
Expected: all pass.

```bash
git add -u
git commit -m "fix(live-render): dogfood fixes — D3 complete

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 27: Update status board

**Files:**
- Modify: `docs/d-arc/d-arc-status.md`
- Modify: `docs/d-arc/2026-05-04-d-arc-roadmap.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update `d-arc-status.md`**

- Change D3 status: `spec-approved` → `complete`
- Change D4 status line from `stubbed` to `stubbed` (no change, but note D3 complete)
- Add entries to the recent-changes log
- Update TL;DR to point at D4 as the next phase

- [ ] **Step 2: Update `d-arc-roadmap.md`**

Change D3 row status badge from `🟢 active` to `✅ done`.

- [ ] **Step 3: Update `CLAUDE.md` banner**

Change the banner to reflect D3 complete and D4 as next:
```
> **D3 complete — D4 next.** D4 (parser scope reduction) is the next active phase. Spec at `docs/specs/2026-05-04-d4-parser-scope-reduction-STUB.md`. Needs substantive design before plan derivation.
```

- [ ] **Step 4: Commit**

```bash
git add docs/d-arc/d-arc-status.md \
        docs/d-arc/2026-05-04-d-arc-roadmap.md \
        CLAUDE.md
git commit -m "docs: D3 complete — update status board, roadmap, CLAUDE.md banner

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Open questions to resolve during implementation

| # | Question | When |
|---|---|---|
| Q3 | Math rendering: `QPixmap → QML Image` requires a `QQuickImageProvider`. Wire one up or use a temp-file path. | Task 22 polish |
| Q4 | `BlockKindDescriptor::serializer` callback signature vs `BlockSerializerRegistry::serialize` — confirm the `text` parameter type is `QByteArray` (not `QString`). Check `BlockSerializer` typedef in `BlockSerializer.h`. | Task 7 |
| Q5 | `Cmd::insertCharacter` — verify exact name in `Cmd/Edit.h` or equivalent. If absent, use `d2ApplyBufferEdit` directly in context menu test. | Task 24 |
| Q6 | `UndoLog::hasEntryForBlock` — verify whether the public UndoLog API exposes this, or whether the internal scan must go through `MarkoffDocument::undoForBlock(BlockId)` as a dry-run. May need to expose an `undoLog()` const accessor. | Task 3 |
