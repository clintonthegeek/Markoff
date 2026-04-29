// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveSelectionModel.h>

#include <QClipboard>
#include <QGuiApplication>

namespace Markoff::View::Qml {

LiveSelectionModel::LiveSelectionModel(QObject *parent) : QObject(parent) {}

bool LiveSelectionModel::hasSelection() const
{
    if (m_anchorBlock < 0) return false;
    if (m_anchorBlock != m_activeBlock) return true;
    return m_anchorOffset != m_activeOffset;
}

void LiveSelectionModel::begin(int block, int offset)
{
    m_anchorBlock = block;
    m_anchorOffset = offset;
    m_activeBlock = block;
    m_activeOffset = offset;
    Q_EMIT selectionChanged();
}

void LiveSelectionModel::extend(int block, int offset)
{
    if (m_anchorBlock < 0) {
        begin(block, offset);
        return;
    }
    if (m_activeBlock == block && m_activeOffset == offset) return;
    m_activeBlock = block;
    m_activeOffset = offset;
    Q_EMIT selectionChanged();
}

void LiveSelectionModel::clear()
{
    if (m_anchorBlock < 0) return;
    m_anchorBlock = m_anchorOffset = m_activeBlock = m_activeOffset = -1;
    Q_EMIT selectionChanged();
}

void LiveSelectionModel::normalized(int &fb, int &fo, int &lb, int &lo) const
{
    if (m_anchorBlock < m_activeBlock ||
        (m_anchorBlock == m_activeBlock && m_anchorOffset <= m_activeOffset)) {
        fb = m_anchorBlock; fo = m_anchorOffset;
        lb = m_activeBlock; lo = m_activeOffset;
    } else {
        fb = m_activeBlock; fo = m_activeOffset;
        lb = m_anchorBlock; lo = m_anchorOffset;
    }
}

QPoint LiveSelectionModel::rangeForBlock(int blockIndex) const
{
    if (!hasSelection()) return QPoint(-1, -1);
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    if (blockIndex < fb || blockIndex > lb) return QPoint(-1, -1);
    if (fb == lb) return QPoint(fo, lo);
    if (blockIndex == fb) return QPoint(fo, INT32_MAX);
    if (blockIndex == lb) return QPoint(0, lo);
    return QPoint(0, INT32_MAX);
}

QString LiveSelectionModel::collectSelectedText(const QStringList &blockTexts) const
{
    if (!hasSelection()) return {};
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    if (fb < 0 || lb >= blockTexts.size()) return {};
    if (fb == lb) {
        return blockTexts.at(fb).mid(fo, lo - fo);
    }
    QString out;
    out += blockTexts.at(fb).mid(fo);
    out += QChar('\n');
    for (int i = fb + 1; i < lb; ++i) {
        out += blockTexts.at(i);
        out += QChar('\n');
    }
    out += blockTexts.at(lb).left(lo);
    return out;
}

void LiveSelectionModel::copySelectionToClipboard(const QStringList &blockTexts) const
{
    const QString txt = collectSelectedText(blockTexts);
    if (txt.isEmpty()) return;
    QGuiApplication::clipboard()->setText(txt);
}

}  // namespace Markoff::View::Qml
