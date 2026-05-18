// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_PARSER_WIKILINK_DECOMPOSITION_H
#define MARKOFF_PARSER_WIKILINK_DECOMPOSITION_H

#include <QStringView>

#include <markoff/parser/LinkTarget.h>

namespace Markoff::Detail {

/// Decompose the inner text of a wikilink ([[...]]) into structured
/// page/section/blockRef/alias fields per the spec table. Pure
/// function; no allocation beyond the QStrings in the returned struct.
Markoff::LinkTarget decomposeWikilinkInner(QStringView inner);

}  // namespace Markoff::Detail

#endif  // MARKOFF_PARSER_WIKILINK_DECOMPOSITION_H
