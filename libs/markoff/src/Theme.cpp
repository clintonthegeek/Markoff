// libs/markoff/src/Theme.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/Theme.h"
#include <QFontDatabase>
#include <QSettings>

namespace Markoff {

// Helper to create a format with foreground color
static QTextCharFormat fgFormat(const QColor &color)
{
    QTextCharFormat fmt;
    fmt.setForeground(color);
    return fmt;
}

// Helper to create a format with foreground + background
static QTextCharFormat fgBgFormat(const QColor &fg, const QColor &bg)
{
    QTextCharFormat fmt;
    fmt.setForeground(fg);
    fmt.setBackground(bg);
    return fmt;
}

// Helper to create a heading format
static QTextCharFormat headingFormat(const QColor &fg, const QColor &bg,
                                      const QFont &baseFont, int sizePercent)
{
    QTextCharFormat fmt;
    fmt.setForeground(fg);
    fmt.setBackground(bg);
    fmt.setFontWeight(QFont::Bold);
    QFont font = baseFont;
    font.setPointSize(qRound(baseFont.pointSize() * sizePercent / 100.0));
    fmt.setFont(font, QTextCharFormat::FontPropertiesSpecifiedOnly);
    // Re-set weight after setFont since setFont may reset it
    fmt.setFontWeight(QFont::Bold);
    return fmt;
}

Theme Theme::defaultLight()
{
    Theme t;
    t.textFont = QFont();
    t.textFont.setPointSize(14);
    t.codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    t.codeFont.setPointSize(14);

    // --- Base ---
    t.formats[Element::Text] = fgBgFormat(QColor(0, 0, 0), QColor(255, 255, 255));

    QTextCharFormat currentLineFmt;
    currentLineFmt.setBackground(QColor(255, 250, 226)); // #fffae2
    t.formats[Element::CurrentLineBackground] = currentLineFmt;

    // --- Headings (dark blue on light gray, bold, scaled sizes) ---
    QColor headFg(0, 69, 153);      // #004599
    QColor headBg(241, 241, 244);   // #f1f1f4
    t.formats[Element::H1] = headingFormat(headFg, headBg, t.textFont, 200);
    t.formats[Element::H2] = headingFormat(headFg, headBg, t.textFont, 160);
    t.formats[Element::H3] = headingFormat(headFg, headBg, t.textFont, 130);
    t.formats[Element::H4] = headingFormat(headFg, headBg, t.textFont, 100);
    t.formats[Element::H5] = headingFormat(headFg, headBg, t.textFont, 90);
    t.formats[Element::H6] = headingFormat(headFg, headBg, t.textFont, 90);

    // --- Inline formatting ---
    QTextCharFormat boldFmt;
    boldFmt.setFontWeight(QFont::Bold);
    t.formats[Element::Bold] = boldFmt;

    QTextCharFormat italicFmt;
    italicFmt.setFontItalic(true);
    t.formats[Element::Italic] = italicFmt;

    QTextCharFormat strikeFmt;
    strikeFmt.setFontStrikeOut(true);
    strikeFmt.setForeground(QColor(150, 150, 150));
    t.formats[Element::Strikethrough] = strikeFmt;

    QTextCharFormat inlineCodeFmt;
    inlineCodeFmt.setForeground(QColor(0, 128, 0));     // #008000
    inlineCodeFmt.setBackground(QColor(237, 252, 237));  // #edfced
    inlineCodeFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::InlineCode] = inlineCodeFmt;

    QTextCharFormat mathFmt;
    mathFmt.setForeground(QColor(46, 125, 50));         // #2E7D32 darker green
    mathFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::Math] = mathFmt;

    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(QColor(255, 249, 196));   // #fff9c4
    t.formats[Element::Highlight] = highlightFmt;

    QTextCharFormat commentFmt;
    commentFmt.setForeground(QColor(150, 150, 150));
    t.formats[Element::Comment] = commentFmt;

    t.formats[Element::Tag] = fgFormat(QColor(230, 81, 0)); // #E65100

    QTextCharFormat footnoteRefFmt;
    footnoteRefFmt.setForeground(QColor(21, 101, 192)); // #1565C0 blue
    footnoteRefFmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    t.formats[Element::FootnoteRef] = footnoteRefFmt;

    // --- Links ---
    QTextCharFormat linkFmt;
    linkFmt.setForeground(QColor(252, 109, 0));   // #fc6d00
    linkFmt.setFontUnderline(true);
    t.formats[Element::Link] = linkFmt;

    QTextCharFormat wikiLinkFmt;
    wikiLinkFmt.setForeground(QColor(0, 137, 123));  // #00897b teal
    wikiLinkFmt.setFontUnderline(true);
    t.formats[Element::WikiLink] = wikiLinkFmt;

    QTextCharFormat brokenLinkFmt;
    brokenLinkFmt.setForeground(QColor(211, 47, 47));  // #d32f2f red
    brokenLinkFmt.setFontUnderline(true);
    t.formats[Element::BrokenLink] = brokenLinkFmt;

    t.formats[Element::Image] = fgFormat(QColor(0, 137, 123)); // same as wikilink

    // --- Block-level ---
    QTextCharFormat codeBlockFmt;
    codeBlockFmt.setForeground(QColor(0, 128, 0));
    codeBlockFmt.setBackground(QColor(237, 252, 237));
    codeBlockFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::CodeBlock] = codeBlockFmt;

    t.formats[Element::BlockQuote] = fgFormat(QColor(111, 159, 0)); // #6f9f00
    t.formats[Element::HorizontalRule] = fgBgFormat(QColor(180, 180, 180), QColor(235, 235, 235));
    t.formats[Element::ListMarker] = fgFormat(QColor(163, 0, 123)); // #a3007b
    t.formats[Element::Table] = fgBgFormat(QColor(99, 109, 239), QColor(247, 246, 255)); // #636def on #f7f6ff
    t.formats[Element::FrontmatterBlock] = fgBgFormat(QColor(120, 144, 156), QColor(245, 245, 245));
    t.formats[Element::Callout] = fgFormat(QColor(68, 138, 255)); // #448aff

    // --- Checkboxes ---
    t.formats[Element::CheckboxUnchecked] = fgFormat(QColor(120, 120, 120));
    QTextCharFormat checkedFmt;
    checkedFmt.setForeground(QColor(76, 175, 80)); // #4caf50 green
    checkedFmt.setFontStrikeOut(true);
    t.formats[Element::CheckboxChecked] = checkedFmt;

    // --- Code syntax ---
    QColor codeBg(237, 252, 237);
    t.formats[Element::CodeKeyword] = fgBgFormat(QColor(249, 38, 114), codeBg);  // #f92672
    t.formats[Element::CodeString]  = fgBgFormat(QColor(59, 162, 63), codeBg);   // #3ba23f
    t.formats[Element::CodeComment] = fgBgFormat(QColor(144, 139, 116), codeBg); // #908b74
    t.formats[Element::CodeType]    = fgBgFormat(QColor(99, 109, 239), codeBg);  // #636def
    t.formats[Element::CodeNumLiteral] = fgBgFormat(QColor(181, 124, 80), codeBg); // #b57c50
    t.formats[Element::CodeBuiltIn] = fgBgFormat(QColor(0, 134, 179), codeBg);   // #0086b3
    t.formats[Element::CodeOther]   = fgBgFormat(QColor(80, 80, 80), codeBg);

    // --- Misc ---
    t.formats[Element::MaskedSyntax] = fgFormat(QColor(200, 200, 200));

    QTextCharFormat trailingFmt;
    trailingFmt.setBackground(QColor(235, 235, 235));
    t.formats[Element::TrailingSpace] = trailingFmt;

    return t;
}

Theme Theme::defaultDark()
{
    Theme t;
    t.textFont = QFont();
    t.textFont.setPointSize(14);
    t.codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    t.codeFont.setPointSize(14);

    // --- Base ---
    t.formats[Element::Text] = fgBgFormat(QColor(210, 210, 210), QColor(40, 44, 52));

    QTextCharFormat currentLineFmt;
    currentLineFmt.setBackground(QColor(50, 55, 65));
    t.formats[Element::CurrentLineBackground] = currentLineFmt;

    // --- Headings ---
    QColor headFg(130, 170, 255);
    QColor headBg(45, 50, 60);
    t.formats[Element::H1] = headingFormat(headFg, headBg, t.textFont, 200);
    t.formats[Element::H2] = headingFormat(headFg, headBg, t.textFont, 160);
    t.formats[Element::H3] = headingFormat(headFg, headBg, t.textFont, 130);
    t.formats[Element::H4] = headingFormat(headFg, headBg, t.textFont, 100);
    t.formats[Element::H5] = headingFormat(headFg, headBg, t.textFont, 90);
    t.formats[Element::H6] = headingFormat(headFg, headBg, t.textFont, 90);

    // --- Inline formatting ---
    QTextCharFormat boldFmt;
    boldFmt.setFontWeight(QFont::Bold);
    boldFmt.setForeground(QColor(230, 230, 230));
    t.formats[Element::Bold] = boldFmt;

    QTextCharFormat italicFmt;
    italicFmt.setFontItalic(true);
    italicFmt.setForeground(QColor(220, 220, 220));
    t.formats[Element::Italic] = italicFmt;

    QTextCharFormat strikeFmt;
    strikeFmt.setFontStrikeOut(true);
    strikeFmt.setForeground(QColor(120, 120, 120));
    t.formats[Element::Strikethrough] = strikeFmt;

    QTextCharFormat inlineCodeFmt;
    inlineCodeFmt.setForeground(QColor(130, 200, 130));
    inlineCodeFmt.setBackground(QColor(50, 60, 50));
    inlineCodeFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::InlineCode] = inlineCodeFmt;

    QTextCharFormat mathFmt;
    mathFmt.setForeground(QColor(165, 220, 165));
    mathFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::Math] = mathFmt;

    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(QColor(100, 90, 40));
    highlightFmt.setForeground(QColor(255, 249, 196));
    t.formats[Element::Highlight] = highlightFmt;

    QTextCharFormat commentFmt;
    commentFmt.setForeground(QColor(100, 100, 100));
    t.formats[Element::Comment] = commentFmt;

    t.formats[Element::Tag] = fgFormat(QColor(255, 152, 0)); // #ff9800

    QTextCharFormat footnoteRefFmt;
    footnoteRefFmt.setForeground(QColor(100, 181, 246)); // #64B5F6 lighter blue
    footnoteRefFmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    t.formats[Element::FootnoteRef] = footnoteRefFmt;

    // --- Links ---
    QTextCharFormat linkFmt;
    linkFmt.setForeground(QColor(255, 152, 60));
    linkFmt.setFontUnderline(true);
    t.formats[Element::Link] = linkFmt;

    QTextCharFormat wikiLinkFmt;
    wikiLinkFmt.setForeground(QColor(77, 208, 191));
    wikiLinkFmt.setFontUnderline(true);
    t.formats[Element::WikiLink] = wikiLinkFmt;

    QTextCharFormat brokenLinkFmt;
    brokenLinkFmt.setForeground(QColor(239, 83, 80));
    brokenLinkFmt.setFontUnderline(true);
    t.formats[Element::BrokenLink] = brokenLinkFmt;

    t.formats[Element::Image] = fgFormat(QColor(77, 208, 191));

    // --- Block-level ---
    QTextCharFormat codeBlockFmt;
    codeBlockFmt.setForeground(QColor(130, 200, 130));
    codeBlockFmt.setBackground(QColor(50, 60, 50));
    codeBlockFmt.setFont(t.codeFont, QTextCharFormat::FontPropertiesSpecifiedOnly);
    t.formats[Element::CodeBlock] = codeBlockFmt;

    t.formats[Element::BlockQuote] = fgFormat(QColor(160, 200, 80));
    t.formats[Element::HorizontalRule] = fgBgFormat(QColor(100, 100, 100), QColor(55, 60, 68));
    t.formats[Element::ListMarker] = fgFormat(QColor(200, 120, 200));
    t.formats[Element::Table] = fgBgFormat(QColor(150, 160, 255), QColor(45, 48, 60));
    t.formats[Element::FrontmatterBlock] = fgBgFormat(QColor(140, 155, 165), QColor(45, 48, 55));
    t.formats[Element::Callout] = fgFormat(QColor(100, 160, 255));

    // --- Checkboxes ---
    t.formats[Element::CheckboxUnchecked] = fgFormat(QColor(160, 160, 160));
    QTextCharFormat checkedFmt;
    checkedFmt.setForeground(QColor(100, 200, 100));
    checkedFmt.setFontStrikeOut(true);
    t.formats[Element::CheckboxChecked] = checkedFmt;

    // --- Code syntax ---
    QColor codeBg(50, 60, 50);
    t.formats[Element::CodeKeyword] = fgBgFormat(QColor(249, 38, 114), codeBg);
    t.formats[Element::CodeString]  = fgBgFormat(QColor(152, 195, 121), codeBg);
    t.formats[Element::CodeComment] = fgBgFormat(QColor(92, 99, 112), codeBg);
    t.formats[Element::CodeType]    = fgBgFormat(QColor(150, 160, 255), codeBg);
    t.formats[Element::CodeNumLiteral] = fgBgFormat(QColor(209, 154, 102), codeBg);
    t.formats[Element::CodeBuiltIn] = fgBgFormat(QColor(86, 182, 194), codeBg);
    t.formats[Element::CodeOther]   = fgBgFormat(QColor(190, 190, 190), codeBg);

    // --- Misc ---
    t.formats[Element::MaskedSyntax] = fgFormat(QColor(80, 80, 80));

    QTextCharFormat trailingFmt;
    trailingFmt.setBackground(QColor(55, 60, 68));
    t.formats[Element::TrailingSpace] = trailingFmt;

    return t;
}

Theme Theme::fromSchemeFile(const QString &path)
{
    QSettings settings(path, QSettings::IniFormat);

    // Find the first schema key
    QString schemaList = settings.value(QStringLiteral("Editor/DefaultColorSchemes")).toString();
    if (schemaList.isEmpty())
        return defaultLight();

    QString schemaKey = schemaList.split(QStringLiteral(",")).first().trimmed();
    if (schemaKey.isEmpty())
        return defaultLight();

    Theme t;
    t.textFont = QFont();
    t.textFont.setPointSize(14);
    t.codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    t.codeFont.setPointSize(14);

    settings.beginGroup(schemaKey);

    // Map QOwnNotes indices to Markoff elements
    struct IndexMapping {
        int qonIndex;
        Element element;
    };
    static const IndexMapping mappings[] = {
        {-1, Element::Text},
        {0,  Element::Link},
        {4,  Element::CodeBlock},
        {7,  Element::Italic},
        {8,  Element::Bold},
        {9,  Element::ListMarker},
        {11, Element::Comment},
        {12, Element::H1},
        {13, Element::H2},
        {14, Element::H3},
        {15, Element::H4},
        {16, Element::H5},
        {17, Element::H6},
        {18, Element::BlockQuote},
        {21, Element::HorizontalRule},
        {22, Element::Table},
        {23, Element::InlineCode},
        {24, Element::MaskedSyntax},
        {25, Element::CurrentLineBackground},
        {26, Element::BrokenLink},
        {27, Element::FrontmatterBlock},
        {28, Element::TrailingSpace},
        {29, Element::CheckboxUnchecked},
        {30, Element::CheckboxChecked},
        {1000, Element::CodeKeyword},
        {1001, Element::CodeString},
        {1002, Element::CodeComment},
        {1003, Element::CodeType},
        {1004, Element::CodeOther},
        {1005, Element::CodeNumLiteral},
        {1006, Element::CodeBuiltIn},
    };

    for (const auto &m : mappings) {
        QTextCharFormat fmt;
        QString idx = QString::number(m.qonIndex);

        // Foreground
        bool fgEnabled = settings.value(
            QStringLiteral("ForegroundColorEnabled_%1").arg(idx), false).toBool();
        if (fgEnabled) {
            QColor color = settings.value(
                QStringLiteral("ForegroundColor_%1").arg(idx)).value<QColor>();
            if (color.isValid())
                fmt.setForeground(color);
        }

        // Background
        bool bgEnabled = settings.value(
            QStringLiteral("BackgroundColorEnabled_%1").arg(idx), false).toBool();
        if (bgEnabled) {
            QColor color = settings.value(
                QStringLiteral("BackgroundColor_%1").arg(idx)).value<QColor>();
            if (color.isValid())
                fmt.setBackground(color);
        }

        // Bold
        if (settings.value(QStringLiteral("Bold_%1").arg(idx), false).toBool())
            fmt.setFontWeight(QFont::Bold);

        // Italic
        if (settings.value(QStringLiteral("Italic_%1").arg(idx), false).toBool())
            fmt.setFontItalic(true);

        // Underline
        if (settings.value(QStringLiteral("Underline_%1").arg(idx), false).toBool())
            fmt.setFontUnderline(true);

        // Font size adaptation (for headings)
        int sizeAdapt = settings.value(
            QStringLiteral("FontSizeAdaption_%1").arg(idx), 100).toInt();
        if (sizeAdapt != 100) {
            QFont font = t.textFont;
            font.setPointSize(qRound(t.textFont.pointSize() * sizeAdapt / 100.0));
            fmt.setFont(font, QTextCharFormat::FontPropertiesSpecifiedOnly);
            // Re-apply bold if it was set (setFont may clear it)
            if (settings.value(QStringLiteral("Bold_%1").arg(idx), false).toBool())
                fmt.setFontWeight(QFont::Bold);
        }

        // Code-related elements get monospace font family (preserves size adaptation)
        if (m.qonIndex == 4 || m.qonIndex == 23 ||
            (m.qonIndex >= 1000 && m.qonIndex <= 1006)) {
            fmt.setFontFamilies(t.codeFont.families());
        }

        t.formats[m.element] = fmt;
    }

    settings.endGroup();

    // Elements not in QOwnNotes — fill with sensible defaults
    if (!t.formats.contains(Element::Highlight)) {
        QTextCharFormat hlFmt;
        hlFmt.setBackground(QColor(255, 249, 196));
        t.formats[Element::Highlight] = hlFmt;
    }
    if (!t.formats.contains(Element::Tag)) {
        t.formats[Element::Tag] = fgFormat(QColor(230, 81, 0));
    }
    if (!t.formats.contains(Element::WikiLink)) {
        QTextCharFormat wlFmt;
        wlFmt.setForeground(QColor(0, 137, 123));
        wlFmt.setFontUnderline(true);
        t.formats[Element::WikiLink] = wlFmt;
    }
    if (!t.formats.contains(Element::Image)) {
        t.formats[Element::Image] = fgFormat(QColor(0, 137, 123));
    }
    if (!t.formats.contains(Element::Callout)) {
        t.formats[Element::Callout] = fgFormat(QColor(68, 138, 255));
    }
    if (!t.formats.contains(Element::Strikethrough)) {
        QTextCharFormat sFmt;
        sFmt.setFontStrikeOut(true);
        sFmt.setForeground(QColor(150, 150, 150));
        t.formats[Element::Strikethrough] = sFmt;
    }
    if (!t.formats.contains(Element::Math)) {
        QTextCharFormat mFmt;
        mFmt.setForeground(QColor(46, 125, 50));
        mFmt.setFontFamilies(t.codeFont.families());
        t.formats[Element::Math] = mFmt;
    }
    if (!t.formats.contains(Element::FootnoteRef)) {
        QTextCharFormat fFmt;
        fFmt.setForeground(QColor(21, 101, 192));
        fFmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        t.formats[Element::FootnoteRef] = fFmt;
    }

    return t;
}

} // namespace Markoff
