// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QDebug>

#include "indent_funcs.h"
#include "text_block_utils.h"

#include "alg_lisp.h"

namespace Qutepart {

const QString &IndentAlgLisp::triggerCharacters() const {
    static QString ch(';');
    return ch;
}

QString IndentAlgLisp::computeSmartIndent(QTextBlock block, int /*cursorPos*/) const {
    /* special rules: ;;; -> indent 0
                      ;;  -> align with next line, if possible
                      ;   -> usually on the same line as code -> ignore
     */
    QString text = block.text();
    QStringView leftStripped = text.right(text.length() - firstNonSpaceColumn(text));

    if (leftStripped.startsWith(QLatin1String(";;;"))) {
        return "";
    } else if (leftStripped.startsWith(QLatin1String(";;"))) {
        // try to align with the next line
        QTextBlock nextBlock = nextNonEmptyBlock(block);
        if (nextBlock.isValid()) {
            return blockIndent(nextBlock);
        }
    }

    TextPosition pos = findOpeningBracketBackward('(', TextPosition(block, 0));
    if (!pos.isValid()) {
        return QString();
    }

    return blockIndent(pos.block) + indentText();
}

} // namespace Qutepart
