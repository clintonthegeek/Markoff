// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TREESITTERPARSER_H
#define MARKOFF_TREESITTERPARSER_H

#include <QString>
#include <QByteArray>
#include <QList>

#include <vector>

// Forward declare tree-sitter C types
typedef struct TSParser TSParser;
typedef struct TSTree TSTree;
typedef struct TSNode TSNode;

namespace Markoff {

struct HeadingInfo;
struct LinkInfo;
struct TagInfo;
struct TopLevelBlock;

struct DocumentQueryResult {
    QList<HeadingInfo> headings;
    QList<LinkInfo> links;
    QList<TagInfo> tags;
    QList<TopLevelBlock> topLevelBlocks;
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
    /// Discards any prior tree state — including on failure, so no accessor can
    /// read a previous document's tree against this call's byte map. After a
    /// false return, `hasTree()` is false and every accessor returns empty.
    /// Afterwards, use buildSpanMap() to get formatting spans.
    bool parse(const QString &text);

    /// Re-parse after an edit, reusing the previous tree. `editStart` is a byte
    /// offset in the previous text; `oldEnd` is the byte offset in the previous
    /// text where the replaced range ended and `newEnd` the offset in `text`
    /// where the replacement ends (both equal `editStart` for a pure insertion).
    /// The tree is moved to the current text first, so unchanged subtrees are
    /// reused instead of re-parsed (ASK-M31). Falls back to a fresh parse when
    /// there is no tree to update.
    bool reparse(const QString &text, quint32 editStart, quint32 oldEnd,
                 quint32 newEnd);

    /// Walk the CST and extract headings, links, and tags as structured data.
    DocumentQueryResult buildDocumentQueries() const;

    /// True when the last parse() completed and the trees are consistent with
    /// utf8Source(). False before any parse and after a failed one, so callers
    /// never read a new byte map against a stale tree.
    bool hasTree() const { return m_valid; }

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

    /// Walk the CST and build a flat list of formatting spans.
    /// Spans are in document-absolute UTF-8 byte coordinates.
    QList<SourceSpan> buildSpanMap() const;

private:
    struct ByteRange { quint32 startByte; quint32 endByte; };

    void walkNode(TSNode node, QList<SourceSpan> &spans) const;

    TSParser *m_blockParser = nullptr;
    TSParser *m_inlineParser = nullptr;
    TSTree *m_blockTree = nullptr;
    QList<TSTree *> m_inlineTrees;  // one per inline region
    std::vector<ByteRange> m_inlineRanges;  // parallel to m_inlineTrees
    QByteArray m_utf8;
    QList<int> m_byteToChar;  // UTF-8 byte offset → QString char offset
    bool m_valid = false;  // true only after a fully successful parse()
    /// Text the current trees describe, so parse() can skip an unchanged
    /// document and reparse() knows what the edit offsets refer to.
    QString m_lastText;
    bool m_hasParsed = false;

    /// Shared body of parse() and reparse(), with the tree-sitter previous tree
    /// already validated/edited by the caller.
    bool parseWithPrevious(const QString &text, TSTree *previous);
};

/// Parse a single block's UTF-8 content and return its inline formatting spans.
/// Convenience free function: constructs a temporary TreeSitterParser, parses
/// `blockContent`, and returns the resulting span map. Spans are document-relative
/// (offsets from byte 0 of `blockContent`).
QList<SourceSpan> inlineSpansFor(const QByteArray &blockContent);

} // namespace Markoff

#endif // MARKOFF_TREESITTERPARSER_H
