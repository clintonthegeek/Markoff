// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QTextCharFormat>
#include <QtCore/qmetatype.h>

#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

enum class CodeTokenKind;  // forward (defined in CodeTokenKind.h, Task 43)

class MARKOFF_CORE_EXPORT Theme {
    Q_GADGET
public:
    enum class Slot {
        TextDefault,
        Heading1, Heading2, Heading3, Heading4, Heading5, Heading6,
        InlineCode, CodeBlock,
        Link, WikiLink, Tag, Math,
        Quote,
        BoldEmphasis, ItalicEmphasis, StrikeEmphasis,
        Highlight,
        HiddenMarker,
        SelectionBackground,
        CursorPrimary, CursorSecondary, CursorPresence,
        SearchMatchBackground, SearchActiveMatchBackground,
        SelectionOccurrenceBackground,
        BracketMatchBackground,
        EditorBackground, GutterBackground,
        CodeBlockBackground, QuoteBackground,
        CalloutNote, CalloutWarning, CalloutTip,
        CalloutImportant, CalloutCaution,
        CodeKeyword, CodeControlFlow, CodeBuiltin,
        CodeType, CodeFunction, CodeVariable, CodeConstant,
        CodeOperator, CodePunctuation,
        CodeString, CodeNumber, CodeBoolean,
        CodeComment, CodeDocumentation,
        CodePreprocessor, CodeAnnotation,
        FoldArrow, ScrollbarThumb,
    };
    Q_ENUM(Slot)

    enum class FontRole { Body, Monospace, Heading };
    Q_ENUM(FontRole)

    // QML-facing color views over the most-used slots. New delegates that
    // need additional slot views add their own here in lowerCamelCase.
    Q_PROPERTY(QColor codeBlockBackground READ codeBlockBackground)
    Q_PROPERTY(QColor codeBlock           READ codeBlockColor)
    Q_PROPERTY(QColor selectionBackground READ selectionBackground)

    QColor codeBlockBackground() const { return color(Slot::CodeBlockBackground); }
    QColor codeBlockColor()      const { return color(Slot::CodeBlock); }
    QColor selectionBackground() const { return color(Slot::SelectionBackground); }

    QColor         color(Slot) const;
    void           setColor(Slot, QColor);
    QTextCharFormat charFormat(Slot) const;

    QFont  font(FontRole) const;
    void   setFont(FontRole, QFont);

    bool   isBold(Slot) const;
    void   setBold(Slot, bool);
    bool   isItalic(Slot) const;
    void   setItalic(Slot, bool);
    qreal  fontSizeMultiplier(Slot) const;
    void   setFontSizeMultiplier(Slot, qreal);

    qreal   pixelSizeFor(Slot) const;
    QString familyFor(Slot) const;

    QColor colorForCodeToken(CodeTokenKind) const;

    static Theme defaultLight();
    static Theme defaultDark();

    QJsonObject toJson() const;
    static Theme fromJson(const QJsonObject &);

private:
    QHash<int, QColor>    m_colors;
    QHash<int, QFont>     m_fonts;
    QHash<int, bool>      m_bolds;
    QHash<int, bool>      m_italics;
    QHash<int, qreal>     m_sizeMul;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::Theme)
