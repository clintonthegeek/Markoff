// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_PARSER_LINK_TARGET_H
#define MARKOFF_PARSER_LINK_TARGET_H

#include <QMetaType>
#include <QString>

namespace Markoff {

/// Structured link target. Populated by the parser at parse time for
/// link/wikilink SourceSpans and LinkInfos. Consumers read the
/// pre-decomposed fields directly; no string-parsing in view layers.
///
/// For [text](url):     url is set; all other fields empty.
/// For [[Page]]:        page is set.
/// For [[Page|Alias]]:  page + alias.
/// For [[Page#Section]]: page + section.
/// For [[Page#^id]]:    page + blockRef (no leading '^').
/// For [[#Section]]:    section only (same-document anchor).
/// For [[#^id]]:        blockRef only (same-document block).
struct LinkTarget {
    QString url;
    QString page;
    QString section;
    QString blockRef;
    QString alias;

    bool isEmpty() const noexcept {
        return url.isEmpty() && page.isEmpty() && section.isEmpty()
            && blockRef.isEmpty() && alias.isEmpty();
    }

    bool operator==(const LinkTarget &) const = default;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::LinkTarget)

#endif  // MARKOFF_PARSER_LINK_TARGET_H
