// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/Editor.h>
#include "Detail/SourceFindAdapter.h"
#include "Gutter.h"
#include "InnerEditor.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/core/Detail/FlatBlockResolve.h>

#include <QKeyEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#include <optional>

namespace Markoff::Source {

using Detail::Gutter;
using Detail::InnerEditor;

namespace {
KSyntaxHighlighting::Repository &repo() {
    static KSyntaxHighlighting::Repository r;
    return r;
}
} // anon

Editor::Editor(QWidget *parent)
    : Markoff::MarkdownView(parent),
      m_editor(new InnerEditor(this)),
      m_binding(new Markoff::SourceTextDocumentBinding(this)),
      m_highlighter(new KSyntaxHighlighting::SyntaxHighlighter(this)),
      m_theme(Markoff::Theme::defaultLight())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editor);

    // The binding talks to the QPlainTextEdit's underlying QTextDocument.
    // It also disables that QTextDocument's own undo stack — CRDT undo via
    // MarkoffDocument is canonical (see SourceTextDocumentBinding::rewireQtDocument).
    m_binding->setTextDocument(m_editor->document());
    connect(m_binding, &Markoff::SourceTextDocumentBinding::caretResolved,
            this, [this](int start, int active) {
                QTextCursor c(m_editor->document());
                c.setPosition(start);
                if (active != start)
                    c.setPosition(active, QTextCursor::KeepAnchor);
                m_editor->setTextCursor(c);
            });

    // Parent the highlighter to the Editor (not to the QTextDocument) so its
    // lifetime is tied to the Editor regardless of any later setDocument() on
    // the underlying QPlainTextEdit.
    m_highlighter->setDocument(m_editor->document());
    m_highlighter->setDefinition(repo().definitionForName(QStringLiteral("Markdown")));
    m_highlighter->setTheme(repo().defaultTheme(KSyntaxHighlighting::Repository::LightTheme));

    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    m_gutter = new Gutter(this);
    m_findAdapter = new Detail::SourceFindAdapter(this, this);
    connect(m_editor, &QPlainTextEdit::blockCountChanged,
            this, [this]() { recomputeGutterWidth(); });
    connect(m_editor, &QPlainTextEdit::updateRequest,
            this, [this](const QRect &rect, int dy) {
        if (dy) m_gutter->scroll(0, dy);
        else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        if (rect.contains(m_editor->viewport()->rect())) recomputeGutterWidth();
    });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
            this, [this]() { if (m_gutter) m_gutter->update(); });
    recomputeGutterWidth();

    // Forward key events (undo/redo) from the inner editor
    m_editor->installEventFilter(this);
}

Editor::~Editor() = default;

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    if (auto *prev = Markoff::MarkdownView::document()) {
        if (prev == doc) return;
        // Tear down the previous session, if any.
        if (m_session) {
            m_binding->setSession(nullptr);
            prev->destroySession(m_session.data());
            m_session = nullptr;
        }
        // No direct prev→this connections exist; SourceTextDocumentBinding
        // owns all doc subscriptions and cleans them up via setMarkoffDocument(nullptr).
    }

    // Disconnect the stale d2DocumentChanged→applyParagraphMargins connection
    // from the previous document before wiring the new one.
    if (m_paragraphMarginsCon) {
        QObject::disconnect(m_paragraphMarginsCon);
        m_paragraphMarginsCon = {};
    }

    Markoff::MarkdownView::setDocument(doc);

    if (doc) {
        m_session = doc->createSession();
        m_binding->setMarkoffDocument(doc);
        m_binding->setSession(m_session.data());
        m_paragraphMarginsCon = QObject::connect(
            doc, &Markoff::MarkoffDocument::d2DocumentChanged,
            this, &Editor::applyParagraphMargins);
        // Initial pass — the binding seeded qdoc from widgetFlatView in
        // setMarkoffDocument; margins for the initial blocks need to land too.
        applyParagraphMargins();
    } else {
        m_binding->setMarkoffDocument(nullptr);
    }
}

Markoff::CursorPos Editor::cursorPosition() const {
    auto cursor = m_editor->textCursor();
    return { cursor.blockNumber() + 1, cursor.positionInBlock() + 1 };
}

void Editor::setCursorPosition(Markoff::CursorPos pos) {
    auto block = m_editor->document()->findBlockByNumber(pos.line - 1);
    if (!block.isValid())
        block = m_editor->document()->lastBlock();
    QTextCursor cursor(block);
    // length() includes the block separator, so length()-1 is end-of-text.
    cursor.setPosition(block.position()
                       + qMin(qMax(0, pos.column - 1), block.length() - 1));
    m_editor->setTextCursor(cursor);
}

float Editor::scrollPositionVisualLine() const {
    auto *sb = m_editor->verticalScrollBar();
    if (!sb || sb->maximum() == 0) return 0.0f;
    return static_cast<float>(sb->value()) / static_cast<float>(sb->maximum());
}

void Editor::setScrollPositionVisualLine(float pos) {
    auto *sb = m_editor->verticalScrollBar();
    if (!sb || sb->maximum() == 0) return;
    sb->setValue(static_cast<int>(pos * static_cast<float>(sb->maximum())));
}

void Editor::setReadOnly(bool ro) {
    Markoff::MarkdownView::setReadOnly(ro);
    m_editor->setReadOnly(ro);
}

bool Editor::isReadOnly() const { return m_editor->isReadOnly(); }

void Editor::attachFindController(Markoff::FindController *fc)
{
    if (m_findAdapter) m_findAdapter->attach(fc);
}

void Editor::detachFindController()
{
    if (m_findAdapter) m_findAdapter->detach();
}

Markoff::Theme Editor::theme() const { return m_theme; }

void Editor::setTheme(const Markoff::Theme &t) {
    MarkdownView::setTheme(t);  // base stores + emits themeChanged
    m_theme = t;
    QPalette p = m_editor->palette();
    p.setColor(QPalette::Base,            t.color(Markoff::Theme::Slot::EditorBackground));
    p.setColor(QPalette::Text,            t.color(Markoff::Theme::Slot::TextDefault));
    p.setColor(QPalette::Highlight,       t.color(Markoff::Theme::Slot::SelectionBackground));
    p.setColor(QPalette::HighlightedText, t.color(Markoff::Theme::Slot::TextDefault));
    m_editor->setPalette(p);

    const bool darkUi = t.color(Markoff::Theme::Slot::EditorBackground).lightnessF() < 0.5;
    if (m_highlighter) {
        m_highlighter->setTheme(repo().defaultTheme(
            darkUi ? KSyntaxHighlighting::Repository::DarkTheme
                   : KSyntaxHighlighting::Repository::LightTheme));
        m_highlighter->rehighlight();
    }
    if (m_gutter) m_gutter->update();
}

bool Editor::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *e = static_cast<QKeyEvent *>(event);
        const auto m = e->modifiers();
        auto *doc = Markoff::MarkdownView::document();
        if (doc && (m & Qt::ControlModifier)) {
            if (e->key() == Qt::Key_Z && !(m & Qt::ShiftModifier)) {
                doc->undoD2();
                return true;
            }
            if (e->key() == Qt::Key_Y || (e->key() == Qt::Key_Z && (m & Qt::ShiftModifier))) {
                doc->redoD2();
                return true;
            }
        }
    }
    return Markoff::MarkdownView::eventFilter(watched, event);
}

void Editor::resizeEvent(QResizeEvent *e) {
    Markoff::MarkdownView::resizeEvent(e);
    // Gutter is not in the layout; position it to match the inner editor.
    QRect r = m_editor->geometry();
    m_gutter->setGeometry(QRect(r.left(), r.top(), gutterWidth(), r.height()));
}

int Editor::gutterWidth() const {
    int digits = 1;
    int max = qMax(1, m_editor->blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 3 + m_editor->fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 6;
}

void Editor::recomputeGutterWidth() {
    static_cast<InnerEditor *>(m_editor)->setViewportMargins(gutterWidth(), 0, 0, 0);
}

// --- Markdown format operations -------------------------------------------
//
// All operate by resolving the cursor's qt-position to a markoff block and
// applying the edit via d2ApplyBufferEdit, bypassing the lossy sep→no-sep
// translation in SourceTextDocumentBinding (which sends range edits through
// MarkoffDocument::applyFlatEdit). The translation drops boundary direction
// and would route boundary-touching edits through applyFlatEdit's
// cross-block branch, merging blocks. See setHeadingLevel below and the
// 2026-05-21 source-view dogfood fix for the root-cause writeup.

namespace {

// Toggle `delim` wrap around the QPlainTextEdit's selection (or insert an
// empty pair at the cursor), mediated through the block-aware d2 API.
//
// Detection (per slice, matching the legacy QTextCursor impl):
//   * surroundedOutside — bytes outside the selection in the same block are
//     already `delim`. Unwrap by removing both.
//   * insideMarkers     — selection itself starts and ends with `delim`.
//     Unwrap by stripping the inner markers.
//   * otherwise         — wrap by inserting `delim` at both ends.
//
// Multi-block selections: each block's slice is handled independently
// (matching LiveFormatController::wrapPerBlock).
void wrapToggle(QPlainTextEdit *te,
                Markoff::SourceTextDocumentBinding *binding,
                const QByteArray &delim) {
    if (!te || !binding) return;
    Markoff::MarkoffDocument *doc = binding->markoffDocument();
    if (!doc) return;

    QTextCursor c = te->textCursor();
    const QString docText = te->toPlainText();
    const int delimLen = delim.size();  // ASCII delims: bytes == UTF-16 units

    // --- No selection: insert delim+delim, park cursor between. -----------
    if (!c.hasSelection()) {
        const int qtPos = c.position();
        const quint32 sepByte =
            Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtPos);
        auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepByte, /*biasForward=*/true);
        if (!hit) {
            // Empty document: applyFlatEdit auto-creates a paragraph block.
            doc->applyFlatEdit(0, 0, delim + delim, Markoff::Origin::UserEdit);
        } else {
            Markoff::UndoLog::Transaction t(doc->d2UndoLog());
            doc->d2ApplyBufferEdit(hit->blockId, hit->byteInBlock, 0,
                                   delim + delim, t);
        }
        doc->flushPendingD2Changed();
        QTextCursor c2 = te->textCursor();
        c2.setPosition(qtPos + delimLen);
        te->setTextCursor(c2);
        return;
    }

    // --- Selection: per-block toggle. ------------------------------------
    const int qtStart = c.selectionStart();
    const int qtEnd   = c.selectionEnd();
    const quint32 sepStart =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtStart);
    const quint32 sepEnd =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtEnd);
    const QList<Markoff::Detail::BlockSlice> slices = Markoff::Detail::sliceByBlocks(doc, sepStart, sepEnd);
    if (slices.isEmpty()) return;

    enum class Mode { SurroundedOutside, InsideMarkers, Wrap };

    // Determine per-slice mode and compute the post-edit selection
    // restoration for the SINGLE-slice common case. For multi-slice we
    // collapse the cursor to the end after all edits.
    Mode firstMode = Mode::Wrap;

    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());

        // Process slices in reverse so later block edits don't shift earlier
        // ones' bytes (each slice is intra-block, so this matters only in
        // case of multi-block selection).
        for (int n = slices.size() - 1; n >= 0; --n) {
            const Markoff::Detail::BlockSlice &s = slices[n];
            const QByteArray content = doc->blockText(s.blockId);
            const int blockSize = content.size();
            const int loInt = static_cast<int>(s.byteLo);
            const int hiInt = static_cast<int>(s.byteHi);

            const bool surroundedOutside =
                loInt >= delimLen
                && hiInt + delimLen <= blockSize
                && content.mid(loInt - delimLen, delimLen) == delim
                && content.mid(hiInt, delimLen) == delim;
            const bool insideMarkers =
                !surroundedOutside
                && (hiInt - loInt) >= 2 * delimLen
                && content.mid(loInt, delimLen) == delim
                && content.mid(hiInt - delimLen, delimLen) == delim;

            Mode mode = Mode::Wrap;
            if (surroundedOutside)    mode = Mode::SurroundedOutside;
            else if (insideMarkers)   mode = Mode::InsideMarkers;
            if (n == 0) firstMode = mode;

            switch (mode) {
            case Mode::SurroundedOutside:
                // Remove trailing delim (higher byte) first, then leading.
                doc->d2ApplyBufferEdit(s.blockId, s.byteHi,
                                       static_cast<quint32>(delimLen),
                                       QByteArray(), t);
                doc->d2ApplyBufferEdit(s.blockId, s.byteLo - delimLen,
                                       static_cast<quint32>(delimLen),
                                       QByteArray(), t);
                break;
            case Mode::InsideMarkers:
                doc->d2ApplyBufferEdit(s.blockId, s.byteHi - delimLen,
                                       static_cast<quint32>(delimLen),
                                       QByteArray(), t);
                doc->d2ApplyBufferEdit(s.blockId, s.byteLo,
                                       static_cast<quint32>(delimLen),
                                       QByteArray(), t);
                break;
            case Mode::Wrap:
                // Insert trailing delim first (higher byte), then leading.
                doc->d2ApplyBufferEdit(s.blockId, s.byteHi, 0, delim, t);
                doc->d2ApplyBufferEdit(s.blockId, s.byteLo, 0, delim, t);
                break;
            }
        }
    }

    doc->flushPendingD2Changed();

    // Restore selection. For a single-slice (intra-block) edit we know the
    // exact range that survived. For multi-slice, collapse to the trailing
    // edge — multi-block format toggles are an edge case and per-slice
    // modes may differ, making exact restoration ambiguous.
    QTextCursor c2 = te->textCursor();
    if (slices.size() == 1) {
        int newStart = qtStart;
        int newEnd   = qtEnd;
        switch (firstMode) {
        case Mode::SurroundedOutside:
            newStart -= delimLen;
            newEnd   -= delimLen;
            break;
        case Mode::InsideMarkers:
            newEnd -= 2 * delimLen;
            break;
        case Mode::Wrap:
            newStart += delimLen;
            newEnd   += delimLen;
            break;
        }
        c2.setPosition(newStart);
        c2.setPosition(newEnd, QTextCursor::KeepAnchor);
    } else {
        // Multi-slice: park cursor near the trailing end without a
        // restored selection.
        const int docLen = te->document()->characterCount() - 1;
        int newPos = qtEnd;
        if (newPos > docLen) newPos = docLen;
        if (newPos < 0)      newPos = 0;
        c2.setPosition(newPos);
    }
    te->setTextCursor(c2);
}

} // namespace

// Format verbs are blocked while read-only (MarkdownView contract §10
// check 2) — they mutate via d2ApplyBufferEdit, so the inner widget's
// readOnly flag alone would not stop them.
void Editor::toggleBold()          { if (isReadOnly()) return; wrapToggle(m_editor, m_binding, "**"); }
void Editor::toggleItalic()        { if (isReadOnly()) return; wrapToggle(m_editor, m_binding, "_");  }
void Editor::toggleStrikethrough() { if (isReadOnly()) return; wrapToggle(m_editor, m_binding, "~~"); }
void Editor::toggleInlineCode()    { if (isReadOnly()) return; wrapToggle(m_editor, m_binding, "`");  }

void Editor::insertLink() {
    if (isReadOnly()) return;
    if (!m_editor || !m_binding) return;
    Markoff::MarkoffDocument *doc = m_binding->markoffDocument();
    if (!doc) return;

    QTextCursor c = m_editor->textCursor();
    const QString docText = m_editor->toPlainText();

    // --- No selection: insert `[](url)` template, park cursor between `[]`. -
    if (!c.hasSelection()) {
        const int qtPos = c.position();
        const quint32 sepByte =
            Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtPos);
        auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepByte, /*biasForward=*/true);
        const QByteArray payload = QByteArrayLiteral("[](url)");
        if (!hit) {
            doc->applyFlatEdit(0, 0, payload, Markoff::Origin::UserEdit);
        } else {
            Markoff::UndoLog::Transaction t(doc->d2UndoLog());
            doc->d2ApplyBufferEdit(hit->blockId, hit->byteInBlock, 0,
                                   payload, t);
        }
        doc->flushPendingD2Changed();
        QTextCursor c2 = m_editor->textCursor();
        c2.setPosition(qtPos + 1);
        m_editor->setTextCursor(c2);
        return;
    }

    // --- Selection: wrap `[selection](url)` per block slice. ---------------
    const int qtStart = c.selectionStart();
    const int qtEnd   = c.selectionEnd();
    const quint32 sepStart =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtStart);
    const quint32 sepEnd =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtEnd);
    const QList<Markoff::Detail::BlockSlice> slices = Markoff::Detail::sliceByBlocks(doc, sepStart, sepEnd);
    if (slices.isEmpty()) return;

    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        // Reverse order: later block edits don't shift earlier ones' bytes.
        // Within a slice: insert trailing `](url)` first (higher byte), then
        // leading `[` — same higher-then-lower pattern as wrapToggle.
        for (int n = slices.size() - 1; n >= 0; --n) {
            const Markoff::Detail::BlockSlice &s = slices[n];
            doc->d2ApplyBufferEdit(s.blockId, s.byteHi, 0,
                                   QByteArrayLiteral("](url)"), t);
            doc->d2ApplyBufferEdit(s.blockId, s.byteLo, 0,
                                   QByteArrayLiteral("["), t);
        }
    }

    doc->flushPendingD2Changed();

    QTextCursor c2 = m_editor->textCursor();
    if (slices.size() == 1) {
        // Single-block selection: park selection over `url` for easy replace.
        // Layout: `[selection](url)` starting at qtStart; url at qtEnd+3..qtEnd+6.
        c2.setPosition(qtEnd + 3);
        c2.setPosition(qtEnd + 6, QTextCursor::KeepAnchor);
    } else {
        // Multi-slice: collapse near the trailing end (matches wrapToggle).
        const int docLen = m_editor->document()->characterCount() - 1;
        int newPos = qtEnd;
        if (newPos > docLen) newPos = docLen;
        if (newPos < 0)      newPos = 0;
        c2.setPosition(newPos);
    }
    m_editor->setTextCursor(c2);
}

void Editor::setHeadingLevel(int level) {
    if (isReadOnly()) return;
    if (!m_editor || !m_binding) return;
    if (level < 0 || level > 6) return;
    auto *doc = m_binding->markoffDocument();
    if (!doc) return;

    // Editing the heading prefix via QTextCursor on the inner QPlainTextEdit
    // and letting it route through SourceTextDocumentBinding::onQtContentsChange
    // would issue a range edit whose start sits exactly at a markoff block
    // boundary whenever the heading is the first line of a non-first block.
    // The sep-view→no-sep-view translation loses the boundary direction, and
    // applyFlatEdit's range-edit boundary bias ("<= blkEnd") then routes the
    // edit through the cross-block-edit branch, removing the heading block
    // and merging its tail into the previous block. Bypass the lossy
    // coordinate translation by mutating the target block's buffer directly,
    // the same way LiveFormatController::setHeadingLevel does.

    const QTextCursor c = m_editor->textCursor();
    const int origQtPos = c.position();
    const QTextBlock qtb = c.block();
    const int lineStartQt = qtb.position();
    const QString text = m_editor->toPlainText();
    const quint32 lineStartSep =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(text, lineStartQt);

    // Resolve the line-start sep-byte to its model block via the shared helper,
    // which tracks the WP-unification single-'\n' separator (SEP_LEN == 1). The
    // bespoke walk this replaced hardcoded SEP_LEN == 2 ("\n\n"); once
    // widgetFlatView() went single-'\n', that walk over-advanced its cursor by
    // one byte per preceding boundary, underflowing byteInBlock (quint32 wrap)
    // for any heading below the first block — so the prefix landed at
    // end-of-block ("Hello## ") instead of the line start. biasForward == false
    // keeps an empty line's own (zero-length) block as the target rather than
    // skipping forward to the next block.
    const auto hit = Markoff::Detail::findBlockAtSepByte(
        doc, lineStartSep, /*biasForward=*/false);
    if (!hit) return;
    const Markoff::BlockId targetBlock = hit->blockId;
    const quint32 byteInBlock = hit->byteInBlock;

    const QByteArray content = doc->blockText(targetBlock);
    const int blockSize = content.size();
    int oldBytes = 0;
    while (oldBytes < 6
           && static_cast<int>(byteInBlock) + oldBytes < blockSize
           && content[static_cast<int>(byteInBlock) + oldBytes] == '#') {
        ++oldBytes;
    }
    if (oldBytes > 0
        && static_cast<int>(byteInBlock) + oldBytes < blockSize
        && content[static_cast<int>(byteInBlock) + oldBytes] == ' ') {
        ++oldBytes;
    } else if (oldBytes > 0
               && static_cast<int>(byteInBlock) + oldBytes != blockSize) {
        // "##" with non-space follower — not an ATX prefix; leave alone.
        oldBytes = 0;
    }

    const QByteArray newPrefix = (level == 0)
        ? QByteArray()
        : QByteArray(level, '#') + ' ';

    if (newPrefix.size() == oldBytes
        && content.mid(static_cast<int>(byteInBlock), oldBytes) == newPrefix)
        return;

    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(targetBlock, byteInBlock,
                               static_cast<quint32>(oldBytes),
                               newPrefix, t);
    }

    // Flush the debounced d2DocumentChanged so the binding syncs the
    // QTextDocument synchronously. Users expect immediate visual feedback;
    // tests expect toPlainText() to reflect the change without spinning the
    // event loop.
    doc->flushPendingD2Changed();

    // Restore cursor: prefixes are ASCII so byte-count == UTF-16-unit count.
    const int delta = static_cast<int>(newPrefix.size()) - oldBytes;
    int newPos = origQtPos + delta;
    if (newPos < lineStartQt) newPos = lineStartQt;
    const int docLen = m_editor->document()->characterCount() - 1;
    if (newPos > docLen) newPos = docLen;
    QTextCursor c2 = m_editor->textCursor();
    c2.setPosition(newPos);
    m_editor->setTextCursor(c2);
}

void Editor::applyParagraphMargins()
{
    if (!m_editor) return;
    QTextDocument *qdoc = m_editor->document();
    if (!qdoc) return;
    QTextCursor c(qdoc);
    QSignalBlocker block(qdoc);   // do NOT loop back through the binding
    c.beginEditBlock();
    for (QTextBlock b = qdoc->begin(); b.isValid(); b = b.next()) {
        c.setPosition(b.position());
        QTextBlockFormat bf = b.blockFormat();
        // Same values as styled — symmetry maintains a consistent visual
        // gap between view leaves (spec 2026-05-28 §3.5).
        bf.setTopMargin(5);
        bf.setBottomMargin(5);
        c.setBlockFormat(bf);
    }
    c.endEditBlock();
}

} // namespace Markoff::Source
