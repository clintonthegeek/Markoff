// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "text_block_user_data.h"

#include "text_type.h"

namespace Qutepart {

namespace {

static inline TextBlockUserData *getData(const QTextBlock &block) {
    return static_cast<TextBlockUserData *>(block.userData());
}

QChar getTextType(const QTextBlock &block, int column) {
    TextBlockUserData *data = getData(block);
    if (data == nullptr) {
        return ' ';
    } else {
        // this may happen on empty files
        if (column < 0 || data->textTypeMap.size() <= column) {
            return ' ';
        }
        return data->textTypeMap[column];
    }
}

} // namespace

QString textTypeMap(const QTextBlock &block) {
    const TextBlockUserData *data = getData(block);
    if (data == nullptr) {
        return QString().fill(' ', block.text().length());
    }

    return data->textTypeMap;
}

bool isCode(const QTextBlock &block, int column) { return getTextType(block, column) == ' '; }

bool isComment(const QTextBlock &block, int column) {
    QChar type = getTextType(block, column);

    return type == 'c' || type == 'b' || type == 'h';
}

bool isBlockComment(const QTextBlock &block, int column) {
    return getTextType(block, column) == 'b';
}

bool isHereDoc(const QTextBlock &block, int column) { return getTextType(block, column) == 'h'; }

} // namespace Qutepart
