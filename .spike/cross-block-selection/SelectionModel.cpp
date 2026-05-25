#include "SelectionModel.h"

#include <QClipboard>
#include <QGuiApplication>

SelectionModel::SelectionModel(QObject *parent) : QObject(parent) {}

bool SelectionModel::hasSelection() const
{
    if (m_anchorBlock < 0) return false;
    if (m_anchorBlock != m_activeBlock) return true;
    return m_anchorOffset != m_activeOffset;
}

void SelectionModel::begin(int block, int offset)
{
    m_anchorBlock = block;
    m_anchorOffset = offset;
    m_activeBlock = block;
    m_activeOffset = offset;
    Q_EMIT selectionChanged();
}

void SelectionModel::extend(int block, int offset)
{
    if (m_anchorBlock < 0) {
        // No anchor yet — treat as begin().
        begin(block, offset);
        return;
    }
    if (m_activeBlock == block && m_activeOffset == offset) return;
    m_activeBlock = block;
    m_activeOffset = offset;
    Q_EMIT selectionChanged();
}

void SelectionModel::clear()
{
    if (m_anchorBlock < 0) return;
    m_anchorBlock = m_anchorOffset = m_activeBlock = m_activeOffset = -1;
    Q_EMIT selectionChanged();
}

void SelectionModel::normalized(int &fb, int &fo, int &lb, int &lo) const
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

QPoint SelectionModel::rangeForBlock(int blockIndex) const
{
    if (!hasSelection()) return QPoint(-1, -1);
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);

    if (blockIndex < fb || blockIndex > lb) return QPoint(-1, -1);

    if (fb == lb) {
        // Selection contained in single block.
        return QPoint(fo, lo);
    }
    if (blockIndex == fb) {
        // From fo to end-of-block. We don't know the block length here;
        // pass a sentinel "to end" via INT_MAX. The QML side calls
        // textEdit.select(fo, length); here we use a large value that
        // TextEdit clamps automatically.
        return QPoint(fo, INT32_MAX);
    }
    if (blockIndex == lb) {
        return QPoint(0, lo);
    }
    // Interior block: select all.
    return QPoint(0, INT32_MAX);
}

QString SelectionModel::collectSelectedText(const QStringList &blockTexts) const
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

void SelectionModel::copySelectionToClipboard(const QStringList &blockTexts) const
{
    const QString txt = collectSelectedText(blockTexts);
    if (txt.isEmpty()) return;
    QGuiApplication::clipboard()->setText(txt);
}
