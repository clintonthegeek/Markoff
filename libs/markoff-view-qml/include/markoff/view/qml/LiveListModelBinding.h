// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <memory>
#include <qqmlintegration.h>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/LiveSelectionView.h>

namespace Markoff { class Document; }

namespace Markoff::View::Qml {

/// Wires `EditorBackend::parseUpdatedAt` → `BlockWalker` → `AstBlockDiff` →
/// `LiveBlockModel`. Also clears `LiveSelectionModel` if any block touched by
/// the edit (i.e. anchor or active block disappears in the diff) is removed.
///
/// As of Stage C-2, `BlockWalker::walk` consumes the foundation's
/// `Markoff::Document::topLevelBlocks()` snapshot directly and runs
/// synchronously on the main thread. The previous off-thread dispatch
/// existed because BlockWalker did regex-based re-parsing; that's gone.
class LiveListModelBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorBackend *editorBackend
               READ editorBackend WRITE setEditorBackend NOTIFY editorBackendChanged)
    Q_PROPERTY(LiveBlockModel *model READ model CONSTANT)
    Q_PROPERTY(LiveSelectionView *selectionModel READ selectionModel CONSTANT)
    Q_PROPERTY(LiveProjectionLayer *projectionLayer READ projectionLayer CONSTANT)

public:
    explicit LiveListModelBinding(QObject *parent = nullptr);
    ~LiveListModelBinding() override;

    EditorBackend *editorBackend() const;
    void setEditorBackend(EditorBackend *eb);

    LiveBlockModel *model() const;
    LiveSelectionView *selectionModel() const;
    LiveProjectionLayer *projectionLayer() const;

    Q_INVOKABLE void notifyFocused(const Markoff::BlockAnchor &anchor, int cursorPos);
    Q_INVOKABLE void notifyFocusedCursorMoved(int cursorPos);
    Q_INVOKABLE bool isFocusRestoreTarget(const Markoff::BlockAnchor &anchor) const;
    Q_INVOKABLE void setRowComposing(int row, bool composing);

Q_SIGNALS:
    void editorBackendChanged();
    void focusRestoreRequested(const Markoff::BlockAnchor &anchor, int qtPos);

private:
    void onParseUpdatedAt(const Markoff::Document *parsed,
                          quint64 parseSequence,
                          const QList<Markoff::BlockAnchor> &blockAnchors);

    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::View::Qml
