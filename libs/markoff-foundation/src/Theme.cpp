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

// Stubs — filled in subsequent tasks.
QFont Theme::font(FontRole) const { return {}; }
void  Theme::setFont(FontRole, QFont) {}
bool  Theme::isBold(Slot) const { return false; }
void  Theme::setBold(Slot, bool) {}
bool  Theme::isItalic(Slot) const { return false; }
void  Theme::setItalic(Slot, bool) {}
qreal Theme::fontSizeMultiplier(Slot) const { return 1.0; }
void  Theme::setFontSizeMultiplier(Slot, qreal) {}

QColor Theme::colorForCodeToken(CodeTokenKind) const { return color(Slot::CodeBlock); }

Theme Theme::defaultLight() { return Theme{}; }
Theme Theme::defaultDark()  { return Theme{}; }

QJsonObject Theme::toJson() const { return {}; }
Theme Theme::fromJson(const QJsonObject &) { return Theme{}; }

}  // namespace Markoff
