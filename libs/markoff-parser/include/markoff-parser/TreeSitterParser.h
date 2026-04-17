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

    /// Parse markdown text. Returns true on success.
    /// After parsing, use buildSpanMap() to get formatting spans.
    bool parse(const QString &text);

    /// Build a flat span map from the CST. Each span has byte offsets
    /// (converted to QString char offsets) and formatting/delimiter flags.
    QList<SourceSpan> buildSpanMap() const;

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

private:
    void walkNode(TSNode node, QList<SourceSpan> &spans) const;

    TSParser *m_blockParser = nullptr;
    TSParser *m_inlineParser = nullptr;
    TSTree *m_blockTree = nullptr;
    QList<TSTree *> m_inlineTrees;  // one per inline region
    QByteArray m_utf8;
    QList<int> m_byteToChar;  // UTF-8 byte offset → QString char offset
};

} // namespace Markoff

#endif // MARKOFF_TREESITTERPARSER_H
