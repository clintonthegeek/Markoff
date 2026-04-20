// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QList>
#include <QPlainTextEdit>

#include "text_pos.h"

namespace Qutepart {

class Qutepart;

class BracketHighlighter {
  public:
    BracketHighlighter(Qutepart *q);
    ~BracketHighlighter() = default;

    QList<QTextEdit::ExtraSelection> extraSelections(const TextPosition &pos);
    QTextEdit::ExtraSelection makeMatchSelection(const TextPosition &pos, bool matched);

    inline const TextPosition getCachedMatch(TextPosition &pos) {
        if (pos == cachedBracket_) {
            return cachedMatchingBracket_;
        }
        return {};
    }

  private:
    QList<QTextEdit::ExtraSelection> highlightBracket(QChar bracket, const TextPosition &pos);
    TextPosition cachedBracket_;
    TextPosition cachedMatchingBracket_;

    Qutepart *qpart;
};

} // namespace Qutepart
