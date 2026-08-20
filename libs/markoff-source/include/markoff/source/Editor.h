// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <QPointer>

#include <KSyntaxHighlighting/SyntaxHighlighter>

#include <markoff/core/EditorContext.h>
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
    QRect caretRect() const override;

    // Font scale (spec §8): scales the inner editor font, gutter, and
    // paragraph margins from the captured base size. Clamping + signal
    // are delegated to the base MarkdownView::setFontScale.
    void setFontScale(qreal s) override;

    // Theme
    Markoff::Theme theme() const override;
    void setTheme(const Markoff::Theme &) override;

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
    void attachFindController(Markoff::FindController *fc) override;
    void detachFindController() override;

    // ---- Markdown format operations (parity with Live's LiveFormatController)
    //
    // All operate via the inner QPlainTextEdit's QTextCursor; edits flow
    // through SourceTextDocumentBinding to the document's applyFlatEdit path.
    // Wrap-style ops toggle: if the selection (or cursor's enclosing run) is
    // already wrapped with the delimiters, they're unwrapped; otherwise
    // wrapped. Empty selection inserts the delimiter pair and parks the
    // cursor between them.

    /// Wrap selection with `**...**` (or unwrap if already wrapped).
    Q_INVOKABLE void toggleBold() override;
    /// Wrap selection with `_..._`.
    Q_INVOKABLE void toggleItalic() override;
    /// Wrap selection with `~~...~~`.
    Q_INVOKABLE void toggleStrikethrough() override;
    /// Wrap selection with single backticks.
    Q_INVOKABLE void toggleInlineCode() override;
    /// Insert `[](url)` at cursor or wrap selection as `[selection](url)`.
    Q_INVOKABLE void insertLink() override;
    /// Set the current line's heading level. `level == 0` strips any
    /// leading ATX markers; `level` 1..6 replaces them with that many
    /// hashes + a space. Operates on the line containing the cursor.
    Q_INVOKABLE void setHeadingLevel(int level) override;

    // Clipboard verbs (MarkdownView contract — Cluster N). Default Copy is
    // multi-flavor via InnerEditor::createMimeDataFromSelection; exclusive
    // Copy-as / Paste-as-Plain land here for the host Edit menu.
    void copy() override;
    void copyAsPlain() override;
    void copyAsMarkdown() override;
    void copyAsHtml() override;
    void copyAsRtf() override;
    void cut() override;
    void paste() override;
    void pasteAsPlain() override;

    /// Test-only accessor: the raw-markdown ListItem marker string
    /// currently painted for the QTextBlock at `blockNumber` (queue #8.3).
    /// Empty for non-ListItem blocks and once no document is set.
    QString listItemMarkerForBlock(int blockNumber) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void recomputeGutterWidth();
    int  gutterWidth() const;

    /// Re-apply WP-unification paragraph margins to every QTextBlock after
    /// the binding has settled a reverse-diff. Cheap idempotent pass.
    void applyParagraphMargins();

    /// Reserve left-margin space + refresh the paint-time marker table for
    /// every ListItem QTextBlock (queue #8.3; decoration-only — does not
    /// touch QTextDocument content). Cheap idempotent pass, run alongside
    /// applyParagraphMargins().
    void applyListItemMarkerDecorations();

    /// Recompute the EditorContext from the current caret position and emit
    /// contextChanged if the context has changed (change-gated, spec §7).
    void recomputeContext();

    // Captured on first setFontScale call (lazy: avoids a QFont query in the
    // ctor before the inner editor has its final default font from the style).
    qreal                                    m_baseFontPt   = 0.0;

    QPlainTextEdit                          *m_editor      = nullptr;
    QPointer<Markoff::Session>              m_session;
    Markoff::SourceTextDocumentBinding     *m_binding      = nullptr;
    KSyntaxHighlighting::SyntaxHighlighter *m_highlighter  = nullptr;
    Detail::Gutter                         *m_gutter        = nullptr;
    Detail::SourceFindAdapter              *m_findAdapter   = nullptr;
    Markoff::Theme                          m_theme;
    QMetaObject::Connection                 m_paragraphMarginsCon;
    QMetaObject::Connection                 m_listMarkerCon;
    QMetaObject::Connection                 m_contextCursorCon;
    QMetaObject::Connection                 m_contextD2Con;
    Markoff::EditorContext                  m_lastContext;
    quint64                                 m_lastStructuralSeq = 0;

    friend class Markoff::Source::Detail::Gutter;
};

} // namespace Markoff::Source
