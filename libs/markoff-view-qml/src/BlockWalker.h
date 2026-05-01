// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include <markoff/view/qml/BlockRecord.h>

namespace Markoff { class Document; }

namespace Markoff::View::Qml {

/// Convert a parsed `Markoff::Document` into a flat list of `BlockRecord`s
/// in document order. Pure function over the tree-sitter AST snapshot
/// exposed by `Markoff::Document::topLevelBlocks()`. No regex, no
/// re-parsing — the foundation has already done that work.
///
/// Records carry source-faithful `text` / `source` (raw markdown for the
/// block's byte range), plus kind-specific metadata (heading level,
/// code language, etc.) where the foundation surfaces it.
///
/// Replaces the legacy regex-based line walker. See Stage C-2 in the
/// new-foundation refactor plan.
class BlockWalker {
public:
    /// Walk the document's top-level blocks and emit records. The
    /// `BlockAnchor` field on each record is left default-constructed —
    /// LiveListModelBinding fills those in from the parse-aligned
    /// anchors list it receives via `parseUpdatedAt`.
    static QList<BlockRecord> walk(const Markoff::Document *parsed);
};

}  // namespace Markoff::View::Qml
