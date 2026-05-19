// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/Editor.h>
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

void Editor::showFindBar()    { /* find bar integration: v1.1 */ }
void Editor::showReplaceBar() { /* replace bar integration: v1.1 */ }
void Editor::hideFindBar()    { /* find bar integration: v1.1 */ }

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

} // namespace Markoff::Source
