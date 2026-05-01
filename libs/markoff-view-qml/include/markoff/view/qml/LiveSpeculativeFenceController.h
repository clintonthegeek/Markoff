// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <qqmlintegration.h>
#include <markoff-foundation/BlockAnchor.h>
#include <markoff/view/qml/LiveBlockModel.h>

namespace Markoff::View::Qml {

/// Watches LiveEditBinding::editApplied signals and speculatively changes
/// a paragraph block's kind to code_block when a ``` or ~~~ fence opener is
/// typed at the start of the block text. The speculation is reverted or
/// confirmed when the next parse arrives (LiveBlockModel::applyOps clears
/// all speculative state, and the Equal-op path sets the parser-confirmed kind).
class LiveSpeculativeFenceController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::View::Qml::LiveBlockModel *model
               READ model WRITE setModel NOTIFY modelChanged)

public:
    explicit LiveSpeculativeFenceController(QObject *parent = nullptr);

    LiveBlockModel *model() const;
    void setModel(LiveBlockModel *m);

    /// Called by each text-bearing delegate after an edit is applied.
    /// anchor    — the block's anchor
    /// row       — the block's current row index in the model
    /// postText  — the block's post-edit text
    Q_INVOKABLE void onEditApplied(const Markoff::BlockAnchor &anchor,
                                   int row,
                                   const QString &postText);

Q_SIGNALS:
    void modelChanged();

private:
    static bool isFenceOpener(const QString &text);

    LiveBlockModel *m_model = nullptr;
};

}  // namespace Markoff::View::Qml
