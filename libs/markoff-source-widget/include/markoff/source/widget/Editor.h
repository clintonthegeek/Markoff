// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>

#include <KSyntaxHighlighting/SyntaxHighlighter>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/SourceTextDocumentBinding.h>
#include <markoff-foundation/Theme.h>

namespace Markoff::Source::Widget {

class Gutter;

class Editor : public QPlainTextEdit {
    Q_OBJECT
    Q_PROPERTY(Markoff::MarkoffDocument *document READ document
               WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Theme theme READ theme
               WRITE setTheme NOTIFY themeChanged)
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    Markoff::MarkoffDocument *document() const;
    void setDocument(Markoff::MarkoffDocument *);

    Markoff::Theme theme() const;
    void setTheme(const Markoff::Theme &);

Q_SIGNALS:
    void documentChanged();
    void themeChanged();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    Markoff::MarkoffDocument               *m_document     = nullptr;
    Markoff::Session                       *m_session      = nullptr;
    Markoff::SourceTextDocumentBinding     *m_binding      = nullptr;
    KSyntaxHighlighting::SyntaxHighlighter *m_highlighter  = nullptr;
    Gutter                                 *m_gutter       = nullptr;
    Markoff::Theme                          m_theme;
};

} // namespace Markoff::Source::Widget
