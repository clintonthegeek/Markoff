// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveSpeculativeFenceController.h>

namespace Markoff::View::Qml {

LiveSpeculativeFenceController::LiveSpeculativeFenceController(QObject *parent)
    : QObject(parent)
{}

LiveBlockModel *LiveSpeculativeFenceController::model() const { return m_model; }

void LiveSpeculativeFenceController::setModel(LiveBlockModel *m)
{
    if (m_model == m) return;
    m_model = m;
    Q_EMIT modelChanged();
}

void LiveSpeculativeFenceController::onEditApplied(const Markoff::BlockAnchor &/*anchor*/,
                                                   int row,
                                                   const QString &postText)
{
    if (!m_model) return;
    if (row < 0 || row >= m_model->rowCount()) return;

    const QString currentKind = m_model->data(
        m_model->index(row, 0),
        m_model->roleForName("kind")).toString();

    if (currentKind != QStringLiteral("paragraph")) {
        // Not a paragraph — revert any stale speculation on this row.
        if (m_model->isSpeculative(row))
            m_model->revertSpeculativeKind(row);
        return;
    }

    if (isFenceOpener(postText)) {
        if (!m_model->isSpeculative(row))
            m_model->speculativelyChangeKind(row, QStringLiteral("code_block"));
    } else {
        // Fence no longer present — revert.
        if (m_model->isSpeculative(row))
            m_model->revertSpeculativeKind(row);
    }
}

bool LiveSpeculativeFenceController::isFenceOpener(const QString &text)
{
    // A fence opener: text starts with ``` or ~~~ (optionally followed by a lang tag).
    return text.startsWith(QStringLiteral("```"))
        || text.startsWith(QStringLiteral("~~~"));
}

}  // namespace Markoff::View::Qml
