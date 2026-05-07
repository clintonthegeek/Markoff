// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LINKTEXTPARSER_H
#define MARKOFF_LINKTEXTPARSER_H

#include <QString>

namespace Markoff {

struct LinkTarget {
    QString path;       ///< "Note" or "Note.md"
    QString subpath;    ///< "#Heading", "#^blockid", or "" if none
};

/// Split a wikilink target into path and subpath at the first '#'.
/// Pipe display text (|alias) must already be stripped before calling.
LinkTarget parseLinktext(const QString &linktext);

} // namespace Markoff

#endif // MARKOFF_LINKTEXTPARSER_H
