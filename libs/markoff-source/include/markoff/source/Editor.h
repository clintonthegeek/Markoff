// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <QPointer>

#include <KSyntaxHighlighting/SyntaxHighlighter>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/core/Theme.h>

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
    void resizeEvent(QResizeEvent *e) override;

private:
    void recomputeGutterWidth();
    int  gutterWidth() const;

    QPointer<Markoff::MarkoffDocument>      m_document;
    QPointer<Markoff::Session>              m_session;
    Markoff::SourceTextDocumentBinding     *m_binding      = nullptr;
    KSyntaxHighlighting::SyntaxHighlighter *m_highlighter  = nullptr;
    Gutter                                 *m_gutter       = nullptr;
    Markoff::Theme                          m_theme;

    friend class Gutter;
};

} // namespace Markoff::Source::Widget
