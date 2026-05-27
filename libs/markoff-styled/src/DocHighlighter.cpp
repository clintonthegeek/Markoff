// SPDX-License-Identifier: GPL-3.0-or-later
#include "DocHighlighter.h"

namespace Markoff::Styled {

DocHighlighter::DocHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {}

DocHighlighter::~DocHighlighter() = default;

void DocHighlighter::highlightBlock(const QString & /*text*/) {
    // v0 stub. v0.1: cursor-aware delimiter visibility goes here.
}

}  // namespace Markoff::Styled
