// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
#include <markoff/canvas/EditorWidget.h>

#include <QVBoxLayout>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/TextUnits.h>

#include <markoff/canvas/View.h>

namespace coords = Markoff::TextUnits;

namespace Markoff::Canvas {

namespace {

// Flat-line model (matches live/source/styled — see contract-v2 spec §3):
// each document block contributes 1 + (internal '\n' count) visual flat
// lines; blocks are separated by exactly one line boundary; `column` is
// the 1-based UTF-16 position within the line. Canvas's own coordinate
// space is per-block UTF-8 bytes (C4), so both directions round-trip
// through `Markoff::TextUnits::byteToQtPos`/`qtPosToByte` against the
// block's own `blockText()` — never a layout string (P1.2 note: the
// layout substitutes U+2028 for '\n', which would throw the QChar count
// off by one per preceding newline).

Markoff::CursorPos toCursorPos(const Markoff::MarkoffDocument *doc,
                                BlockId caretBlock, int caretByteOffset)
{
    int line = 1;
    for (const BlockId id : doc->iterateBlocks()) {
        const QByteArray bytes = doc->blockText(id);
        if (id == caretBlock) {
            const qsizetype byteOff = qBound(qsizetype(0), qsizetype(caretByteOffset), bytes.size());
            const qsizetype qtPos = coords::byteToQtPos(bytes, byteOff);
            const QString text = QString::fromUtf8(bytes);
            const QStringView before = QStringView(text).left(qtPos);
            const int innerLine = int(before.count(QLatin1Char('\n')));
            const qsizetype lastNl = before.lastIndexOf(QLatin1Char('\n'));
            const qsizetype lineStart = (lastNl < 0) ? 0 : lastNl + 1;
            return { line + innerLine, int(qtPos - lineStart) + 1 };
        }
        line += 1 + int(QString::fromUtf8(bytes).count(QLatin1Char('\n')));
    }
    return {1, 1};
}

// Inverse: flat (line, column) -> (BlockId, block-relative byte offset).
// Clamps — never a no-op: a past-the-end line lands at the end of the last
// block; an over-long column lands at the line's end. Empty document
// returns a null BlockId / byte 0 (View::setCaretPosition's empty-cache
// guard handles that).
std::pair<BlockId, int> fromCursorPos(const Markoff::MarkoffDocument *doc,
                                       Markoff::CursorPos p)
{
    const auto ids = doc->iterateBlocks();
    if (ids.empty())
        return { BlockId{}, 0 };

    int line = 1;
    for (const BlockId id : ids) {
        const QByteArray bytes = doc->blockText(id);
        const QString text = QString::fromUtf8(bytes);
        const int span = 1 + int(text.count(QLatin1Char('\n')));
        if (p.line < line + span) {
            qsizetype qtPos = 0;
            for (int i = 0; i < p.line - line; ++i)
                qtPos = text.indexOf(QLatin1Char('\n'), qtPos) + 1;
            const qsizetype nl = text.indexOf(QLatin1Char('\n'), qtPos);
            const qsizetype lineEnd = (nl < 0) ? text.size() : nl;
            qtPos = qMin(qtPos + qMax(0, p.column - 1), lineEnd);
            return { id, int(coords::qtPosToByte(bytes, qtPos)) };
        }
        line += span;
    }
    return { ids.back(), int(doc->blockText(ids.back()).size()) };
}

}  // namespace

EditorWidget::EditorWidget(QWidget *parent)
    : Markoff::MarkdownView(parent)
{
    m_view = new View(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view);
}

EditorWidget::~EditorWidget()
{
    // Session owned by the document (live's pattern, spec §4.1): when the
    // document outlives this widget (the typical case), destroy the
    // session we created so it doesn't accumulate a ghost session across
    // leaf swaps. When the document was destroyed first, m_docDestroyedCon
    // already nulled the base's document pointer and m_session — nothing
    // to tear down here.
    if (m_session && document())
        document()->destroySession(m_session);
}

void EditorWidget::setDocument(Markoff::MarkoffDocument *doc)
{
    if (document() == doc)
        return;

    if (m_session && document())
        document()->destroySession(m_session);
    m_session = nullptr;

    QObject::disconnect(m_docDestroyedCon);
    m_docDestroyedCon = {};

    // Base store + documentChanged, then the composed View: View::setDocument
    // resets its caret to {} and re-realizes from the new document
    // synchronously (no queued/deferred step exists anywhere in this leaf,
    // C2) — by the time this function returns, that reset has already
    // happened and nothing further will touch the caret on its own. A
    // caller's setCursorPosition() issued right after this call therefore
    // sticks; see the attach-window note on the class doc.
    Markoff::MarkdownView::setDocument(doc);
    // FALSIFY (throwaway, plan P3.1): defer the caret-resetting half of
    // setDocument's teardown past this call's return. A caller's
    // setCursorPosition() issued right after setDocument() now loses the
    // race against this queued reset once the event loop turns over.
    QMetaObject::invokeMethod(this, [this, doc] { m_view->setDocument(doc); },
                               Qt::QueuedConnection);

    if (doc) {
        m_docDestroyedCon = QObject::connect(doc, &QObject::destroyed, this, [this] {
            // Retire-on-destroy (INVARIANTS #3): don't dereference a freed
            // document from any base accessor. Qualified call avoids
            // re-entering our own setDocument (which would touch the dying
            // document's session).
            m_session = nullptr;
            Markoff::MarkdownView::setDocument(nullptr);
        });
        m_session = doc->createSession();
    }
}

Markoff::CursorPos EditorWidget::cursorPosition() const
{
    auto *doc = document();
    if (!doc || !m_view)
        return {1, 1};
    return toCursorPos(doc, m_view->caretBlock(), m_view->caretByteOffset());
}

void EditorWidget::setCursorPosition(Markoff::CursorPos pos)
{
    auto *doc = document();
    if (!doc || !m_view)
        return;
    const auto [block, byteOffset] = fromCursorPos(doc, pos);
    m_view->setCaretPosition(block, byteOffset);
}

View *EditorWidget::view() const noexcept
{
    return m_view;
}

}  // namespace Markoff::Canvas
