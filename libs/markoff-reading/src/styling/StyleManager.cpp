// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transplanted from Penelope's stylemanager at:
//   ~/dev/Penelope/src/style/stylemanager.{h,cpp}
// Penelope HEAD at transplant time: 6b9c32344032c9eb54c041970a5a3e2feff7caff
// Penelope is GPL-3.0 (see ~/dev/Penelope/COPYING).
// Adapted for Corbomite's libs/readingview/ — TableStyle / FootnoteStyle
// methods dropped; `populateObsidianDefaults()` preset factory added so
// SectionLayout can run without the caller hand-building the style table.
// Namespace rebadged.
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/styling/StyleManager.h"

#include <QSet>

namespace Corbomite::ReadingView {

StyleManager::StyleManager(QObject *parent)
    : QObject(parent)
{
}

void StyleManager::addParagraphStyle(const ParagraphStyle &style)
{
    m_paraStyles.insert(style.name(), style);
}

void StyleManager::addCharacterStyle(const CharacterStyle &style)
{
    m_charStyles.insert(style.name(), style);
}

ParagraphStyle *StyleManager::paragraphStyle(const QString &name)
{
    auto it = m_paraStyles.find(name);
    return it != m_paraStyles.end() ? &it.value() : nullptr;
}

CharacterStyle *StyleManager::characterStyle(const QString &name)
{
    auto it = m_charStyles.find(name);
    return it != m_charStyles.end() ? &it.value() : nullptr;
}

QStringList StyleManager::paragraphStyleNames() const
{
    return m_paraStyles.keys();
}

QStringList StyleManager::characterStyleNames() const
{
    return m_charStyles.keys();
}

ParagraphStyle StyleManager::resolvedParagraphStyle(const QString &name)
{
    auto it = m_paraStyles.find(name);
    if (it == m_paraStyles.end())
        return ParagraphStyle(name);

    ParagraphStyle resolved = it.value();
    QSet<QString> visited;
    visited.insert(name);

    // Cross-type linkage: fill unset char properties from the referenced
    // character style. Applied BEFORE the paragraph parent chain so the
    // character style's properties (e.g. Code → monospace font) win over
    // inherited paragraph defaults.
    QString baseCharName = it.value().baseCharacterStyleName();
    if (!baseCharName.isEmpty()) {
        CharacterStyle charResolved = resolvedCharacterStyle(baseCharName);
        if (!resolved.hasFontFamily() && charResolved.hasFontFamily())
            resolved.setFontFamily(charResolved.fontFamily());
        if (!resolved.hasFontSize() && charResolved.hasFontSize())
            resolved.setFontSize(charResolved.fontSize());
        if (!resolved.hasFontWeight() && charResolved.hasFontWeight())
            resolved.setFontWeight(charResolved.fontWeight());
        if (!resolved.hasFontItalic() && charResolved.hasFontItalic())
            resolved.setFontItalic(charResolved.fontItalic());
        if (!resolved.hasForeground() && charResolved.hasForeground())
            resolved.setForeground(charResolved.foreground());
    }

    QString parentName = resolved.parentStyleName();
    while (!parentName.isEmpty() && !visited.contains(parentName)) {
        visited.insert(parentName);
        auto pit = m_paraStyles.find(parentName);
        if (pit == m_paraStyles.end())
            break;
        resolved.inheritFrom(pit.value());
        parentName = pit.value().parentStyleName();
    }

    return resolved;
}

CharacterStyle StyleManager::resolvedCharacterStyle(const QString &name)
{
    auto it = m_charStyles.find(name);
    if (it == m_charStyles.end())
        return CharacterStyle(name);

    CharacterStyle resolved = it.value();
    QSet<QString> visited;
    visited.insert(name);

    QString parentName = resolved.parentStyleName();
    while (!parentName.isEmpty() && !visited.contains(parentName)) {
        visited.insert(parentName);
        auto pit = m_charStyles.find(parentName);
        if (pit == m_charStyles.end())
            break;
        resolved.inheritFrom(pit.value());
        parentName = pit.value().parentStyleName();
    }

    return resolved;
}

StyleManager *StyleManager::clone(QObject *parent) const
{
    auto *copy = new StyleManager(parent);
    copy->m_paraStyles = m_paraStyles;
    copy->m_charStyles = m_charStyles;
    return copy;
}

void StyleManager::populateObsidianDefaults(Theme theme)
{
    const QColor fg = (theme == Theme::Dark) ? QColor(220, 221, 222)
                                             : QColor(32, 32, 32);
    const QColor muted = (theme == Theme::Dark) ? QColor(160, 160, 160)
                                                : QColor(100, 100, 100);
    const QColor blockquoteBg = (theme == Theme::Dark) ? QColor(40, 40, 44)
                                                       : QColor(245, 245, 248);
    const QColor codeBg = (theme == Theme::Dark) ? QColor(30, 30, 34)
                                                 : QColor(246, 248, 250);

    // Body
    ParagraphStyle body(QStringLiteral("Body"));
    body.setFontFamily(QStringLiteral("Inter"));
    body.setFontSize(14);
    body.setForeground(fg);
    body.setSpaceAfter(8);
    body.setLineHeightPercent(150);
    m_paraStyles.insert(body.name(), body);

    // Headings
    const qreal headingSizes[6] = { 28, 22, 18, 16, 15, 14 };
    for (int i = 0; i < 6; ++i) {
        const int level = i + 1;
        ParagraphStyle h(QStringLiteral("Heading%1").arg(level));
        h.setParentStyleName(QStringLiteral("Body"));
        h.setFontSize(headingSizes[i]);
        h.setFontWeight(QFont::Bold);
        h.setHeadingLevel(level);
        h.setSpaceBefore(level == 1 ? 16 : 12);
        h.setSpaceAfter(level == 1 ? 10 : 8);
        h.setForeground(fg);
        m_paraStyles.insert(h.name(), h);
    }

    // Code block paragraph
    ParagraphStyle code(QStringLiteral("CodeBlock"));
    code.setFontFamily(QStringLiteral("JetBrains Mono"));
    code.setFontSize(13);
    code.setForeground(fg);
    code.setBackground(codeBg);
    code.setLeftMargin(10);
    code.setRightMargin(10);
    code.setSpaceBefore(6);
    code.setSpaceAfter(10);
    m_paraStyles.insert(code.name(), code);

    // Blockquote
    ParagraphStyle bq(QStringLiteral("Blockquote"));
    bq.setParentStyleName(QStringLiteral("Body"));
    bq.setLeftMargin(16);
    bq.setRightMargin(8);
    bq.setForeground(muted);
    bq.setBackground(blockquoteBg);
    bq.setFontItalic(true);
    m_paraStyles.insert(bq.name(), bq);

    // List item
    ParagraphStyle li(QStringLiteral("ListItem"));
    li.setParentStyleName(QStringLiteral("Body"));
    li.setLeftMargin(24);
    li.setSpaceAfter(4);
    m_paraStyles.insert(li.name(), li);

    // Horizontal rule (used for spacing only; the line is drawn by SectionLayout)
    ParagraphStyle hr(QStringLiteral("HorizontalRule"));
    hr.setSpaceBefore(12);
    hr.setSpaceAfter(12);
    m_paraStyles.insert(hr.name(), hr);

    // Character styles — inline runs
    CharacterStyle emph(QStringLiteral("Emphasis"));
    emph.setFontItalic(true);
    m_charStyles.insert(emph.name(), emph);

    CharacterStyle strong(QStringLiteral("Strong"));
    strong.setFontWeight(QFont::Bold);
    m_charStyles.insert(strong.name(), strong);

    CharacterStyle inlineCode(QStringLiteral("InlineCode"));
    inlineCode.setFontFamily(QStringLiteral("JetBrains Mono"));
    inlineCode.setBackground(codeBg);
    m_charStyles.insert(inlineCode.name(), inlineCode);

    CharacterStyle link(QStringLiteral("Link"));
    link.setForeground(QColor(70, 130, 200));
    link.setFontUnderline(true);
    m_charStyles.insert(link.name(), link);

    // WikiLink: underline + Obsidian-ish purple accent so it's visually
    // distinct from standard links. SectionLayout stores the target in
    // user data (see SpanRenderer::WikiLinkTargetProperty).
    CharacterStyle wikiLink(QStringLiteral("WikiLink"));
    wikiLink.setForeground((theme == Theme::Dark) ? QColor(167, 139, 250)
                                                  : QColor(123, 108, 217));
    wikiLink.setFontUnderline(true);
    m_charStyles.insert(wikiLink.name(), wikiLink);

    // Strikethrough: ~~text~~
    CharacterStyle strike(QStringLiteral("Strikethrough"));
    strike.setFontStrikeOut(true);
    m_charStyles.insert(strike.name(), strike);

    // Highlight: ==text==
    CharacterStyle highlight(QStringLiteral("Highlight"));
    highlight.setBackground((theme == Theme::Dark) ? QColor(100, 90, 40)
                                                   : QColor(255, 243, 176));
    m_charStyles.insert(highlight.name(), highlight);

    // ImageCaption: used for fallback text when an image fails to resolve.
    CharacterStyle imageCaption(QStringLiteral("ImageCaption"));
    imageCaption.setFontItalic(true);
    imageCaption.setForeground(muted);
    m_charStyles.insert(imageCaption.name(), imageCaption);

    // MathInline: reserved style entry for inline-math rendered regions
    // (font-sizing + spacing cues, not used to colour the pixmap itself).
    CharacterStyle mathInline(QStringLiteral("MathInline"));
    mathInline.setFontFamily(QStringLiteral("JetBrains Mono"));
    m_charStyles.insert(mathInline.name(), mathInline);
}

StyleManager *StyleManager::makeObsidianDefault(Theme theme, QObject *parent)
{
    auto *mgr = new StyleManager(parent);
    mgr->populateObsidianDefaults(theme);
    return mgr;
}

} // namespace Corbomite::ReadingView
