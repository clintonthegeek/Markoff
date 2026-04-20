// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transplanted from Penelope's characterstyle at:
//   ~/dev/Penelope/src/style/characterstyle.{h,cpp}
// Penelope HEAD at transplant time: 6b9c32344032c9eb54c041970a5a3e2feff7caff
// Penelope is GPL-3.0 (see ~/dev/Penelope/COPYING).
// Adapted for Corbomite's libs/readingview/ — OpenType font-features stripped
// (Phase 3a does not need them); namespace rebadged to Corbomite::ReadingView.
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_CHARACTERSTYLE_H
#define CORBOMITE_READINGVIEW_CHARACTERSTYLE_H

#include <QColor>
#include <QFont>
#include <QString>
#include <QTextCharFormat>

namespace Corbomite::ReadingView {

class CharacterStyle
{
public:
    CharacterStyle() = default;
    explicit CharacterStyle(const QString &name);

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // Parent style for cascading hierarchy
    QString parentStyleName() const { return m_parentStyleName; }
    void setParentStyleName(const QString &name) { m_parentStyleName = name; }

    // Setters
    void setFontFamily(const QString &family) { m_fontFamily = family; m_hasFontFamily = true; }
    void setFontSize(qreal pts) { m_fontSize = pts; m_hasFontSize = true; }
    void setFontWeight(QFont::Weight w) { m_fontWeight = w; m_hasFontWeight = true; }
    void setFontItalic(bool on) { m_fontItalic = on; m_hasFontItalic = true; }
    void setFontUnderline(bool on) { m_fontUnderline = on; m_hasFontUnderline = true; }
    void setFontStrikeOut(bool on) { m_fontStrikeOut = on; m_hasFontStrikeOut = true; }
    void setForeground(const QColor &c) { m_foreground = c; m_hasForeground = true; }
    void setBackground(const QColor &c) { m_background = c; m_hasBackground = true; }
    void setLetterSpacing(qreal pts) { m_letterSpacing = pts; m_hasLetterSpacing = true; }

    // Getters
    QString fontFamily() const { return m_fontFamily; }
    qreal fontSize() const { return m_fontSize; }
    QFont::Weight fontWeight() const { return m_fontWeight; }
    bool fontItalic() const { return m_fontItalic; }
    bool fontUnderline() const { return m_fontUnderline; }
    bool fontStrikeOut() const { return m_fontStrikeOut; }
    QColor foreground() const { return m_foreground; }
    QColor background() const { return m_background; }
    qreal letterSpacing() const { return m_letterSpacing; }

    // Has* flags
    bool hasFontFamily() const { return m_hasFontFamily; }
    bool hasFontSize() const { return m_hasFontSize; }
    bool hasFontWeight() const { return m_hasFontWeight; }
    bool hasFontItalic() const { return m_hasFontItalic; }
    bool hasFontUnderline() const { return m_hasFontUnderline; }
    bool hasFontStrikeOut() const { return m_hasFontStrikeOut; }
    bool hasForeground() const { return m_hasForeground; }
    bool hasBackground() const { return m_hasBackground; }
    bool hasLetterSpacing() const { return m_hasLetterSpacing; }

    // Apply to QTextCharFormat (merge -- does not reset existing properties)
    void applyFormat(QTextCharFormat &cf) const;

    // Inherit unset properties from parent
    void inheritFrom(const CharacterStyle &parent);

private:
    QString m_name;
    QString m_parentStyleName;
    QString m_fontFamily;
    qreal m_fontSize = 0;
    QFont::Weight m_fontWeight = QFont::Normal;
    bool m_fontItalic = false;
    bool m_fontUnderline = false;
    bool m_fontStrikeOut = false;
    QColor m_foreground;
    QColor m_background;

    bool m_hasFontFamily = false;
    bool m_hasFontSize = false;
    bool m_hasFontWeight = false;
    bool m_hasFontItalic = false;
    bool m_hasFontUnderline = false;
    bool m_hasFontStrikeOut = false;
    bool m_hasForeground = false;
    bool m_hasBackground = false;
    qreal m_letterSpacing = 0;
    bool m_hasLetterSpacing = false;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_CHARACTERSTYLE_H
