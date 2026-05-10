// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/parser/SourceSpan.h>

#include <QList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace Markoff { class Theme; }

namespace Markoff::Live {

/// Per-delegate inline-format painter. Reads BlockRecord::inlineSpans for
/// the bound row and paints the configured Markoff::Theme emphasis tokens
/// via QTextCharFormat. E1 covers 8 flags: bold, italic, strikethrough,
/// code, highlight, isLink, isWikilink, isTag.
class MARKOFF_LIVE_EXPORT InlineHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit InlineHighlighter(QTextDocument *parent);
    ~InlineHighlighter() override;

    void setInlineSpans(const QList<Markoff::SourceSpan> &spans);
    const QList<Markoff::SourceSpan> &inlineSpans() const noexcept { return m_spans; }

    void setTheme(const Markoff::Theme *theme);
    const Markoff::Theme *theme() const noexcept { return m_theme; }

    /// Local caret qtPos within the bound block, or -1 for "no local caret
    /// in this block". Spec §3.1 / §4.4 — peer cursors do NOT use this slot.
    void setLocalCaretPosition(int qtPos);
    int  localCaretPosition() const noexcept { return m_localCaretPos; }

    /// Selection range within the bound block in qtPos coords. -1/-1 = no selection.
    void setSelectionRange(int startQtPos, int endQtPos);
    int  selectionStart() const noexcept { return m_selStart; }
    int  selectionEnd()   const noexcept { return m_selEnd;   }

    void  setFontScale(qreal s);
    qreal fontScale() const noexcept { return m_fontScale; }

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat formatFor(const Markoff::SourceSpan &span) const;
    bool delimiterShouldHide(const Markoff::SourceSpan &span) const;
    QTextCharFormat hiddenFormatForChar(QChar ch) const;

    QList<Markoff::SourceSpan> m_spans;
    const Markoff::Theme      *m_theme = nullptr;
    int   m_localCaretPos = -1;
    int   m_selStart      = -1;
    int   m_selEnd        = -1;
    qreal m_fontScale     = 1.0;
};

}  // namespace Markoff::Live
