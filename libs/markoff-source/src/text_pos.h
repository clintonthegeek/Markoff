// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QTextBlock>

namespace Qutepart {

struct TextPosition {
  public:
    inline TextPosition() : column(-1) {}

    inline TextPosition(QTextBlock block_, int column_) : block(block_), column(column_) {}

    inline bool isValid() const { return block.isValid(); }

    inline bool operator==(const TextPosition &other) const {
        return block == other.block && column == other.column;
    }

    inline bool operator!=(const TextPosition &other) const { return !(*this == other); }

    QTextBlock block;
    int column;
};

} // namespace Qutepart
