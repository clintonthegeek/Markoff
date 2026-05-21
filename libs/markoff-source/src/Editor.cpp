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

#include <QKeyEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollBar>
#include <QVBoxLayout>

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

    Markoff::MarkdownView::setDocument(doc);

    if (doc) {
        m_session = doc->createSession();
        m_binding->setMarkoffDocument(doc);
        m_binding->setSession(m_session.data());
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
    if (!block.isValid()) return;
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + qMax(0, pos.column - 1));
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
    emit themeChanged();
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
// All operate via QTextCursor on the inner QPlainTextEdit. Changes flow
// through SourceTextDocumentBinding to MarkoffDocument::applyFlatEdit, so
// the live view sees them on the next d2DocumentChanged tick.

namespace {

// Wrap or unwrap the selection with `delim` on both sides. Strategy mirrors
// LiveFormatController::wrapPerBlock: check whether the surrounding bytes
// (or selection inside) already match the delimiter, unwrap if so;
// otherwise wrap.
void wrapToggle(QPlainTextEdit *te, const QString &delim) {
    if (!te) return;
    QTextCursor c = te->textCursor();
    const int n = delim.length();

    if (!c.hasSelection()) {
        // No selection: insert delim+delim, park cursor in the middle.
        const int pos = c.position();
        c.beginEditBlock();
        c.insertText(delim + delim);
        c.setPosition(pos + n);
        c.endEditBlock();
        te->setTextCursor(c);
        return;
    }

    int start = c.selectionStart();
    int end   = c.selectionEnd();
    const QString docText = te->toPlainText();

    const bool surroundedOutside =
        start >= n && end + n <= docText.length()
        && docText.mid(start - n, n) == delim
        && docText.mid(end, n) == delim;
    const bool insideMarkers =
        end - start >= 2 * n
        && docText.mid(start, n) == delim
        && docText.mid(end - n, n) == delim;

    c.beginEditBlock();
    if (surroundedOutside) {
        c.setPosition(end);
        c.setPosition(end + n, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setPosition(start - n);
        c.setPosition(start, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setPosition(start - n);
        c.setPosition(end - n, QTextCursor::KeepAnchor);
    } else if (insideMarkers) {
        c.setPosition(end - n);
        c.setPosition(end, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setPosition(start);
        c.setPosition(start + n, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setPosition(start);
        c.setPosition(end - 2 * n, QTextCursor::KeepAnchor);
    } else {
        c.setPosition(end);
        c.insertText(delim);
        c.setPosition(start);
        c.insertText(delim);
        c.setPosition(start + n);
        c.setPosition(end + n, QTextCursor::KeepAnchor);
    }
    c.endEditBlock();
    te->setTextCursor(c);
}

} // namespace

void Editor::toggleBold()          { wrapToggle(m_editor, QStringLiteral("**")); }
void Editor::toggleItalic()        { wrapToggle(m_editor, QStringLiteral("_"));  }
void Editor::toggleStrikethrough() { wrapToggle(m_editor, QStringLiteral("~~")); }
void Editor::toggleInlineCode()    { wrapToggle(m_editor, QStringLiteral("`"));  }

void Editor::insertLink() {
    if (!m_editor) return;
    QTextCursor c = m_editor->textCursor();
    c.beginEditBlock();
    if (c.hasSelection()) {
        const int start = c.selectionStart();
        const int end   = c.selectionEnd();
        c.setPosition(end);
        c.insertText(QStringLiteral("](url)"));
        c.setPosition(start);
        c.insertText(QStringLiteral("["));
        // Park cursor over the `url` placeholder for easy replacement.
        // After both inserts: layout is `[selection](url)` starting at start.
        // selection is at [start+1, end+1); url at end+3..end+6.
        c.setPosition(end + 3);
        c.setPosition(end + 6, QTextCursor::KeepAnchor);
    } else {
        const int pos = c.position();
        c.insertText(QStringLiteral("[](url)"));
        c.setPosition(pos + 1);
    }
    c.endEditBlock();
    m_editor->setTextCursor(c);
}

void Editor::setHeadingLevel(int level) {
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

    const auto blocks = doc->iterateBlocks();
    if (blocks.empty()) return;
    constexpr quint32 SEP_LEN = 2;  // "\n\n"
    quint32 sepCursor = 0;
    Markoff::BlockId targetBlock;
    quint32 byteInBlock = 0;
    bool found = false;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const quint32 sz = static_cast<quint32>(doc->blockText(blocks[i]).size());
        const quint32 blkEnd = sepCursor + sz;
        if (lineStartSep <= blkEnd) {
            targetBlock  = blocks[i];
            byteInBlock  = lineStartSep - sepCursor;
            found = true;
            break;
        }
        sepCursor = blkEnd;
        if (i + 1 < blocks.size()) sepCursor += SEP_LEN;
    }
    if (!found) return;

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

} // namespace Markoff::Source
