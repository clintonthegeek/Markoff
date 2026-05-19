// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <QPointer>

#include <KSyntaxHighlighting/SyntaxHighlighter>

#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/core/Theme.h>

namespace Markoff::Source {

namespace Detail { class Gutter; }

class Editor : public Markoff::MarkdownView {
    Q_OBJECT
    Q_PROPERTY(Markoff::Theme theme READ theme
               WRITE setTheme NOTIFY themeChanged)
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    // MarkdownView contract
    void setDocument(Markoff::MarkoffDocument *doc) override;
    Markoff::CursorPos cursorPosition() const override;
    void setCursorPosition(Markoff::CursorPos pos) override;
    float scrollPositionVisualLine() const override;
    void  setScrollPositionVisualLine(float pos) override;
    void setReadOnly(bool ro) override;
    bool isReadOnly() const override;
    bool hasCursor()  const override { return true; }
    bool hasEditing() const override { return !isReadOnly(); }
    void showFindBar()    override;
    void showReplaceBar() override;
    void hideFindBar()    override;

    // Theme (source-specific)
    Markoff::Theme theme() const;
    void setTheme(const Markoff::Theme &);

    // Accessor to the inner QPlainTextEdit (for Gutter, FindBar, and tests)
    QPlainTextEdit *plainTextEdit() const { return m_editor; }

    // Forwarding methods for QPlainTextEdit API used by Gutter, FindBar, tests
    QString toPlainText() const { return m_editor->toPlainText(); }
    QList<QTextEdit::ExtraSelection> extraSelections() const { return m_editor->extraSelections(); }
    void setExtraSelections(const QList<QTextEdit::ExtraSelection> &sels) { m_editor->setExtraSelections(sels); }
    QTextCursor textCursor() const { return m_editor->textCursor(); }
    void setTextCursor(const QTextCursor &c) { m_editor->setTextCursor(c); }
    void ensureCursorVisible() { m_editor->ensureCursorVisible(); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *e) override;

Q_SIGNALS:
    void themeChanged();

private:
    void recomputeGutterWidth();
    int  gutterWidth() const;

    QPlainTextEdit                          *m_editor      = nullptr;
    QPointer<Markoff::Session>              m_session;
    Markoff::SourceTextDocumentBinding     *m_binding      = nullptr;
    KSyntaxHighlighting::SyntaxHighlighter *m_highlighter  = nullptr;
    Detail::Gutter                         *m_gutter        = nullptr;
    Markoff::Theme                          m_theme;

    friend class Markoff::Source::Detail::Gutter;
};

} // namespace Markoff::Source
