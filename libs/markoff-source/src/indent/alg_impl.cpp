// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "indent_funcs.h"
#include "text_block_utils.h"

#include "alg_impl.h"

namespace Qutepart {

IndentAlgImpl::~IndentAlgImpl() {}

void IndentAlgImpl::setConfig(int width, bool useTabs) {
    width_ = width;
    useTabs_ = useTabs;
}

void IndentAlgImpl::setLanguage(const QString &language) { language_ = language; }

QString IndentAlgImpl::autoFormatLine(QTextBlock block) const {
    return computeSmartIndent(block, -1) + stripLeftWhitespace(block.text());
}

QString IndentAlgImpl::indentLine(QTextBlock block, int cursorPos) const {
    QString indent = computeSmartIndent(block, cursorPos);
    if (!indent.isNull()) {
        return indent + stripLeftWhitespace(block.text());
    } else {
        return block.text();
    }
}

QString IndentAlgImpl::computeSmartIndent(QTextBlock /*block*/, int /*cursorPos*/) const {
    return "";
}

const QString &IndentAlgImpl::triggerCharacters() const {
    static const QString NULL_STRING = QString();
    return NULL_STRING;
}

QString IndentAlgImpl::indentText() const { return makeIndent(width_, useTabs_); }

} // namespace Qutepart
