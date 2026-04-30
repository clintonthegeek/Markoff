// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TREESITTERPARSER_H
#define MARKOFF_TREESITTERPARSER_H

#include <QString>
#include <QByteArray>
#include <QList>

// Forward declare tree-sitter C types
typedef struct TSParser TSParser;
typedef struct TSTree TSTree;
typedef struct TSNode TSNode;

namespace Markoff {

/// A surgical edit in old-text UTF-8 byte coordinates. Independent of
/// markoff-foundation's MarkoffEdit (which carries the new-text bytes too);
/// for tree-sitter incremental parsing we only need ranges + new lengths
/// since the post-edit buffer is supplied separately.
///
/// `oldEnd >= oldStart`. `newLength` is bytes inserted in the slice's place
/// (zero for pure deletion).
struct ByteEdit {
    quint32 oldStart = 0;
    quint32 oldEnd = 0;
    quint32 newLength = 0;
};

struct HeadingInfo;
struct LinkInfo;
struct TagInfo;

struct DocumentQueryResult {
    QList<HeadingInfo> headings;
    QList<LinkInfo> links;
    QList<TagInfo> tags;
};

struct SourceSpan;

/// Wraps tree-sitter's C API for parsing markdown.
///
/// Produces a Concrete Syntax Tree where every character — including
/// syntax delimiters — has an explicit node with byte offsets.
class TreeSitterParser {
public:
    TreeSitterParser();
    ~TreeSitterParser();

    /// Parse markdown text from scratch. Returns true on success.
    /// Discards any prior tree state. After parsing, use buildSpanMap()
    /// to get formatting spans.
    bool parse(const QString &text);

    /// Incrementally re-parse after a set of byte-range edits.
    ///
    /// `edits` describe the transformation from the previously-parsed
    /// buffer (m_utf8) to `newUtf8`, in old-buffer byte coordinates.
    /// Edits may be in any order; this function sorts and applies them
    /// internally. `newUtf8` is the full post-edit buffer.
    ///
    /// If no prior tree exists (first parse, or parse() never succeeded),
    /// this falls through to a full parse of `newUtf8` — callers do not
    /// need a "first parse" branch.
    ///
    /// Returns true on success.
    bool parseIncremental(const QList<ByteEdit> &edits,
                          const QByteArray &newUtf8);

    /// Build a flat span map from the CST. Each span has byte offsets
    /// (converted to QString char offsets) and formatting/delimiter flags.
    QList<SourceSpan> buildSpanMap() const;

    /// Walk the CST and extract headings, links, and tags as structured data.
    DocumentQueryResult buildDocumentQueries() const;

    /// Check if a tree exists (parse was successful)
    bool hasTree() const { return m_blockTree != nullptr; }

    /// Number of inline regions whose tree was reused (not reparsed) on
    /// the most recent `parseIncremental()` call. Reset to 0 on `parse()`.
    /// Primarily an observability hook for tests/benchmarks.
    int inlineTreeReuseCount() const { return m_lastInlineReuseCount; }

    /// Total bytes covered by ts_tree_get_changed_ranges(prevTree, newTree)
    /// on the most recent parseIncremental() call. Returns -1 after a fresh
    /// parse() (no previous tree to compare). Observability hook for benches.
    int blockChangedByteCount() const { return m_lastBlockChangedBytes; }

    /// Wall-clock nanoseconds spent in the block-tree edit + parse phase of
    /// the most recent parseIncremental() (or parse() — full block parse).
    /// Reset to 0 if the call returns false. Observability hook for benches.
    quint64 lastParseBlockNs() const { return m_lastParseBlockNs; }

    /// Wall-clock nanoseconds spent in the inline-tree reuse/reparse phase
    /// of the most recent parseIncremental() (or parse() — full inline pass).
    /// Reset to 0 if the call returns false. Observability hook for benches.
    quint64 lastParseInlineNs() const { return m_lastParseInlineNs; }

    /// A non-text block boundary found by the parser.
    struct BlockBoundary {
        enum Type { Table, FencedCodeBlock, Image };
        Type type;
        int startByte = 0;  ///< UTF-8 byte offset of block start
        int endByte = 0;    ///< UTF-8 byte offset of block end
        int startChar = 0;  ///< QString char offset of block start
        int endChar = 0;    ///< QString char offset of block end
    };

    /// Find all non-text block boundaries in the parsed document.
    /// Must call parse() first.
    QList<BlockBoundary> findBlockBoundaries() const;

    /// Get the raw UTF-8 source (for offset mapping)
    const QByteArray &utf8Source() const { return m_utf8; }

    /// Convert a UTF-8 byte offset to a QString char offset.
    int utf8ToCharOffset(int byteOffset) const;

private:
    void walkNode(TSNode node, QList<SourceSpan> &spans) const;

    TSParser *m_blockParser = nullptr;
    TSParser *m_inlineParser = nullptr;
    TSTree *m_blockTree = nullptr;
    QList<TSTree *> m_inlineTrees;  // one per inline region
    QByteArray m_utf8;
    QList<int> m_byteToChar;  // UTF-8 byte offset → QString char offset
    int m_lastInlineReuseCount = 0;
    int m_lastBlockChangedBytes = -1;
    quint64 m_lastParseBlockNs  = 0;
    quint64 m_lastParseInlineNs = 0;
};

} // namespace Markoff

#endif // MARKOFF_TREESITTERPARSER_H
