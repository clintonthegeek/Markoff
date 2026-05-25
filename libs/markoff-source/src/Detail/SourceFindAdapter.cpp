// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceFindAdapter.h"

#include <QColor>
#include <QPlainTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/source/Editor.h>

namespace Markoff::Source::Detail {

SourceFindAdapter::SourceFindAdapter(Editor *editor, QObject *parent)
    : QObject(parent), m_editor(editor)
{}

SourceFindAdapter::~SourceFindAdapter() = default;

void SourceFindAdapter::attach(Markoff::FindController *fc)
{
    if (m_controller == fc) return;
    if (m_controller) detach();
    m_controller = fc;
    if (!m_controller) return;
    connect(m_controller, &Markoff::FindController::matchesChanged,
            this, &SourceFindAdapter::onMatchesChanged);
    connect(m_controller, &Markoff::FindController::navigationRequested,
            this, &SourceFindAdapter::onNavigationRequested);
    onMatchesChanged();
}

void SourceFindAdapter::detach()
{
    if (!m_controller) return;
    disconnect(m_controller, nullptr, this, nullptr);
    m_controller = nullptr;
    m_highlights.clear();
    if (auto *pte = m_editor ? m_editor->plainTextEdit() : nullptr)
        pte->setExtraSelections({});
}

void SourceFindAdapter::onMatchesChanged()
{
    renderHighlights();
}

void SourceFindAdapter::onNavigationRequested(Markoff::FindController::Match m)
{
    auto *pte = m_editor ? m_editor->plainTextEdit() : nullptr;
    if (!pte) return;
    const int globalPos = globalCharPosFor(m);
    QTextCursor cur(pte->document());
    cur.setPosition(globalPos);
    pte->setTextCursor(cur);  // Does NOT call setFocus; focus stays where the user has it.
    pte->ensureCursorVisible();
}

void SourceFindAdapter::renderHighlights()
{
    m_highlights.clear();
    auto *pte = m_editor ? m_editor->plainTextEdit() : nullptr;
    if (!pte || !m_controller) {
        if (pte) pte->setExtraSelections({});
        return;
    }
    QTextCharFormat hlFmt;
    hlFmt.setBackground(QColor(255, 235, 59, 120));  // soft yellow, theme follow-up
    auto *doc = m_editor->document();
    for (const auto &m : m_controller->matches()) {
        const int globalPos = globalCharPosFor(m);
        const QByteArray blockText = doc ? doc->blockText(m.block) : QByteArray();
        const int blockCharLen = QString::fromUtf8(
            blockText.mid(static_cast<int>(m.byteOffset),
                          static_cast<int>(m.byteLength))).size();
        QTextCursor cur(pte->document());
        cur.setPosition(globalPos);
        cur.setPosition(globalPos + blockCharLen, QTextCursor::KeepAnchor);
        QTextEdit::ExtraSelection sel;
        sel.cursor = cur;
        sel.format = hlFmt;
        m_highlights.append(sel);
    }
    pte->setExtraSelections(m_highlights);
}

int SourceFindAdapter::globalCharPosFor(Markoff::FindController::Match m) const
{
    auto *doc = m_editor ? m_editor->document() : nullptr;
    if (!doc) return 0;
    // Walk iterateBlocks() until we hit m.block, accumulating QChar lengths
    // plus the per-block separator. Source widget's flat text uses
    // interBlockSeparator() == "\n\n" per the buffer convention.
    int globalChar = 0;
    const auto ids = doc->iterateBlocks();
    for (const Markoff::BlockId id : ids) {
        const QByteArray btext = doc->blockText(id);
        if (id == m.block) {
            const QByteArray prefix = btext.left(static_cast<int>(m.byteOffset));
            return globalChar + QString::fromUtf8(prefix).size();
        }
        globalChar += QString::fromUtf8(btext).size();
        globalChar += 2;  // "\n\n" interBlockSeparator (D2 buffer convention)
    }
    return globalChar;
}

}  // namespace Markoff::Source::Detail
