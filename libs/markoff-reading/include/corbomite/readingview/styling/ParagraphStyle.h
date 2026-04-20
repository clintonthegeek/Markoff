// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transplanted from Penelope's paragraphstyle at:
//   ~/dev/Penelope/src/style/paragraphstyle.{h,cpp}
// Penelope HEAD at transplant time: 6b9c32344032c9eb54c041970a5a3e2feff7caff
// Penelope is GPL-3.0 (see ~/dev/Penelope/COPYING).
// Adapted for Corbomite's libs/readingview/ — print/PDF-specific fields
// stripped (page templates, footnote layout, master-page coupling, OpenType
// font-features); namespace rebadged to Corbomite::ReadingView; cross-type
// character-style base reference preserved for the StyleManager resolve path.
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_PARAGRAPHSTYLE_H
#define CORBOMITE_READINGVIEW_PARAGRAPHSTYLE_H

#include <QColor>
#include <QFont>
#include <QString>
#include <QTextBlockFormat>
#include <QTextCharFormat>

namespace Corbomite::ReadingView {

class ParagraphStyle
{
public:
    ParagraphStyle() = default;
    explicit ParagraphStyle(const QString &name);

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // Parent style for cascading hierarchy
    QString parentStyleName() const { return m_parentStyleName; }
    void setParentStyleName(const QString &name) { m_parentStyleName = name; }

    // Base character style — a character style whose text properties (font,
    // etc.) fill in unset char properties after the paragraph parent chain
    // walk.
    QString baseCharacterStyleName() const { return m_baseCharStyleName; }
    void setBaseCharacterStyleName(const QString &name) { m_baseCharStyleName = name; }
    bool hasBaseCharacterStyle() const { return !m_baseCharStyleName.isEmpty(); }

    // Block formatting — setters
    void setAlignment(Qt::Alignment align) { m_alignment = align; m_hasAlignment = true; }
    void setSpaceBefore(qreal pts) { m_spaceBefore = pts; m_hasSpaceBefore = true; }
    void setSpaceAfter(qreal pts) { m_spaceAfter = pts; m_hasSpaceAfter = true; }
    void setLeftMargin(qreal pts) { m_leftMargin = pts; m_hasLeftMargin = true; }
    void setRightMargin(qreal pts) { m_rightMargin = pts; m_hasRightMargin = true; }
    void setLineHeightPercent(int pct) { m_lineHeightPct = pct; m_hasLineHeight = true; }
    void setBackground(const QColor &c) { m_background = c; m_hasBackground = true; }
    void setHeadingLevel(int level) { m_headingLevel = level; }
    void setFirstLineIndent(qreal pts) { m_firstLineIndent = pts; m_hasFirstLineIndent = true; }

    // Block formatting — getters
    Qt::Alignment alignment() const { return m_alignment; }
    qreal spaceBefore() const { return m_spaceBefore; }
    qreal spaceAfter() const { return m_spaceAfter; }
    qreal leftMargin() const { return m_leftMargin; }
    qreal rightMargin() const { return m_rightMargin; }
    int lineHeightPercent() const { return m_lineHeightPct; }
    QColor background() const { return m_background; }
    int headingLevel() const { return m_headingLevel; }
    qreal firstLineIndent() const { return m_firstLineIndent; }

    // Block formatting — has* flags
    bool hasAlignment() const { return m_hasAlignment; }
    bool hasSpaceBefore() const { return m_hasSpaceBefore; }
    bool hasSpaceAfter() const { return m_hasSpaceAfter; }
    bool hasLeftMargin() const { return m_hasLeftMargin; }
    bool hasRightMargin() const { return m_hasRightMargin; }
    bool hasLineHeight() const { return m_hasLineHeight; }
    bool hasBackground() const { return m_hasBackground; }
    bool hasFirstLineIndent() const { return m_hasFirstLineIndent; }

    // Character formatting (inherited by text in this paragraph) — setters
    void setFontFamily(const QString &family) { m_fontFamily = family; m_hasFontFamily = true; }
    void setFontSize(qreal pts) { m_fontSize = pts; m_hasFontSize = true; }
    void setFontWeight(QFont::Weight w) { m_fontWeight = w; m_hasFontWeight = true; }
    void setFontItalic(bool on) { m_fontItalic = on; m_hasFontItalic = true; }
    void setForeground(const QColor &c) { m_foreground = c; m_hasForeground = true; }

    // Character formatting — getters
    QString fontFamily() const { return m_fontFamily; }
    qreal fontSize() const { return m_fontSize; }
    QFont::Weight fontWeight() const { return m_fontWeight; }
    bool fontItalic() const { return m_fontItalic; }
    QColor foreground() const { return m_foreground; }

    // Character formatting — has* flags
    bool hasFontFamily() const { return m_hasFontFamily; }
    bool hasFontSize() const { return m_hasFontSize; }
    bool hasFontWeight() const { return m_hasFontWeight; }
    bool hasFontItalic() const { return m_hasFontItalic; }
    bool hasForeground() const { return m_hasForeground; }

    // Apply to QTextBlockFormat + QTextCharFormat
    void applyBlockFormat(QTextBlockFormat &bf) const;
    void applyCharFormat(QTextCharFormat &cf) const;

    // Inherit unset properties from parent
    void inheritFrom(const ParagraphStyle &parent);

private:
    QString m_name;
    QString m_parentStyleName;
    QString m_baseCharStyleName;

    // Block properties
    Qt::Alignment m_alignment = Qt::AlignLeft;
    qreal m_spaceBefore = 0;
    qreal m_spaceAfter = 0;
    qreal m_leftMargin = 0;
    qreal m_rightMargin = 0;
    int m_lineHeightPct = 100;
    QColor m_background;
    int m_headingLevel = 0;
    qreal m_firstLineIndent = 0;

    bool m_hasAlignment = false;
    bool m_hasSpaceBefore = false;
    bool m_hasSpaceAfter = false;
    bool m_hasLeftMargin = false;
    bool m_hasRightMargin = false;
    bool m_hasLineHeight = false;
    bool m_hasBackground = false;
    bool m_hasFirstLineIndent = false;

    // Char properties
    QString m_fontFamily;
    qreal m_fontSize = 0;
    QFont::Weight m_fontWeight = QFont::Normal;
    bool m_fontItalic = false;
    QColor m_foreground;

    bool m_hasFontFamily = false;
    bool m_hasFontSize = false;
    bool m_hasFontWeight = false;
    bool m_hasFontItalic = false;
    bool m_hasForeground = false;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_PARAGRAPHSTYLE_H
