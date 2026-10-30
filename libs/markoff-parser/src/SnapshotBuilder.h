// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to the parser leaf (not installed): the snapshot builder shared
// by Document::fromMarkdown()'s one-shot path and the persistent
// ParserSession (ASK-M31). Document's Private is file-local to
// Document.cpp, so the builder lives there and both entry points call it.
#pragma once

#include <markoff/parser/Document.h>
#include <markoff/parser/TreeSitterParser.h>

#include <memory>
#include <utility>

namespace Markoff::Detail {

/// Assemble the Document snapshot both parse entry points produce.
/// Takes ownership of the extracted frontmatter/footnotes and the parsed
/// document queries.
std::unique_ptr<Document> buildDocumentSnapshot(const QString &source,
                                                ExtractedSource &&extracted,
                                                DocumentQueryResult &&q);

} // namespace Markoff::Detail
