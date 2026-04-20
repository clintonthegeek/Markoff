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

// Check if text at given position is a code
bool isCode(const QTextBlock &block, int column);

// Check if text at given position is a comment. Including block comments and
// here documents
bool isComment(const QTextBlock &block, int column);

// Check if text at given position is a block comment
bool isBlockComment(const QTextBlock &block, int column);

// Check if text at given position is a here document
bool isHereDoc(const QTextBlock &block, int column);

QString textTypeMap(const QTextBlock &block);

} // namespace Qutepart
