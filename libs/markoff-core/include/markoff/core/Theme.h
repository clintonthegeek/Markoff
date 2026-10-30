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
        InvisibleCharBox,
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
        // Appended (M7): the styled leaf's remaining colour sites had no
        // slot and hardcoded their values. Appended at the END on purpose —
        // Theme's JSON serializes slots by NUMBER, so inserting before an
        // existing slot would silently re-colour every persisted theme.
        InlineCodeBackground, FootnoteRef, HorizontalRule,
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
    /// True when this theme actually defines `slot`'s colour. A consumer
    /// overlaying its own colours per slot (rather than taking the theme's
    /// fallback chain) queries this; `color()` alone cannot distinguish
    /// "set to black" from "unset, fell back to TextDefault".
    bool           hasColor(Slot) const;
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

    /// An order-insensitive signature of this theme's full content (every
    /// slot's colour, font, weight, italic and size multiplier). Equal
    /// contents hash equal regardless of the slots' insertion order; a
    /// content change changes the hash. This is what a cache keyed on
    /// "the theme the block was rendered with" mixes into its key (M7:
    /// the styled leaf's per-block hash gate).
    quint64 contentHash() const;

    /// Value equality over every slot's colour, font, weight, italic and
    /// size multiplier — the comparison a restyle path needs to skip work
    /// when the "new" theme is byte-identical to the one already applied
    /// (M7: StyleApplier stored a pointer and pointer-compared, so a
    /// changed theme at the same address was invisible).
    friend bool operator==(const Theme &a, const Theme &b)
    {
        return a.m_colors   == b.m_colors
            && a.m_fonts    == b.m_fonts
            && a.m_bolds    == b.m_bolds
            && a.m_italics  == b.m_italics
            && a.m_sizeMul  == b.m_sizeMul;
    }
    friend bool operator!=(const Theme &a, const Theme &b) { return !(a == b); }

private:
    QHash<int, QColor>    m_colors;
    QHash<int, QFont>     m_fonts;
    QHash<int, bool>      m_bolds;
    QHash<int, bool>      m_italics;
    QHash<int, qreal>     m_sizeMul;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::Theme)
