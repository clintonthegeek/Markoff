// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFont>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <markoff/parser/SourceSpan.h>

namespace E2Test {

inline QTextCharFormat formatAt(QTextDocument *doc, int charPos) {
    QTextBlock block = doc->firstBlock();
    while (block.isValid() && (charPos < block.position() ||
           charPos >= block.position() + block.length())) {
        const QTextBlock next = block.next();
        if (!next.isValid()) break;
        block = next;
    }
    auto layout = block.layout();
    for (const QTextLayout::FormatRange &fr : layout->formats()) {
        if (charPos - block.position() >= fr.start &&
            charPos - block.position() <  fr.start + fr.length)
            return fr.format;
    }
    return QTextCharFormat();
}

inline bool isHidden(const QTextCharFormat &fmt) {
    return fmt.font().letterSpacingType() == QFont::AbsoluteSpacing
        && fmt.font().letterSpacing() < 0.0;
}

inline Markoff::SourceSpan delimiterSpan(int charOffset, int charLength,
                                          int parentStart, int parentEnd,
                                          std::function<void(Markoff::SourceSpan &)> setKind) {
    Markoff::SourceSpan s;
    s.charOffset = charOffset; s.charLength = charLength;
    s.isDelimiter = true;
    s.parentCharStart = parentStart;
    s.parentCharEnd = parentEnd;
    setKind(s);
    return s;
}

inline Markoff::SourceSpan contentSpan(int charOffset, int charLength,
                                        std::function<void(Markoff::SourceSpan &)> setKind) {
    Markoff::SourceSpan s;
    s.charOffset = charOffset; s.charLength = charLength;
    setKind(s);
    return s;
}

}  // namespace E2Test
