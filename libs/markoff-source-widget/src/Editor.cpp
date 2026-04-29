// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/source/widget/Editor.h>
#include "Gutter.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/SourceTextDocumentBinding.h>

#include <QKeyEvent>

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
      m_highlighter(new KSyntaxHighlighting::SyntaxHighlighter(QPlainTextEdit::document())),
      m_theme(Markoff::Theme::defaultLight())
{
    // The binding talks to the QPlainTextEdit's underlying QTextDocument.
    // It also disables that QTextDocument's own undo stack — CRDT undo via
    // MarkoffDocument is canonical (see SourceTextDocumentBinding::rewireQtDocument).
    m_binding->setTextDocument(QPlainTextEdit::document());

    m_highlighter->setDefinition(repo().definitionForName(QStringLiteral("Markdown")));
    m_highlighter->setTheme(repo().defaultTheme(KSyntaxHighlighting::Repository::LightTheme));

    setLineWrapMode(QPlainTextEdit::WidgetWidth);
}

Editor::~Editor() = default;

Markoff::MarkoffDocument *Editor::document() const { return m_document; }

void Editor::setDocument(Markoff::MarkoffDocument *doc) {
    if (m_document == doc) return;

    // Tear down the previous session, if any.
    if (m_document && m_session) {
        m_binding->setSession(nullptr);
        m_document->destroySession(m_session);
        m_session = nullptr;
    }

    m_document = doc;

    if (m_document) {
        m_session = m_document->createSession();
        m_binding->setMarkoffDocument(m_document);
        m_binding->setSession(m_session);

        // If the document outlives or is destroyed before the editor, null our
        // pointers so destruction is safe and a subsequent setDocument(nullptr)
        // doesn't dereference a dead object.
        connect(m_document, &QObject::destroyed, this, [this]() {
            m_document = nullptr;
            m_session  = nullptr;
        });
        connect(m_session, &QObject::destroyed, this, [this]() {
            m_session = nullptr;
        });
    } else {
        m_binding->setMarkoffDocument(nullptr);
    }

    emit documentChanged();
}

Markoff::Theme Editor::theme() const { return m_theme; }

void Editor::setTheme(const Markoff::Theme &t) {
    m_theme = t;
    if (m_gutter) m_gutter->update();
    emit themeChanged();
}

void Editor::keyPressEvent(QKeyEvent *e) {
    const auto m = e->modifiers();
    if (m_document && (m & Qt::ControlModifier)) {
        if (e->key() == Qt::Key_Z && !(m & Qt::ShiftModifier)) {
            m_document->undo();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Y || (e->key() == Qt::Key_Z && (m & Qt::ShiftModifier))) {
            m_document->redo();
            e->accept();
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(e);
}

} // namespace
