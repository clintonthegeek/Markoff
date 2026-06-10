// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include <markoff/live/EditorWidget.h>

#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/live/LiveCursorState.h>

namespace Markoff::Live {

namespace {

// Flat-line model per contract-v2 spec §3 (normative): each model block
// contributes 1 + (internal '\n' count) visual flat lines; blocks are
// separated by exactly one line boundary; `column` is the 1-based UTF-16
// position within the line. This matches what the Styled/Source leaves
// report over their widgetFlatView-seeded QTextDocument
// (blockNumber()+1, positionInBlock()+1).
//
// Maps (model block row, intra-block UTF-16 qtPos) → CursorPos.
// Unknown block row → {1,1}.
Markoff::CursorPos toCursorPos(const Markoff::MarkoffDocument *doc,
                               int blockRow, int qtPos)
{
    int line = 1;
    const auto ids = doc->iterateBlocks();
    for (int row = 0; row < int(ids.size()); ++row) {
        const QString text =
            QString::fromUtf8(doc->blockText(ids[std::size_t(row)]));
        if (row == blockRow) {
            const int pos = qBound(0, qtPos, int(text.size()));
            const QStringView before = QStringView(text).left(pos);
            const int innerLine = int(before.count(QLatin1Char('\n')));
            const qsizetype lastNl = before.lastIndexOf(QLatin1Char('\n'));
            const int lineStart = (lastNl < 0) ? 0 : int(lastNl) + 1;
            return { line + innerLine, pos - lineStart + 1 };
        }
        line += 1 + int(text.count(QLatin1Char('\n')));
    }
    return {1, 1};
}

// Inverse: flat (line, column) → (model block row, intra-block qtPos).
// Clamps — never a no-op: a past-the-end line lands at the end of the
// last block; an over-long column lands at the line's end. Empty
// document → {0, 0} (the chokepoint rejects row 0 on an empty model).
std::pair<int, int> fromCursorPos(const Markoff::MarkoffDocument *doc,
                                  Markoff::CursorPos p)
{
    const auto ids = doc->iterateBlocks();
    if (ids.empty()) return {0, 0};
    int line = 1;
    for (int row = 0; row < int(ids.size()); ++row) {
        const QString text =
            QString::fromUtf8(doc->blockText(ids[std::size_t(row)]));
        const int span = 1 + int(text.count(QLatin1Char('\n')));
        if (p.line < line + span) {
            // Skip (p.line - line) inner '\n's to the start of the
            // target line, then clamp the column to that line's end.
            int pos = 0;
            for (int i = 0; i < p.line - line; ++i)
                pos = int(text.indexOf(QLatin1Char('\n'), pos)) + 1;
            const qsizetype nl = text.indexOf(QLatin1Char('\n'), pos);
            const int lineEnd = (nl < 0) ? int(text.size()) : int(nl);
            return { row, qMin(pos + qMax(0, p.column - 1), lineEnd) };
        }
        line += span;
    }
    const QString last = QString::fromUtf8(doc->blockText(ids.back()));
    return { int(ids.size()) - 1, int(last.size()) };
}

}  // namespace

struct EditorWidget::Private {
    LiveListModelBinding   *binding     = nullptr;
    QQuickWidget           *quickWidget = nullptr;
    QPointer<Session>       session;        // owned by the document
};

EditorWidget::EditorWidget(LiveListModelBinding::Capabilities caps,
                           QWidget *parent)
    : Markoff::MarkdownView(parent), d(std::make_unique<Private>())
{
    d->binding     = new LiveListModelBinding(caps, this);
    d->quickWidget = new QQuickWidget(this);
    d->quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    d->quickWidget->rootContext()->setContextProperty(
        QStringLiteral("modelBinding"), d->binding);
    d->quickWidget->setSource(QUrl(QStringLiteral(
        "qrc:/qt/qml/org/markoff/live/qml/EditorContent.qml")));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(d->quickWidget);

    // Contract §4.1: surface every canonical cursor move as the base
    // signal, mapped to the flat-line coordinate model. LiveCursorState
    // is created once in the binding's constructor (CONSTANT Q_PROPERTY)
    // and outlives every setDocument, so this single constructor-time
    // connect is inherently duplicate-free across document swaps.
    connect(d->binding->cursorState(), &LiveCursorState::cursorChanged,
            this, [this] {
                const auto p = cursorPosition();
                Q_EMIT cursorPositionChanged(p.line, p.column);
            });
}

EditorWidget::~EditorWidget()
{
    // Session owned by document — when EditorWidget outlives its document,
    // session is already null via QPointer. When document outlives the
    // widget (typical), explicitly destroy the session we created so the
    // document doesn't accumulate ghost sessions across leaf swaps.
    if (d->session && document()) {
        document()->destroySession(d->session);
    }
}

void EditorWidget::setDocument(Markoff::MarkoffDocument *doc)
{
    if (document() == doc) return;

    // Tear down old session.
    if (d->session && document()) {
        document()->destroySession(d->session);
    }
    d->session = nullptr;

    Markoff::MarkdownView::setDocument(doc);
    d->binding->setDocument(doc);

    if (doc) {
        d->session = doc->createSession();
        d->binding->setSession(d->session);
        // Force initial model population: setDocument only connects to
        // documentLoaded/d2DocumentChanged signals, but loadFromMarkdown
        // typically ran BEFORE this widget was constructed (the host
        // populates the document then hands it over). Without this nudge
        // the LiveBlockModel stays empty until the next user edit.
        doc->flushPendingD2Changed();
    }
}

LiveListModelBinding *EditorWidget::binding() const noexcept
{
    return d->binding;
}

Markoff::CursorPos EditorWidget::cursorPosition() const
{
    auto *doc = document();
    auto *cs  = d->binding ? d->binding->cursorState() : nullptr;
    if (!doc || !cs) return {1, 1};

    // Read the canonical cursor (L3: LiveCursorState is authoritative;
    // 2026-05-22-cursor-authority-decision.md). Non-TextCaret variants
    // report the block's first line, column 1 (spec §4.1).
    Markoff::BlockAnchor block;
    int qtPos = 0;
    if (const auto caret = cs->currentTextCaret()) {
        block = caret->block;
        qtPos = int(caret->cachedQtPos);
    } else {
        const auto cur = cs->cursor();
        if (const auto *bs = std::get_if<Markoff::BlockSelected>(&cur)) {
            block = bs->block;
        } else if (const auto *bi =
                       std::get_if<Markoff::BlockInternalEdit>(&cur)) {
            block = bi->block;
        } else {
            return {1, 1};
        }
    }

    // Row of the cursor's block via an index scan — same id-equality
    // pattern as SourceFindAdapter. O(blocks); deliberately uncached.
    int row = -1;
    const auto ids = doc->iterateBlocks();
    for (int i = 0; i < int(ids.size()); ++i) {
        if (ids[std::size_t(i)] == block) { row = i; break; }
    }
    if (row < 0) return {1, 1};
    return toCursorPos(doc, row, qtPos);
}

void EditorWidget::setReadOnly(bool ro)
{
    // Base stores (isReadOnly()/hasEditing()/undo()/redo() read it); the
    // binding's flag is the single authority the live leaf's mutation-
    // ingress gates consult (spec §4.2). The flag is binding state, not
    // per-document state, so it survives setDocument unchanged.
    Markoff::MarkdownView::setReadOnly(ro);
    d->binding->setReadOnly(ro);
}

void EditorWidget::setCursorPosition(Markoff::CursorPos pos)
{
    auto *doc = document();
    auto *cs  = d->binding ? d->binding->cursorState() : nullptr;
    if (!doc || !cs) return;
    const auto [row, qtPos] = fromCursorPos(doc, pos);
    // Chokepoint write (L3) — no widget-side cursor mutation.
    cs->requestTextCaretAtRow(row, qtPos);
}

void EditorWidget::attachFindController(Markoff::FindController *fc)
{
    d->binding->attachFindController(fc);
}

void EditorWidget::detachFindController()
{
    d->binding->detachFindController();
}

}  // namespace Markoff::Live
