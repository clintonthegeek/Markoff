// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Theme.h>
#include <markoff-foundation/CodeTokenKind.h>

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

QColor Theme::colorForCodeToken(CodeTokenKind k) const
{
    using K = CodeTokenKind;
    switch (k) {
    case K::Keyword:        return color(Slot::CodeKeyword);
    case K::ControlFlow:    return color(Slot::CodeControlFlow);
    case K::Builtin:        return color(Slot::CodeBuiltin);
    case K::Type:           return color(Slot::CodeType);
    case K::Function:       return color(Slot::CodeFunction);
    case K::Variable:       return color(Slot::CodeVariable);
    case K::Constant:       return color(Slot::CodeConstant);
    case K::Operator:       return color(Slot::CodeOperator);
    case K::Punctuation:    return color(Slot::CodePunctuation);
    case K::String:         return color(Slot::CodeString);
    case K::Number:         return color(Slot::CodeNumber);
    case K::Boolean:        return color(Slot::CodeBoolean);
    case K::Comment:        return color(Slot::CodeComment);
    case K::Documentation:  return color(Slot::CodeDocumentation);
    case K::Preprocessor:   return color(Slot::CodePreprocessor);
    case K::Annotation:     return color(Slot::CodeAnnotation);
    case K::Default:        break;
    }
    return color(Slot::CodeBlock);
}

Theme Theme::defaultLight()
{
    Theme t;
    t.setColor(Slot::EditorBackground, QColor("#ffffff"));
    t.setColor(Slot::TextDefault,      QColor("#222222"));
    t.setColor(Slot::Heading1,         QColor("#1a1a1a"));
    t.setColor(Slot::Heading2,         QColor("#1f1f1f"));
    t.setColor(Slot::Heading3,         QColor("#262626"));
    t.setColor(Slot::Heading4,         QColor("#333333"));
    t.setColor(Slot::Heading5,         QColor("#404040"));
    t.setColor(Slot::Heading6,         QColor("#4d4d4d"));
    t.setColor(Slot::Link,             QColor("#0066cc"));
    t.setColor(Slot::WikiLink,         QColor("#5050cc"));
    t.setColor(Slot::Tag,              QColor("#a04080"));
    t.setColor(Slot::Quote,            QColor("#666666"));
    t.setColor(Slot::InlineCode,       QColor("#882020"));
    t.setColor(Slot::CodeBlock,        QColor("#222222"));
    t.setColor(Slot::CodeBlockBackground, QColor("#f4f4f4"));
    t.setColor(Slot::SelectionBackground, QColor("#b0d0ff"));
    t.setColor(Slot::SearchMatchBackground, QColor("#ffe080"));
    t.setColor(Slot::SearchActiveMatchBackground, QColor("#ffb050"));
    t.setBold(Slot::Heading1, true);
    t.setBold(Slot::Heading2, true);
    t.setBold(Slot::BoldEmphasis, true);
    t.setItalic(Slot::ItalicEmphasis, true);
    t.setFontSizeMultiplier(Slot::Heading1, 1.8);
    t.setFontSizeMultiplier(Slot::Heading2, 1.5);
    t.setFontSizeMultiplier(Slot::Heading3, 1.3);
    t.setFont(FontRole::Body, QFont("sans-serif", 11));
    t.setFont(FontRole::Monospace, QFont("monospace", 11));
    t.setFont(FontRole::Heading, QFont("sans-serif", 11));
    return t;
}

Theme Theme::defaultDark()
{
    Theme t = defaultLight();
    t.setColor(Slot::EditorBackground, QColor("#1e1e1e"));
    t.setColor(Slot::TextDefault,      QColor("#e0e0e0"));
    t.setColor(Slot::Heading1,         QColor("#ffffff"));
    t.setColor(Slot::Heading2,         QColor("#f0f0f0"));
    t.setColor(Slot::Heading3,         QColor("#dcdcdc"));
    t.setColor(Slot::Quote,            QColor("#aaaaaa"));
    t.setColor(Slot::CodeBlockBackground, QColor("#2d2d2d"));
    t.setColor(Slot::SelectionBackground, QColor("#264070"));
    return t;
}

QJsonObject Theme::toJson() const
{
    QJsonObject obj;
    QJsonObject colors;
    for (auto it = m_colors.constBegin(); it != m_colors.constEnd(); ++it)
        colors.insert(QString::number(it.key()), it.value().name(QColor::HexArgb));
    obj.insert("colors", colors);

    QJsonObject bolds;
    for (auto it = m_bolds.constBegin(); it != m_bolds.constEnd(); ++it)
        bolds.insert(QString::number(it.key()), it.value());
    obj.insert("bolds", bolds);

    QJsonObject italics;
    for (auto it = m_italics.constBegin(); it != m_italics.constEnd(); ++it)
        italics.insert(QString::number(it.key()), it.value());
    obj.insert("italics", italics);

    QJsonObject sizes;
    for (auto it = m_sizeMul.constBegin(); it != m_sizeMul.constEnd(); ++it)
        sizes.insert(QString::number(it.key()), it.value());
    obj.insert("sizeMul", sizes);

    QJsonObject fonts;
    for (auto it = m_fonts.constBegin(); it != m_fonts.constEnd(); ++it)
        fonts.insert(QString::number(it.key()), it.value().toString());
    obj.insert("fonts", fonts);

    return obj;
}

Theme Theme::fromJson(const QJsonObject &obj)
{
    Theme t;
    const QJsonObject colors = obj.value("colors").toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it)
        t.m_colors[it.key().toInt()] = QColor(it.value().toString());

    const QJsonObject bolds = obj.value("bolds").toObject();
    for (auto it = bolds.begin(); it != bolds.end(); ++it)
        t.m_bolds[it.key().toInt()] = it.value().toBool();

    const QJsonObject italics = obj.value("italics").toObject();
    for (auto it = italics.begin(); it != italics.end(); ++it)
        t.m_italics[it.key().toInt()] = it.value().toBool();

    const QJsonObject sizes = obj.value("sizeMul").toObject();
    for (auto it = sizes.begin(); it != sizes.end(); ++it)
        t.m_sizeMul[it.key().toInt()] = it.value().toDouble();

    const QJsonObject fonts = obj.value("fonts").toObject();
    for (auto it = fonts.begin(); it != fonts.end(); ++it) {
        QFont f; f.fromString(it.value().toString());
        t.m_fonts[it.key().toInt()] = f;
    }
    return t;
}

}  // namespace Markoff
