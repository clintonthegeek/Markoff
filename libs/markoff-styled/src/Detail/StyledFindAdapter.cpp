// SPDX-License-Identifier: GPL-3.0-or-later
#include "StyledFindAdapter.h"

#include <QColor>
#include <QHash>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/styled/Editor.h>

#include "../BlockPositionWalk.h"

namespace Markoff::Styled::Detail {

namespace {

/// Visible-document span for a match within an already-walked entry.
/// Frame entries (tables) and desynced entries are unmappable: the
/// match's byte offsets index the block's raw source bytes, which a
/// compact QTextTable frame does not expose positionally.
StyledFindAdapter::MappedSpan spanWithinEntry(
    const WalkEntry &entry, Markoff::FindController::Match m)
{
    if (entry.isFrame || entry.qtBlocks.isEmpty()) return {};

    const QByteArray &buf = entry.text;
    // Byte offsets → UTF-16 offsets over the whole block buffer first
    // (multi-byte chars before the match shift QChar positions).
    const int charOffset =
        QString::fromUtf8(buf.left(static_cast<int>(m.byteOffset))).size();
    const int charLen = QString::fromUtf8(
        buf.mid(static_cast<int>(m.byteOffset),
                static_cast<int>(m.byteLength))).size();
    if (charLen <= 0) return {};

    // The buffer may span multiple top-level QTextBlocks (internal '\n',
    // e.g. code blocks). Each '\n' in the buffer corresponds to exactly
    // one QTextBlock separator position in the document.
    int remaining = charOffset;
    for (const QTextBlock &qblk : entry.qtBlocks) {
        if (!qblk.isValid()) return {};
        const int blockLen = static_cast<int>(qblk.text().size());
        if (remaining <= blockLen)
            return { qblk.position() + remaining, charLen };
        remaining -= blockLen + 1;  // +1: block separator ↔ buffer '\n'
    }
    return {};
}

}  // namespace

StyledFindAdapter::StyledFindAdapter(Editor *editor, QObject *parent)
    : QObject(parent), m_editor(editor)
{}

StyledFindAdapter::~StyledFindAdapter() = default;

void StyledFindAdapter::attach(Markoff::FindController *fc)
{
    if (m_controller == fc) return;
    if (m_controller) detach();
    m_controller = fc;
    if (!m_controller) return;
    connect(m_controller, &Markoff::FindController::matchesChanged,
            this, &StyledFindAdapter::onMatchesChanged);
    connect(m_controller, &Markoff::FindController::navigationRequested,
            this, &StyledFindAdapter::onNavigationRequested);
    // A model change may rebuild frames/blocks wholesale via the binding's
    // reverse path — re-derive highlight positions from the live document.
    // This connection is made AFTER the binding's (attach follows
    // setDocument), so FIFO delivery runs the reconciliation first.
    if (auto *doc = m_editor ? m_editor->document() : nullptr) {
        m_docChangedCon = connect(doc, &Markoff::MarkoffDocument::d2DocumentChanged,
                                  this, &StyledFindAdapter::onMatchesChanged);
    }
    onMatchesChanged();
}

void StyledFindAdapter::detach()
{
    if (!m_controller) return;
    disconnect(m_controller, nullptr, this, nullptr);
    m_controller = nullptr;
    if (m_docChangedCon) {
        QObject::disconnect(m_docChangedCon);
        m_docChangedCon = {};
    }
    m_highlights.clear();
    if (auto *te = m_editor ? m_editor->textEdit() : nullptr)
        te->setExtraSelections({});
}

void StyledFindAdapter::onMatchesChanged()
{
    renderHighlights();
}

void StyledFindAdapter::onNavigationRequested(Markoff::FindController::Match m)
{
    auto *te = m_editor ? m_editor->textEdit() : nullptr;
    if (!te) return;
    const MappedSpan span = mapMatch(m);
    if (span.start < 0) return;  // table-frame match: documented degradation
    QTextCursor cur(te->document());
    cur.setPosition(span.start);
    te->setTextCursor(cur);  // Does NOT call setFocus; focus stays where the user has it.
    te->ensureCursorVisible();
}

void StyledFindAdapter::renderHighlights()
{
    m_highlights.clear();
    auto *te = m_editor ? m_editor->textEdit() : nullptr;
    if (!te || !m_controller) {
        if (te) te->setExtraSelections({});
        return;
    }
    auto *doc = m_editor->document();
    if (!doc) {
        te->setExtraSelections({});
        return;
    }

    QTextCharFormat hlFmt;
    hlFmt.setBackground(QColor(255, 235, 59, 120));  // soft yellow, theme follow-up

    // One frame-aware walk per render; entries are consumed immediately and
    // never cached across model changes (stale QTextBlock handles).
    QHash<Markoff::BlockId, WalkEntry> entries;
    walkBlocks(doc, te->document(), [&entries](const WalkEntry &e) {
        entries.insert(e.blockId, e);
    });

    for (const auto &m : m_controller->matches()) {
        const auto it = entries.constFind(m.block);
        if (it == entries.constEnd()) continue;
        const MappedSpan span = spanWithinEntry(*it, m);
        if (span.start < 0) continue;  // table frame / desync: no highlight
        QTextCursor cur(te->document());
        cur.setPosition(span.start);
        cur.setPosition(span.start + span.length, QTextCursor::KeepAnchor);
        QTextEdit::ExtraSelection sel;
        sel.cursor = cur;
        sel.format = hlFmt;
        m_highlights.append(sel);
    }
    te->setExtraSelections(m_highlights);
}

StyledFindAdapter::MappedSpan
StyledFindAdapter::mapMatch(Markoff::FindController::Match m) const
{
    auto *te = m_editor ? m_editor->textEdit() : nullptr;
    auto *doc = m_editor ? m_editor->document() : nullptr;
    if (!te || !doc) return {};
    MappedSpan result;
    walkBlocks(doc, te->document(), [&result, &m](const WalkEntry &e) {
        if (e.blockId == m.block)
            result = spanWithinEntry(e, m);
    });
    return result;
}

}  // namespace Markoff::Styled::Detail
