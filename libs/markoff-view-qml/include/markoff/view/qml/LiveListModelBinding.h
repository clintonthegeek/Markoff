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
/// `BlockWalker::walk` is dispatched to `QThreadPool::globalInstance()` and
/// posted back to the binding's thread for the LCS-diff + model apply step.
/// See `docs/specs/2026-04-30-blockwalker-threading-decision.md` for the
/// rationale (option 1A) and the trigger conditions for promoting to a
/// foundation-side shared decomposition (option 1C).
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
