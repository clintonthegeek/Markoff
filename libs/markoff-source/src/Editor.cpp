// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/Editor.h>
#include "Gutter.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SourceTextDocumentBinding.h>

#include <QKeyEvent>
#include <QPalette>
#include <QResizeEvent>

namespace Markoff::Source::Widget {

namespace {
KSyntaxHighlighting::Repository &repo() {
    static KSyntaxHighlighting::Repository r;
    return r;
}
} // anon

Editor::Editor(QWidget *parent)
    : QPlainTextEdit(parent),
      m_binding(new Markoff::SourceTextDocumentBinding(this)),
      m_highlighter(new KSyntaxHighlighting::SyntaxHighlighter(this)),
      m_theme(Markoff::Theme::defaultLight())
{
    // The binding talks to the QPlainTextEdit's underlying QTextDocument.
    // It also disables that QTextDocument's own undo stack — CRDT undo via
    // MarkoffDocument is canonical (see SourceTextDocumentBinding::rewireQtDocument).
    m_binding->setTextDocument(QPlainTextEdit::document());

    // Parent the highlighter to the Editor (not to the QTextDocument) so its
    // lifetime is tied to the Editor regardless of any later setDocument() on
    // the underlying QPlainTextEdit.
    m_highlighter->setDocument(QPlainTextEdit::document());
    m_highlighter->setDefinition(repo().definitionForName(QStringLiteral("Markdown")));
    m_highlighter->setTheme(repo().defaultTheme(KSyntaxHighlighting::Repository::LightTheme));

    setLineWrapMode(QPlainTextEdit::WidgetWidth);

    m_gutter = new Gutter(this);
    connect(this, &QPlainTextEdit::blockCountChanged,
            this, [this]() { recomputeGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest,
            this, [this](const QRect &rect, int dy) {
        if (dy) m_gutter->scroll(0, dy);
        else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        if (rect.contains(viewport()->rect())) recomputeGutterWidth();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, [this]() { if (m_gutter) m_gutter->update(); });
    recomputeGutterWidth();
}

Editor::~Editor() = default;

Markoff::MarkoffDocument *Editor::document() const { return m_document.data(); }

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    if (m_document.data() == doc) return;

    // Tear down the previous session, if any. QPointer auto-nulls when the
    // watched object dies, so we only act if both are still alive.
    if (m_document && m_session) {
        m_binding->setSession(nullptr);
        m_document->destroySession(m_session.data());
        m_session = nullptr;
    }

    m_document = doc;

    if (m_document) {
        m_session = m_document->createSession();
        m_binding->setMarkoffDocument(m_document.data());
        m_binding->setSession(m_session.data());
    } else {
        m_binding->setMarkoffDocument(nullptr);
    }

    emit documentChanged();
}

Markoff::Theme Editor::theme() const { return m_theme; }

void Editor::setTheme(const Markoff::Theme &t) {
    m_theme = t;
    QPalette p = palette();
    p.setColor(QPalette::Base,            t.color(Markoff::Theme::Slot::EditorBackground));
    p.setColor(QPalette::Text,            t.color(Markoff::Theme::Slot::TextDefault));
    p.setColor(QPalette::Highlight,       t.color(Markoff::Theme::Slot::SelectionBackground));
    p.setColor(QPalette::HighlightedText, t.color(Markoff::Theme::Slot::TextDefault));
    setPalette(p);

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

void Editor::keyPressEvent(QKeyEvent *e) {
    const auto m = e->modifiers();
    if (m_document && (m & Qt::ControlModifier)) {
        if (e->key() == Qt::Key_Z && !(m & Qt::ShiftModifier)) {
            m_document->undoD2();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Y || (e->key() == Qt::Key_Z && (m & Qt::ShiftModifier))) {
            m_document->redoD2();
            e->accept();
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(e);
}

void Editor::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

int Editor::gutterWidth() const {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 6;
}

void Editor::recomputeGutterWidth() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

} // namespace
