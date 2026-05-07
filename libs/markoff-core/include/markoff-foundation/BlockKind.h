// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace Markoff {
enum class BlockKind : uint8_t {
    Paragraph, Heading, CodeBlock, ListItem, BlockQuote,
    HorizontalRule, Image, Math, Mermaid, HtmlBlock, Table,
};
}
