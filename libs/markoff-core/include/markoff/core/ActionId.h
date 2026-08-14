// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
namespace Markoff {
Q_NAMESPACE
enum class ActionId {
    None = 0,
    Bold, Italic, InlineCode, Strikethrough,
    HeadingLevel0, HeadingLevel1, HeadingLevel2,
    HeadingLevel3, HeadingLevel4, HeadingLevel5, HeadingLevel6,
    Blockquote, BulletedList, NumberedList, TaskList,
    IndentMore, IndentLess,
    Link, Image, HorizontalRule,
    InsertTable, DeleteRow, DeleteColumn,
    InsertRowAbove, InsertRowBelow, InsertColumnLeft, InsertColumnRight,
    AlignColumnNone, AlignColumnLeft, AlignColumnCenter, AlignColumnRight,
};
Q_ENUM_NS(ActionId)
} // namespace Markoff
