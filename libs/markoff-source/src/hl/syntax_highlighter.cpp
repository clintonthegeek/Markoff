// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QTextLayout>
#include <Qt>

#include "language.h"
#include "syntax_highlighter.h"
#include "theme.h"

namespace Qutepart {

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *parent, QSharedPointer<Language> language)
    : QSyntaxHighlighter(parent), language(language) {}

SyntaxHighlighter::SyntaxHighlighter(QObject *parent, QSharedPointer<Language> language)
    : QSyntaxHighlighter(parent), language(language) {}

void SyntaxHighlighter::highlightBlock(const QString &) {
    QVector<QTextLayout::FormatRange> formats;

    auto state = language->highlightBlock(currentBlock(), formats);
    for (auto &range : std::as_const(formats)) {
        setFormat(range.start, range.length, range.format);
    }
    setCurrentBlockState(state);
}

} // namespace Qutepart
