// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Theme.h>

namespace Markoff {

QColor Theme::color(Slot s) const
{
    const auto it = m_colors.constFind(static_cast<int>(s));
    if (it != m_colors.constEnd()) return it.value();
    // Fallback to TextDefault.
    const auto td = m_colors.constFind(static_cast<int>(Slot::TextDefault));
    if (td != m_colors.constEnd()) return td.value();
    return QColor(Qt::black);
}

void Theme::setColor(Slot s, QColor c) { m_colors[static_cast<int>(s)] = std::move(c); }

QFont Theme::font(FontRole r) const
{
    const auto it = m_fonts.constFind(static_cast<int>(r));
    return it != m_fonts.constEnd() ? it.value() : QFont{};
}
void Theme::setFont(FontRole r, QFont f) { m_fonts[static_cast<int>(r)] = std::move(f); }

bool Theme::isBold(Slot s) const { return m_bolds.value(static_cast<int>(s), false); }
void Theme::setBold(Slot s, bool b) { m_bolds[static_cast<int>(s)] = b; }

bool Theme::isItalic(Slot s) const { return m_italics.value(static_cast<int>(s), false); }
void Theme::setItalic(Slot s, bool b) { m_italics[static_cast<int>(s)] = b; }

qreal Theme::fontSizeMultiplier(Slot s) const
{
    return m_sizeMul.value(static_cast<int>(s), 1.0);
}
void Theme::setFontSizeMultiplier(Slot s, qreal m)
{
    m_sizeMul[static_cast<int>(s)] = m;
}

QColor Theme::colorForCodeToken(CodeTokenKind) const { return color(Slot::CodeBlock); }

Theme Theme::defaultLight() { return Theme{}; }
Theme Theme::defaultDark()  { return Theme{}; }

QJsonObject Theme::toJson() const { return {}; }
Theme Theme::fromJson(const QJsonObject &) { return Theme{}; }

}  // namespace Markoff
