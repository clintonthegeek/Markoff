// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QMetaType>
namespace Markoff {
namespace BlockKindNames {
    constexpr auto Paragraph      = "paragraph";
    constexpr auto Heading        = "heading";
    constexpr auto ListItem       = "list-item";
    constexpr auto CodeBlock      = "code-block";
    constexpr auto Blockquote     = "blockquote";
    constexpr auto Image          = "image";
    constexpr auto Math           = "math";
    constexpr auto HorizontalRule = "horizontal-rule";
    constexpr auto Table          = "table";
} // namespace BlockKindNames
struct EditorContext {
    QString blockKind = BlockKindNames::Paragraph;
    int     headingLevel = 0;
    bool    inTable = false;
    int     tableRow = -1;
    int     tableCol = -1;

    friend bool operator==(const EditorContext &a, const EditorContext &b) noexcept {
        return a.blockKind    == b.blockKind
            && a.headingLevel == b.headingLevel
            && a.inTable      == b.inTable
            && a.tableRow     == b.tableRow
            && a.tableCol     == b.tableCol;
    }
    friend bool operator!=(const EditorContext &a, const EditorContext &b) noexcept {
        return !(a == b);
    }
};
} // namespace Markoff
Q_DECLARE_METATYPE(Markoff::EditorContext)
