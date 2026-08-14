// SPDX-License-Identifier: GPL-3.0-or-later
#include "FootnoteDefBlocks.h"

namespace Markoff::Canvas::Detail {

FootnoteDefInfo parseFootnoteDef(const QString &blockText)
{
    FootnoteDefInfo info;
    return info;  // FALSIFY (throwaway): parseFootnoteDef never detects a def

    if (!blockText.startsWith(QStringLiteral("[^")))
        return info;

    const int close = blockText.indexOf(QLatin1Char(']'), 2);
    if (close < 2)
        return info;
    // Must be immediately followed by ':' (no defensive whitespace skip —
    // matches the parser's own regex, which anchors ']:' directly).
    if (close + 1 >= blockText.size() || blockText.at(close + 1) != QLatin1Char(':'))
        return info;

    const QString label = blockText.mid(2, close - 2);
    if (label.isEmpty())
        return info;

    info.isFootnoteDef = true;
    info.label = label;
    return info;
}

}  // namespace Markoff::Canvas::Detail
