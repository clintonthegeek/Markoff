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

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat formatFor(const Markoff::SourceSpan &span) const;

    QList<Markoff::SourceSpan> m_spans;
    const Markoff::Theme      *m_theme = nullptr;
};

}  // namespace Markoff::Live
