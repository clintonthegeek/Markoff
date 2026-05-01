// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveClipboardController.h>
#include <QClipboard>
#include <QGuiApplication>
#include <markoff-foundation/MarkoffEdit.h>

namespace Markoff::View::Qml {

LiveClipboardController::LiveClipboardController(QObject *parent) : QObject(parent) {}

LiveSelectionView *LiveClipboardController::selectionModel() const { return m_selectionModel; }
void LiveClipboardController::setSelectionModel(LiveSelectionView *m)
{
    if (m_selectionModel == m) return;
    m_selectionModel = m;
    Q_EMIT selectionModelChanged();
}

LiveBlockModel *LiveClipboardController::blockModel() const { return m_blockModel; }
void LiveClipboardController::setBlockModel(LiveBlockModel *m)
{
    if (m_blockModel == m) return;
    m_blockModel = m;
    Q_EMIT blockModelChanged();
}

void LiveClipboardController::copy()
{
    if (!m_selectionModel || !m_selectionModel->hasSelection()) return;
    m_selectionModel->copySelectionToClipboard(collectBlockTexts());
}

void LiveClipboardController::cut()
{
    if (!m_selectionModel || !m_selectionModel->hasSelection()) return;
    // Copy first, then delete the selection range from the document.
    copy();
    auto *doc = m_selectionModel->document();
    if (!doc) return;
    const auto [start, end] = m_selectionModel->selectionByteRange();
    if (start == end) return;
    Markoff::MarkoffEdit ed;
    ed.oldStart = start;
    ed.oldEnd   = end;
    ed.newText  = QByteArray();
    doc->applyLocalEdit({ ed });
    m_selectionModel->clear();
}

void LiveClipboardController::paste()
{
    if (!m_selectionModel) return;
    auto *doc = m_selectionModel->document();
    if (!doc) return;
    const QString clipText = QGuiApplication::clipboard()->text();
    if (clipText.isEmpty()) return;
    const auto [start, end] = m_selectionModel->selectionByteRange();
    Markoff::MarkoffEdit ed;
    ed.oldStart = start;
    ed.oldEnd   = end;
    ed.newText  = clipText.toUtf8();
    doc->applyLocalEdit({ ed });
    if (m_selectionModel->hasSelection())
        m_selectionModel->clear();
}

QStringList LiveClipboardController::collectBlockTexts() const
{
    QStringList out;
    if (!m_blockModel) return out;
    const int count = m_blockModel->rowCount();
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        out.append(m_blockModel->data(
            m_blockModel->index(i, 0),
            LiveBlockModel::TextRole).toString());
    }
    return out;
}

}  // namespace Markoff::View::Qml
