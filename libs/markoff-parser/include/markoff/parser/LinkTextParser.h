// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LINKTEXTPARSER_H
#define MARKOFF_LINKTEXTPARSER_H

#include <QString>

namespace Markoff {

/// Raw linktext split into path + subpath at the first '#'. Distinct from
/// the structured 5-field `Markoff::LinkTarget` in `LinkTarget.h` — they
/// previously collided as two different `Markoff::LinkTarget` structs in
/// the same library, an ODR violation that surfaced as a SIGSEGV in
/// Corbomite's MetadataWorker thread (LinkResolver::resolve at scope-exit
/// destruction picked the larger destructor and walked past the smaller
/// stack frame into adjacent garbage). The rename to LinkTextSplit
/// landed during the 2026-05-21 foundation-port debugging session.
struct LinkTextSplit {
    QString path;       ///< "Note" or "Note.md"
    QString subpath;    ///< "#Heading", "#^blockid", or "" if none
};

/// Split a wikilink target into path and subpath at the first '#'.
/// Pipe display text (|alias) must already be stripped before calling.
LinkTextSplit parseLinktext(const QString &linktext);

} // namespace Markoff

#endif // MARKOFF_LINKTEXTPARSER_H
