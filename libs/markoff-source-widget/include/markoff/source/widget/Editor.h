// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Theme.h>

namespace Markoff { class SourceTextDocumentBinding; }

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

private:
    Markoff::MarkoffDocument          *m_document = nullptr;
    Markoff::SourceTextDocumentBinding *m_binding = nullptr;
    Gutter                             *m_gutter  = nullptr;
    Markoff::Theme                      m_theme;
};

} // namespace Markoff::Source::Widget
