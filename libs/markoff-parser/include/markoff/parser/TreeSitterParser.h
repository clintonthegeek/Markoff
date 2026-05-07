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
    /// Discards any prior tree state. After parsing, use buildSpanMap()
    /// to get formatting spans.
    bool parse(const QString &text);

    /// Walk the CST and extract headings, links, and tags as structured data.
    DocumentQueryResult buildDocumentQueries() const;

    /// Check if a tree exists (parse was successful)
    bool hasTree() const { return m_blockTree != nullptr; }

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
};

/// Parse a single block's UTF-8 content and return its inline formatting spans.
/// Convenience free function: constructs a temporary TreeSitterParser, parses
/// `blockContent`, and returns the resulting span map. Spans are document-relative
/// (offsets from byte 0 of `blockContent`).
QList<SourceSpan> inlineSpansFor(const QByteArray &blockContent);

} // namespace Markoff

#endif // MARKOFF_TREESITTERPARSER_H
