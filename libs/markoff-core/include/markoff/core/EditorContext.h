// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
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
} // namespace BlockKindNames
struct EditorContext {
    QString blockKind = BlockKindNames::Paragraph;
    int     headingLevel = 0;
    bool    inTable = false;
    int     tableRow = -1;
    int     tableCol = -1;
};
} // namespace Markoff
