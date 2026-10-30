// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_PARSERSESSION_H
#define MARKOFF_PARSERSESSION_H

#include <QString>
#include <memory>

namespace Markoff {

class Document;

/// A persistent parse session (ASK-M31, second half).
///
/// Every consumer call that re-reads a document pays TreeSitterParser's
/// cold-parse cost again when it goes through the one-shot
/// `Document::fromMarkdown()` — even when the new text differs from the old
/// by a few characters, because the one-shot constructs a fresh internal
/// parser that has no previous tree to reuse.
///
/// A ParserSession holds that parser across calls. Each `parse()` diffs the
/// new source against the last one and, when the session already holds a
/// tree, hands tree-sitter the previous tree with a minimal scalar-aligned
/// edit, so unchanged subtrees are reused instead of re-parsed. The first
/// `parse()` on a fresh session is a normal full parse.
///
/// Byte→char maps stay live: `Document` builds them from the current
/// tree via the same accessors the one-shot path uses, so no accessor can
/// read a previous document's tree against this call's byte map (the tree
/// and map are replaced together inside the parser, and a failed re-parse
/// falls back to a fresh parse).
///
/// Typical use — the vault/filter re-parse loop Corbomite asked for:
/// ```cpp
/// Markoff::ParserSession session;
/// for (const QString &vault : reparseCandidates())
///     auto doc = session.parse(vault);   // reuses unchanged subtrees
/// ```
class ParserSession
{
public:
    ParserSession();
    ~ParserSession();
    ParserSession(ParserSession &&) noexcept;
    ParserSession &operator=(ParserSession &&) noexcept;

    /// Parse (or incrementally re-parse) a full markdown source and return
    /// the document snapshot — the same snapshot `fromMarkdown()` builds.
    /// Reuses unchanged subtrees when this session has a tree and `source`
    /// differs from the last source by a contiguous edit.
    std::unique_ptr<Document> parse(const QString &source);

    /// The source the session's tree currently describes.
    const QString &lastSource() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Markoff

#endif // MARKOFF_PARSERSESSION_H
