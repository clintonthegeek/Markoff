// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <QPointer>

#include <KSyntaxHighlighting/SyntaxHighlighter>

#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/core/Theme.h>

namespace Markoff { class FindController; }

namespace Markoff::Source {

namespace Detail { class Gutter; class SourceFindAdapter; }

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

    // Theme (source-specific)
    Markoff::Theme theme() const;
    void setTheme(const Markoff::Theme &);

    // Accessor to the inner QPlainTextEdit (for Gutter, find adapter, and tests)
    QPlainTextEdit *plainTextEdit() const { return m_editor; }

    // Forwarding methods for QPlainTextEdit API used by Gutter, find adapter, tests
    QString toPlainText() const { return m_editor->toPlainText(); }
    QList<QTextEdit::ExtraSelection> extraSelections() const { return m_editor->extraSelections(); }
    void setExtraSelections(const QList<QTextEdit::ExtraSelection> &sels) { m_editor->setExtraSelections(sels); }
    QTextCursor textCursor() const { return m_editor->textCursor(); }
    void setTextCursor(const QTextCursor &c) { m_editor->setTextCursor(c); }
    void ensureCursorVisible() { m_editor->ensureCursorVisible(); }

    /// Attaches a Markoff::FindController. The Editor renders its matches
    /// as ExtraSelections on the inner QPlainTextEdit and seeks to them
    /// on navigation; focus stays with whatever widget currently has it
    /// (the consumer's find input). Owned by the consumer; pass nullptr
    /// to detach.
    void attachFindController(Markoff::FindController *fc);
    void detachFindController();

    // ---- Markdown format operations (parity with Live's LiveFormatController)
    //
    // All operate via the inner QPlainTextEdit's QTextCursor; edits flow
    // through SourceTextDocumentBinding to the document's applyFlatEdit path.
    // Wrap-style ops toggle: if the selection (or cursor's enclosing run) is
    // already wrapped with the delimiters, they're unwrapped; otherwise
    // wrapped. Empty selection inserts the delimiter pair and parks the
    // cursor between them.

    /// Wrap selection with `**...**` (or unwrap if already wrapped).
    Q_INVOKABLE void toggleBold();
    /// Wrap selection with `_..._`.
    Q_INVOKABLE void toggleItalic();
    /// Wrap selection with `~~...~~`.
    Q_INVOKABLE void toggleStrikethrough();
    /// Wrap selection with single backticks.
    Q_INVOKABLE void toggleInlineCode();
    /// Insert `[](url)` at cursor or wrap selection as `[selection](url)`.
    Q_INVOKABLE void insertLink();
    /// Set the current line's heading level. `level == 0` strips any
    /// leading ATX markers; `level` 1..6 replaces them with that many
    /// hashes + a space. Operates on the line containing the cursor.
    Q_INVOKABLE void setHeadingLevel(int level);

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
    Detail::SourceFindAdapter              *m_findAdapter   = nullptr;
    Markoff::Theme                          m_theme;

    friend class Markoff::Source::Detail::Gutter;
};

} // namespace Markoff::Source
