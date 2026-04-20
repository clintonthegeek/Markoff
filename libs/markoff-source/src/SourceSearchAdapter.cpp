// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceSearchAdapter.h"

#include <QList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>

#include <qutepart/qutepart.h>

#include <markoff/source/SourceEditor.h>

namespace Markoff::Source {

SourceSearchAdapter::SourceSearchAdapter(SourceEditor *owner) : m_editor(owner) {}

int SourceSearchAdapter::cursorSourceOffset() const
{
    if (!m_editor || !m_editor->qutepart()) return 0;
    return m_editor->qutepart()->textCursor().position();
}

void SourceSearchAdapter::highlightMatches(QVector<Markoff::TextSpan> spans)
{
    if (!m_editor || !m_editor->qutepart()) return;
    auto *qp = m_editor->qutepart();
    QList<QTextEdit::ExtraSelection> sel;
    QTextCharFormat fmt;
    fmt.setBackground(Qt::yellow);
    for (const auto &s : spans) {
        QTextEdit::ExtraSelection e;
        e.format = fmt;
        e.cursor = QTextCursor(qp->document());
        e.cursor.setPosition(s.offset);
        e.cursor.setPosition(s.offset + s.length, QTextCursor::KeepAnchor);
        sel.append(e);
    }
    qp->setExtraSelections(sel);
}

void SourceSearchAdapter::clearMatchHighlight()
{
    if (m_editor && m_editor->qutepart()) {
        m_editor->qutepart()->setExtraSelections({});
    }
}

void SourceSearchAdapter::scrollMatchIntoView(Markoff::TextSpan span)
{
    if (!m_editor || !m_editor->qutepart()) return;
    auto *qp = m_editor->qutepart();
    QTextCursor c(qp->document());
    c.setPosition(span.offset);
    qp->setTextCursor(c);
    qp->ensureCursorVisible();
}

}  // namespace Markoff::Source
